/**************************************************************************/
/*  cluster_lod_builder.cpp                                               */
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

#include "cluster_lod_builder.h"

#include "core/templates/local_vector.h"
#include "scene/resources/surface_tool.h"
#include "servers/rendering/mesh_cluster_data.h"

#include <thirdparty/meshoptimizer/clusterlod.h>

#include <cfloat>

using namespace MeshClusterData;

template <typename T>
static void _append_bytes(Vector<uint8_t> &r_data, uint64_t &r_offset, const T *p_items, uint64_t p_count) {
	const uint64_t size = p_count * sizeof(T);
	memcpy(r_data.ptrw() + r_offset, p_items, size);
	r_offset += size;
}

// Raises each group's error to account for the surface area its simplification drops, which the positional error
// doesn't see: a leaf collapsed away is a tiny positional error, but foliage made of many leaves loses most of its
// coverage that way, and a bush seen from a little further turns into bare stems. The area a group stands for is that of
// the source triangles behind its clusters (shared out by area where a group was simplified into several clusters); the
// area lost is what its simplified clusters lack of it. Lost area A seen from distance d covers about A / 2 / d^2 of the
// view (the mean projection of a randomly turned flat patch), so an error of sqrt(A / 2) projects to the side of a square
// of that coverage, and a cut within the threshold loses at most that many square pixels per group. Clusters are in the
// order their groups were made, each group after the groups its clusters were simplified from, which keeps the DAG
// monotonic as the errors are raised.
static void _add_area_error(const float *p_positions, const LocalVector<uint8_t> &p_triangles, const LocalVector<uint32_t> &p_vertices, LocalVector<Group> &r_groups, LocalVector<Cluster> &r_clusters) {
	LocalVector<double> cluster_area;
	cluster_area.resize_initialized(r_clusters.size());
	LocalVector<double> simplified_area; // Per group: the area of the clusters it was simplified into.
	simplified_area.resize_initialized(r_groups.size());
	for (uint32_t c = 0; c < r_clusters.size(); c++) {
		const Cluster &cluster = r_clusters[c];
		double area = 0.0;
		for (uint32_t t = cluster.triangle_offset; t < cluster.triangle_offset + cluster.triangle_count; t++) {
			Vector3 corners[3];
			for (int k = 0; k < 3; k++) {
				const float *p = p_positions + uint64_t(p_vertices[cluster.vertex_offset + p_triangles[t * 3 + k]]) * 3;
				corners[k] = Vector3(p[0], p[1], p[2]);
			}
			area += 0.5 * double((corners[1] - corners[0]).cross(corners[2] - corners[0]).length());
		}
		cluster_area[c] = area;
		if (cluster.refined >= 0) {
			simplified_area[cluster.refined] += area;
		}
	}

	LocalVector<double> source_area; // Per group: the source area its clusters stand for.
	source_area.resize_initialized(r_groups.size());
	for (uint32_t c = 0; c < r_clusters.size();) {
		const uint32_t group = r_clusters[c].group;
		for (; c < r_clusters.size() && r_clusters[c].group == group; c++) {
			Cluster &cluster = r_clusters[c];
			if (cluster.refined < 0) {
				source_area[group] += cluster_area[c];
				continue;
			}
			// The group it came from is final: its error was raised before any cluster made from it is reached.
			cluster.error = r_groups[cluster.refined].error;
			r_groups[group].error = MAX(r_groups[group].error, cluster.error);
			if (simplified_area[cluster.refined] > 0.0) {
				source_area[group] += source_area[cluster.refined] * cluster_area[c] / simplified_area[cluster.refined];
			}
		}
		// Groups that weren't simplified further keep FLT_MAX; their clusters are never replaced.
		if (r_groups[group].error < FLT_MAX) {
			const double lost = MAX(0.0, source_area[group] - simplified_area[group]);
			r_groups[group].error = MAX(r_groups[group].error, float(Math::sqrt(lost / 2.0)));
		}
	}
}

