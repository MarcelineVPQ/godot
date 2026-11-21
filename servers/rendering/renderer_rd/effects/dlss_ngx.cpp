/**************************************************************************/
/*  dlss_ngx.cpp                                                          */
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

// DLSS through the NVIDIA NGX SDK, for Linux builds where Streamline is not available (see
// drivers/ngx). It mirrors the Streamline path in dlss.cpp: same motion vector decode, same inputs
// and settings, but calls NGX directly. Super Resolution upscales rasterized frames; Ray
// Reconstruction denoises and upscales the path tracer's.

#include "dlss.h"

#if defined(NGX_ENABLED) && !defined(STREAMLINE_ENABLED)

#include "core/config/project_settings.h"
#include "drivers/ngx/ngx_context.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

namespace RendererRD {
class DLSSContextNGX : public DLSSContext {
public:
	// Quality mode of each feature for this viewport's sizes, QUALITY_OFF when it has none.
	NGXContext::Quality quality[NGXContext::FEATURE_COUNT] = { NGXContext::QUALITY_OFF, NGXContext::QUALITY_OFF };
	void *feature = nullptr; // Created on first use, inside the render graph.
	NGXContext::Feature feature_kind = NGXContext::FEATURE_SUPER_RESOLUTION; // What the feature was created as.
	bool feature_alpha_upscaling = false; // The alpha upscaling the feature was created with.

	virtual ~DLSSContextNGX() {
		// Frames still in flight may use the feature, so NGX releases it once they are done.
		NGXContext::get().feature_release(feature, RD::get_singleton()->get_frames_drawn() + RD::get_singleton()->get_frame_delay() + 1);
	}
};
} // namespace RendererRD

// The feature upscale() with p_params uses.
static NGXContext::Feature ngx_feature(const DLSSContext::Parameters &p_params) {
	return p_params.dlss_rr ? NGXContext::FEATURE_RAY_RECONSTRUCTION : NGXContext::FEATURE_SUPER_RESOLUTION;
}

// Same layout as sl_convert_matrix in dlss.cpp, which Streamline passes on to NGX.
static void ngx_convert_matrix(const Projection &p_mtx, float *r_matrix) {
	for (int i = 0; i < 4; i++) {
		r_matrix[i * 4 + 0] = p_mtx.columns[0][i];
		r_matrix[i * 4 + 1] = p_mtx.columns[1][i];
		r_matrix[i * 4 + 2] = p_mtx.columns[2][i];
		r_matrix[i * 4 + 3] = p_mtx.columns[3][i];
	}
}

static NGXContext::Resource ngx_resource(RID p_texture, bool p_read_write) {
	NGXContext::Resource resource;
	const RD::TextureFormat format = RD::get_singleton()->texture_get_format(p_texture);
	resource.image = RD::get_singleton()->get_driver_resource(RD::DRIVER_RESOURCE_TEXTURE, p_texture);
	resource.view = RD::get_singleton()->get_driver_resource(RD::DRIVER_RESOURCE_TEXTURE_VIEW, p_texture);
	resource.format = (uint32_t)RD::get_singleton()->get_driver_resource(RD::DRIVER_RESOURCE_TEXTURE_DATA_FORMAT, p_texture);
	resource.width = format.width;
	resource.height = format.height;
	// Formats with depth; S8_UINT is in the range but has stencil only.
	resource.depth = format.format >= RD::DATA_FORMAT_D16_UNORM && format.format <= RD::DATA_FORMAT_D32_SFLOAT_S8_UINT && format.format != RD::DATA_FORMAT_S8_UINT;
	resource.read_write = p_read_write;
	return resource;
}

DLSSEffect::DLSSEffect() {
	// Initialize motion vector decode
	Vector<String> modes;
	modes.push_back("\n");
	shaders.mvec_decode_shader.initialize(modes, "");
	shaders.mvec_decode_version = shaders.mvec_decode_shader.version_create();
	shaders.mvec_decode_pipeline = RD::get_singleton()->compute_pipeline_create(shaders.mvec_decode_shader.version_get_shader(shaders.mvec_decode_version, 0));
	RD::get_singleton()->set_resource_name(shaders.mvec_decode_pipeline, "DLSS Motion Vector Decode Pipeline");
}

DLSSEffect::~DLSSEffect() {
	// Deinitialize motion vector decode
	shaders.mvec_decode_shader.version_free(shaders.mvec_decode_version);
}

