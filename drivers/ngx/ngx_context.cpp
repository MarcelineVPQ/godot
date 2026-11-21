/**************************************************************************/
/*  ngx_context.cpp                                                       */
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

#include "ngx_context.h"

#ifdef NGX_ENABLED

#include "core/os/os.h"
#include "core/version.h"
#include "drivers/vulkan/godot_vulkan.h"

#include <cstring>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

// clang-format off
// The DLSSD helper uses a macro from the Vulkan helper, so this order matters.
#include <thirdparty/ngx/include/nvsdk_ngx_helpers_vk.h>
#include <thirdparty/ngx/include/nvsdk_ngx_helpers_dlssd_vk.h>
// clang-format on

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// NGX takes wide strings; Godot's String is UTF-32, which is what wchar_t holds on Linux.
static_assert(sizeof(wchar_t) == sizeof(char32_t));

// Identifies this engine to NGX (NVSDK_NGX_ENGINE_TYPE_CUSTOM needs a GUID-formatted project ID).
static constexpr const char *NGX_PROJECT_ID = "bcdc19e4-0a44-45aa-a421-18cf11798cd4";

static const NVSDK_NGX_PerfQuality_Value ngx_quality_values[NGXContext::QUALITY_COUNT] = {
	NVSDK_NGX_PerfQuality_Value_DLAA,
	NVSDK_NGX_PerfQuality_Value_MaxQuality,
	NVSDK_NGX_PerfQuality_Value_Balanced,
	NVSDK_NGX_PerfQuality_Value_MaxPerf,
	NVSDK_NGX_PerfQuality_Value_UltraPerformance,
};

static String ngx_result_string(uint32_t p_result) {
	return "0x" + String::num_uint64(p_result, 16);
}

static void ngx_log_callback(const char *p_message, NVSDK_NGX_Logging_Level p_level, NVSDK_NGX_Feature p_component) {
	print_verbose(String("NGX: ") + String::utf8(p_message).strip_edges());
}

static NVSDK_NGX_Resource_VK ngx_resource(const NGXContext::Resource &p_resource) {
	VkImageSubresourceRange range = {};
	range.aspectMask = p_resource.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1;
	range.layerCount = 1;
	return NVSDK_NGX_Create_ImageView_Resource_VK((VkImageView)p_resource.view, (VkImage)p_resource.image, range, (VkFormat)p_resource.format, p_resource.width, p_resource.height, p_resource.read_write);
}

NGXContext &NGXContext::get() {
	static NGXContext context;
	return context;
}

NGXContext::NGXContext() {
	_set_unavailable("NVIDIA NGX was not initialized (Vulkan on an NVIDIA GPU is required).");
}

static const NVSDK_NGX_Feature ngx_features[NGXContext::FEATURE_COUNT] = {
	NVSDK_NGX_Feature_SuperSampling,
	NVSDK_NGX_Feature_RayReconstruction,
};

static NVSDK_NGX_FeatureDiscoveryInfo ngx_discovery_info(NGXContext::Feature p_feature, const String &p_application_data_path) {
	NVSDK_NGX_FeatureDiscoveryInfo info = {};
	info.SDKVersion = NVSDK_NGX_Version_API;
	info.FeatureID = ngx_features[p_feature];
	info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
	info.Identifier.v.ProjectDesc.ProjectId = NGX_PROJECT_ID;
	info.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
	info.Identifier.v.ProjectDesc.EngineVersion = GODOT_VERSION_NUMBER;
	info.ApplicationDataPath = (const wchar_t *)p_application_data_path.get_data();
	return info;
}