Vector<uint8_t> cluster_lod_build(const float *p_positions, const float *p_attributes, size_t p_vertex_count, const unsigned int *p_indices, size_t p_index_count, bool p_preserve_area) {
	ERR_FAIL_COND_V(p_index_count == 0 || p_index_count % 3 != 0, Vector<uint8_t>());

	// Normals steer simplification at the weight meshoptimizer suggests; UV seams are protected, not weighted.
	const float attribute_weights[3] = { 0.5f, 0.5f, 0.5f };

	clodMesh mesh = {};
	mesh.indices = p_indices;
	mesh.index_count = p_index_count;
	mesh.vertex_count = p_vertex_count;
	mesh.vertex_positions = p_positions;
	mesh.vertex_positions_stride = sizeof(float) * 3;
	mesh.vertex_attributes = p_attributes;
	mesh.vertex_attributes_stride = sizeof(float) * SurfaceTool::CLUSTER_ATTRIBUTE_COUNT;
	mesh.attribute_weights = attribute_weights;
	mesh.attribute_count = 3;
	mesh.attribute_protect_mask = (1u << 3) | (1u << 4);

	LocalVector<Group> groups;
	LocalVector<Cluster> clusters;
	LocalVector<uint8_t> triangles;
	LocalVector<uint32_t> vertices;

	const auto output = [&](clodGroup p_group, const clodCluster *p_clusters, size_t p_cluster_count) -> int {
		const uint32_t group_index = groups.size();
		Group group;
		group.center[0] = p_group.simplified.center[0];
		group.center[1] = p_group.simplified.center[1];
		group.center[2] = p_group.simplified.center[2];
		group.radius = p_group.simplified.radius;
		group.error = p_group.simplified.error;
		group.level = p_group.depth;

		for (size_t i = 0; i < p_cluster_count; i++) {
			const clodCluster &source = p_clusters[i];
			Cluster cluster;
			cluster.group = group_index;
			cluster.refined = source.refined;
			cluster.triangle_offset = triangles.size() / 3;
			cluster.triangle_count = source.index_count / 3;
			cluster.vertex_offset = vertices.size();
			if (source.refined >= 0) {
				const Group &refined = groups[source.refined];
				cluster.level = refined.level + 1;
				memcpy(cluster.center, refined.center, sizeof(cluster.center));
				cluster.radius = refined.radius;
				cluster.error = refined.error;
			} else {
				memcpy(cluster.center, source.bounds.center, sizeof(cluster.center));
				cluster.radius = source.bounds.radius;
			}
			// Monotonic DAG: a group's error never falls below the error of the clusters it replaces, and its sphere
			// holds theirs, so a cut that depends on the distance to the camera stays free of cracks too.
			group.error = MAX(group.error, cluster.error);
			group.radius = MAX(group.radius, cluster_sphere_reach(group, cluster));

			triangles.resize(triangles.size() + source.index_count);
			vertices.resize(vertices.size() + source.index_count);
			cluster.vertex_count = clodLocalIndices(vertices.ptr() + cluster.vertex_offset, triangles.ptr() + cluster.triangle_offset * 3, source.indices, source.index_count);
			vertices.resize(cluster.vertex_offset + cluster.vertex_count);
			clusters.push_back(cluster);
		}
		groups.push_back(group);
		return int(group_index);
	};
	clodBuild(clodDefaultConfigRT(MAX_CLUSTER_TRIANGLES), mesh, output);
	ERR_FAIL_COND_V(clusters.is_empty(), Vector<uint8_t>());
	if (p_preserve_area) {
		_add_area_error(p_positions, triangles, vertices, groups, clusters);
	}

	Header header;
	header.cluster_count = clusters.size();
	header.group_count = groups.size();
	header.triangle_count = triangles.size() / 3;
	header.vertex_count = vertices.size();
	for (const Cluster &cluster : clusters) {
		// The renderer relies on clusters being sorted by level; clusterlod emits them that way.
		ERR_FAIL_COND_V(cluster.level + 1 < header.level_count, Vector<uint8_t>());
		ERR_FAIL_COND_V(cluster.triangle_count > MAX_CLUSTER_TRIANGLES || cluster.vertex_count > MAX_CLUSTER_VERTICES, Vector<uint8_t>());
		header.level_count = MAX(header.level_count, cluster.level + 1);
		if (cluster.level == 0) {
			header.finest_cluster_count++;
			header.finest_triangle_count += cluster.triangle_count;
			header.finest_vertex_count += cluster.vertex_count;
		}
	}

	LocalVector<float> positions;
	positions.resize(vertices.size() * 3);
	for (uint32_t i = 0; i < vertices.size(); i++) {
		memcpy(&positions[i * 3], p_positions + uint64_t(vertices[i]) * 3, sizeof(float) * 3);
	}
	const uint64_t padding = (4 - triangles.size() % 4) % 4;
	for (uint64_t i = 0; i < padding; i++) {
		triangles.push_back(0);
	}

	Vector<uint8_t> data;
	data.resize(sizeof(Header) + groups.size() * sizeof(Group) + clusters.size() * sizeof(Cluster) + triangles.size() + vertices.size() * sizeof(uint32_t) + positions.size() * sizeof(float));
	uint64_t offset = 0;
	_append_bytes(data, offset, &header, 1);
	_append_bytes(data, offset, groups.ptr(), groups.size());
	_append_bytes(data, offset, clusters.ptr(), clusters.size());
	_append_bytes(data, offset, triangles.ptr(), triangles.size());
	_append_bytes(data, offset, vertices.ptr(), vertices.size());
	_append_bytes(data, offset, positions.ptr(), positions.size());
	DEV_ASSERT(offset == uint64_t(data.size()));
	return data;
}