DLSSContext *DLSSEffect::create_context(Size2i p_internal_size, Size2i p_target_size) {
	DLSSContextNGX *context = memnew(RendererRD::DLSSContextNGX);
	for (int i = 0; i < NGXContext::FEATURE_COUNT; i++) {
		if (NGXContext::get().is_available((NGXContext::Feature)i)) {
			context->quality[i] = NGXContext::get().find_quality((NGXContext::Feature)i, p_target_size.width, p_target_size.height, p_internal_size.width, p_internal_size.height);
			if (context->quality[i] == NGXContext::QUALITY_OFF) {
				ERR_PRINT(i == NGXContext::FEATURE_RAY_RECONSTRUCTION ? "Couldn't find an appropriate DLSS Ray Reconstruction mode." : "Couldn't find an appropriate DLSS mode.");
			}
		}
	}
	return context;
}

void DLSSEffect::upscale(const DLSSContext::Parameters &p_params) {
	DLSSContextNGX *context = (DLSSContextNGX *)p_params.context;

	const NGXContext::Feature feature = ngx_feature(p_params);

	// Delay enablement
	if (context->delay > 0) {
		--context->delay;
		return;
	}

	if (!NGXContext::get().is_available(feature) || context->quality[feature] == NGXContext::QUALITY_OFF) {
		return;
	}

	context->last_parameters = p_params;
	context->last_effect = this;

	// Decode mvecs
	{
		RD::get_singleton()->draw_command_begin_label("Decode Invalid Motion Vectors");
		UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
		ERR_FAIL_NULL(uniform_set_cache);
		MaterialStorage *material_storage = MaterialStorage::get_singleton();
		ERR_FAIL_NULL(material_storage);

		// setup our uniforms
		RD::Uniform u_velocity_image(RD::UNIFORM_TYPE_IMAGE, 0, p_params.velocity);
		RD::Uniform u_depth_texture(RD::UNIFORM_TYPE_TEXTURE, 0, p_params.depth);

		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();

		RID shader = shaders.mvec_decode_shader.version_get_shader(shaders.mvec_decode_version, 0);
		ERR_FAIL_COND(shader.is_null());

		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, shaders.mvec_decode_pipeline);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_velocity_image), 0);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_depth_texture), 1);

		auto texture_format = RD::get_singleton()->texture_get_format(p_params.velocity);

		float push_constants[20];
		push_constants[0] = texture_format.width;
		push_constants[1] = texture_format.height;
		push_constants[2] = 0.0f;
		push_constants[3] = 0.0f;
		memcpy(push_constants + 4, &p_params.reprojection.columns[0].x, sizeof(float) * 16);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, push_constants, sizeof(push_constants));

		RD::get_singleton()->compute_list_dispatch_threads(compute_list, texture_format.width, texture_format.height, 1);
		RD::get_singleton()->compute_list_add_barrier(compute_list);

		RD::get_singleton()->compute_list_end();
		RD::get_singleton()->draw_command_end_label();
	}

	// Inject DLSS into the render graph. Unlike Streamline, NGX does not change image layouts itself:
	// inputs must be sampled images and the output a storage image (VK_IMAGE_LAYOUT_GENERAL).
	RD::CallbackResource res[8];
	int num_resources = 0;
	res[num_resources++].rid = p_params.color;
	res[num_resources++].rid = p_params.depth;
	res[num_resources++].rid = p_params.velocity;
	if (feature == NGXContext::FEATURE_RAY_RECONSTRUCTION) {
		res[num_resources++].rid = p_params.dlss_rr_diffuse_albedo;
		res[num_resources++].rid = p_params.dlss_rr_specular_albedo;
		res[num_resources++].rid = p_params.dlss_rr_normal_roughness;
		res[num_resources++].rid = p_params.dlss_rr_specular_motion_vectors;
	}
	for (int i = 0; i < num_resources; i++) {
		ERR_FAIL_COND(res[i].rid.is_null());
		res[i].usage = RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE;
	}
	res[num_resources].rid = p_params.output;
	res[num_resources++].usage = RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE;
	RD::get_singleton()->driver_callback_add((RDD::DriverCallback)DLSSEffect::_upscale_internal_graph_callback, p_params.context, VectorView<RD::CallbackResource>(res, num_resources));
}