void NGXContext::get_vulkan_instance_extensions(LocalVector<CharString> &r_extensions) {
	const String data_path = OS::get_singleton()->get_user_data_dir();
	bool queried = false;
	for (int i = 0; i < FEATURE_COUNT; i++) {
		NVSDK_NGX_FeatureDiscoveryInfo info = ngx_discovery_info((Feature)i, data_path);
		uint32_t count = 0;
		VkExtensionProperties *properties = nullptr;
		if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &properties))) {
			for (uint32_t j = 0; j < count; j++) {
				r_extensions.push_back(CharString(properties[j].extensionName));
			}
			queried = true;
		}
	}
	if (queried) {
		return;
	}

	// Older drivers: fall back to the fixed list of the deprecated query.
	unsigned int instance_count = 0;
	unsigned int device_count = 0;
	const char **instance_extensions = nullptr;
	const char **device_extensions = nullptr;
	if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_RequiredExtensions(&instance_count, &instance_extensions, &device_count, &device_extensions))) {
		for (unsigned int i = 0; i < instance_count; i++) {
			r_extensions.push_back(CharString(instance_extensions[i]));
		}
	}
}

void NGXContext::get_vulkan_device_extensions(void *p_instance, void *p_physical_device, LocalVector<CharString> &r_extensions) {
	const String data_path = OS::get_singleton()->get_user_data_dir();
	bool queried = false;
	for (int i = 0; i < FEATURE_COUNT; i++) {
		NVSDK_NGX_FeatureDiscoveryInfo info = ngx_discovery_info((Feature)i, data_path);
		uint32_t count = 0;
		VkExtensionProperties *properties = nullptr;
		if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements((VkInstance)p_instance, (VkPhysicalDevice)p_physical_device, &info, &count, &properties))) {
			for (uint32_t j = 0; j < count; j++) {
				r_extensions.push_back(CharString(properties[j].extensionName));
			}
			queried = true;
		}
	}
	if (queried) {
		return;
	}

	unsigned int instance_count = 0;
	unsigned int device_count = 0;
	const char **instance_extensions = nullptr;
	const char **device_extensions = nullptr;
	if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_RequiredExtensions(&instance_count, &instance_extensions, &device_count, &device_extensions))) {
		for (unsigned int i = 0; i < device_count; i++) {
			r_extensions.push_back(CharString(device_extensions[i]));
		}
	}
}

void NGXContext::_set_unavailable(const String &p_reason) {
	for (int i = 0; i < FEATURE_COUNT; i++) {
		available[i] = false;
		unavailable_reasons[i] = p_reason;
	}
}

// Whether the library of p_feature loaded and runs on this GPU and driver.
void NGXContext::_check_feature(Feature p_feature) {
	NVSDK_NGX_Parameter *capabilities = (NVSDK_NGX_Parameter *)capability_parameters;
	static const char *const needs_updated_driver_keys[FEATURE_COUNT] = { NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver };
	static const char *const min_driver_major_keys[FEATURE_COUNT] = { NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMajor };
	static const char *const min_driver_minor_keys[FEATURE_COUNT] = { NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMinor };
	static const char *const available_keys[FEATURE_COUNT] = { NVSDK_NGX_Parameter_SuperSampling_Available, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available };
	static const char *const init_result_keys[FEATURE_COUNT] = { NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, NVSDK_NGX_Parameter_SuperSamplingDenoising_FeatureInitResult };
	static const char *const names[FEATURE_COUNT] = { "DLSS Super Resolution", "DLSS Ray Reconstruction" };
	static const char *const libraries[FEATURE_COUNT] = { "libnvidia-ngx-dlss.so", "libnvidia-ngx-dlssd.so" };

	int needs_updated_driver = 0;
	if (NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetI(capabilities, needs_updated_driver_keys[p_feature], &needs_updated_driver)) && needs_updated_driver) {
		unsigned int min_driver_major = 0;
		unsigned int min_driver_minor = 0;
		NVSDK_NGX_Parameter_GetUI(capabilities, min_driver_major_keys[p_feature], &min_driver_major);
		NVSDK_NGX_Parameter_GetUI(capabilities, min_driver_minor_keys[p_feature], &min_driver_minor);
		unavailable_reasons[p_feature] = vformat("%s needs NVIDIA driver %d.%d or newer. Please update the NVIDIA driver.", names[p_feature], min_driver_major, min_driver_minor);
		print_verbose("NGX: " + unavailable_reasons[p_feature]);
		return;
	}

	int is_available = 0;
	NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetI(capabilities, available_keys[p_feature], &is_available);
	if (NVSDK_NGX_FAILED(result) || !is_available) {
		int init_result = 0;
		NVSDK_NGX_Parameter_GetI(capabilities, init_result_keys[p_feature], &init_result);
		unavailable_reasons[p_feature] = vformat("%s is not available on this GPU, or %s was not found next to the Godot executable (NGX result %s).", names[p_feature], libraries[p_feature], ngx_result_string((uint32_t)init_result));
		print_verbose("NGX: " + unavailable_reasons[p_feature]);
		return;
	}

	available[p_feature] = true;
	unavailable_reasons[p_feature] = String();
	print_verbose(vformat("NGX: %s is available.", names[p_feature]));
}

