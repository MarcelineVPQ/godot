/**************************************************************************/
/*  mesh_cluster_data.h                                                   */
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

#include "core/math/math_funcs.h"
#include "core/templates/span.h"
#include "core/templates/vector.h"

// Cluster LOD hierarchy of one triangle mesh surface, baked at import (see ImporterMesh::generate_clusters()) and
// stored as RenderingServerTypes::SurfaceData::cluster_data. The renderer reads it to trace virtualised geometry.
//
// Layout, little endian, every section 4-byte aligned:
//   Header
//   Group[group_count]
//   Cluster[cluster_count], sorted by level, finest (0) first
//   uint8_t triangles[triangle_count * 3], local vertex indices of each cluster, zero padded to 4 bytes
//   uint32_t vertices[vertex_count], surface vertex index of each local vertex
//   float positions[vertex_count * 3], mesh space position of each local vertex
//
// Clusters in one group were simplified together into the clusters of the next level, whose `refined` is that group.
// The bake keeps the DAG monotonic: a group's error is at least the error of every cluster in it, so for any error
// threshold exactly one cut exists (draw a cluster when its own error is within the threshold and its group's is not).
// A group's sphere also holds the spheres of its clusters (see cluster_sphere_reach()), so the cut stays exact when the
// threshold applies to the error projected from a distance to the camera.
namespace MeshClusterData {

static constexpr uint32_t MAGIC = 0x534c4347; // "GCLS".
static constexpr uint32_t VERSION = 1;
static constexpr uint32_t MAX_CLUSTER_TRIANGLES = 128;
static constexpr uint32_t MAX_CLUSTER_VERTICES = 256;

struct Header {
	uint32_t magic = MAGIC;
	uint32_t version = VERSION;
	uint32_t cluster_count = 0;
	uint32_t group_count = 0;
	uint32_t level_count = 0;
	uint32_t triangle_count = 0; // All clusters of all levels.
	uint32_t vertex_count = 0; // Local vertices of all clusters.
	uint32_t finest_cluster_count = 0; // Clusters of level 0: the source mesh.
	uint32_t finest_triangle_count = 0;
	uint32_t finest_vertex_count = 0;
};

struct Group {
	// Bounds and simplification error of the group's clusters taken together, in mesh space. The error is FLT_MAX
	// for groups that weren't simplified further (the DAG roots).
	float center[3] = {};
	float radius = 0.0f;
	float error = 0.0f;
	uint32_t level = 0; // Level of the clusters in the group.
};

struct Cluster {
	uint32_t group = 0; // The group this cluster is in.
	int32_t refined = -1; // The group simplified into this cluster, or -1 for clusters of the source mesh.
	uint32_t level = 0; // 0 for the source mesh, one more for each simplification step.
	uint32_t triangle_offset = 0; // First triangle in `triangles`.
	uint32_t triangle_count = 0;
	uint32_t vertex_offset = 0; // First local vertex in `vertices` and `positions`.
	uint32_t vertex_count = 0;
	// Bounds and error of this cluster against the source mesh, in mesh space: those of group `refined`, or this
	// cluster's own bounds with zero error for the source mesh.
	float center[3] = {};
	float radius = 0.0f;
	float error = 0.0f;
};

static_assert(sizeof(Header) == 40 && sizeof(Group) == 24 && sizeof(Cluster) == 48);

// How far from p_group's center the sphere of p_cluster reaches. The bake makes each group's radius at least this for
// every cluster in it.
inline float cluster_sphere_reach(const Group &p_group, const Cluster &p_cluster) {
	const float dx = p_cluster.center[0] - p_group.center[0];
	const float dy = p_cluster.center[1] - p_group.center[1];
	const float dz = p_cluster.center[2] - p_group.center[2];
	return Math::sqrt(dx * dx + dy * dy + dz * dz) + p_cluster.radius;
}

// Read-only view of baked data; is_valid() is false when the data is empty or malformed. Every record of valid data has
// been checked against the header, so readers may index with it directly. Surface vertex indices (`vertices`) can only
// be checked against the surface, by the reader.
class View {
	const Header *header = nullptr;
	const Group *groups = nullptr;
	const Cluster *clusters = nullptr;
	const uint8_t *triangles = nullptr;
	const uint32_t *vertices = nullptr;
	const float *positions = nullptr;