void DLSSEffect::_upscale_internal(RDD::CommandBufferID cmdid, const DLSSContext::Parameters &p_params) {
	DLSSContextNGX *context = (DLSSContextNGX *)p_params.context;
	void *native_command_buffer = RD::get_singleton()->get_device_driver()->command_buffer_get_native_handle(cmdid);
	const NGXContext::Feature feature = ngx_feature(p_params);

	const NGXContext::Resource color = ngx_resource(p_params.color, false);
	const NGXContext::Resource output = ngx_resource(p_params.output, true);

	if (context->feature != nullptr && (context->feature_kind != feature || context->feature_alpha_upscaling != p_params.dlss_rr_alpha_upscaling)) {
		// The feature and alpha upscaling are fixed at creation, so the feature is made again.
		NGXContext::get().feature_release(context->feature, RD::get_singleton()->get_frames_drawn() + RD::get_singleton()->get_frame_delay() + 1);
		context->feature = nullptr;
	}
	if (context->feature == nullptr) {
		NGXContext::CreateInfo create_info;
		create_info.render_width = color.width;
		create_info.render_height = color.height;
		create_info.target_width = output.width;
		create_info.target_height = output.height;
		create_info.quality = context->quality[feature];
		create_info.depth_inverted = p_params.reverse_depth;
		create_info.alpha_upscaling = p_params.dlss_rr_alpha_upscaling;
		create_info.preset = p_params.preset;
		if (create_info.preset == '?') {
			// Shared with the Streamline path, which reads the same project settings.
			const String preset = GLOBAL_GET(feature == NGXContext::FEATURE_RAY_RECONSTRUCTION ? "rendering/streamline/dlss_ray_reconstruction_preset" : "rendering/streamline/dlss_preset");
			if (preset.length() == 1) {
				create_info.preset = (char)preset[0];
			}
		}
		if (feature == NGXContext::FEATURE_RAY_RECONSTRUCTION) {
			context->feature = NGXContext::get().dlssd_create(native_command_buffer, create_info);
		} else {
			context->feature = NGXContext::get().dlss_create(native_command_buffer, create_info);
		}
		if (context->feature == nullptr) {
			context->quality[feature] = NGXContext::QUALITY_OFF; // Don't retry every frame; the error was printed once.
			return;
		}
		context->feature_kind = feature;
		context->feature_alpha_upscaling = create_info.alpha_upscaling;
	}

	// Motion vectors are in UV units (as for Streamline's mvecScale of 1 and FSR2); NGX wants pixels.
	if (feature == NGXContext::FEATURE_SUPER_RESOLUTION) {
		NGXContext::DLSSEvaluateInfo info;
		info.color = color;
		info.output = output;
		info.depth = ngx_resource(p_params.depth, false);
		info.motion_vectors = ngx_resource(p_params.velocity, false);
		info.mv_scale_x = (float)color.width;
		info.mv_scale_y = (float)color.height;
		info.jitter_x = p_params.jitter.x;
		info.jitter_y = p_params.jitter.y;
		info.frame_time_ms = p_params.delta_time * 1000.0f;
		info.reset = p_params.reset_accumulation;
		NGXContext::get().dlss_evaluate(native_command_buffer, context->feature, info);
		return;
	}

	NGXContext::DLSSDEvaluateInfo info;
	info.color = color;
	info.output = output;
	info.depth = ngx_resource(p_params.depth, false);
	info.motion_vectors = ngx_resource(p_params.velocity, false);
	info.diffuse_albedo = ngx_resource(p_params.dlss_rr_diffuse_albedo, false);
	info.specular_albedo = ngx_resource(p_params.dlss_rr_specular_albedo, false);
	info.normal_roughness = ngx_resource(p_params.dlss_rr_normal_roughness, false);
	info.specular_motion_vectors = ngx_resource(p_params.dlss_rr_specular_motion_vectors, false);
	info.mv_scale_x = (float)color.width;
	info.mv_scale_y = (float)color.height;
	info.jitter_x = p_params.jitter.x;
	info.jitter_y = p_params.jitter.y;
	info.frame_time_ms = p_params.delta_time * 1000.0f;
	info.reset = p_params.reset_accumulation;
	ngx_convert_matrix(Projection(p_params.cam_transform.affine_inverse()), info.world_to_view);
	ngx_convert_matrix(p_params.cam_projection, info.view_to_clip);
	NGXContext::get().dlssd_evaluate(native_command_buffer, context->feature, info);
}

void RendererRD::DLSSEffect::_upscale_internal_graph_callback(RenderingDeviceDriver *p_driver, RDD::CommandBufferID p_command_buffer, void *p_userdata) {
	DLSSContextNGX *self = (DLSSContextNGX *)p_userdata;
	self->last_effect->_upscale_internal(p_command_buffer, self->last_parameters);
}

bool DLSSEffect::is_ready(const DLSSContext::Parameters &p_params) {
	const DLSSContextNGX *context = (const DLSSContextNGX *)p_params.context;
	// The same conditions upscale() writes the output under.
	const NGXContext::Feature feature = ngx_feature(p_params);
	return NGXContext::get().is_available(feature) && context->quality[feature] != NGXContext::QUALITY_OFF && context->delay == 0;
}

#endif // defined(NGX_ENABLED) && !defined(STREAMLINE_ENABLED)
