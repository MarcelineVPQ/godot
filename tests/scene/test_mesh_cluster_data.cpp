/**************************************************************************/
/*  test_mesh_cluster_data.cpp                                            */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mesh_cluster_data)

#ifndef _3D_DISABLED

#include "core/math/geometry_2d.h"
#include "core/math/random_pcg.h"
#include "scene/resources/surface_tool.h"
#include "servers/rendering/mesh_cluster_data.h"

#include <cfloat>

namespace TestMeshClusterData {

// A gently rolling grid of p_size x p_size quads over [0, 1]^2, so simplification has some error to report and its
// triangles never fold over when seen from above.
struct Grid {
	LocalVector<float> positions;
	LocalVector<float> attributes;
	LocalVector<uint32_t> indices;
	uint32_t vertex_count = 0;
};

static Grid make_grid(uint32_t p_size) {
	Grid grid;
	const uint32_t side = p_size + 1;
	grid.vertex_count = side * side;
	grid.positions.resize(grid.vertex_count * 3);
	grid.attributes.resize(grid.vertex_count * SurfaceTool::CLUSTER_ATTRIBUTE_COUNT);
	for (uint32_t y = 0; y < side; y++) {
		for (uint32_t x = 0; x < side; x++) {
			const uint32_t v = y * side + x;
			const float u = float(x) / p_size;
			const float w = float(y) / p_size;
			grid.positions[v * 3 + 0] = u;
			grid.positions[v * 3 + 1] = w;
			grid.positions[v * 3 + 2] = 0.02f * Math::sin(u * 7.0f) * Math::cos(w * 5.0f);
			float *attribute = &grid.attributes[v * SurfaceTool::CLUSTER_ATTRIBUTE_COUNT];
			attribute[0] = 0.0f;
			attribute[1] = 0.0f;
			attribute[2] = 1.0f;
			attribute[3] = u;
			attribute[4] = w;
		}
	}
	for (uint32_t y = 0; y < p_size; y++) {
		for (uint32_t x = 0; x < p_size; x++) {
			const uint32_t v = y * side + x;
			for (uint32_t index : { v, v + 1, v + side + 1, v, v + side + 1, v + side }) {
				grid.indices.push_back(index);
			}
		}
	}
	return grid;
}

static Vector<uint8_t> bake(const Grid &p_grid) {
	return SurfaceTool::build_clusters_func(p_grid.positions.ptr(), p_grid.attributes.ptr(), p_grid.vertex_count, p_grid.indices.ptr(), p_grid.indices.size());
}

TEST_CASE("[SceneTree][MeshClusterData] Bake and read back") {
	REQUIRE_MESSAGE(SurfaceTool::build_clusters_func != nullptr, "The meshoptimizer module bakes clusters.");
	const Grid grid = make_grid(64);
	const Vector<uint8_t> data = bake(grid);
	const MeshClusterData::View view(data);
	REQUIRE(view.is_valid());

	const MeshClusterData::Header &header = view.get_header();
	CHECK(header.level_count > 1);
	CHECK(header.finest_triangle_count == grid.indices.size() / 3);

	// The source mesh's clusters hold every source triangle once, with the source positions.
	const Span<MeshClusterData::Cluster> clusters = view.get_clusters();
	const Span<uint8_t> triangles = view.get_triangles();
	const Span<uint32_t> vertices = view.get_vertices();
	const Span<float> positions = view.get_positions();
	HashMap<uint64_t, int> source_triangles;
	for (uint32_t i = 0; i < grid.indices.size(); i += 3) {
		source_triangles[(uint64_t(grid.indices[i]) << 42) | (uint64_t(grid.indices[i + 1]) << 21) | grid.indices[i + 2]]++;
	}
	bool positions_match = true;
	for (uint32_t c = 0; c < header.finest_cluster_count; c++) {
		const MeshClusterData::Cluster &cluster = clusters[c];
		for (uint32_t t = cluster.triangle_offset; t < cluster.triangle_offset + cluster.triangle_count; t++) {
			uint32_t corners[3];
			for (int k = 0; k < 3; k++) {
				const uint32_t local = cluster.vertex_offset + triangles[t * 3 + k];
				corners[k] = vertices[local];
				for (int axis = 0; axis < 3; axis++) {
					positions_match = positions_match && positions[local * 3 + axis] == grid.positions[corners[k] * 3 + axis];
				}
			}
			// Clusters keep the winding but may start a triangle at any corner.
			bool found = false;
			for (int r = 0; r < 3 && !found; r++) {
				const uint64_t key = (uint64_t(corners[r]) << 42) | (uint64_t(corners[(r + 1) % 3]) << 21) | corners[(r + 2) % 3];
				int *count = source_triangles.getptr(key);
				if (count && *count > 0) {
					(*count)--;
					found = true;
				}
			}
			CHECK(found);
		}
	}
	CHECK(positions_match);
	for (const KeyValue<uint64_t, int> &E : source_triangles) {
		CHECK(E.value == 0);
	}

	SUBCASE("Malformed data is not valid") {
		Vector<uint8_t> broken = data;
		MeshClusterData::Cluster *first = reinterpret_cast<MeshClusterData::Cluster *>(broken.ptrw() + sizeof(MeshClusterData::Header) + header.group_count * sizeof(MeshClusterData::Group));
		first->group = header.group_count;
		CHECK_FALSE(MeshClusterData::View(broken).is_valid());

		Vector<uint8_t> truncated = data;
		truncated.resize(data.size() - 4);
		CHECK_FALSE(MeshClusterData::View(truncated).is_valid());
	}
}

TEST_CASE("[SceneTree][MeshClusterData] Group errors bound their clusters' errors") {
	REQUIRE_MESSAGE(SurfaceTool::build_clusters_func != nullptr, "The meshoptimizer module bakes clusters.");
	const Vector<uint8_t> data = bake(make_grid(64));
	const MeshClusterData::View view(data);
	REQUIRE(view.is_valid());
	const Span<MeshClusterData::Group> groups = view.get_groups();
	for (const MeshClusterData::Cluster &cluster : view.get_clusters()) {
		CHECK(groups[cluster.group].error >= cluster.error);
	}
}

// Number of triangles of the cut at p_threshold that contain (p_x, p_y) seen from above.
static int cut_coverage(const MeshClusterData::View &p_view, float p_threshold, float p_x, float p_y) {
	const Span<MeshClusterData::Group> groups = p_view.get_groups();
	const Span<uint8_t> triangles = p_view.get_triangles();
	const Span<float> positions = p_view.get_positions();
	int covered = 0;
	for (const MeshClusterData::Cluster &cluster : p_view.get_clusters()) {
		if (!(cluster.error <= p_threshold && groups[cluster.group].error > p_threshold)) {
			continue;
		}
		for (uint32_t t = cluster.triangle_offset; t < cluster.triangle_offset + cluster.triangle_count; t++) {
			Vector2 corners[3];
			for (int k = 0; k < 3; k++) {
				const uint32_t local = cluster.vertex_offset + triangles[t * 3 + k];
				corners[k] = Vector2(positions[local * 3 + 0], positions[local * 3 + 1]);
			}
			if (Geometry2D::is_point_in_triangle(Vector2(p_x, p_y), corners[0], corners[1], corners[2])) {
				covered++;
			}
		}
	}
	return covered;
}

TEST_CASE("[SceneTree][MeshClusterData] Every cut covers the mesh once") {
	REQUIRE_MESSAGE(SurfaceTool::build_clusters_func != nullptr, "The meshoptimizer module bakes clusters.");
	const Vector<uint8_t> data = bake(make_grid(64));
	const MeshClusterData::View view(data);
	REQUIRE(view.is_valid());

	// Thresholds at, between and around the errors the groups have, so cuts land on every level.
	LocalVector<float> errors;
	for (const MeshClusterData::Group &group : view.get_groups()) {
		if (group.error < FLT_MAX) {
			errors.push_back(group.error);
		}
	}
	errors.sort();
	LocalVector<float> thresholds = { 0.0f };
	for (uint32_t i = 1; i <= 6 && !errors.is_empty(); i++) {
		thresholds.push_back(errors[(errors.size() - 1) * i / 6]);
	}
	thresholds.push_back(1e30f);

	// Simplification may move the mesh's outer edge, so the points stay inside it.
	RandomPCG rng(12345);
	for (float threshold : thresholds) {
		int wrong = 0;
		for (int i = 0; i < 300; i++) {
			const float x = 0.2f + 0.6f * rng.randf();
			const float y = 0.2f + 0.6f * rng.randf();
			wrong += cut_coverage(view, threshold, x, y) != 1;
		}
		CHECK_MESSAGE(wrong == 0, vformat("Points not covered exactly once at threshold %f: %d of 300.", threshold, wrong));
	}
}

} // namespace TestMeshClusterData

#endif // _3D_DISABLED
