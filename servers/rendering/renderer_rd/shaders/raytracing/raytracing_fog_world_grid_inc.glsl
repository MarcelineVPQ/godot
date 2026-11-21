// World-space fog grid lookups for the path tracer. Fog::volumetric_fog_world_grid_update() renders the FogVolumes
// into a world-aligned grid around the camera, so unlike the froxels any path segment can sample it.
// The maps hold signed fixed-point sums at RT_FOG_WORLD_GRID_SCALE (see MODE_WORLD_GRID in volumetric_fog.glsl): the
// extinction, and per colour channel the scattering coefficient and the emitted radiance per meter. The scattering and
// emission maps hold one slab of the grid's size per channel, stacked along z.

layout(set = 0, binding = 42) uniform utexture3D rt_fog_world_grid_density;
layout(set = 0, binding = 43) uniform utexture3D rt_fog_world_grid_scattering;
layout(set = 0, binding = 44) uniform utexture3D rt_fog_world_grid_emission;
// Highest density of each brick of RT_FOG_WORLD_GRID_BRICK_SIZE^3 voxels, packed like the density map.
layout(set = 0, binding = 45) uniform utexture3D rt_fog_world_grid_majorant;

// Must match DENSITY_SCALE of MODE_WORLD_GRID in volumetric_fog.glsl.
#define RT_FOG_WORLD_GRID_SCALE 16384.0
#define RT_FOG_WORLD_GRID_BRICK_SIZE 8.0
// Tentative collisions a tracked ray tests freely. Past them, each one ends the walk with probability
// RT_FOG_WORLD_GRID_ROULETTE_END and the walks that go on weigh that much more (Russian roulette, no bias).
#define RT_FOG_WORLD_GRID_ROULETTE_COLLISIONS 64
#define RT_FOG_WORLD_GRID_ROULETTE_END 0.1
// Safety cap: a walk that reaches it is absorbed. After the roulette above, that takes odds of 0.9^4032.
#define RT_FOG_WORLD_GRID_MAX_COLLISIONS 4096

struct RTFogWorldGridSample {
	float extinction; // DENSITY, per meter.
	vec3 albedo; // ALBEDO: the scattering coefficient over the extinction.
	vec3 emission; // Emitted radiance per meter: EMISSION * DENSITY.
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

// Voxel box holding every FogVolume this frame. Empty when light transport through the grid is off.
vec3 rt_fog_world_grid_occupied_min() {
	return vec3(get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MIN_X), get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MIN_Y), get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MIN_Z));
}

vec3 rt_fog_world_grid_occupied_max() {
	return vec3(get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MAX_X), get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MAX_Y), get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MAX_Z));
}

// True when path segments and shadow rays scatter, absorb and emit in the grid (rendering/pathtracing/volumetric_fog_world_grid_transport).
bool rt_fog_world_grid_transport_enabled() {
	return rt_fog_world_grid_enabled() && get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MAX_X) > get_rt_param(RT_PARAM_FOG_WORLD_GRID_OCCUPIED_MIN_X);
}

// Subtractive volumes can leave a sum below zero; there is then no medium.
float rt_fog_world_grid_unpack(uint p_value) {
	return max(0.0, float(int(p_value)) / RT_FOG_WORLD_GRID_SCALE);
}

float rt_fog_world_grid_extinction_at_voxel(ivec3 p_voxel) {
	return rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_density, SAMPLER_NEAREST_CLAMP), p_voxel, 0).x);
}

// Extinction at p_world (nearest voxel), none outside the grid.
float rt_fog_world_grid_extinction_at(vec3 p_world) {
	ivec3 voxel = ivec3(floor((p_world - rt_fog_world_grid_origin()) / get_rt_param(RT_PARAM_FOG_WORLD_GRID_VOXEL_SIZE)));
	if (any(lessThan(voxel, ivec3(0))) || any(greaterThanEqual(voxel, rt_fog_world_grid_size()))) {
		return 0.0;
	}
	return rt_fog_world_grid_extinction_at_voxel(voxel);
}

