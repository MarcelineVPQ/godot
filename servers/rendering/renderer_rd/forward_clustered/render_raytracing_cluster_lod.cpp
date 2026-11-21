/**************************************************************************/
/*  render_raytracing_cluster_lod.cpp                                     */
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

#include "render_raytracing_cluster_lod.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/templates/sort_array.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server_enums.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/utilities.h"

#include <cfloat>

static void _free_rid_if_owned(RID &r_rid) {
	if (r_rid.is_valid() && RD::get_singleton()->acceleration_structure_is_valid(r_rid)) {
		RD::get_singleton()->free_rid(r_rid);
	}
	r_rid = RID();
}

float RTClusterLodSelector::pixels_per_error(const RenderDataRD *p_render_data) {
	if (!p_render_data->scene_data || p_render_data->render_buffers.is_null()) {
		return 0.0f;
	}
	return p_render_data->scene_data->cam_projection.columns[1][1] * 0.5f * float(p_render_data->render_buffers->get_target_size().y);
}

void RTClusterLodSelector::initialize(bool p_enabled) {
	enabled = p_enabled;
	if (!enabled) {
		return;
	}
	// The selection compacts each cut with subgroup ballots and sums its triangles with subgroup arithmetic.
	const uint64_t needed_operations = RD::SUBGROUP_BASIC_BIT | RD::SUBGROUP_BALLOT_BIT | RD::SUBGROUP_ARITHMETIC_BIT;
	const bool subgroups = (RD::get_singleton()->limit_get(RD::LIMIT_SUBGROUP_OPERATIONS) & needed_operations) == needed_operations && (RD::get_singleton()->limit_get(RD::LIMIT_SUBGROUP_IN_SHADERS) & RD::SHADER_STAGE_COMPUTE_BIT);
	if (!subgroups) {
		enabled = false;
		ERR_FAIL_MSG("Path tracing: Cluster level of detail needs subgroup ballot and arithmetic operations in compute shaders, which this GPU doesn't offer; surfaces are traced from their finest clusters.");
	}
	Vector<String> modes;
	modes.push_back("\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, 0));
	RD::get_singleton()->set_resource_name(pipeline, "RT cluster LOD select pipeline");
}

void RTClusterLodSelector::finalize() {
	if (shader_version.is_valid()) {
		// Frees the pipeline with the shader.
		shader.version_free(shader_version);
		shader_version = RID();
		pipeline = RID();
	}
	enabled = false;
}

void RTClusterLodSelector::begin_view(View &r_view, const RenderDataRD *p_render_data) {
	r_view.active = false;
	if (!enabled || !p_render_data->scene_data || p_render_data->render_buffers.is_null()) {
		return;
	}
	r_view.frame++;
	r_view.active = true;

	const Projection &projection = p_render_data->scene_data->cam_projection;
	r_view.camera = p_render_data->scene_data->cam_transform.origin;
	r_view.orthographic = projection.is_orthogonal();
	r_view.z_near = MAX(p_render_data->scene_data->z_near, 1e-6f);
	const float threshold = MAX(float(GLOBAL_GET_CACHED(double, "rendering/pathtracing/cluster_geometry/lod_error_threshold")), 0.01f);
	r_view.pixel_scale = pixels_per_error(p_render_data) * (1.0f + HYSTERESIS) / threshold;
}

// Smallest and largest scale of p_basis along its axes; exact for rotations with scale, which instances use.
void RTClusterLodSelector::_scale_range(const Basis &p_basis, float &r_min, float &r_max) {
	const Vector3 scale = p_basis.get_scale_abs();
	r_min = MIN(scale.x, MIN(scale.y, scale.z));
	r_max = MAX(scale.x, MAX(scale.y, scale.z));
}

// How much the projected errors of p_instance's cut may have grown or shrunk since it was picked, as a factor, or 0
// while it holds within HYSTERESIS. FLT_MAX when the instance has no cut yet.
//
// Distances are in the selection shader's metric (projected_error() in cluster_lod_select.glsl): the gap of a sphere
// at mesh-space distance d with radius r is d * min_scale - r * max_scale. Every cluster's sphere is inside the surface
// bounds, so no gap is smaller than the bounds' own, `distance`; a camera move by m in mesh space changes a gap by at
// most m * min_scale, `moved`. So the errors change by at most a factor of 1 + moved / distance, under any scale. An
// orthographic projection doesn't depend on distances at all. A non-uniform scale changing its proportions counts by
// how much they changed.
float RTClusterLodSelector::_staleness(const View &p_view, const View::Instance &p_instance, const RTClusterLodSurface &p_surface, const Transform3D &p_transform) const {
	if (!p_instance.selected) {
		return FLT_MAX;
	}
	float min_scale;
	float max_scale;
	_scale_range(p_transform.basis, min_scale, max_scale);
	if (min_scale <= 0.0f) {
		// A zero scale hides the instance; any cut does.
		return 0.0f;
	}

	const bool moved_instance = p_transform != p_instance.transform;
	const Vector3 camera = (moved_instance ? p_transform.affine_inverse() : p_instance.inverse_transform).xform(p_view.camera);
	const float distance = MAX(camera.distance_to(p_surface.center) * min_scale - p_surface.radius * max_scale, p_view.z_near);
	const float moved = p_view.orthographic ? 0.0f : camera.distance_to(p_instance.camera) * min_scale;
	const float projection_change = p_view.pixel_scale > p_instance.pixel_scale ? p_view.pixel_scale / p_instance.pixel_scale : p_instance.pixel_scale / p_view.pixel_scale;
	const float anisotropy = max_scale / min_scale;
	const float anisotropy_change = anisotropy > p_instance.anisotropy ? anisotropy / p_instance.anisotropy : p_instance.anisotropy / anisotropy;
	const float staleness = projection_change * anisotropy_change * (1.0f + moved / distance);
	return staleness > 1.0f + HYSTERESIS ? staleness : 0.0f;
}

RID RTClusterLodSelector::instance_blas(View &r_view, const Key &p_key, const RTClusterLodSurface &p_surface, const Transform3D &p_transform) {
	ERR_FAIL_COND_V(!r_view.active, RID());
	if (failed_cluster_sets.has(p_surface.cluster_set)) {
		return RID();
	}
	RD *rd = RD::get_singleton();
	View::Instance *instance = r_view.instances.getptr(p_key);
	if (instance && instance->cluster_set != p_surface.cluster_set) {
		// The surface was baked again.
		_free_instance(r_view, *instance);
		r_view.instances.erase(p_key);
		instance = nullptr;
	}
	if (!instance) {
		View::Instance created;
		const uint64_t references_size = RT_CLUSTER_LOD_REFERENCES_OFFSET + uint64_t(p_surface.cluster_count) * sizeof(uint64_t);
		created.references = rd->storage_buffer_create(references_size, {}, 0, RD::BUFFER_CREATION_DEVICE_ADDRESS_BIT | RD::BUFFER_CREATION_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT);
		ERR_FAIL_COND_V_MSG(created.references.is_null(), RID(), "Path tracing: Couldn't allocate the cluster references of a mesh instance; it isn't traced.");
		rd->set_resource_name(created.references, "RT cluster LOD references");
		created.blas = rd->blas_create_from_cluster_references(p_surface.cluster_set, created.references, 0, p_surface.cluster_count, RD::ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT);
		if (created.blas.is_null()) {
			rd->free_rid(created.references);
			ERR_FAIL_V_MSG(RID(), "Path tracing: Couldn't create the cluster BLAS of a mesh instance; it isn't traced.");
		}
		rd->set_resource_name(created.blas, "RT cluster LOD BLAS");
		created.cluster_set = p_surface.cluster_set;
		created.finest_triangle_count = p_surface.finest_triangle_count;
		if (r_view.free_stats_slots.is_empty()) {
			created.stats_slot = r_view.stats_slot_count++;
		} else {
			created.stats_slot = r_view.free_stats_slots[r_view.free_stats_slots.size() - 1];
			r_view.free_stats_slots.resize(r_view.free_stats_slots.size() - 1);
		}
		instance = &r_view.instances.insert(p_key, created)->value;
	} else if (instance->last_used_frame == r_view.frame) {
		// Already traced this frame, under another TLAS instance.
		return instance->blas;
	}
	instance->last_used_frame = r_view.frame;

	const float staleness = _staleness(r_view, *instance, p_surface, p_transform);
	if (staleness > 0.0f) {
		View::Candidate candidate;
		candidate.instance = instance;
		candidate.staleness = staleness;
		candidate.lods_address = p_surface.lods_address;
		candidate.cluster_addresses_address = p_surface.cluster_addresses_address;
		candidate.cluster_count = p_surface.cluster_count;
		candidate.transform = p_transform;
		r_view.candidates.push_back(candidate);
	}
	return instance->blas;
}

void RTClusterLodSelector::_build_pending_cluster_sets() {
	RD *rd = RD::get_singleton();
	LocalVector<RID> builds;
	for (const RID &cluster_set : pending_cluster_sets) {
		// A surface may have been freed again since it queued its set.
		if (rd->acceleration_structure_is_valid(cluster_set)) {
			builds.push_back(cluster_set);
		}
	}
	pending_cluster_sets.clear();
	if (builds.is_empty()) {
		return;
	}
	// The batch skips a set it can't build, after an error, and builds the others.
	rd->acceleration_structure_build_batch(builds, {}, {});
	for (const RID &cluster_set : builds) {
		if (!rd->acceleration_structure_is_built(cluster_set)) {
			failed_cluster_sets.insert(cluster_set);
			ERR_PRINT("Path tracing: Couldn't build the clusters of a mesh surface (see the error above); its instances aren't traced.");
		}
	}
	// The selection reads the cluster addresses these builds write.
	rd->untracked_barrier();
}

void RTClusterLodSelector::_ensure_stats_capacity(View &r_view) {
	if (r_view.stats_slot_count <= r_view.stats_capacity && r_view.stats_buffer.is_valid()) {
		return;
	}
	RD *rd = RD::get_singleton();
	const uint32_t capacity = MAX(r_view.stats_slot_count * 2, 64u);
	const RID buffer = rd->storage_buffer_create(capacity * sizeof(uint32_t) * 2);
	ERR_FAIL_COND(buffer.is_null());
	rd->set_resource_name(buffer, "RT cluster LOD stats");
	rd->buffer_clear(buffer, 0, capacity * sizeof(uint32_t) * 2);
	if (r_view.stats_buffer.is_valid()) {
		// Instances keep their counts until they are picked again.
		rd->buffer_copy(r_view.stats_buffer, buffer, 0, 0, r_view.stats_capacity * sizeof(uint32_t) * 2);
		rd->free_rid(r_view.stats_buffer);
	}
	r_view.stats_buffer = buffer;
	r_view.stats_capacity = capacity;
}

void RTClusterLodSelector::end_view(View &r_view, LocalVector<RID> &r_dirty_blas_list, int *r_render_info) {
	r_view.picked = 0;
	r_view.deferred = 0;
	if (!r_view.active) {
		return;
	}
	_build_pending_cluster_sets();
	if (!failed_cluster_sets.is_empty()) {
		// No cut can be picked from a set that wasn't built. These instances were added to this frame's TLAS before the
		// build, whose validation reports their unbuilt BLASes; from the next frame on, instance_blas() leaves them out.
		for (uint32_t i = 0; i < r_view.candidates.size();) {
			if (failed_cluster_sets.has(r_view.candidates[i].instance->cluster_set)) {
				r_view.candidates.remove_at_unordered(i);
			} else {
				i++;
			}
		}
	}

	// Instances without a cut first, then the stalest, up to the budget.
	struct StalestFirst {
		_FORCE_INLINE_ bool operator()(const View::Candidate &p_a, const View::Candidate &p_b) const { return p_a.staleness > p_b.staleness; }
	};
	r_view.candidates.sort_custom<StalestFirst>();
	const uint32_t budget = MAX(GLOBAL_GET_CACHED(int, "rendering/pathtracing/cluster_geometry/lod_max_rebuilds_per_frame"), 1);
	uint32_t count = 0;
	while (count < r_view.candidates.size() && (count < budget || r_view.candidates[count].staleness == FLT_MAX)) {
		count++;
	}
	// They keep their cut, and stay candidates until they get a new one.
	r_view.deferred = r_view.candidates.size() - count;

	if (count > 0) {
		_ensure_stats_capacity(r_view);
	}
	RD *rd = RD::get_singleton();
	if (count > 0 && r_view.stats_buffer.is_valid()) {
		jobs.resize(count);
		for (uint32_t i = 0; i < count; i++) {
			const View::Candidate &candidate = r_view.candidates[i];
			View::Instance &instance = *candidate.instance;
			instance.transform = candidate.transform;
			// A zero scale hides the instance; any cut does.
			instance.inverse_transform = Math::is_zero_approx(instance.transform.basis.determinant()) ? Transform3D() : instance.transform.affine_inverse();
			instance.camera = instance.inverse_transform.xform(r_view.camera);
			instance.pixel_scale = r_view.pixel_scale;
			instance.selected = true;

			Job &job = jobs[i];
			job = {};
			job.camera[0] = instance.camera.x;
			job.camera[1] = instance.camera.y;
			job.camera[2] = instance.camera.z;
			job.pixel_scale = r_view.pixel_scale;
			_scale_range(instance.transform.basis, job.min_scale, job.max_scale);
			instance.anisotropy = job.min_scale > 0.0f ? job.max_scale / job.min_scale : 1.0f;
			job.z_near = r_view.z_near;
			job.flags = r_view.orthographic ? JOB_FLAG_ORTHOGRAPHIC : 0u;
			job.cluster_count = candidate.cluster_count;
			job.stats_slot = instance.stats_slot;
			job.lods = candidate.lods_address;
			job.cluster_addresses = candidate.cluster_addresses_address;
			job.references = rd->buffer_get_device_address(instance.references);
			r_dirty_blas_list.push_back(instance.blas);
		}

		if (count > r_view.job_capacity) {
			if (r_view.job_buffer.is_valid()) {
				rd->free_rid(r_view.job_buffer);
			}
			r_view.job_capacity = count * 2;
			r_view.job_buffer = rd->storage_buffer_create(r_view.job_capacity * sizeof(Job));
			rd->set_resource_name(r_view.job_buffer, "RT cluster LOD jobs");
		}
		rd->buffer_update(r_view.job_buffer, 0, count * sizeof(Job), jobs.ptr());

		const RD::Uniform u_jobs(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, r_view.job_buffer);
		const RD::Uniform u_stats(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, r_view.stats_buffer);
		const RID uniform_set = UniformSetCacheRD::get_singleton()->get_cache(shader.version_get_shader(shader_version, 0), 0, u_jobs, u_stats);
		RENDER_TIMESTAMP("Cluster LOD Select");
		RD::ComputeListID compute_list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute_list, pipeline);
		rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
		rd->compute_list_dispatch(compute_list, count, 1, 1);
		rd->compute_list_end();
		r_view.picked = count;
	}
	r_view.candidates.clear();
	if (r_render_info) {
		_record_render_info(r_view, r_render_info);
	}

	LocalVector<Key> evicted;
	for (KeyValue<Key, View::Instance> &kv : r_view.instances) {
		if (r_view.frame - kv.value.last_used_frame > EVICT_FRAMES) {
			evicted.push_back(kv.key);
		}
	}
	for (const Key &key : evicted) {
		_free_instance(r_view, r_view.instances[key]);
		r_view.instances.erase(key);
	}
}