void NGXContext::initialize_vulkan(void *p_instance, void *p_physical_device, void *p_device, void *p_get_instance_proc_addr, void *p_get_device_proc_addr) {
	if (device != nullptr) {
		// Only the first (main) rendering device uses NGX; local devices created later share nothing with it.
		return;
	}

	// NGX writes its logs here, and looks for the DLSS libraries next to the executable.
	application_data_path = OS::get_singleton()->get_user_data_dir();
	library_path = OS::get_singleton()->get_executable_path().get_base_dir();
	const wchar_t *library_paths[] = { (const wchar_t *)library_path.get_data() };

	NVSDK_NGX_FeatureCommonInfo common_info = {};
	common_info.PathListInfo.Path = library_paths;
	common_info.PathListInfo.Length = 1;
	if (OS::get_singleton()->is_stdout_verbose()) {
		common_info.LoggingInfo.LoggingCallback = ngx_log_callback;
		common_info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
	}

	NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(NGX_PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, GODOT_VERSION_NUMBER,
			(const wchar_t *)application_data_path.get_data(), (VkInstance)p_instance, (VkPhysicalDevice)p_physical_device, (VkDevice)p_device,
			(PFN_vkGetInstanceProcAddr)p_get_instance_proc_addr, (PFN_vkGetDeviceProcAddr)p_get_device_proc_addr, &common_info);
	if (NVSDK_NGX_FAILED(result)) {
		_set_unavailable("NVIDIA NGX could not start on this GPU or driver (error " + ngx_result_string(result) + "). DLSS needs an NVIDIA RTX GPU and a recent NVIDIA driver.");
		print_verbose("NGX: " + unavailable_reasons[0]);
		return;
	}
	device = p_device;

	NVSDK_NGX_Parameter *capabilities = nullptr;
	result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&capabilities);
	if (NVSDK_NGX_FAILED(result) || capabilities == nullptr) {
		_set_unavailable("The NVIDIA driver is too old for DLSS (error " + ngx_result_string(result) + "). Please update the NVIDIA driver.");
		print_verbose("NGX: " + unavailable_reasons[0]);
		return;
	}
	capability_parameters = capabilities;

	NVSDK_NGX_Parameter *parameters = nullptr;
	result = NVSDK_NGX_VULKAN_AllocateParameters(&parameters);
	if (NVSDK_NGX_FAILED(result) || parameters == nullptr) {
		_set_unavailable("NVIDIA NGX could not allocate its parameters (error " + ngx_result_string(result) + ").");
		print_verbose("NGX: " + unavailable_reasons[0]);
		return;
	}
	feature_parameters = parameters;

	for (int i = 0; i < FEATURE_COUNT; i++) {
		_check_feature((Feature)i);
	}
}

void NGXContext::shutdown_vulkan(void *p_device) {
	if (device == nullptr || device != p_device) {
		return;
	}
	// Shutting down releases every feature that is still alive.
	pending_releases.clear();
	if (feature_parameters) {
		NVSDK_NGX_VULKAN_DestroyParameters((NVSDK_NGX_Parameter *)feature_parameters);
		feature_parameters = nullptr;
	}
	if (capability_parameters) {
		NVSDK_NGX_VULKAN_DestroyParameters((NVSDK_NGX_Parameter *)capability_parameters);
		capability_parameters = nullptr;
	}
	NVSDK_NGX_VULKAN_Shutdown1((VkDevice)device);
	device = nullptr;
	_set_unavailable("NVIDIA NGX was shut down.");
}

