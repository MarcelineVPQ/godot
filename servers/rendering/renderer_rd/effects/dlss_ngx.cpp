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

// DLSS Ray Reconstruction through the NVIDIA NGX SDK, for Linux builds where Streamline is not
// available (see drivers/ngx). It mirrors the Streamline path in dlss.cpp: same motion vector
// decode, same inputs and settings, but calls NGX directly. Only Ray Reconstruction is offered;
// plain DLSS Super Resolution is not.

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
	NGXContext::Quality quality = NGXContext::QUALITY_OFF;
	void *feature = nullptr; // Created on first use, inside the render graph.
	bool ray_reconstruction = false; // Whether the last upscale() got the Ray Reconstruction guides.

	virtual ~DLSSContextNGX() {
		// Frames still in flight may use the feature, so NGX releases it once they are done.
		NGXContext::get().dlssd_release(feature, RD::get_singleton()->get_frames_drawn() + RD::get_singleton()->get_frame_delay() + 1);
	}
};
} // namespace RendererRD

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
	resource.depth = format.format >= RD::DATA_FORMAT_D16_UNORM && format.format <= RD::DATA_FORMAT_D32_SFLOAT_S8_UINT;
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
	if (NGXContext::get().is_dlssd_available()) {
		context->quality = NGXContext::get().dlssd_find_quality(p_target_size.width, p_target_size.height, p_internal_size.width, p_internal_size.height);
		if (context->quality == NGXContext::QUALITY_OFF) {
			ERR_PRINT("Couldn't find an appropriate DLSS Ray Reconstruction mode.");
		}
	}
	return context;
}

void DLSSEffect::upscale(const DLSSContext::Parameters &p_params) {
	DLSSContextNGX *context = (DLSSContextNGX *)p_params.context;

	NGXContext::get().dlssd_release_pending(RD::get_singleton()->get_frames_drawn());

	context->ray_reconstruction = p_params.dlss_rr;
	if (!p_params.dlss_rr) {
		if (NGXContext::get().is_dlssd_available()) {
			WARN_PRINT_ONCE("On Linux, DLSS works only as Ray Reconstruction for the path tracer. Set the Environment's path tracing denoiser to DLSS Ray Reconstruction to use it.");
		}
		return;
	}

	// Delay enablement
	if (context->delay > 0) {
		--context->delay;
		return;
	}

	if (!NGXContext::get().is_dlssd_available() || context->quality == NGXContext::QUALITY_OFF) {
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
	res[num_resources++].rid = p_params.dlss_rr_diffuse_albedo;
	res[num_resources++].rid = p_params.dlss_rr_specular_albedo;
	res[num_resources++].rid = p_params.dlss_rr_normal_roughness;
	res[num_resources++].rid = p_params.dlss_rr_specular_motion_vectors;
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

	NGXContext::DLSSDEvaluateInfo info;
	info.color = ngx_resource(p_params.color, false);
	info.output = ngx_resource(p_params.output, true);
	info.depth = ngx_resource(p_params.depth, false);
	info.motion_vectors = ngx_resource(p_params.velocity, false);
	info.diffuse_albedo = ngx_resource(p_params.dlss_rr_diffuse_albedo, false);
	info.specular_albedo = ngx_resource(p_params.dlss_rr_specular_albedo, false);
	info.normal_roughness = ngx_resource(p_params.dlss_rr_normal_roughness, false);
	info.specular_motion_vectors = ngx_resource(p_params.dlss_rr_specular_motion_vectors, false);

	if (context->feature == nullptr) {
		NGXContext::DLSSDCreateInfo create_info;
		create_info.render_width = info.color.width;
		create_info.render_height = info.color.height;
		create_info.target_width = info.output.width;
		create_info.target_height = info.output.height;
		create_info.quality = context->quality;
		create_info.depth_inverted = p_params.reverse_depth;
		create_info.preset = p_params.preset;
		if (create_info.preset == '?') {
			// Shared with the Streamline path, which reads the same project setting.
			const String preset = GLOBAL_GET("rendering/streamline/dlss_ray_reconstruction_preset");
			if (preset.length() == 1) {
				create_info.preset = (char)preset[0];
			}
		}
		context->feature = NGXContext::get().dlssd_create(native_command_buffer, create_info);
		if (context->feature == nullptr) {
			context->quality = NGXContext::QUALITY_OFF; // Don't retry every frame; the error was printed once.
			return;
		}
	}

	// Motion vectors are in UV units (as for Streamline's mvecScale of 1 and FSR2); NGX wants pixels.
	info.mv_scale_x = (float)info.color.width;
	info.mv_scale_y = (float)info.color.height;
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

bool DLSSEffect::is_ready(DLSSContext *p_context) {
	DLSSContextNGX *context = (DLSSContextNGX *)p_context;
	// ray_reconstruction comes from the previous upscale() call, so a viewport that stops using the
	// path tracer falls back one frame late.
	return NGXContext::get().is_dlssd_available() && context->quality != NGXContext::QUALITY_OFF && context->delay == 0 && context->ray_reconstruction;
}

#endif // defined(NGX_ENABLED) && !defined(STREAMLINE_ENABLED)