void RTClusterLodSelector::_receive_stats(const PackedByteArray &p_data, Array p_readback) {
	p_readback[0] = p_data;
}

void RTClusterLodSelector::_record_render_info(View &r_view, int *r_render_info) {
	// A small buffer, but read back only every so often: the render info is there for people to read.
	if (r_view.stats_buffer.is_valid() && r_view.frame % STATS_READBACK_FRAMES == 0) {
		if (r_view.stats_readback.is_empty()) {
			r_view.stats_readback.push_back(PackedByteArray());
		}
		RD::get_singleton()->buffer_get_data_async(r_view.stats_buffer, callable_mp_static(&RTClusterLodSelector::_receive_stats).bind(r_view.stats_readback), 0, r_view.stats_capacity * sizeof(uint32_t) * 2);
	}

	// Counts of the last readback, up to STATS_READBACK_FRAMES old; an instance picked since shows its previous cut until
	// the next one lands.
	const PackedByteArray counts = r_view.stats_readback.is_empty() ? PackedByteArray() : PackedByteArray(r_view.stats_readback[0]);
	const uint32_t *slots = reinterpret_cast<const uint32_t *>(counts.ptr());
	const uint32_t slot_count = counts.size() / (sizeof(uint32_t) * 2);
	uint64_t instances = 0;
	uint64_t clusters = 0;
	uint64_t triangles = 0;
	uint64_t finest_triangles = 0;
	for (const KeyValue<Key, View::Instance> &kv : r_view.instances) {
		const View::Instance &instance = kv.value;
		if (instance.last_used_frame != r_view.frame) {
			continue;
		}
		instances++;
		finest_triangles += instance.finest_triangle_count;
		if (instance.stats_slot < slot_count) {
			clusters += slots[instance.stats_slot * 2];
			triangles += slots[instance.stats_slot * 2 + 1];
		}
	}
	const auto add = [r_render_info](RSE::ViewportRenderInfo p_info, uint64_t p_value) {
		r_render_info[p_info] = int(MIN(uint64_t(r_render_info[p_info]) + p_value, uint64_t(INT32_MAX)));
	};
	add(RSE::VIEWPORT_RENDER_INFO_RT_CLUSTER_LOD_INSTANCES, instances);
	add(RSE::VIEWPORT_RENDER_INFO_RT_CLUSTER_LOD_CLUSTERS, clusters);
	add(RSE::VIEWPORT_RENDER_INFO_RT_CLUSTER_LOD_TRIANGLES_K, (triangles + 500) / 1000);
	add(RSE::VIEWPORT_RENDER_INFO_RT_CLUSTER_LOD_FINEST_TRIANGLES_K, (finest_triangles + 500) / 1000);
	add(RSE::VIEWPORT_RENDER_INFO_RT_CLUSTER_LOD_REBUILDS, r_view.picked);
	add(RSE::VIEWPORT_RENDER_INFO_RT_CLUSTER_LOD_DEFERRED, r_view.deferred);
}

void RTClusterLodSelector::_free_instance(View &r_view, View::Instance &r_instance) {
	// Freeing the surface's cluster set may already have freed the BLAS.
	_free_rid_if_owned(r_instance.blas);
	if (r_instance.references.is_valid()) {
		RD::get_singleton()->free_rid(r_instance.references);
		r_instance.references = RID();
	}
	r_view.free_stats_slots.push_back(r_instance.stats_slot);
}

void RTClusterLodSelector::free_view(View &r_view) {
	for (KeyValue<Key, View::Instance> &kv : r_view.instances) {
		_free_instance(r_view, kv.value);
	}
	r_view.instances.clear();
	r_view.candidates.clear();
	r_view.free_stats_slots.clear();
	r_view.stats_slot_count = 0;
	for (RID *buffer : { &r_view.job_buffer, &r_view.stats_buffer }) {
		if (buffer->is_valid()) {
			RD::get_singleton()->free_rid(*buffer);
			*buffer = RID();
		}
	}
	r_view.job_capacity = 0;
	r_view.stats_capacity = 0;
}