bool NGXContext::get_optimal_settings(Feature p_feature, uint32_t p_target_width, uint32_t p_target_height, Quality p_quality, OptimalSettings &r_settings) {
	ERR_FAIL_INDEX_V(p_feature, FEATURE_COUNT, false);
	ERR_FAIL_INDEX_V(p_quality, QUALITY_COUNT, false);
	if (!available[p_feature]) {
		return false;
	}
	NVSDK_NGX_Parameter *parameters = (NVSDK_NGX_Parameter *)capability_parameters;

	// Each library publishes a callback in the capability parameters that fills in the render sizes it accepts for a
	// target size and quality mode.
	static const char *const callback_keys[FEATURE_COUNT] = { NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback, NVSDK_NGX_Parameter_DLSSDOptimalSettingsCallback };
	void *callback = nullptr;
	NVSDK_NGX_Parameter_GetVoidPointer(parameters, callback_keys[p_feature], &callback);
	if (callback == nullptr) {
		return false;
	}

	// The parameters are shared by every call, so a size the library doesn't write would still hold the value of an
	// earlier mode. Zero means the library left it out.
	static const char *const outputs[] = {
		NVSDK_NGX_Parameter_OutWidth,
		NVSDK_NGX_Parameter_OutHeight,
		NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width,
		NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height,
		NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width,
		NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height,
	};
	for (const char *output : outputs) {
		NVSDK_NGX_Parameter_SetUI(parameters, output, 0);
	}
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_Width, p_target_width);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_Height, p_target_height);
	NVSDK_NGX_Parameter_SetI(parameters, NVSDK_NGX_Parameter_PerfQualityValue, ngx_quality_values[p_quality]);
	NVSDK_NGX_Parameter_SetI(parameters, NVSDK_NGX_Parameter_RTXValue, 0);
	if (NVSDK_NGX_FAILED(((PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback)callback)(parameters))) {
		return false;
	}

	unsigned int values[std_size(outputs)] = {};
	for (uint32_t i = 0; i < std_size(outputs); i++) {
		NVSDK_NGX_Parameter_GetUI(parameters, outputs[i], &values[i]);
	}
	r_settings.optimal_width = values[0];
	r_settings.optimal_height = values[1];
	// Older libraries don't report a range; the optimal size is then the only size they accept.
	r_settings.min_width = values[2] ? values[2] : values[0];
	r_settings.min_height = values[3] ? values[3] : values[1];
	r_settings.max_width = values[4] ? values[4] : values[0];
	r_settings.max_height = values[5] ? values[5] : values[1];
	// A quality mode the library does not offer reports a zero size.
	return r_settings.optimal_width > 0 && r_settings.optimal_height > 0;
}

NGXContext::Quality NGXContext::find_quality(Feature p_feature, uint32_t p_target_width, uint32_t p_target_height, uint32_t p_render_width, uint32_t p_render_height) {
	Quality closest = QUALITY_OFF;
	float closest_distance_squared = 0.0f;
	for (int i = 0; i < QUALITY_COUNT; i++) {
		OptimalSettings settings;
		if (!get_optimal_settings(p_feature, p_target_width, p_target_height, (Quality)i, settings)) {
			continue;
		}
		if (p_render_width < settings.min_width || p_render_width > settings.max_width ||
				p_render_height < settings.min_height || p_render_height > settings.max_height) {
			continue;
		}
		const float dx = (float)settings.optimal_width - (float)p_render_width;
		const float dy = (float)settings.optimal_height - (float)p_render_height;
		const float distance_squared = dx * dx + dy * dy;
		if (closest == QUALITY_OFF || distance_squared < closest_distance_squared) {
			closest = (Quality)i;
			closest_distance_squared = distance_squared;
		}
	}
	return closest;
}

