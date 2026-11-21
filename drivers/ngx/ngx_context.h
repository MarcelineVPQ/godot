/**************************************************************************/
/*  ngx_context.h                                                         */
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

#pragma once

#ifdef NGX_ENABLED

#include "core/string/ustring.h"
#include "core/templates/local_vector.h"

// DLSS Ray Reconstruction on Linux through the NVIDIA NGX SDK (Vulkan), used where Streamline is not
// available. Only this file's .cpp includes the SDK headers; Vulkan handles cross this API as plain
// integers and pointers so callers need neither the SDK nor Vulkan headers.
class NGXContext {
public:
	// Same order as the quality modes Streamline's DLSSEffect tries: highest render resolution first.
	enum Quality {
		QUALITY_OFF = -1,
		QUALITY_DLAA,
		QUALITY_MAX_QUALITY,
		QUALITY_BALANCED,
		QUALITY_MAX_PERFORMANCE,
		QUALITY_ULTRA_PERFORMANCE,
		QUALITY_COUNT,
	};

	struct OptimalSettings {
		uint32_t optimal_width = 0;
		uint32_t optimal_height = 0;
		uint32_t min_width = 0;
		uint32_t min_height = 0;
		uint32_t max_width = 0;
		uint32_t max_height = 0;
	};

	struct Resource {
		uint64_t image = 0; // VkImage
		uint64_t view = 0; // VkImageView
		uint32_t format = 0; // VkFormat
		uint32_t width = 0;
		uint32_t height = 0;
		bool depth = false;
		bool read_write = false;
	};

	struct DLSSDCreateInfo {
		uint32_t render_width = 0;
		uint32_t render_height = 0;
		uint32_t target_width = 0;
		uint32_t target_height = 0;
		Quality quality = QUALITY_OFF;
		char preset = '?';
		bool depth_inverted = true;
		bool alpha_upscaling = false; // Upscales the colour's alpha too.
	};

	struct DLSSDEvaluateInfo {
		Resource color;
		Resource output;
		Resource depth;
		Resource motion_vectors;
		Resource diffuse_albedo;
		Resource specular_albedo;
		Resource normal_roughness; // World-space normal XYZ, roughness W.
		Resource specular_motion_vectors;
		float jitter_x = 0.0f;
		float jitter_y = 0.0f;
		float mv_scale_x = 1.0f;
		float mv_scale_y = 1.0f;
		float frame_time_ms = 0.0f;
		bool reset = false;
		float world_to_view[16] = {}; // Same row-major layout Streamline receives.
		float view_to_clip[16] = {};
	};

	static NGXContext &get();

	// Vulkan setup, called by the Vulkan driver (NVIDIA GPUs only for the device-level calls).
	void get_vulkan_instance_extensions(LocalVector<CharString> &r_extensions);
	void get_vulkan_device_extensions(void *p_instance, void *p_physical_device, LocalVector<CharString> &r_extensions);
	void initialize_vulkan(void *p_instance, void *p_physical_device, void *p_device, void *p_get_instance_proc_addr, void *p_get_device_proc_addr);
	void shutdown_vulkan(void *p_device);

	bool is_dlssd_available() const { return dlssd_available; }
	// Plain-words reason shown to users when Ray Reconstruction cannot run.
	String get_unavailable_reason() const { return unavailable_reason; }

	bool dlssd_get_optimal_settings(uint32_t p_target_width, uint32_t p_target_height, Quality p_quality, OptimalSettings &r_settings);
	// Picks the quality mode whose render-size range holds the requested size, closest to its
	// optimal size (as DLSSEffect does through Streamline).
	Quality dlssd_find_quality(uint32_t p_target_width, uint32_t p_target_height, uint32_t p_render_width, uint32_t p_render_height);
	// Same rule as StreamlineContext::dlss_clamp_render_size, using the Ray Reconstruction table.
	bool dlssd_clamp_render_size(int p_target_width, int p_target_height, int &r_render_width, int &r_render_height);

	// p_command_buffer is a VkCommandBuffer. Returns an NGX feature handle, or nullptr on failure.
	void *dlssd_create(void *p_command_buffer, const DLSSDCreateInfo &p_info);
	bool dlssd_evaluate(void *p_command_buffer, void *p_feature, const DLSSDEvaluateInfo &p_info);
	// The GPU may still be using the feature, so it is released once p_release_frame has been reached.
	void dlssd_release(void *p_feature, uint64_t p_release_frame);
	// Called by the Vulkan driver at the start of every frame of p_device (a VkDevice).
	void dlssd_release_pending(void *p_device, uint64_t p_current_frame);

private:
	struct PendingRelease {
		void *feature = nullptr;
		uint64_t frame = 0;
	};

	void *device = nullptr; // VkDevice, set while NGX is initialized.
	void *capability_parameters = nullptr; // NVSDK_NGX_Parameter *
	void *feature_parameters = nullptr; // NVSDK_NGX_Parameter *
	bool dlssd_available = false;
	String unavailable_reason = "NVIDIA NGX was not initialized (Vulkan on an NVIDIA GPU is required).";
	LocalVector<PendingRelease> pending_releases;

	// Kept alive for NGX, which holds on to these strings.
	String application_data_path;
	String library_path;
};

#endif // NGX_ENABLED
