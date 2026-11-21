// World-space fog grid lookups for the path tracer. Fog::volumetric_fog_world_grid_update() renders the FogVolumes
// into a world-aligned grid around the camera, so unlike the froxels any path segment can sample it.
// The maps hold the froxel pass's packed atomics (see volumetric_fog.glsl).

layout(set = 0, binding = 42) uniform utexture3D rt_fog_world_grid_density;
layout(set = 0, binding = 43) uniform utexture3D rt_fog_world_grid_scattering;
layout(set = 0, binding = 44) uniform utexture3D rt_fog_world_grid_emission;

#define RT_FOG_WORLD_GRID_DENSITY_SCALE 1024.0

struct RTFogWorldGridSample {
	float extinction; // DENSITY, per meter.
	vec3 albedo; // ALBEDO, recovered from the stored scattering.
	vec3 emission; // EMISSION scaled by min(DENSITY, 1), as the froxel pass stores it.
};

bool rt_fog_world_grid_enabled() {
	return get_rt_param(RT_PARAM_FOG_WORLD_GRID_VOXEL_SIZE) > 0.0;
}

vec3 rt_fog_world_grid_origin() {
	return vec3(get_rt_param(RT_PARAM_FOG_WORLD_GRID_ORIGIN_X), get_rt_param(RT_PARAM_FOG_WORLD_GRID_ORIGIN_Y), get_rt_param(RT_PARAM_FOG_WORLD_GRID_ORIGIN_Z));
}

ivec3 rt_fog_world_grid_size() {
	return textureSize(usampler3D(rt_fog_world_grid_density, SAMPLER_NEAREST_CLAMP), 0);
}

float rt_fog_world_grid_extinction_at_voxel(ivec3 p_voxel) {
	uint density_u = texelFetch(usampler3D(rt_fog_world_grid_density, SAMPLER_NEAREST_CLAMP), p_voxel, 0).x;
	return max(0.0, float(int(density_u)) / RT_FOG_WORLD_GRID_DENSITY_SCALE);
}

RTFogWorldGridSample rt_fog_world_grid_sample_voxel(ivec3 p_voxel) {
	RTFogWorldGridSample s;
	s.extinction = rt_fog_world_grid_extinction_at_voxel(p_voxel);

	uint scattering_u = texelFetch(usampler3D(rt_fog_world_grid_scattering, SAMPLER_NEAREST_CLAMP), p_voxel, 0).x;
	vec3 scattering = vec3(scattering_u >> 21, (scattering_u << 11) >> 21, scattering_u % 1024) / vec3(2047.0, 2047.0, 1023.0);
	// Stored as ALBEDO * min(DENSITY, 1). Overlapping volumes add up, so this is their density-weighted mix.
	s.albedo = clamp(scattering / clamp(s.extinction, 1e-4, 1.0), vec3(0.0), vec3(1.0));

	uint emission_u = texelFetch(usampler3D(rt_fog_world_grid_emission, SAMPLER_NEAREST_CLAMP), p_voxel, 0).x;
	s.emission = vec3(emission_u >> 21, (emission_u << 11) >> 21, emission_u % 1024) / vec3(511.0, 511.0, 255.0);
	return s;
}

// The medium at p_world (nearest voxel). Outside the grid, or without one, there is none.
RTFogWorldGridSample rt_fog_world_grid_sample(vec3 p_world) {
	RTFogWorldGridSample s;
	s.extinction = 0.0;
	s.albedo = vec3(0.0);
	s.emission = vec3(0.0);
	if (!rt_fog_world_grid_enabled()) {
		return s;
	}
	ivec3 voxel = ivec3(floor((p_world - rt_fog_world_grid_origin()) / get_rt_param(RT_PARAM_FOG_WORLD_GRID_VOXEL_SIZE)));
	if (any(lessThan(voxel, ivec3(0))) || any(greaterThanEqual(voxel, rt_fog_world_grid_size()))) {
		return s;
	}
	return rt_fog_world_grid_sample_voxel(voxel);
}

// Optical depth from p_origin along the unit direction p_dir up to p_t_max, walking every voxel the ray crosses.
float rt_fog_world_grid_optical_depth(vec3 p_origin, vec3 p_dir, float p_t_max) {
	if (!rt_fog_world_grid_enabled()) {
		return 0.0;
	}
	float voxel_size = get_rt_param(RT_PARAM_FOG_WORLD_GRID_VOXEL_SIZE);
	ivec3 size = rt_fog_world_grid_size();

	// In voxel units from here on. Zero direction components get a tiny positive one, so they never step.
	vec3 o = (p_origin - rt_fog_world_grid_origin()) / voxel_size;
	vec3 d = mix(p_dir, vec3(1e-8), lessThan(abs(p_dir), vec3(1e-8)));
	vec3 inv_d = 1.0 / d;

	vec3 t_a = -o * inv_d;
	vec3 t_b = (vec3(size) - o) * inv_d;
	vec3 t_near3 = min(t_a, t_b);
	vec3 t_far3 = max(t_a, t_b);
	float t = max(max(max(t_near3.x, t_near3.y), t_near3.z), 0.0);
	float t_end = min(min(min(t_far3.x, t_far3.y), t_far3.z), p_t_max / voxel_size);
	if (t >= t_end) {
		return 0.0;
	}

	ivec3 step_dir = ivec3(sign(d));
	ivec3 voxel = clamp(ivec3(floor(o + d * t)), ivec3(0), size - 1);
	vec3 t_next = (vec3(voxel) + vec3(greaterThan(step_dir, ivec3(0))) - o) * inv_d;
	vec3 t_delta = abs(inv_d);

	float optical_depth = 0.0;
	int max_steps = size.x + size.y + size.z;
	for (int i = 0; i < max_steps; i++) {
		float t_exit = min(min(min(t_next.x, t_next.y), t_next.z), t_end);
		optical_depth += rt_fog_world_grid_extinction_at_voxel(voxel) * max(t_exit - t, 0.0);
		if (t_exit >= t_end) {
			break;
		}
		t = t_exit;
		if (t_next.x <= t_next.y && t_next.x <= t_next.z) {
			voxel.x += step_dir.x;
			t_next.x += t_delta.x;
		} else if (t_next.y <= t_next.z) {
			voxel.y += step_dir.y;
			t_next.y += t_delta.y;
		} else {
			voxel.z += step_dir.z;
			t_next.z += t_delta.z;
		}
		if (any(lessThan(voxel, ivec3(0))) || any(greaterThanEqual(voxel, size))) {
			break;
		}
	}
	return optical_depth * voxel_size;
}

#define RT_VIS_VOLUMETRIC_FOG_WORLD_GRID 30

// Opacity of the grid along a ray up to p_t (in units of p_dir), magenta when no grid is bound.
vec3 rt_fog_world_grid_debug(vec3 p_origin, vec3 p_dir, float p_t) {
	if (!rt_fog_world_grid_enabled()) {
		return vec3(1.0, 0.0, 1.0);
	}
	float dir_length = length(p_dir);
	return vec3(1.0 - exp(-rt_fog_world_grid_optical_depth(p_origin, p_dir / dir_length, p_t * dir_length)));
}