bool NGXContext::clamp_render_size(int p_target_width, int p_target_height, int &r_render_width, int &r_render_height) {
	for (int f = 0; f < FEATURE_COUNT; f++) {
		if (find_quality((Feature)f, p_target_width, p_target_height, r_render_width, r_render_height) != QUALITY_OFF) {
			return false; // A mode already accepts this size; leave it alone.
		}
	}

	const Feature feature = available[FEATURE_RAY_RECONSTRUCTION] ? FEATURE_RAY_RECONSTRUCTION : FEATURE_SUPER_RESOLUTION;
	OptimalSettings settings[QUALITY_COUNT];
	bool valid[QUALITY_COUNT] = {};
	for (int i = 0; i < QUALITY_COUNT; i++) {
		valid[i] = get_optimal_settings(feature, p_target_width, p_target_height, (Quality)i, settings[i]);
	}

	// Round down to the nearest mode that does not render more pixels than were asked for, or to
	// the lowest mode when the request is below all of them.
	int chosen = -1;
	for (int i = 0; i < QUALITY_COUNT; i++) {
		if (valid[i] && (int)settings[i].optimal_width <= r_render_width) {
			chosen = i;
			break;
		}
	}
	if (chosen == -1) {
		for (int i = QUALITY_COUNT - 1; i >= 0; i--) {
			if (valid[i]) {
				chosen = i;
				break;
			}
		}
	}
	if (chosen == -1) {
		return false;
	}

	r_render_width = (int)settings[chosen].optimal_width;
	r_render_height = (int)settings[chosen].optimal_height;
	return true;
}

void *NGXContext::dlss_create(void *p_command_buffer, const CreateInfo &p_info) {
	ERR_FAIL_COND_V(!available[FEATURE_SUPER_RESOLUTION], nullptr);
	ERR_FAIL_INDEX_V(p_info.quality, QUALITY_COUNT, nullptr);
	NVSDK_NGX_Parameter *parameters = (NVSDK_NGX_Parameter *)feature_parameters;

	// '?' is NVIDIA's default. The SDK marks presets G, H, I, N and O "do not use": they fall back to the default.
	NVSDK_NGX_DLSS_Hint_Render_Preset preset = NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
	if (p_info.preset == 'F' || (p_info.preset >= 'J' && p_info.preset <= 'M')) {
		preset = (NVSDK_NGX_DLSS_Hint_Render_Preset)(p_info.preset - 'A' + 1);
	} else if (p_info.preset != '?') {
		WARN_PRINT_ONCE(vformat("DLSS preset %s isn't offered on Linux, which has presets F, J, K, L and M. NVIDIA's default preset is used instead.", String::chr(p_info.preset)));
	}
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, preset);

	// HDR colour, hardware depth (reversed), unjittered motion vectors at render resolution, as for Ray
	// Reconstruction. Exposure is left to DLSS: Godot's luminance buffer holds the average luminance, not an exposure.
	int flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
	if (p_info.depth_inverted) {
		flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
	}
	if (p_info.alpha_upscaling) {
		flags |= NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling;
	}

	NVSDK_NGX_DLSS_Create_Params create_params = {};
	create_params.Feature.InWidth = p_info.render_width;
	create_params.Feature.InHeight = p_info.render_height;
	create_params.Feature.InTargetWidth = p_info.target_width;
	create_params.Feature.InTargetHeight = p_info.target_height;
	create_params.Feature.InPerfQualityValue = ngx_quality_values[p_info.quality];
	create_params.InFeatureCreateFlags = flags;

	NVSDK_NGX_Handle *handle = nullptr;
	NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSS_EXT1((VkDevice)device, (VkCommandBuffer)p_command_buffer, 1, 1, &handle, parameters, &create_params);
	ERR_FAIL_COND_V_MSG(NVSDK_NGX_FAILED(result), nullptr, "Failed to create the DLSS Super Resolution feature (NGX error " + ngx_result_string(result) + ").");
	return handle;
}

