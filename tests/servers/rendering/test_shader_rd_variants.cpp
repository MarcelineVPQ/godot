/**************************************************************************/
/*  test_shader_rd_variants.cpp                                           */
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

TEST_FORCE_LINK(test_shader_rd_variants)

#include "modules/modules_enabled.gen.h" // For glslang.

#if defined(RD_ENABLED) && defined(MODULE_GLSLANG_ENABLED)

#include "servers/rendering/renderer_rd/environment/fog.h"
#include "servers/rendering/renderer_rd/forward_clustered/scene_shader_raytracing.h"
#include "servers/rendering/renderer_rd/shaders/blit.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/canvas.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/canvas_occlusion.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/canvas_sdf.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/cluster_debug.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/cluster_render.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/cluster_store.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/blur_raster.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/bokeh_dof.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/bokeh_dof_raster.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/copy.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/copy_to_fb.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/cube_to_dp.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/cube_to_octmap.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/depth_reconstruct.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_accumulate_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_autogen_reactive_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_compute_luminance_pyramid_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_depth_clip_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_lock_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_rcas_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_reconstruct_previous_depth_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_tcr_autogen_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr_upscale.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/luminance_reduce.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/luminance_reduce_raster.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/motion_vector_decode.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/motion_vectors.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/motion_vectors_store.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/octmap_downsampler.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/octmap_downsampler_raster.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/octmap_filter.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/octmap_filter_raster.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/octmap_roughness.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/octmap_roughness_raster.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/resolve.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/resolve_raster.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/roughness_limiter.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_downsample.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_filter.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_hiz.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_resolve.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/shadow_frustum.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/smaa_blending.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/smaa_edge_detection.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/smaa_weight_calculation.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/sort.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/specular_merge.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ss_effects_downsample.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao_blur.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao_importance_map.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao_interleave.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil_blur.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil_importance_map.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil_interleave.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/subsurface_scattering.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/tonemap.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/tonemap_mobile.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/vrs.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/gi.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/sdfgi_debug.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/sdfgi_debug_probes.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/sdfgi_direct_light.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/sdfgi_integrate.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/sdfgi_preprocess.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/sky.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/volumetric_fog.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/volumetric_fog_process.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/volumetric_fog_world_grid_majorant.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/voxel_gi.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/environment/voxel_gi_debug.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/best_fit_normal.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/integrate_dfg.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/oit_resolve.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/particles.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/particles_copy.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/raytracing/multimesh_merge.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/raytracing/scene_raytracing_raygen.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/skeleton.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/tex_blit.glsl.gen.h"
#include "servers/rendering/shader_include_db.h"

#include "modules/glslang/shader_compile.h"

