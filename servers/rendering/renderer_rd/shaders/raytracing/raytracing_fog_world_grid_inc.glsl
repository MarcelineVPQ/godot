// World-space fog grid lookups for the path tracer. Fog::volumetric_fog_world_grid_update() renders the FogVolumes
// into a world-aligned grid around the camera, so unlike the froxels any path segment can sample it.
// The maps hold signed fixed-point sums (see MODE_WORLD_GRID in volumetric_fog.glsl): the extinction, and per colour
// channel the scattering coefficient at RT_FOG_WORLD_GRID_SCALE and the emitted radiance per meter at
// RT_FOG_WORLD_GRID_EMISSION_SCALE. The scattering and emission maps hold one slab of the grid's size per channel,
// stacked along z.

layout(set = 0, binding = 42) uniform utexture3D rt_fog_world_grid_density;
layout(set = 0, binding = 43) uniform utexture3D rt_fog_world_grid_scattering;
layout(set = 0, binding = 44) uniform utexture3D rt_fog_world_grid_emission;
// Highest density of each brick of RT_FOG_WORLD_GRID_BRICK_SIZE^3 voxels, packed like the density map.
layout(set = 0, binding = 45) uniform utexture3D rt_fog_world_grid_majorant;

// Must match DENSITY_SCALE of MODE_WORLD_GRID in volumetric_fog.glsl.
#define RT_FOG_WORLD_GRID_SCALE 16384.0
// Must match EMISSION_SCALE of MODE_WORLD_GRID in volumetric_fog.glsl.
#define RT_FOG_WORLD_GRID_EMISSION_SCALE 4096.0
// Small bricks keep each majorant close to the densities under it, so a ray through thin fog next to a dense core
// tests few tentative collisions.
#define RT_FOG_WORLD_GRID_BRICK_SIZE 4.0
// Safety cap: a walk that reaches it is absorbed, which darkens it (the collisions debug view shows these in white).
#define RT_FOG_WORLD_GRID_MAX_COLLISIONS 4096
// Tentative collisions shown in red by the collisions debug view.
#define RT_FOG_WORLD_GRID_DEBUG_COLLISIONS 64

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

// With light transport through the grid, the Environment's global volumetric fog is part of the same medium: a
// homogeneous one inside a sphere of radius volumetric_fog_length centered on the camera, so it moves with the camera
// and isn't fixed in the world. Its density is 0 otherwise.
float rt_fog_global_density() {
	return get_rt_param(RT_PARAM_FOG_GLOBAL_DENSITY);
}

// The part [x, y) of the ray from p_origin along the unit p_dir up to p_t_max meters inside the global fog, or (0, 0).
vec2 rt_fog_global_interval(vec3 p_origin, vec3 p_dir, float p_t_max) {
	if (rt_fog_global_density() <= 0.0) {
		return vec2(0.0);
	}
	mat3x4 inv_view = scene_data_block.data.inv_view_matrix;
	vec3 from_camera = p_origin - vec3(inv_view[0].w, inv_view[1].w, inv_view[2].w);
	float radius = get_rt_param(RT_PARAM_FOG_GLOBAL_LENGTH);
	float b = dot(from_camera, p_dir);
	float discriminant = b * b - (dot(from_camera, from_camera) - radius * radius);
	if (discriminant <= 0.0) {
		return vec2(0.0);
	}
	float root = sqrt(discriminant);
	vec2 interval = vec2(max(-b - root, 0.0), min(-b + root, p_t_max));
	return interval.x < interval.y ? interval : vec2(0.0);
}

// True when path segments and shadow rays are traced through a medium: FogVolumes in the grid, the global fog or both.
bool rt_medium_enabled() {
	return rt_fog_world_grid_transport_enabled() || rt_fog_global_density() > 0.0;
}

// Subtractive volumes can leave a sum below zero; there is then no medium.
float rt_fog_world_grid_unpack(uint p_value, float p_scale) {
	return max(0.0, float(int(p_value)) / p_scale);
}

float rt_fog_world_grid_unpack(uint p_value) {
	return rt_fog_world_grid_unpack(p_value, RT_FOG_WORLD_GRID_SCALE);
}

float rt_fog_world_grid_extinction_at_voxel(ivec3 p_voxel) {
	return rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_density, SAMPLER_NEAREST_CLAMP), p_voxel, 0).x);
}

RTFogWorldGridSample rt_fog_world_grid_sample_voxel(ivec3 p_voxel) {
	RTFogWorldGridSample s;
	s.extinction = rt_fog_world_grid_extinction_at_voxel(p_voxel);

	vec3 scattering;
	for (int c = 0; c < 3; c++) {
		ivec3 texel = ivec3(p_voxel.xy, p_voxel.z + c * rt_fog_world_grid_size().z);
		scattering[c] = rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_scattering, SAMPLER_NEAREST_CLAMP), texel, 0).x);
		s.emission[c] = rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_emission, SAMPLER_NEAREST_CLAMP), texel, 0).x, RT_FOG_WORLD_GRID_EMISSION_SCALE);
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

