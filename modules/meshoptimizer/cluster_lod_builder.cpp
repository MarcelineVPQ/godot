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

using namespace MeshClusterData;

template <typename T>
static void _append_bytes(Vector<uint8_t> &r_data, uint64_t &r_offset, const T *p_items, uint64_t p_count) {
	const uint64_t size = p_count * sizeof(T);
	memcpy(r_data.ptrw() + r_offset, p_items, size);
	r_offset += size;
}

Vector<uint8_t> cluster_lod_build(const float *p_positions, const float *p_attributes, size_t p_vertex_count, const unsigned int *p_indices, size_t p_index_count) {
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
			// Monotonic DAG: a group's error never falls below the error of the clusters it replaces.
			group.error = MAX(group.error, cluster.error);

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