	bool _validate() const {
		const Header &h = *header;
		if (h.level_count == 0 || h.finest_cluster_count == 0 || h.finest_cluster_count > h.cluster_count || h.finest_triangle_count > h.triangle_count || h.finest_vertex_count > h.vertex_count) {
			return false;
		}
		for (uint32_t g = 0; g < h.group_count; g++) {
			if (groups[g].level >= h.level_count) {
				return false;
			}
		}
		uint32_t previous_level = 0;
		for (uint32_t c = 0; c < h.cluster_count; c++) {
			const Cluster &cluster = clusters[c];
			const bool finest = c < h.finest_cluster_count;
			// Sorted by level, the source mesh's clusters first; each in a group of its level, made from a group of the
			// level before.
			if (cluster.level < previous_level || cluster.level >= h.level_count || finest != (cluster.level == 0)) {
				return false;
			}
			previous_level = cluster.level;
			if (cluster.group >= h.group_count || groups[cluster.group].level != cluster.level) {
				return false;
			}
			if (finest ? cluster.refined != -1 : (cluster.refined < 0 || uint32_t(cluster.refined) >= h.group_count || groups[cluster.refined].level + 1 != cluster.level)) {
				return false;
			}
			if (cluster.triangle_count == 0 || cluster.triangle_count > MAX_CLUSTER_TRIANGLES || cluster.vertex_count == 0 || cluster.vertex_count > MAX_CLUSTER_VERTICES) {
				return false;
			}
			const uint64_t triangle_end = uint64_t(cluster.triangle_offset) + cluster.triangle_count;
			const uint64_t vertex_end = uint64_t(cluster.vertex_offset) + cluster.vertex_count;
			if (triangle_end > (finest ? h.finest_triangle_count : h.triangle_count) || vertex_end > (finest ? h.finest_vertex_count : h.vertex_count)) {
				return false;
			}
			for (uint64_t i = uint64_t(cluster.triangle_offset) * 3; i < triangle_end * 3; i++) {
				if (triangles[i] >= cluster.vertex_count) {
					return false;
				}
			}
		}
		return true;
	}

public:
	explicit View(const Vector<uint8_t> &p_data) {
		const uint64_t size = p_data.size();
		if (size < sizeof(Header)) {
			return;
		}
		const uint8_t *ptr = p_data.ptr();
		const Header *h = reinterpret_cast<const Header *>(ptr);
		if (h->magic != MAGIC || h->version != VERSION || h->cluster_count == 0) {
			return;
		}
		const uint64_t groups_offset = sizeof(Header);
		const uint64_t clusters_offset = groups_offset + uint64_t(h->group_count) * sizeof(Group);
		const uint64_t triangles_offset = clusters_offset + uint64_t(h->cluster_count) * sizeof(Cluster);
		const uint64_t vertices_offset = triangles_offset + ((uint64_t(h->triangle_count) * 3 + 3) & ~uint64_t(3));
		const uint64_t positions_offset = vertices_offset + uint64_t(h->vertex_count) * sizeof(uint32_t);
		if (positions_offset + uint64_t(h->vertex_count) * 3 * sizeof(float) != size) {
			return;
		}
		header = h;
		groups = reinterpret_cast<const Group *>(ptr + groups_offset);
		clusters = reinterpret_cast<const Cluster *>(ptr + clusters_offset);
		triangles = ptr + triangles_offset;
		vertices = reinterpret_cast<const uint32_t *>(ptr + vertices_offset);
		positions = reinterpret_cast<const float *>(ptr + positions_offset);
		if (!_validate()) {
			header = nullptr;
		}
	}

	bool is_valid() const { return header != nullptr; }
	const Header &get_header() const { return *header; }
	Span<Group> get_groups() const { return Span<Group>(groups, header->group_count); }
	Span<Cluster> get_clusters() const { return Span<Cluster>(clusters, header->cluster_count); }
	Span<uint8_t> get_triangles() const { return Span<uint8_t>(triangles, uint64_t(header->triangle_count) * 3); }
	Span<uint32_t> get_vertices() const { return Span<uint32_t>(vertices, header->vertex_count); }
	Span<float> get_positions() const { return Span<float>(positions, uint64_t(header->vertex_count) * 3); }
};

} // namespace MeshClusterData
