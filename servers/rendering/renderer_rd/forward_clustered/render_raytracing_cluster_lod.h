/**************************************************************************/
/*  render_raytracing_cluster_lod.h                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/math/transform_3d.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/variant/array.h"
#include "servers/rendering/renderer_rd/shaders/raytracing/cluster_lod_select.glsl.gen.h"

class RenderDataRD;

// What the LOD selection reads of one cluster. Must match ClusterLod in cluster_lod_select.glsl (std430, 48 bytes).
struct RTClusterLod {
	float center[3]; // Bounds of the cluster's own error, in mesh space.
	float radius;
	float group_center[3]; // Bounds of the error of the group it's in, whose simplified clusters replace it.
	float group_radius;
	float error;
	float group_error; // FLT_MAX when the group isn't simplified further.
	uint32_t triangle_count;
	uint32_t pad;
};
static_assert(sizeof(RTClusterLod) == 48, "RTClusterLod must be 48 bytes for std430");

// Bytes before the cluster references in an instance's references buffer: an RD::AccelerationStructureClusterReferences,
// padded. Must match REFERENCES_OFFSET in cluster_lod_select.glsl.
static constexpr uint64_t RT_CLUSTER_LOD_REFERENCES_OFFSET = 256;

// The cluster LOD data of one surface, owned by its RTClusterBuffers.
struct RTClusterLodSurface {
	RID cluster_set; // Every cluster of every level, built once.
	RID cluster_addresses; // Device address of each cluster of cluster_set, written by its build.
	RID lods; // RTClusterLod of each cluster.
	uint64_t cluster_addresses_address = 0;
	uint64_t lods_address = 0;
	uint32_t cluster_count = 0;
	// The most clusters a cut can have; each instance's BLAS and references are sized for it.
	uint32_t max_cut_cluster_count = 0;
	uint32_t finest_triangle_count = 0;
	// Bounds holding every cluster, in mesh space.
	Vector3 center;
	float radius = 0.0f;
};

// Level of detail of surfaces traced from clusters (rendering/pathtracing/cluster_geometry/lod).
//
// Every traced instance of such a surface has its own BLAS over a cut of the surface's cluster DAG, built from the
// cluster acceleration structures of all levels, which are built once per surface (RTClusterLodSurface::cluster_set).
// A compute shader picks the cut on the GPU and writes the clusters' addresses, from which the BLAS is rebuilt in
// place, in the same frame, before anything traces it.
//
// The cut depends on the camera position only, never the view direction, so reflections, shadows and indirect rays see
// what the camera would see from there. Its projected errors are within the threshold, in pixels at output resolution,
// for what the camera sees directly; secondary rays can magnify them (a distant caster's shadow on a near receiver).
// A cut is picked again only when the camera, the instance or the projection moved enough that its errors could have
// grown or shrunk by HYSTERESIS: cuts are picked that much finer than the threshold, so they still hold until then,
// and a camera at rest never switches cuts back and forth. At most the per-frame budget of instances is picked again,
// the stalest first; an instance seen for the first time is always picked, so none is ever missing.
class RTClusterLodSelector {
public:
	// The band a cut holds over, as a fraction of its projected errors.
	static constexpr float HYSTERESIS = 0.1f;
	// Frames an instance keeps its BLAS while it isn't traced.
	static constexpr uint32_t EVICT_FRAMES = 60;
	// Frames between readbacks of the cut sizes for the render info.
	static constexpr uint32_t STATS_READBACK_FRAMES = 30;

	// One traced instance of a surface: the geometry instance or MultiMesh, and the surface or MultiMesh instance.
	struct Key {
		const void *owner = nullptr;
		uint32_t index = 0;

		bool operator==(const Key &p_other) const { return owner == p_other.owner && index == p_other.index; }
		uint32_t hash() const { return hash_fmix32(hash_murmur3_one_32(index, hash_murmur3_one_64(uint64_t(uintptr_t(owner))))); }
	};

	// The instances of one viewport, with the cuts picked from its camera.
	struct View {
		struct Instance {
			RID blas;
			RID references; // RD::AccelerationStructureClusterReferences, then the references.
			RID cluster_set; // The surface's, to notice a surface baked again.
			uint32_t finest_triangle_count = 0;
			uint32_t max_cut_cluster_count = 0; // What its BLAS and references hold.
			uint32_t stats_slot = 0;
			uint32_t last_used_frame = 0;
			bool selected = false; // False until its first cut.
			// What the last cut was picked for.
			Transform3D transform;
			Transform3D inverse_transform;
			Vector3 camera; // In mesh space.
			float pixel_scale = 0.0f;
			float anisotropy = 1.0f; // Largest over smallest scale of the transform.
		};

		// An instance that wants a new cut this frame.
		struct Candidate {
			Instance *instance = nullptr;
			float staleness = 0.0f; // FLT_MAX for an instance without a cut.
			uint64_t lods_address = 0;
			uint64_t cluster_addresses_address = 0;
			uint32_t cluster_count = 0;
			uint32_t max_cut_cluster_count = 0;
			Transform3D transform; // The instance's, which the cut is picked for.
		};

		HashMap<Key, Instance> instances;
		LocalVector<Candidate> candidates;
		uint32_t frame = 0;
		bool active = false; // Began this frame with a camera.

		// This frame's camera.
		Vector3 camera;
		float pixel_scale = 0.0f; // Projected error over threshold per unit of world error at unit distance, picked finer by HYSTERESIS.
		float z_near = 0.0f;
		bool orthographic = false;

		RID job_buffer;
		uint32_t job_capacity = 0;
		// Clusters and triangles of each instance's last cut, by stats slot.
		RID stats_buffer;
		uint32_t stats_capacity = 0;
		uint32_t stats_slot_count = 0;
		LocalVector<uint32_t> free_stats_slots;

		uint32_t picked = 0; // Instances given a new cut this frame.
		uint32_t deferred = 0; // Instances over the budget this frame, which keep their cut.
		// The stats buffer as last read back: one PackedByteArray, shared with the readbacks still in flight.
		Array stats_readback;
	};

	// Pixels at output resolution per unit of world error, at unit distance for a perspective camera.
	static float pixels_per_error(const RenderDataRD *p_render_data);

	void initialize(bool p_enabled);
	void finalize();
	bool is_enabled() const { return enabled; }

	// Queues the first build of a surface's cluster set; it is built before any cut is picked from it.
	void add_cluster_set(RID p_cluster_set) { pending_cluster_sets.push_back(p_cluster_set); }

	// Takes the camera of p_render_data for the cuts of this frame.
	void begin_view(View &r_view, const RenderDataRD *p_render_data);
	// The BLAS to trace the surface with for instance p_key at p_transform; invalid when it can't be made or the
	// surface's cluster set failed to build.
	RID instance_blas(View &r_view, const Key &p_key, const RTClusterLodSurface &p_surface, const Transform3D &p_transform);
	// Picks this frame's cuts on the GPU and queues the BLASes over them in r_dirty_blas_list. Call after every
	// instance_blas() of the view and before the BLASes are built. Adds the view's counters to r_render_info, the
	// visible render info, when given.
	void end_view(View &r_view, LocalVector<RID> &r_dirty_blas_list, int *r_render_info);
	void free_view(View &r_view);

private:
	// Must match Job in cluster_lod_select.glsl (std430, 80 bytes).
	struct alignas(8) Job {
		float camera[3];
		float pixel_scale;
		float min_scale;
		float max_scale;
		float z_near;
		uint32_t flags;
		uint32_t cluster_count;
		uint32_t stats_slot;
		uint32_t reference_capacity;
		uint32_t pad1;
		uint64_t lods;
		uint64_t cluster_addresses;
		uint64_t references;
		uint64_t pad2;
	};
	static_assert(sizeof(Job) == 80, "Job must be 80 bytes for std430");
	// Matches JOB_FLAG_ORTHOGRAPHIC.
	static constexpr uint32_t JOB_FLAG_ORTHOGRAPHIC = 1;

	bool enabled = false;
	ClusterLodSelectShaderRD shader;
	RID shader_version;
	RID pipeline;
	LocalVector<RID> pending_cluster_sets;
	// Cluster sets whose build failed: their cluster addresses were never written, so no cut is picked from them.
	HashSet<RID> failed_cluster_sets;
	LocalVector<Job> jobs;

	static void _scale_range(const Basis &p_basis, float &r_min, float &r_max);
	float _staleness(const View &p_view, const View::Instance &p_instance, const RTClusterLodSurface &p_surface, const Transform3D &p_transform) const;
	void _build_pending_cluster_sets();
	void _ensure_stats_capacity(View &r_view);
	void _record_render_info(View &r_view, int *r_render_info);
	static void _receive_stats(const PackedByteArray &p_data, Array p_readback);
	void _free_instance(View &r_view, View::Instance &r_instance);
};