RTFogWorldGridSample rt_fog_world_grid_sample_voxel(ivec3 p_voxel) {
	RTFogWorldGridSample s;
	s.extinction = rt_fog_world_grid_extinction_at_voxel(p_voxel);

	vec3 scattering;
	for (int c = 0; c < 3; c++) {
		ivec3 texel = ivec3(p_voxel.xy, p_voxel.z + c * rt_fog_world_grid_size().z);
		scattering[c] = rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_scattering, SAMPLER_NEAREST_CLAMP), texel, 0).x);
		s.emission[c] = rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_emission, SAMPLER_NEAREST_CLAMP), texel, 0).x);
	}
	// Overlapping volumes add up, so this is their extinction-weighted mix of ALBEDO.
	s.albedo = s.extinction > 0.0 ? clamp(scattering / s.extinction, vec3(0.0), vec3(1.0)) : vec3(0.0);
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

// Walks every voxel the ray crosses inside the voxel box [p_box_min, p_box_max), from p_origin along the unit p_dir up to
// p_t_max meters, and returns the optical depth. Stops early once that reaches p_stop_depth: r_t is then the distance
// where it did, and -1 when it never does.
float rt_fog_world_grid_march(vec3 p_origin, vec3 p_dir, float p_t_max, vec3 p_box_min, vec3 p_box_max, float p_stop_depth, out float r_t) {
	r_t = -1.0;
	float voxel_size = get_rt_param(RT_PARAM_FOG_WORLD_GRID_VOXEL_SIZE);
	ivec3 size = rt_fog_world_grid_size();

	// In voxel units from here on. Zero direction components get a tiny positive one, so they never step.
	vec3 o = (p_origin - rt_fog_world_grid_origin()) / voxel_size;
	vec3 d = mix(p_dir, vec3(1e-8), lessThan(abs(p_dir), vec3(1e-8)));
	vec3 inv_d = 1.0 / d;

	vec3 t_a = (p_box_min - o) * inv_d;
	vec3 t_b = (p_box_max - o) * inv_d;
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

	// Optical depth per voxel unit of distance until the end.
	float stop_depth = p_stop_depth / voxel_size;
	float optical_depth = 0.0;
	int max_steps = size.x + size.y + size.z;
	for (int i = 0; i < max_steps; i++) {
		float t_exit = min(min(min(t_next.x, t_next.y), t_next.z), t_end);
		float extinction = rt_fog_world_grid_extinction_at_voxel(voxel);
		float voxel_depth = extinction * max(t_exit - t, 0.0);
		if (optical_depth + voxel_depth >= stop_depth) {
			r_t = (t + (stop_depth - optical_depth) / extinction) * voxel_size;
			return p_stop_depth;
		}
		optical_depth += voxel_depth;
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

// Optical depth from p_origin along the unit direction p_dir up to p_t_max, walking every voxel the ray crosses.
float rt_fog_world_grid_optical_depth(vec3 p_origin, vec3 p_dir, float p_t_max) {
	if (!rt_fog_world_grid_enabled()) {
		return 0.0;
	}
	float t_stop;
	return rt_fog_world_grid_march(p_origin, p_dir, p_t_max, vec3(0.0), vec3(rt_fog_world_grid_size()), 1e30, t_stop);
}

// Tracks a ray from p_origin along the unit p_dir up to p_t_max meters through the occupied part of the grid, sampling
// tentative collisions against each brick's highest extinction, so clear bricks cost one fetch and thin ones few steps.
// Delta tracking (p_ratio false) stops at the first real collision: r_collided is then true and r_t its distance. Ratio
// tracking (p_ratio true) walks the whole ray. The return value is the weight of the walk, which Russian roulette past
// RT_FOG_WORLD_GRID_ROULETTE_COLLISIONS raises, or 0 when roulette or the safety cap ended it; for ratio tracking it
// includes the transmittance estimate. Both estimators are unbiased but for the safety cap, which absorbs the walks
// that reach it (RT_FOG_WORLD_GRID_MAX_COLLISIONS). r_collisions counts the tentative collisions tested.
float rt_fog_world_grid_track(vec3 p_origin, vec3 p_dir, float p_t_max, bool p_ratio, inout uint r_rng, out bool r_collided, out float r_t, out int r_collisions) {
	r_collided = false;
	r_t = p_t_max;
	r_collisions = 0;
	float voxel_size = get_rt_param(RT_PARAM_FOG_WORLD_GRID_VOXEL_SIZE);
	float brick_size = voxel_size * RT_FOG_WORLD_GRID_BRICK_SIZE;
	ivec3 brick_count = textureSize(usampler3D(rt_fog_world_grid_majorant, SAMPLER_NEAREST_CLAMP), 0);
	ivec3 grid_size = rt_fog_world_grid_size();
	vec3 grid_origin = rt_fog_world_grid_origin();

	// Positions in brick units from here on; s is the distance in brick sizes, t in meters.
	vec3 o = (p_origin - grid_origin) / brick_size;
	vec3 d = mix(p_dir, vec3(1e-8), lessThan(abs(p_dir), vec3(1e-8)));
	vec3 inv_d = 1.0 / d;

	vec3 s_a = (rt_fog_world_grid_occupied_min() / RT_FOG_WORLD_GRID_BRICK_SIZE - o) * inv_d;
	vec3 s_b = (rt_fog_world_grid_occupied_max() / RT_FOG_WORLD_GRID_BRICK_SIZE - o) * inv_d;
	vec3 s_near3 = min(s_a, s_b);
	vec3 s_far3 = max(s_a, s_b);
	float s = max(max(max(s_near3.x, s_near3.y), s_near3.z), 0.0);
	float s_end = min(min(min(s_far3.x, s_far3.y), s_far3.z), p_t_max / brick_size);
	if (s >= s_end) {
		return 1.0;
	}

	ivec3 step_dir = ivec3(sign(d));
	ivec3 brick = clamp(ivec3(floor(o + d * s)), ivec3(0), brick_count - 1);
	vec3 s_next = (vec3(brick) + vec3(greaterThan(step_dir, ivec3(0))) - o) * inv_d;
	vec3 s_delta = abs(inv_d);

	float weight = 1.0;
	int max_steps = brick_count.x + brick_count.y + brick_count.z;
	for (int i = 0; i < max_steps; i++) {
		float s_exit = min(min(min(s_next.x, s_next.y), s_next.z), s_end);
		float majorant = rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_majorant, SAMPLER_NEAREST_CLAMP), brick, 0).x);
		if (majorant > 0.0) {
			// The voxels this brick's majorant was taken over; a point on its faces is clamped back into them.
			ivec3 voxel_first = brick * int(RT_FOG_WORLD_GRID_BRICK_SIZE);
			ivec3 voxel_last = min(voxel_first + int(RT_FOG_WORLD_GRID_BRICK_SIZE) - 1, grid_size - 1);
			float t = s * brick_size;
			float t_exit = s_exit * brick_size;
			while (true) {
				t -= log(1.0 - rand(r_rng)) / majorant;
				if (t >= t_exit) {
					break;
				}
				r_collisions++;
				if (r_collisions >= RT_FOG_WORLD_GRID_MAX_COLLISIONS) {
					return 0.0;
				}
				if (r_collisions > RT_FOG_WORLD_GRID_ROULETTE_COLLISIONS) {
					if (rand(r_rng) < RT_FOG_WORLD_GRID_ROULETTE_END) {
						return 0.0;
					}
					weight /= 1.0 - RT_FOG_WORLD_GRID_ROULETTE_END;
				}
				ivec3 voxel = clamp(ivec3(floor((p_origin + p_dir * t - grid_origin) / voxel_size)), voxel_first, voxel_last);
				float ratio = rt_fog_world_grid_extinction_at_voxel(voxel) / majorant;
				if (!p_ratio) {
					if (rand(r_rng) < ratio) {
						r_collided = true;
						r_t = t;
						return weight;
					}
				} else {
					weight *= 1.0 - ratio;
					// Russian roulette once little light is left, so dense media end early without bias.
					if (weight < 0.1) {
						if (rand(r_rng) >= weight * 10.0) {
							return 0.0;
						}
						weight = 0.1;
					}
				}
			}
		}
		if (s_exit >= s_end) {
			break;
		}
		s = s_exit;
		if (s_next.x <= s_next.y && s_next.x <= s_next.z) {
			brick.x += step_dir.x;
			s_next.x += s_delta.x;
		} else if (s_next.y <= s_next.z) {
			brick.y += step_dir.y;
			s_next.y += s_delta.y;
		} else {
			brick.z += step_dir.z;
			s_next.z += s_delta.z;
		}
		if (any(lessThan(brick, ivec3(0))) || any(greaterThanEqual(brick, brick_count))) {
			break;
		}
	}
	return weight;
}

