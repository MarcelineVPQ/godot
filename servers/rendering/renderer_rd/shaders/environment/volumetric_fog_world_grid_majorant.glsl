#[compute]

#version 450

#VERSION_DEFINES

// Highest density of each brick of the path tracer's fog world grid and the voxels around it, so its tracking can skip
// clear space.

#define BRICK_SIZE 4

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(r32ui, set = 0, binding = 0) uniform restrict readonly uimage3D density_map;
layout(r32ui, set = 0, binding = 1) uniform restrict writeonly uimage3D majorant_map;

void main() {
	ivec3 brick = ivec3(gl_GlobalInvocationID.xyz);
	if (any(greaterThanEqual(brick, imageSize(majorant_map)))) {
		return;
	}

	// The brick's voxels and one more on each side: the path tracer interpolates the densities between voxel centers,
	// which mixes in the voxels next to the brick, and the majorant must bound what it reads there.
	ivec3 from = max(brick * BRICK_SIZE - 1, ivec3(0));
	ivec3 to = min(brick * BRICK_SIZE + BRICK_SIZE + 1, imageSize(density_map));
	// Densities are signed fixed point; subtractive volumes can leave voxels below zero.
	int majorant = 0;
	for (int z = from.z; z < to.z; z++) {
		for (int y = from.y; y < to.y; y++) {
			for (int x = from.x; x < to.x; x++) {
				majorant = max(majorant, int(imageLoad(density_map, ivec3(x, y, z)).x));
			}
		}
	}
	imageStore(majorant_map, brick, uvec4(uint(majorant)));
}