void *NGXContext::dlssd_create(void *p_command_buffer, const CreateInfo &p_info) {
	ERR_FAIL_COND_V(!available[FEATURE_RAY_RECONSTRUCTION], nullptr);
	ERR_FAIL_INDEX_V(p_info.quality, QUALITY_COUNT, nullptr);
	NVSDK_NGX_Parameter *parameters = (NVSDK_NGX_Parameter *)feature_parameters;

	// '?' is NVIDIA's default. The SDK marks the presets after F "do not use": they fall back to the default.
	NVSDK_NGX_RayReconstruction_Hint_Render_Preset preset = NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default;
	if (p_info.preset >= 'D' && p_info.preset <= 'F') {
		preset = (NVSDK_NGX_RayReconstruction_Hint_Render_Preset)(NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D + (p_info.preset - 'D'));
	} else if (p_info.preset != '?') {
		WARN_PRINT_ONCE(vformat("DLSS Ray Reconstruction preset %s isn't offered on Linux, which has presets D, E and F. NVIDIA's default preset is used instead.", String::chr(p_info.preset)));
	}
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance, preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance, preset);

	// The flags Streamline derives from the constants DLSSEffect sets: HDR colour, hardware depth
	// (reversed), unjittered motion vectors at render resolution.
	int flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
	if (p_info.depth_inverted) {
		flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
	}
	if (p_info.alpha_upscaling) {
		flags |= NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling;
	}

	NVSDK_NGX_DLSSD_Create_Params create_params = {};
	create_params.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
	create_params.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed;
	create_params.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_HW;
	create_params.InWidth = p_info.render_width;
	create_params.InHeight = p_info.render_height;
	create_params.InTargetWidth = p_info.target_width;
	create_params.InTargetHeight = p_info.target_height;
	create_params.InPerfQualityValue = ngx_quality_values[p_info.quality];
	create_params.InFeatureCreateFlags = flags;

	NVSDK_NGX_Handle *handle = nullptr;
	NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSSD_EXT1((VkDevice)device, (VkCommandBuffer)p_command_buffer, 1, 1, &handle, parameters, &create_params);
	ERR_FAIL_COND_V_MSG(NVSDK_NGX_FAILED(result), nullptr, "Failed to create the DLSS Ray Reconstruction feature (NGX error " + ngx_result_string(result) + ").");
	return handle;
}

bool NGXContext::dlss_evaluate(void *p_command_buffer, void *p_feature, const DLSSEvaluateInfo &p_info) {
	ERR_FAIL_COND_V(!available[FEATURE_SUPER_RESOLUTION] || p_feature == nullptr, false);

	NVSDK_NGX_Resource_VK color = ngx_resource(p_info.color);
	NVSDK_NGX_Resource_VK output = ngx_resource(p_info.output);
	NVSDK_NGX_Resource_VK depth = ngx_resource(p_info.depth);
	NVSDK_NGX_Resource_VK motion_vectors = ngx_resource(p_info.motion_vectors);

	NVSDK_NGX_VK_DLSS_Eval_Params eval_params = {};
	eval_params.Feature.pInColor = &color;
	eval_params.Feature.pInOutput = &output;
	eval_params.pInDepth = &depth;
	eval_params.pInMotionVectors = &motion_vectors;
	eval_params.InJitterOffsetX = p_info.jitter_x;
	eval_params.InJitterOffsetY = p_info.jitter_y;
	eval_params.InMVScaleX = p_info.mv_scale_x;
	eval_params.InMVScaleY = p_info.mv_scale_y;
	eval_params.InRenderSubrectDimensions.Width = p_info.color.width;
	eval_params.InRenderSubrectDimensions.Height = p_info.color.height;
	eval_params.InReset = p_info.reset ? 1 : 0;
	eval_params.InFrameTimeDeltaInMsec = p_info.frame_time_ms;

	NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT((VkCommandBuffer)p_command_buffer, (NVSDK_NGX_Handle *)p_feature, (NVSDK_NGX_Parameter *)feature_parameters, &eval_params);
	ERR_FAIL_COND_V_MSG(NVSDK_NGX_FAILED(result), false, "Failed to evaluate DLSS Super Resolution (NGX error " + ngx_result_string(result) + ").");
	return true;
}