// The global fog, the same everywhere inside it.
RTFogWorldGridSample rt_fog_global_sample() {
	RTFogWorldGridSample s;
	s.extinction = rt_fog_global_density();
	s.albedo = vec3(get_rt_param(RT_PARAM_FOG_GLOBAL_ALBEDO_R), get_rt_param(RT_PARAM_FOG_GLOBAL_ALBEDO_G), get_rt_param(RT_PARAM_FOG_GLOBAL_ALBEDO_B));
	s.emission = vec3(get_rt_param(RT_PARAM_FOG_GLOBAL_EMISSION_R), get_rt_param(RT_PARAM_FOG_GLOBAL_EMISSION_G), get_rt_param(RT_PARAM_FOG_GLOBAL_EMISSION_B)) * s.extinction;
	return s;
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
// Delta tracking (p_ratio false) stops at the first real collision: r_collided is then true, r_t its distance and
// r_voxel the voxel it happened in (the one whose extinction was tested). Ratio tracking (p_ratio true) walks the whole
// ray. The return value is the weight of the walk, at most 1: for delta tracking 1, for ratio tracking the
// transmittance estimate, and 0 when the safety cap or ratio tracking's Russian roulette ended the walk. Both
// estimators are unbiased but for the safety cap, which absorbs the walks that reach it
// (RT_FOG_WORLD_GRID_MAX_COLLISIONS). r_collisions counts the tentative collisions tested.
float rt_fog_world_grid_track(vec3 p_origin, vec3 p_dir, float p_t_max, bool p_ratio, inout uint r_rng, out bool r_collided, out float r_t, out ivec3 r_voxel, out int r_collisions) {
	r_collided = false;
	r_t = p_t_max;
	r_voxel = ivec3(0);
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
				ivec3 voxel = clamp(ivec3(floor((p_origin + p_dir * t - grid_origin) / voxel_size)), voxel_first, voxel_last);
				float ratio = rt_fog_world_grid_extinction_at_voxel(voxel) / majorant;
				if (!p_ratio) {
					if (rand(r_rng) < ratio) {
						r_collided = true;
						r_t = t;
						r_voxel = voxel;
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
// there is one, r_t where and r_voxel in which voxel. Returns the weight to multiply the path's throughput with, 0 when
// the walk was ended.
float rt_fog_world_grid_free_flight(vec3 p_origin, vec3 p_dir, float p_t_max, inout uint r_rng, out bool r_collided, out float r_t, out ivec3 r_voxel) {
	int collisions;
	return rt_fog_world_grid_track(p_origin, p_dir, p_t_max, false, r_rng, r_collided, r_t, r_voxel, collisions);
}

// Transmittance of the medium along a ray, estimated by ratio tracking (see rt_fog_world_grid_track()).
float rt_fog_world_grid_transmittance(vec3 p_origin, vec3 p_dir, float p_t_max, inout uint r_rng) {
	bool collided;
	float t;
	ivec3 voxel;
	int collisions;
	return rt_fog_world_grid_track(p_origin, p_dir, p_t_max, true, r_rng, collided, t, voxel, collisions);
}

// The whole medium at p_world: the grid's FogVolumes and the global fog added up.
RTFogWorldGridSample rt_medium_sample(vec3 p_world) {
	RTFogWorldGridSample s = rt_fog_world_grid_sample(p_world);
	mat3x4 inv_view = scene_data_block.data.inv_view_matrix;
	float radius = get_rt_param(RT_PARAM_FOG_GLOBAL_LENGTH);
	if (rt_fog_global_density() > 0.0 && distance(p_world, vec3(inv_view[0].w, inv_view[1].w, inv_view[2].w)) < radius) {
		RTFogWorldGridSample global_fog = rt_fog_global_sample();
		float extinction = s.extinction + global_fog.extinction;
		s.albedo = (s.albedo * s.extinction + global_fog.albedo * global_fog.extinction) / extinction;
		s.emission += global_fog.emission;
		s.extinction = extinction;
	}
	return s;
}

// Free flight through the whole medium: the grid's FogVolumes and the global fog. They are independent, so the first
// real collision is the nearer of the two sampled apart, and r_medium is the medium it happened in. r_collided says
// whether there is one, r_t where. Returns the weight of the grid's walk, see rt_fog_world_grid_free_flight().
float rt_medium_free_flight(vec3 p_origin, vec3 p_dir, float p_t_max, inout uint r_rng, out bool r_collided, out float r_t, out RTFogWorldGridSample r_medium) {
	r_collided = false;
	r_t = p_t_max;
	r_medium.extinction = 0.0;
	r_medium.albedo = vec3(0.0);
	r_medium.emission = vec3(0.0);
	float weight = 1.0;
	ivec3 voxel = ivec3(0);
	if (rt_fog_world_grid_transport_enabled()) {
		weight = rt_fog_world_grid_free_flight(p_origin, p_dir, p_t_max, r_rng, r_collided, r_t, voxel);
	}
	vec2 global_part = rt_fog_global_interval(p_origin, p_dir, p_t_max);
	if (global_part.x < global_part.y) {
		float t = global_part.x - log(1.0 - rand(r_rng)) / rt_fog_global_density();
		if (t < global_part.y && t < r_t) {
			r_collided = true;
			r_t = t;
			r_medium = rt_fog_global_sample();
			return weight;
		}
	}
	if (r_collided) {
		r_medium = rt_fog_world_grid_sample_voxel(voxel);
	}
	return weight;
}

// Transmittance of the whole medium along a ray: the global fog's exactly, the grid's by ratio tracking.
float rt_medium_transmittance(vec3 p_origin, vec3 p_dir, float p_t_max, inout uint r_rng) {
	float transmittance = 1.0;
	vec2 global_part = rt_fog_global_interval(p_origin, p_dir, p_t_max);
	if (global_part.x < global_part.y) {
		transmittance = exp(-rt_fog_global_density() * (global_part.y - global_part.x));
	}
	if (rt_fog_world_grid_transport_enabled()) {
		transmittance *= rt_fog_world_grid_transmittance(p_origin, p_dir, p_t_max, r_rng);
	}
	return transmittance;
}

// Adds the optical depth of [p_from, p_to) to r_depth: the grid's extinction p_extinction there, plus the global fog's
// p_global_density over its part p_global. True when r_depth reaches p_target in it, with r_t the distance where.
bool rt_medium_depth_step(float p_from, float p_to, float p_extinction, vec2 p_global, float p_global_density, float p_target, inout float r_depth, out float r_t) {
	r_t = -1.0;
	if (p_to <= p_from) {
		return false;
	}
	// Up to three parts of constant extinction: before, inside and after the global fog.
	float cuts[4] = float[](p_from, clamp(p_global.x, p_from, p_to), clamp(p_global.y, p_from, p_to), p_to);
	for (int i = 0; i < 3; i++) {
		float part = cuts[i + 1] - cuts[i];
		float extinction = p_extinction + (i == 1 ? p_global_density : 0.0);
		if (part <= 0.0 || extinction <= 0.0) {
			continue;
		}
		if (r_depth + extinction * part >= p_target) {
			r_t = cuts[i] + (p_target - r_depth) / extinction;
			r_depth = p_target;
			return true;
		}
		r_depth += extinction * part;
	}
	return false;
}

// Distance from p_origin along the unit p_dir, within p_t_end meters, where the whole medium's optical depth reaches
// p_depth, or -1 when it doesn't. It walks the grid's occupied bricks, and the voxels of those holding fog only.
float rt_medium_depth_distance(vec3 p_origin, vec3 p_dir, float p_t_end, float p_depth) {
	vec2 global_part = rt_fog_global_interval(p_origin, p_dir, p_t_end);
	float global_density = global_part.x < global_part.y ? rt_fog_global_density() : 0.0;
	float depth = 0.0;
	float t_reached;
	float t = 0.0;
	if (rt_fog_world_grid_transport_enabled()) {
		float voxel_size = get_rt_param(RT_PARAM_FOG_WORLD_GRID_VOXEL_SIZE);
		float brick_size = voxel_size * RT_FOG_WORLD_GRID_BRICK_SIZE;
		ivec3 brick_count = textureSize(usampler3D(rt_fog_world_grid_majorant, SAMPLER_NEAREST_CLAMP), 0);
		ivec3 grid_size = rt_fog_world_grid_size();
		vec3 grid_origin = rt_fog_world_grid_origin();

		// Brick units: s is the distance in brick sizes, t in meters.
		vec3 o = (p_origin - grid_origin) / brick_size;
		vec3 d = mix(p_dir, vec3(1e-8), lessThan(abs(p_dir), vec3(1e-8)));
		vec3 inv_d = 1.0 / d;
		vec3 s_a = (rt_fog_world_grid_occupied_min() / RT_FOG_WORLD_GRID_BRICK_SIZE - o) * inv_d;
		vec3 s_b = (rt_fog_world_grid_occupied_max() / RT_FOG_WORLD_GRID_BRICK_SIZE - o) * inv_d;
		vec3 s_near3 = min(s_a, s_b);
		vec3 s_far3 = max(s_a, s_b);
		float s = max(max(max(s_near3.x, s_near3.y), s_near3.z), 0.0);
		float s_end = min(min(min(s_far3.x, s_far3.y), s_far3.z), p_t_end / brick_size);
		if (s < s_end) {
			// Up to the occupied box, only the global fog.
			if (rt_medium_depth_step(0.0, s * brick_size, 0.0, global_part, global_density, p_depth, depth, t_reached)) {
				return t_reached;
			}
			t = s * brick_size;

			ivec3 step_dir = ivec3(sign(d));
			ivec3 brick = clamp(ivec3(floor(o + d * s)), ivec3(0), brick_count - 1);
			vec3 s_next = (vec3(brick) + vec3(greaterThan(step_dir, ivec3(0))) - o) * inv_d;
			vec3 s_delta = abs(inv_d);
			// The same walk in voxel units, for the bricks holding fog.
			vec3 o_voxel = (p_origin - grid_origin) / voxel_size;
			vec3 t_delta_voxel = abs(inv_d) * voxel_size;

			int max_steps = brick_count.x + brick_count.y + brick_count.z;
			for (int i = 0; i < max_steps; i++) {
				float t_exit = min(min(min(s_next.x, s_next.y), s_next.z), s_end) * brick_size;
				float majorant = rt_fog_world_grid_unpack(texelFetch(usampler3D(rt_fog_world_grid_majorant, SAMPLER_NEAREST_CLAMP), brick, 0).x);
				if (majorant <= 0.0) {
					if (rt_medium_depth_step(t, t_exit, 0.0, global_part, global_density, p_depth, depth, t_reached)) {
						return t_reached;
					}
				} else {
					ivec3 voxel_first = brick * int(RT_FOG_WORLD_GRID_BRICK_SIZE);
					ivec3 voxel_last = min(voxel_first + int(RT_FOG_WORLD_GRID_BRICK_SIZE) - 1, grid_size - 1);
					ivec3 voxel = clamp(ivec3(floor(o_voxel + d * (t / voxel_size))), voxel_first, voxel_last);
					vec3 t_next_voxel = (vec3(voxel) + vec3(greaterThan(step_dir, ivec3(0))) - o_voxel) * inv_d * voxel_size;
					for (int j = 0; j < 3 * int(RT_FOG_WORLD_GRID_BRICK_SIZE); j++) {
						float t_exit_voxel = min(min(min(t_next_voxel.x, t_next_voxel.y), t_next_voxel.z), t_exit);
						if (rt_medium_depth_step(t, t_exit_voxel, rt_fog_world_grid_extinction_at_voxel(voxel), global_part, global_density, p_depth, depth, t_reached)) {
							return t_reached;
						}
						t = max(t, t_exit_voxel);
						if (t >= t_exit) {
							break;
						}
						if (t_next_voxel.x <= t_next_voxel.y && t_next_voxel.x <= t_next_voxel.z) {
							voxel.x += step_dir.x;
							t_next_voxel.x += t_delta_voxel.x;
						} else if (t_next_voxel.y <= t_next_voxel.z) {
							voxel.y += step_dir.y;
							t_next_voxel.y += t_delta_voxel.y;
						} else {
							voxel.z += step_dir.z;
							t_next_voxel.z += t_delta_voxel.z;
						}
						if (any(lessThan(voxel, voxel_first)) || any(greaterThan(voxel, voxel_last))) {
							break;
						}
					}
					// What rounding left of the brick holds only the global fog.
					if (rt_medium_depth_step(t, t_exit, 0.0, global_part, global_density, p_depth, depth, t_reached)) {
						return t_reached;
					}
				}
				t = max(t, t_exit);
				if (t_exit >= s_end * brick_size) {
					break;
				}
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
		}
	}
	// Past the grid's fog, only the global fog.
	if (rt_medium_depth_step(t, p_t_end, 0.0, global_part, global_density, p_depth, depth, t_reached)) {
		return t_reached;
	}
	return -1.0;
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
// through green to red at RT_FOG_WORLD_GRID_DEBUG_COLLISIONS or more; white where the safety cap absorbed the walk.
// Magenta means no grid is bound.
vec3 rt_fog_world_grid_collisions_debug(vec3 p_origin, vec3 p_dir, float p_t, inout uint r_rng) {
	if (!rt_fog_world_grid_enabled()) {
		return vec3(1.0, 0.0, 1.0);
	}
	float dir_length = length(p_dir);
	bool collided;
	float t;
	ivec3 voxel;
	int collisions;
	rt_fog_world_grid_track(p_origin, p_dir / dir_length, p_t * dir_length, true, r_rng, collided, t, voxel, collisions);
	if (collisions >= RT_FOG_WORLD_GRID_MAX_COLLISIONS) {
		return vec3(1.0);
	}
	return debug_heat_color(float(collisions) / float(RT_FOG_WORLD_GRID_DEBUG_COLLISIONS));
}