// Compiles every variant of the renderer's shaders to SPIR-V with the engine's own glslang, without a GPU: the
// variant defines each renderer class passes to ShaderRD::initialize() (mirrored below), and the general defines it
// passes with the engine's default project settings. A shader that fails here fails at run time too, often in a
// variant that a validation run never reaches. Run it after any change to a .glsl file:
//   bin/godot.<platform>.editor.<arch> --test --test-case="*ShaderRD*"
// When a renderer class gains a shader or a variant, add it here; misc/scripts/check_shader_rd_variants.py fails
// until every shader class and define of the renderer appears in this file. Not covered: user material code (compiled
// from the material at run time; these compile the templates with empty code), giprobe_write.glsl and
// voxel_gi_sdf.glsl (no renderer class uses them).
namespace TestShaderRDVariants {

struct Failure {
	String shader;
	int variant = 0;
	String stage;
	String error;
};

static LocalVector<Failure> failures;
static int compiled_variants = 0;

// Compiles the non-empty stages of p_sources and records the first stage that fails.
static void compile_stages(const String &p_shader, int p_variant, const Vector<String> &p_sources) {
	for (int stage = 0; stage < p_sources.size(); stage++) {
		if (p_sources[stage].strip_edges().is_empty()) {
			continue;
		}
		String error;
		// The language and SPIR-V versions RenderingDevice uses for Vulkan, as RDShaderFile does.
		const Vector<uint8_t> spirv = compile_glslang_shader(RD::ShaderStage(stage), ShaderIncludeDB::parse_include_files(p_sources[stage]), RD::SHADER_LANGUAGE_VULKAN_VERSION_1_1, RD::SHADER_SPIRV_VERSION_1_4, &error);
		if (spirv.is_empty()) {
			Failure failure;
			failure.shader = p_shader;
			failure.variant = p_variant;
			failure.stage = RD::SHADER_STAGE_NAMES[stage];
			failure.error = error.is_empty() ? String("No SPIR-V and no error.") : error;
			failures.push_back(failure);
			return;
		}
	}
	compiled_variants++;
}

// Initializes p_shader like the renderer does and compiles each of its variants.
static void check(const String &p_name, ShaderRD &p_shader, const Vector<String> &p_variants, const String &p_general_defines = String()) {
	p_shader.initialize(p_variants, p_general_defines);
	const RID version = p_shader.version_create(false);
	for (int variant = 0; variant < p_variants.size(); variant++) {
		compile_stages(p_name, variant, p_shader.version_build_variant_stage_sources(version, variant));
	}
	p_shader.version_free(version);
}

template <typename T>
static void check(const String &p_name, const Vector<String> &p_variants, const String &p_general_defines = String()) {
	T shader;
	check(p_name, shader, p_variants, p_general_defines);
}

// Every combination of p_flags, each one adding its defines, in the order of the renderer's bit loops.
static Vector<String> flag_combinations(const Vector<String> &p_flags) {
	Vector<String> variants;
	for (int i = 0; i < (1 << p_flags.size()); i++) {
		String variant = "\n";
		for (int j = 0; j < p_flags.size(); j++) {
			if (i & (1 << j)) {
				variant += p_flags[j];
			}
		}
		variants.push_back(variant);
	}
	return variants;
}

// The group defines of Fog::init_fog_shader() for the froxel fog's material shader.
static const Vector<String> fog_groups = {
	"",
	"#define NO_IMAGE_ATOMICS\n",
	"#define USE_VULKAN_MEMORY_MODEL\n",
	"#define USE_VULKAN_MEMORY_MODEL\n#define NO_IMAGE_ATOMICS\n",
};

// RendererSceneRenderRD's defaults: rendering/reflections/sky_reflections/roughness_layers = 8,
// texture_array_reflections = true.
static const String roughness_defines = "\n#define MAX_ROUGHNESS_LOD 7.0\n\n#define USE_RADIANCE_OCTMAP_ARRAY \n";

static void check_effects() {
	check<BlitShaderRD>("blit", { "\n", "\n#define USE_LAYER\n", "\n#define USE_LAYER\n#define APPLY_LENS_DISTORTION\n", "\n" });
	check<SmaaEdgeDetectionShaderRD>("smaa_edge_detection", { "\n" });
	check<SmaaWeightCalculationShaderRD>("smaa_weight_calculation", { "\n" });
	check<SmaaBlendingShaderRD>("smaa_blending", { "\n" });
	check<RoughnessLimiterShaderRD>("roughness_limiter", { "" });
	check<DepthReconstructShaderRD>("depth_reconstruct", { "" });
	check<TaaResolveShaderRD>("taa_resolve", { "\n#define MODE_TAA_RESOLVE" });
	check<ShadowFrustumShaderRD>("shadow_frustum", { "" });
	check<MotionVectorsShaderRD>("motion_vectors", { "" });
	check<MotionVectorDecodeShaderRD>("motion_vector_decode", { "\n" });
	check<MotionVectorsStoreShaderRD>("motion_vectors_store", { "" });
	check<SortShaderRD>("sort", { "\n#define MODE_SORT_BLOCK\n", "\n#define MODE_SORT_STEP\n", "\n#define MODE_SORT_INNER\n" });
	check<ResolveRasterShaderRD>("resolve_raster", { "" });
	check<ResolveShaderRD>("resolve", { "\n#define MODE_RESOLVE_GI\n", "\n#define MODE_RESOLVE_GI\n#define VOXEL_GI_RESOLVE\n", "\n#define MODE_RESOLVE_DEPTH\n" });
	check<VrsShaderRD>("vrs", { "\n", "\n#define USE_MULTIVIEW\n", "\n#define SPLIT_RG\n", "\n#define SPLIT_RG\n#define USE_MULTIVIEW\n" });
	check<LuminanceReduceRasterShaderRD>("luminance_reduce_raster", { "\n#define FIRST_PASS\n", "\n", "\n#define FINAL_PASS\n" });
	check<LuminanceReduceShaderRD>("luminance_reduce", { "\n#define READ_TEXTURE\n", "\n", "\n#define WRITE_LUMINANCE\n" });
	check<FsrUpscaleShaderRD>("fsr_upscale", { "\n#define MODE_FSR_UPSCALE_NORMAL\n", "\n#define MODE_FSR_UPSCALE_FALLBACK\n" });
	check<SubsurfaceScatteringShaderRD>("subsurface_scattering", { "\n#define USE_11_SAMPLES\n", "\n#define USE_17_SAMPLES\n", "\n#define USE_25_SAMPLES\n" });

	const Vector<String> bokeh_modes = {
		"\n#define MODE_GEN_BLUR_SIZE\n",
		"\n#define MODE_BOKEH_BOX\n#define OUTPUT_WEIGHT\n",
		"\n#define MODE_BOKEH_BOX\n",
		"\n#define MODE_BOKEH_HEXAGONAL\n#define OUTPUT_WEIGHT\n",
		"\n#define MODE_BOKEH_HEXAGONAL\n",
		"\n#define MODE_BOKEH_CIRCULAR\n#define OUTPUT_WEIGHT\n",
		"\n#define MODE_COMPOSITE_BOKEH\n",
	};
	check<BokehDofRasterShaderRD>("bokeh_dof_raster", bokeh_modes);
	check<BokehDofShaderRD>("bokeh_dof", bokeh_modes);

	// ToneMapper: the mobile and the regular shader, each with and without multiview.
	Vector<String> tonemap_mobile_modes = { "\n", "\n#define USE_1D_LUT\n", "\n#define SUBPASS\n", "\n#define SUBPASS\n#define USE_1D_LUT\n" };
	Vector<String> tonemap_modes = { "\n", "\n#define USE_GLOW_FILTER_BICUBIC\n", "\n#define USE_1D_LUT\n", "\n#define USE_GLOW_FILTER_BICUBIC\n#define USE_1D_LUT\n" };
	for (Vector<String> *modes : { &tonemap_mobile_modes, &tonemap_modes }) {
		const int count = modes->size();
		for (int i = 0; i < count; i++) {
			modes->push_back("\n#define USE_MULTIVIEW" + (*modes)[i]);
		}
	}
	check<TonemapMobileShaderRD>("tonemap_mobile", tonemap_mobile_modes);
	check<TonemapShaderRD>("tonemap", tonemap_modes);

	// CopyEffects.
	check<BlurRasterShaderRD>("blur_raster", { "\n#define MODE_MIPMAP\n", "\n#define MODE_GAUSSIAN_BLUR\n", "\n#define MODE_GLOW_GATHER\n", "\n#define MODE_GLOW_DOWNSAMPLE\n", "\n#define MODE_GLOW_UPSAMPLE\n", "\n#define MODE_COPY\n", "\n#define MODE_SET_COLOR\n" });
	check<CopyShaderRD>("copy", {
										"\n#define MODE_GAUSSIAN_BLUR\n",
										"\n#define MODE_GAUSSIAN_BLUR\n#define DST_IMAGE_8BIT\n",
										"\n#define MODE_GAUSSIAN_BLUR\n#define MODE_GLOW\n",
										"\n#define MODE_GAUSSIAN_BLUR\n#define MODE_GLOW\n#define GLOW_USE_AUTO_EXPOSURE\n",
										"\n#define MODE_SIMPLE_COPY\n",
										"\n#define MODE_SIMPLE_COPY\n#define DST_IMAGE_8BIT\n",
										"\n#define MODE_SIMPLE_COPY_DEPTH\n",
										"\n#define MODE_SET_COLOR\n",
										"\n#define MODE_SET_COLOR\n#define DST_IMAGE_8BIT\n",
										"\n#define MODE_MIPMAP\n",
										"\n#define MODE_LINEARIZE_DEPTH_COPY\n",
										"\n#define MODE_OCTMAP_TO_PANORAMA\n",
										"\n#define MODE_OCTMAP_ARRAY_TO_PANORAMA\n",
								});
	check<CopyToFbShaderRD>("copy_to_fb", { "\n", "\n#define MODE_PANORAMA_TO_DP\n", "\n#define MODE_TWO_SOURCES\n", "\n#define MODE_SET_COLOR\n", "\n#define MODE_COPY_DEPTH\n", "\n#define MODE_TIMING_HEATMAP\n", "\n#define USE_MULTIVIEW\n", "\n#define USE_MULTIVIEW\n#define MODE_TWO_SOURCES\n" });
	check<CubeToDpShaderRD>("cube_to_dp", { "\n" });
	check<CubeToOctmapShaderRD>("cube_to_octmap", { "" });
	check<OctmapDownsamplerRasterShaderRD>("octmap_downsampler_raster", { "" });
	const Vector<String> octmap_formats = { "\n#define OCTMAP_FORMAT rgba16f\n", "\n#define OCTMAP_FORMAT rgb10_a2\n" };
	check<OctmapDownsamplerShaderRD>("octmap_downsampler", octmap_formats);
	check<OctmapFilterRasterShaderRD>("octmap_filter_raster", { "\n#define USE_LOW_QUALITY\n", "\n#define USE_HIGH_QUALITY\n" });
	Vector<String> octmap_filter_modes;
	for (int i = 0; i < 8; i++) {
		String mode = (i & 1) ? "\n#define USE_HIGH_QUALITY\n" : "\n#define USE_LOW_QUALITY\n";
		if (i & 2) {
			mode += "\n#define USE_TEXTURE_ARRAY\n";
		}
		mode += octmap_formats[(i & 4) ? 1 : 0];
		octmap_filter_modes.push_back(mode);
	}
	check<OctmapFilterShaderRD>("octmap_filter", octmap_filter_modes);
	check<OctmapRoughnessRasterShaderRD>("octmap_roughness_raster", { "" });
	check<OctmapRoughnessShaderRD>("octmap_roughness", octmap_formats);
	check<SpecularMergeShaderRD>("specular_merge", {
														   "\n#define MODE_MERGE\n",
														   "\n#define MODE_MERGE\n#define MODE_SSR\n",
														   "\n",
														   "\n#define MODE_SSR\n",
														   "\n#define USE_MULTIVIEW\n#define MODE_MERGE\n",
														   "\n#define USE_MULTIVIEW\n#define MODE_MERGE\n#define MODE_SSR\n",
														   "\n#define USE_MULTIVIEW\n",
														   "\n#define USE_MULTIVIEW\n#define MODE_SSR\n",
												   });

	// SSEffects.
	check<SsEffectsDownsampleShaderRD>("ss_effects_downsample", { "\n", "\n#define USE_HALF_SIZE\n", "\n#define GENERATE_MIPS\n", "\n#define GENERATE_MIPS\n#define USE_HALF_SIZE\n", "\n#define USE_HALF_BUFFERS\n", "\n#define USE_HALF_BUFFERS\n#define USE_HALF_SIZE\n", "\n#define GENERATE_MIPS\n#define GENERATE_FULL_MIPS" });
	check<SsilShaderRD>("ssil", { "\n", "\n#define SSIL_BASE\n", "\n#define ADAPTIVE\n" });
	check<SsilImportanceMapShaderRD>("ssil_importance_map", { "\n#define GENERATE_MAP\n", "\n#define PROCESS_MAPA\n", "\n#define PROCESS_MAPB\n" });
	check<SsilBlurShaderRD>("ssil_blur", { "\n#define MODE_NON_SMART\n", "\n#define MODE_SMART\n", "\n#define MODE_WIDE\n" });
	check<SsilInterleaveShaderRD>("ssil_interleave", { "\n#define MODE_NON_SMART\n", "\n#define MODE_SMART\n", "\n#define MODE_HALF\n" });
	check<SsaoShaderRD>("ssao", { "\n", "\n#define SSAO_BASE\n", "\n#define ADAPTIVE\n" });
	check<SsaoImportanceMapShaderRD>("ssao_importance_map", { "\n#define GENERATE_MAP\n", "\n#define PROCESS_MAPA\n", "\n#define PROCESS_MAPB\n" });
	check<SsaoBlurShaderRD>("ssao_blur", { "\n#define MODE_NON_SMART\n", "\n#define MODE_SMART\n", "\n#define MODE_WIDE\n" });
	check<SsaoInterleaveShaderRD>("ssao_interleave", { "\n#define MODE_NON_SMART\n", "\n#define MODE_SMART\n", "\n#define MODE_HALF\n" });
	const Vector<String> odd_sizes = { "\n", "\n#define MODE_ODD_WIDTH\n", "\n#define MODE_ODD_HEIGHT\n", "\n#define MODE_ODD_WIDTH\n#define MODE_ODD_HEIGHT\n" };
	check<ScreenSpaceReflectionDownsampleShaderRD>("screen_space_reflection_downsample", odd_sizes);
	check<ScreenSpaceReflectionHizShaderRD>("screen_space_reflection_hiz", odd_sizes);
	check<ScreenSpaceReflectionShaderRD>("screen_space_reflection", { "\n" });
	check<ScreenSpaceReflectionFilterShaderRD>("screen_space_reflection_filter", { "\n" });
	check<ScreenSpaceReflectionResolveShaderRD>("screen_space_reflection_resolve", { "\n" });

	// FSR2, with the general defines of FSR2Effect.
	const String fsr2_defines =
			"\n#define FFX_GPU\n"
			"\n#define FFX_GLSL 1\n"
			"\n#define FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS 1\n"
			"\n#define FFX_FSR2_OPTION_HDR_COLOR_INPUT 1\n"
			"\n#define FFX_FSR2_OPTION_INVERTED_DEPTH 1\n"
			"\n#define FFX_FSR2_OPTION_GODOT_REACTIVE_MASK_CLAMP 1\n"
			"\n#define FFX_FSR2_OPTION_GODOT_DERIVE_INVALID_MOTION_VECTORS 1\n";
	const Vector<String> fsr2_single = { "" };
	const Vector<String> fsr2_with_fp16 = { "", "\n#define FFX_HALF 1\n" };
	check<Fsr2DepthClipPassShaderRD>("fsr2_depth_clip_pass", fsr2_with_fp16, fsr2_defines);
	check<Fsr2ReconstructPreviousDepthPassShaderRD>("fsr2_reconstruct_previous_depth_pass", fsr2_with_fp16, fsr2_defines);
	check<Fsr2LockPassShaderRD>("fsr2_lock_pass", fsr2_with_fp16, fsr2_defines);
	check<Fsr2AccumulatePassShaderRD>("fsr2_accumulate_pass", { "\n", "\n#define FFX_FSR2_OPTION_APPLY_SHARPENING 1\n", "\n#define FFX_HALF 1\n", "\n#define FFX_HALF 1\n#define FFX_FSR2_OPTION_APPLY_SHARPENING 1\n" }, fsr2_defines);
	check<Fsr2RcasPassShaderRD>("fsr2_rcas_pass", fsr2_single, fsr2_defines);
	check<Fsr2ComputeLuminancePyramidPassShaderRD>("fsr2_compute_luminance_pyramid_pass", fsr2_single, fsr2_defines);
	check<Fsr2AutogenReactivePassShaderRD>("fsr2_autogen_reactive_pass", fsr2_with_fp16, fsr2_defines);
	check<Fsr2TcrAutogenPassShaderRD>("fsr2_tcr_autogen_pass", fsr2_with_fp16, fsr2_defines);
}

static void check_storage_and_canvas() {
	// ClusterBuilderSharedDataRD.
	check<ClusterRenderShaderRD>("cluster_render", { "", "\n#define USE_ATTACHMENT\n" });
	check<ClusterStoreShaderRD>("cluster_store", { "" });
	check<ClusterDebugShaderRD>("cluster_debug", { "" });

	// MeshStorage, TextureStorage (SAMPLERS_BINDING_FIRST_INDEX 4) and ParticlesStorage (3).
	check<SkeletonShaderRD>("skeleton", { "\n#define MODE_2D\n", "" });
	check<CanvasSdfShaderRD>("canvas_sdf", { "\n#define MODE_LOAD\n", "\n#define MODE_LOAD_SHRINK\n", "\n#define MODE_PROCESS\n", "\n#define MODE_PROCESS_OPTIMIZED\n", "\n#define MODE_STORE\n", "\n#define MODE_STORE_SHRINK\n" });
	check<TexBlitShaderRD>("tex_blit", { "", "\n#define USE_OUTPUT1\n", "\n#define USE_OUTPUT1\n#define USE_OUTPUT2\n", "\n#define USE_OUTPUT1\n#define USE_OUTPUT2\n#define USE_OUTPUT3\n" }, "\n#define SAMPLERS_BINDING_FIRST_INDEX 4\n#define MAX_GLOBAL_SHADER_UNIFORMS 256\n");
	check<ParticlesShaderRD>("particles", { "" }, "#define SAMPLERS_BINDING_FIRST_INDEX 3\n");
	Vector<String> particles_copy_modes;
	for (int i = 0; i <= 6; i++) { // ParticlesShader::MAX_USERDATAS.
		const String userdata = i == 0 ? String() : "#define USERDATA_COUNT " + itos(i) + "\n";
		particles_copy_modes.push_back("\n#define MODE_FILL_INSTANCES\n" + userdata);
		particles_copy_modes.push_back("\n#define MODE_FILL_SORT_BUFFER\n#define USE_SORT_BUFFER\n" + userdata);
		particles_copy_modes.push_back("\n#define MODE_FILL_INSTANCES\n#define USE_SORT_BUFFER\n" + userdata);
	}
	check<ParticlesCopyShaderRD>("particles_copy", particles_copy_modes);

	// RendererCanvasRenderRD: MAX_LIGHTS_PER_RENDER 256, SAMPLERS_BINDING_FIRST_INDEX 10.
	check<CanvasShaderRD>("canvas", { "", "#define USE_NINEPATCH\n", "#define USE_PRIMITIVE\n", "#define USE_PRIMITIVE\n#define USE_POINT_SIZE\n", "#define USE_ATTRIBUTES\n", "#define USE_ATTRIBUTES\n#define USE_POINT_SIZE\n" }, "#define MAX_LIGHTS 256\n\n#define SAMPLERS_BINDING_FIRST_INDEX 10\n");
	check<CanvasOcclusionShaderRD>("canvas_occlusion", { "\n#define MODE_SHADOW\n", "\n#define MODE_SHADOW\n#define POSITIONAL_SHADOW\n", "\n#define MODE_SDF\n" });
}

static void check_environment() {
	// SkyRD: 4 directional lights, SAMPLERS_BINDING_FIRST_INDEX 4.
	check<SkyShaderRD>("sky", {
									  "",
									  "\n#define USE_HALF_RES_PASS\n",
									  "\n#define USE_QUARTER_RES_PASS\n",
									  "\n#define USE_CUBEMAP_PASS\n",
									  "\n#define USE_CUBEMAP_PASS\n#define USE_HALF_RES_PASS\n",
									  "\n#define USE_CUBEMAP_PASS\n#define USE_QUARTER_RES_PASS\n",
									  "\n#define USE_MULTIVIEW\n",
									  "\n#define USE_HALF_RES_PASS\n#define USE_MULTIVIEW\n",
									  "\n#define USE_QUARTER_RES_PASS\n#define USE_MULTIVIEW\n",
							  },
			"\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS 4\n\n#define SAMPLERS_BINDING_FIRST_INDEX 4\n");

	// Fog (SAMPLERS_BINDING_FIRST_INDEX 3, 8 directional lights): the froxel fog's material shader with the path
	// tracer's world grid groups, its process shader, the process shader with ray-query shadows (RTFogShadows), and the
	// world grid's brick majorants.
	Vector<String> fog_modes;
	for (const String &group : fog_groups) {
		fog_modes.push_back(group);
	}
	for (const String &group : fog_groups) {
		fog_modes.push_back(group + "#define MODE_WORLD_GRID\n");
	}
	check<VolumetricFogShaderRD>("volumetric_fog", fog_modes, "#define SAMPLERS_BINDING_FIRST_INDEX 3\n");
	// Devices that add floats atomically in images sum the world grid's emission as floats.
	check<VolumetricFogShaderRD>("volumetric_fog (float emission)", fog_modes, "#define SAMPLERS_BINDING_FIRST_INDEX 3\n#define WORLD_GRID_FLOAT_EMISSION\n");
	const String process_defines = "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS 8\n\n#define MAX_SKY_LOD 7.0\n\n#define USE_RADIANCE_OCTMAP_ARRAY \n";
	Vector<String> process_modes;
	Vector<String> ray_query_modes;
	for (int i = 0; i < RendererRD::Fog::get_process_shader_group_count(); i++) {
		const String group = RendererRD::Fog::get_process_shader_group_define(i);
		process_modes.push_back(group + "\n#define MODE_DENSITY\n");
		process_modes.push_back(group + "\n#define MODE_DENSITY\n#define ENABLE_SDFGI\n");
		process_modes.push_back(group + "\n#define MODE_FILTER\n");
		process_modes.push_back(group + "\n#define MODE_FOG\n");
		process_modes.push_back(group + "\n#define MODE_COPY\n");
		ray_query_modes.push_back(group + "\n#define MODE_DENSITY\n#define USE_RAY_QUERY_SHADOWS\n");
	}
	check<VolumetricFogProcessShaderRD>("volumetric_fog_process", process_modes, process_defines);
	check<VolumetricFogProcessShaderRD>("volumetric_fog_process (ray query shadows)", ray_query_modes, process_defines);
	check<VolumetricFogWorldGridMajorantShaderRD>("volumetric_fog_world_grid_majorant", { "" });

	// GI: VoxelGI (32 lights), SDFGI (cascade size 128, probe divisor 16, light probe octahedron 6, SH 16).
	check<VoxelGiShaderRD>("voxel_gi", {
											   "\n#define MODE_COMPUTE_LIGHT\n",
											   "\n#define MODE_SECOND_BOUNCE\n",
											   "\n#define MODE_UPDATE_MIPMAPS\n",
											   "\n#define MODE_WRITE_TEXTURE\n",
											   "\n#define MODE_DYNAMIC\n#define MODE_DYNAMIC_LIGHTING\n",
											   "\n#define MODE_DYNAMIC\n#define MODE_DYNAMIC_SHRINK\n#define MODE_DYNAMIC_SHRINK_WRITE\n",
											   "\n#define MODE_DYNAMIC\n#define MODE_DYNAMIC_SHRINK\n#define MODE_DYNAMIC_SHRINK_PLOT\n",
											   "\n#define MODE_DYNAMIC\n#define MODE_DYNAMIC_SHRINK\n#define MODE_DYNAMIC_SHRINK_PLOT\n#define MODE_DYNAMIC_SHRINK_WRITE\n",
									   },
			"\n#define MAX_LIGHTS 32\n");
	check<VoxelGiDebugShaderRD>("voxel_gi_debug", { "\n#define MODE_DEBUG_COLOR\n", "\n#define MODE_DEBUG_LIGHT\n", "\n#define MODE_DEBUG_EMISSION\n", "\n#define MODE_DEBUG_LIGHT\n#define MODE_DEBUG_LIGHT_FULL\n" });
	check<SdfgiPreprocessShaderRD>("sdfgi_preprocess", {
															   "\n#define MODE_SCROLL\n",
															   "\n#define MODE_SCROLL_OCCLUSION\n",
															   "\n#define MODE_INITIALIZE_JUMP_FLOOD\n",
															   "\n#define MODE_INITIALIZE_JUMP_FLOOD_HALF\n",
															   "\n#define MODE_JUMPFLOOD\n",
															   "\n#define MODE_JUMPFLOOD_OPTIMIZED\n",
															   "\n#define MODE_UPSCALE_JUMP_FLOOD\n",
															   "\n#define MODE_OCCLUSION\n",
															   "\n#define MODE_STORE\n",
													   },
			"\n#define OCCLUSION_SIZE 8\n");
	check<SdfgiDirectLightShaderRD>("sdfgi_direct_light", { "\n#define MODE_PROCESS_STATIC\n", "\n#define MODE_PROCESS_DYNAMIC\n" }, "\n#define OCT_SIZE 6\n");
	check<SdfgiIntegrateShaderRD>("sdfgi_integrate", { "\n#define MODE_PROCESS\n", "\n#define MODE_STORE\n", "\n#define MODE_SCROLL\n", "\n#define MODE_SCROLL_STORE\n" }, "\n#define OCT_SIZE 6\n\n#define SH_SIZE 16\n\n#define USE_RADIANCE_OCTMAP_ARRAY\n");
	Vector<String> gi_modes;
	for (const String vrs : { "", "\n#define USE_VRS\n" }) {
		gi_modes.push_back(vrs + "\n#define USE_VOXEL_GI_INSTANCES\n");
		gi_modes.push_back(vrs + "\n#define USE_VOXEL_GI_INSTANCES\n#define SAMPLE_VOXEL_GI_NEAREST\n");
		gi_modes.push_back(vrs + "\n#define USE_SDFGI\n");
		gi_modes.push_back(vrs + "\n#define USE_SDFGI\n\n#define USE_VOXEL_GI_INSTANCES\n");
		gi_modes.push_back(vrs + "\n#define USE_SDFGI\n\n#define USE_VOXEL_GI_INSTANCES\n#define SAMPLE_VOXEL_GI_NEAREST\n");
	}
	check<GiShaderRD>("gi", gi_modes, "\n#define SDFGI_OCT_SIZE 6\n");
	check<SdfgiDebugShaderRD>("sdfgi_debug", { "" }, "\n#define OCT_SIZE 6\n");
	check<SdfgiDebugProbesShaderRD>("sdfgi_debug_probes", { "\n#define MODE_PROBES\n", "\n#define MODE_PROBES\n#define USE_MULTIVIEW\n", "\n#define MODE_VISIBILITY\n", "\n#define MODE_VISIBILITY\n#define USE_MULTIVIEW\n" }, "\n#define OCT_SIZE 6\n");
}

// The scene shaders' general defines with the default project settings: 8 directional lights, 8 lightmaps, specular
// occlusion on, no forced vertex shading. The material and capture sets are those of the renderer.
static String scene_defines(bool p_clustered) {
	String defines = roughness_defines;
	if (p_clustered) {
		defines += "\n#define SDFGI_OCT_SIZE 6\n";
	}
	defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS 8\n";
	defines += "\n#define MAX_LIGHTMAP_TEXTURES 8\n\n#define MAX_LIGHTMAPS 8\n";
	defines += "\n#define MATERIAL_UNIFORM_SET 3\n";
	if (p_clustered) {
		defines += "\n#define RT_CAPTURE_UNIFORM_SET 4\n";
	}
#ifdef REAL_T_IS_DOUBLE
	defines += "\n#define USE_DOUBLE_PRECISION \n";
#endif
	return defines;
}

static void check_scene() {
	// SceneShaderForwardClustered, without the OIT passes that also separate specular or write motion vectors (their
	// group is never enabled).
	Vector<String> clustered_modes;
	for (const String ubershader : { "", "\n#define UBERSHADER\n" }) {
		clustered_modes.push_back(ubershader + "\n#define MODE_RENDER_DEPTH\n");
		clustered_modes.push_back(ubershader + "\n#define MODE_RENDER_DEPTH\n#define MODE_DUAL_PARABOLOID\n");
		clustered_modes.push_back(ubershader + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n");
		clustered_modes.push_back(ubershader + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n#define MODE_RENDER_VOXEL_GI\n");
		clustered_modes.push_back(ubershader + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_DEPTH\n");
		clustered_modes.push_back(ubershader + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n");
		clustered_modes.push_back(ubershader + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_NORMAL_ROUGHNESS\n#define MODE_RENDER_VOXEL_GI\n");
		clustered_modes.push_back(ubershader + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_MATERIAL\n");
		clustered_modes.push_back(ubershader + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_SDF\n");
	}
	// SHADER_COLOR_PASS_FLAG_*: ubershader, separate specular, lightmap, multiview, motion vectors, OIT.
	const Vector<String> color_pass_flags = { "\n#define UBERSHADER\n", "\n#define MODE_SEPARATE_SPECULAR\n", "\n#define USE_LIGHTMAP\n", "\n#define USE_MULTIVIEW\n", "\n#define MOTION_VECTORS\n", "\n#define MODE_OIT\n" };
	const Vector<String> color_passes = flag_combinations(color_pass_flags);
	for (int i = 0; i < color_passes.size(); i++) {
		const bool oit = i & (1 << 5);
		if (oit && (i & ((1 << 1) | (1 << 4)))) {
			continue;
		}
		clustered_modes.push_back(color_passes[i]);
	}
	clustered_modes.push_back("\n#define MODE_RENDER_DEPTH\n#define MODE_RT_CAPTURE\n#define NORMAL_USED\n#define TANGENT_USED\n");
	check<SceneForwardClusteredShaderRD>("scene_forward_clustered", clustered_modes, scene_defines(true));
	// The project settings off their defaults: rendering/shading/overrides/force_vertex_shading on and
	// rendering/reflections/specular_occlusion/enabled off.
	const String optional_scene_defines = "\n#define USE_VERTEX_LIGHTING\n\n#define SPECULAR_OCCLUSION_DISABLED\n";
	check<SceneForwardClusteredShaderRD>("scene_forward_clustered (vertex shading, no specular occlusion)", clustered_modes, scene_defines(true) + optional_scene_defines);
	// Render-mode blend defines that change the color pass output: the plain color pass with each.
	check<SceneForwardClusteredShaderRD>("scene_forward_clustered (blend modes)", { "\n#define BLEND_PREMUL_ALPHA_USED\n", "\n#define BLEND_MUL_USED\n" }, scene_defines(true));

	// SceneShaderForwardMobile.
	Vector<String> mobile_modes;
	for (const String fp16 : { "", "\n#define EXPLICIT_FP16\n" }) {
		for (const String ubershader : { "", "\n#define UBERSHADER\n" }) {
			const String base = fp16 + ubershader;
			mobile_modes.push_back(base);
			mobile_modes.push_back(base + "\n#define USE_LIGHTMAP\n");
			mobile_modes.push_back(base + "\n#define MODE_RENDER_DEPTH\n#define SHADOW_PASS\n");
			mobile_modes.push_back(base + "\n#define MODE_RENDER_DEPTH\n#define MODE_DUAL_PARABOLOID\n#define SHADOW_PASS\n");
			mobile_modes.push_back(base + "\n#define MODE_RENDER_DEPTH\n#define MODE_RENDER_MATERIAL\n");
			mobile_modes.push_back(base + "\n#define USE_MULTIVIEW\n");
			mobile_modes.push_back(base + "\n#define USE_MULTIVIEW\n#define USE_LIGHTMAP\n");
			mobile_modes.push_back(base + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_DEPTH\n#define SHADOW_PASS\n");
			mobile_modes.push_back(base + "\n#define USE_MULTIVIEW\n#define MODE_RENDER_MOTION_VECTORS\n");
		}
	}
	check<SceneForwardMobileShaderRD>("scene_forward_mobile", mobile_modes, scene_defines(false));
	check<SceneForwardMobileShaderRD>("scene_forward_mobile (vertex shading, no specular occlusion)", mobile_modes, scene_defines(false) + optional_scene_defines);

	// RenderForwardClustered's own passes.
	check<OitResolveShaderRD>("oit_resolve", { "\n" });
	check<BestFitNormalShaderRD>("best_fit_normal", { "\n" });
	check<IntegrateDfgShaderRD>("integrate_dfg", { "\n" });
}

// Inserts p_define after the #version line, as SceneShaderRaytracing does.
static String inject_define(const String &p_source, const String &p_define) {
	const int version = p_source.find("#version");
	return version < 0 ? p_source : p_source.insert(p_source.find("\n", version) + 1, p_define);
}

// SceneShaderRaytracing: each raygen mode (DLSS RR, SER, ray-query shadows) with each set of defines the pipeline adds
// from its flags (debug views, timing, decals). The intersection stage is a template for procedural hit groups with
// material code; the base pipeline leaves it out. p_extra_defines are general defines the renderer adds by device.
static void check_raygen(const String &p_name, const String &p_extra_defines) {
	const Vector<String> raygen_modes = flag_combinations({ "#define DLSS_RR_ENABLED\n", "#define USE_SER\n", "#define USE_RAY_QUERY_SHADOWS\n" });
	const Vector<String> pipeline_defines = flag_combinations({ "#define RT_DEBUG_ENABLED\n", "#define RT_TIMING_ENABLED\n", "#define RT_DECALS_ENABLED\n" });
	SceneRaytracingRaygenShaderRD raygen;
	raygen.initialize(raygen_modes, "\n#define RT 1\n" + roughness_defines + "\n#define RT_PARAM_COUNT " + itos(RendererSceneRenderImplementation::SceneShaderRaytracing::RT_PARAM_COUNT) + "\n" + p_extra_defines);
	const RID version = raygen.version_create(false);
	for (int mode = 0; mode < raygen_modes.size(); mode++) {
		const Vector<String> base_sources = raygen.version_build_variant_stage_sources(version, mode);
		for (int i = 0; i < pipeline_defines.size(); i++) {
			Vector<String> sources = base_sources;
			for (int stage = 0; stage < sources.size(); stage++) {
				sources.write[stage] = inject_define(inject_define(sources[stage], pipeline_defines[i]), "#define ENABLE_INTERSECTION_SHADERS\n");
			}
			sources.write[RD::SHADER_STAGE_INTERSECTION] = String();
			compile_stages(vformat("%s (pipeline defines %d)", p_name, i), mode, sources);
		}
	}
	raygen.version_free(version);
}

static void check_path_tracer() {
	check<MultimeshMergeShaderRD>("multimesh_merge", { "\n", "\n#define MODE_INDEXED\n" });
	check_raygen("scene_raytracing_raygen", String());
	check_raygen("scene_raytracing_raygen (float fog emission)", "\n#define FOG_WORLD_GRID_FLOAT_EMISSION\n");
}

TEST_CASE("[ShaderRD] The variant check reports a clash between included blocks") {
	// Two includes declaring the same block name, as raytracing_data_inc.glsl and volumetric_fog_process.glsl once did.
	const String source = R"(#version 450
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer restrict readonly ClusterBuffer {
	uint data[];
} cluster_buffer;
layout(set = 0, binding = 1, std430) buffer restrict ClusterBuffer {
	uint data[];
} other_buffer;
void main() {
	other_buffer.data[0] = cluster_buffer.data[0];
}
)";
	failures.clear();
	compiled_variants = 0;
	Vector<String> sources;
	sources.resize(RD::SHADER_STAGE_MAX);
	sources.write[RD::SHADER_STAGE_COMPUTE] = source;
	compile_stages("block name clash", 0, sources);
	REQUIRE(failures.size() == 1);
	CHECK(compiled_variants == 0);
	CHECK(failures[0].error.contains("ClusterBuffer"));
	failures.clear();
}

TEST_CASE("[ShaderRD] Every variant of the renderer's shaders compiles") {
	failures.clear();
	compiled_variants = 0;
	check_effects();
	check_storage_and_canvas();
	check_environment();
	check_scene();
	check_path_tracer();

	for (const Failure &failure : failures) {
		MESSAGE(vformat("%s, variant %d, %s stage:\n%s", failure.shader, failure.variant, failure.stage, failure.error));
	}
	MESSAGE(vformat("Compiled %d variants, %d failed.", compiled_variants, failures.size()));
	CHECK(compiled_variants > 0);
	CHECK(failures.is_empty());
}

} // namespace TestShaderRDVariants

#endif // defined(RD_ENABLED) && defined(MODULE_GLSLANG_ENABLED)