bool NGXContext::dlssd_evaluate(void *p_command_buffer, void *p_feature, const DLSSDEvaluateInfo &p_info) {
	ERR_FAIL_COND_V(!available[FEATURE_RAY_RECONSTRUCTION] || p_feature == nullptr, false);

	NVSDK_NGX_Resource_VK color = ngx_resource(p_info.color);
	NVSDK_NGX_Resource_VK output = ngx_resource(p_info.output);
	NVSDK_NGX_Resource_VK depth = ngx_resource(p_info.depth);
	NVSDK_NGX_Resource_VK motion_vectors = ngx_resource(p_info.motion_vectors);
	NVSDK_NGX_Resource_VK diffuse_albedo = ngx_resource(p_info.diffuse_albedo);
	NVSDK_NGX_Resource_VK specular_albedo = ngx_resource(p_info.specular_albedo);
	NVSDK_NGX_Resource_VK normal_roughness = ngx_resource(p_info.normal_roughness);
	NVSDK_NGX_Resource_VK specular_motion_vectors = ngx_resource(p_info.specular_motion_vectors);

	// The arrays are only read during the call below.
	float world_to_view[16];
	float view_to_clip[16];
	memcpy(world_to_view, p_info.world_to_view, sizeof(world_to_view));
	memcpy(view_to_clip, p_info.view_to_clip, sizeof(view_to_clip));

	NVSDK_NGX_VK_DLSSD_Eval_Params eval_params = {};
	eval_params.pInColor = &color;
	eval_params.pInOutput = &output;
	eval_params.pInDepth = &depth;
	eval_params.pInMotionVectors = &motion_vectors;
	eval_params.pInDiffuseAlbedo = &diffuse_albedo;
	eval_params.pInSpecularAlbedo = &specular_albedo;
	eval_params.pInNormals = &normal_roughness; // Roughness is read from .w (packed mode).
	eval_params.pInMotionVectorsReflections = &specular_motion_vectors;
	eval_params.InJitterOffsetX = p_info.jitter_x;
	eval_params.InJitterOffsetY = p_info.jitter_y;
	eval_params.InMVScaleX = p_info.mv_scale_x;
	eval_params.InMVScaleY = p_info.mv_scale_y;
	eval_params.InRenderSubrectDimensions.Width = p_info.color.width;
	eval_params.InRenderSubrectDimensions.Height = p_info.color.height;
	eval_params.InReset = p_info.reset ? 1 : 0;
	eval_params.InFrameTimeDeltaInMsec = p_info.frame_time_ms;
	eval_params.pInWorldToViewMatrix = world_to_view;
	eval_params.pInViewToClipMatrix = view_to_clip;

	NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSSD_EXT((VkCommandBuffer)p_command_buffer, (NVSDK_NGX_Handle *)p_feature, (NVSDK_NGX_Parameter *)feature_parameters, &eval_params);
	ERR_FAIL_COND_V_MSG(NVSDK_NGX_FAILED(result), false, "Failed to evaluate DLSS Ray Reconstruction (NGX error " + ngx_result_string(result) + ").");
	return true;
}

void NGXContext::feature_release(void *p_feature, uint64_t p_release_frame) {
	if (p_feature == nullptr || device == nullptr) {
		return; // Shutdown already released it.
	}
	PendingRelease release;
	release.feature = p_feature;
	release.frame = p_release_frame;
	pending_releases.push_back(release);
}

void NGXContext::feature_release_pending(void *p_device, uint64_t p_current_frame) {
	if (p_device != device) {
		return; // Another rendering device, with its own frame count.
	}
	for (uint32_t i = 0; i < pending_releases.size();) {
		if (pending_releases[i].frame <= p_current_frame) {
			NVSDK_NGX_VULKAN_ReleaseFeature((NVSDK_NGX_Handle *)pending_releases[i].feature);
			pending_releases.remove_at_unordered(i);
		} else {
			i++;
		}
	}
}

#endif // NGX_ENABLED
