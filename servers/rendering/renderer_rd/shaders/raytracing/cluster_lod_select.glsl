#[compute]

#version 450

#VERSION_DEFINES

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_arithmetic : require

// Picks the clusters of one traced instance of a surface with cluster LOD, one workgroup per instance (job), and
// writes their addresses as the cluster references its BLAS is built from.
//
// The DAG cut: a cluster is used when its own error, projected from the camera, is within the threshold and the
// error of the group it's in (which the next level replaces it with) is not. The bake makes errors and bounds
// monotonic, so exactly one cluster covers each part of the mesh. Distance only: no frustum, so rays that leave the
// view (reflections, shadows) see the same detail as the camera.
//
// The references keep the order of the clusters, so a cut always gives the same BLAS.

#define WORKGROUP_SIZE 256
#define MAX_SUBGROUPS 64

layout(local_size_x = WORKGROUP_SIZE, local_size_y = 1, local_size_z = 1) in;

// Matches RTClusterLod, 48 bytes.
struct ClusterLod {
	vec4 bounds; // Center and radius of the cluster's own error, in mesh space.
	vec4 group_bounds; // The same for the error of its group.
	float error;
	float group_error; // FLT_MAX when the group isn't simplified further.
	uint triangle_count;
	uint pad;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer ClusterLodBuffer {
	ClusterLod v[];
};

layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer AddressBuffer {
	uint64_t v[];
};

// Matches RD::AccelerationStructureClusterReferences, followed by the references from REFERENCES_OFFSET.
layout(buffer_reference, std430, buffer_reference_align = 16) writeonly buffer ReferencesBuffer {
	uint count;
	uint stride;
	uint64_t address;
};

layout(buffer_reference, std430, buffer_reference_align = 8) writeonly buffer ReferenceArray {
	uint64_t v[];
};

// Matches RTClusterLodJob, 80 bytes.
struct Job {
	vec3 camera; // In mesh space.
	float pixel_scale; // Projected error over threshold per unit of world error at unit distance.
	float min_scale; // Smallest and largest scale of the instance transform.
	float max_scale;
	float z_near;
	uint flags;
	uint cluster_count;
	uint stats_slot;
	uint reference_capacity; // References the buffer and the BLAS hold: the most clusters a cut can have.
	uint pad1;
	uint64_t lods;
	uint64_t cluster_addresses;
	uint64_t references;
	uint64_t pad2;
};

#define JOB_FLAG_ORTHOGRAPHIC 1u

// Must match RT_CLUSTER_LOD_REFERENCES_OFFSET.
#define REFERENCES_OFFSET 256

layout(set = 0, binding = 0, std430) restrict readonly buffer Jobs {
	Job v[];
}
jobs;

// Per stats slot: clusters and triangles of the last cut. The cluster count is the cut's own, even past the reference
// capacity, so the renderer can report an overflow.
layout(set = 0, binding = 1, std430) restrict writeonly buffer Stats {
	uvec2 v[];
}
stats;

shared uint subgroup_counts[MAX_SUBGROUPS];

// Error of bounds p_bounds over the threshold, as seen from the camera; the cut uses what's at most 1.
float projected_error(Job p_job, vec4 p_bounds, float p_error) {
	float world_error = p_error * p_job.max_scale * p_job.pixel_scale;
	if ((p_job.flags & JOB_FLAG_ORTHOGRAPHIC) != 0u) {
		return world_error;
	}
	// The nearest point of the bounds, never closer than the true distance under a non-uniform scale.
	float gap = distance(p_job.camera, p_bounds.xyz) * p_job.min_scale - p_bounds.w * p_job.max_scale;
	return world_error / max(gap, p_job.z_near);
}

void main() {
	Job job = jobs.v[gl_WorkGroupID.x];
	ClusterLodBuffer lods = ClusterLodBuffer(job.lods);
	AddressBuffer cluster_addresses = AddressBuffer(job.cluster_addresses);
	ReferenceArray references = ReferenceArray(job.references + uint64_t(REFERENCES_OFFSET));

	uint selected_count = 0u;
	uint triangle_count = 0u;
	for (uint first = 0u; first < job.cluster_count; first += WORKGROUP_SIZE) {
		uint cluster = first + gl_LocalInvocationIndex;
		bool selected = false;
		if (cluster < job.cluster_count) {
			ClusterLod lod = lods.v[cluster];
			selected = projected_error(job, lod.bounds, lod.error) <= 1.0 && projected_error(job, lod.group_bounds, lod.group_error) > 1.0;
			triangle_count += selected ? lod.triangle_count : 0u;
		}

		uvec4 ballot = subgroupBallot(selected);
		if (subgroupElect()) {
			subgroup_counts[gl_SubgroupID] = subgroupBallotBitCount(ballot);
		}
		barrier();
		uint offset = selected_count + subgroupBallotExclusiveBitCount(ballot);
		for (uint s = 0u; s < gl_NumSubgroups; s++) {
			offset += s < gl_SubgroupID ? subgroup_counts[s] : 0u;
			selected_count += subgroup_counts[s];
		}
		// The capacity bounds every cut of a monotonic DAG; it only guards against rounding.
		if (selected && offset < job.reference_capacity) {
			references.v[offset] = cluster_addresses.v[cluster];
		}
		// The counts are rewritten by the next round.
		barrier();
	}

	triangle_count = subgroupAdd(triangle_count);
	if (subgroupElect()) {
		subgroup_counts[gl_SubgroupID] = triangle_count;
	}
	barrier();
	if (gl_LocalInvocationIndex == 0u) {
		uint total_triangles = 0u;
		for (uint s = 0u; s < gl_NumSubgroups; s++) {
			total_triangles += subgroup_counts[s];
		}
		ReferencesBuffer header = ReferencesBuffer(job.references);
		header.count = min(selected_count, job.reference_capacity);
		header.stride = 8u;
		header.address = job.references + uint64_t(REFERENCES_OFFSET);
		stats.v[job.stats_slot] = uvec2(selected_count, total_triangles);
	}
}