// Samples the first real collision with the medium along a ray, see rt_fog_world_grid_track(): r_collided says whether
// there is one, r_t where. Returns the weight to multiply the path's throughput with, 0 when the walk was ended.
float rt_fog_world_grid_free_flight(vec3 p_origin, vec3 p_dir, float p_t_max, inout uint r_rng, out bool r_collided, out float r_t) {
	int collisions;
	return rt_fog_world_grid_track(p_origin, p_dir, p_t_max, false, r_rng, r_collided, r_t, collisions);
}

// Transmittance of the medium along a ray, estimated by ratio tracking (see rt_fog_world_grid_track()).
float rt_fog_world_grid_transmittance(vec3 p_origin, vec3 p_dir, float p_t_max, inout uint r_rng) {
	bool collided;
	float t;
	int collisions;
	return rt_fog_world_grid_track(p_origin, p_dir, p_t_max, true, r_rng, collided, t, collisions);
}

#define RT_VIS_VOLUMETRIC_FOG_WORLD_GRID 30
#define RT_VIS_VOLUMETRIC_FOG_COLLISIONS 34

// Opacity of the grid along a ray up to p_t (in units of p_dir), magenta when no grid is bound.
vec3 rt_fog_world_grid_debug(vec3 p_origin, vec3 p_dir, float p_t) {
	if (!rt_fog_world_grid_enabled()) {
		return vec3(1.0, 0.0, 1.0);
	}
	float dir_length = length(p_dir);
	return vec3(1.0 - exp(-rt_fog_world_grid_optical_depth(p_origin, p_dir / dir_length, p_t * dir_length)));
}

// Tentative collisions the tracking of a shadow ray tests along a ray up to p_t (in units of p_dir): blue for none,
// through green to red at RT_FOG_WORLD_GRID_ROULETTE_COLLISIONS, where Russian roulette starts; white where the safety
// cap absorbed the walk. Magenta means no grid is bound.
vec3 rt_fog_world_grid_collisions_debug(vec3 p_origin, vec3 p_dir, float p_t, inout uint r_rng) {
	if (!rt_fog_world_grid_enabled()) {
		return vec3(1.0, 0.0, 1.0);
	}
	float dir_length = length(p_dir);
	bool collided;
	float t;
	int collisions;
	rt_fog_world_grid_track(p_origin, p_dir / dir_length, p_t * dir_length, true, r_rng, collided, t, collisions);
	if (collisions >= RT_FOG_WORLD_GRID_MAX_COLLISIONS) {
		return vec3(1.0);
	}
	return debug_heat_color(float(collisions) / float(RT_FOG_WORLD_GRID_ROULETTE_COLLISIONS));
}
