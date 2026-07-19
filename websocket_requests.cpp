#include "websocket_requests.hpp"
#include "source-record.h"
#include "obs-websocket-api.h"
#include <obs-module.h>
#include <util/platform.h>
#include <string>
#include <mutex>
#include <filesystem>
#include <ctime>
#include <vector>
#include <algorithm>

struct websocket_context {
	std::mutex mutex;
	std::string nextFilename;
	std::string activeFilename;
};

static std::string get_base_filename(const std::string &path)
{
	size_t last_slash = path.find_last_of("/\\");
	std::string name = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
	size_t last_dot = name.find_last_of('.');
	if (last_dot != std::string::npos) {
		name = name.substr(0, last_dot);
	}
	return name;
}

extern "C" void *websocket_context_create(void)
{
	return new websocket_context();
}

extern "C" void websocket_context_destroy(void *ctx)
{
	delete static_cast<websocket_context *>(ctx);
}

extern "C" void websocket_context_clear_next_filename(void *ctx)
{
	if (!ctx)
		return;
	auto *wctx = static_cast<websocket_context *>(ctx);
	std::lock_guard<std::mutex> lock(wctx->mutex);
	wctx->nextFilename.clear();
}

extern "C" char *websocket_context_generate_path(void *ctx, const char *record_folder, const char *extension, const char *filename_formatting, bool is_websocket_mode)
{
	if (!ctx)
		return nullptr;
	auto *wctx = static_cast<websocket_context *>(ctx);
	std::lock_guard<std::mutex> lock(wctx->mutex);

	std::string final_path;
	std::string final_filename;
	std::string ext = extension ? extension : "";

	if (!is_websocket_mode) {
		char *formatted = os_generate_formatted_filename(extension, true, filename_formatting);
		if (formatted) {
			final_filename = formatted;
			bfree(formatted);
		}
		final_path = std::string(record_folder) + "/" + final_filename;
		wctx->activeFilename = get_base_filename(final_filename);
	} else {
		std::string combined_format = wctx->nextFilename + (filename_formatting ? filename_formatting : "");
		char *formatted = os_generate_formatted_filename(extension, true, combined_format.c_str());
		if (formatted) {
			final_filename = formatted;
			bfree(formatted);
		}
		final_path = std::string(record_folder) + "/" + final_filename;

		std::error_code ec;
		if (std::filesystem::exists(final_path, ec)) {
			std::time_t t = std::time(nullptr);
			std::tm tm_local;
#ifdef _WIN32
			localtime_s(&tm_local, &t);
#else
			localtime_r(&t, &tm_local);
#endif
			char time_buf[64];
			std::strftime(time_buf, sizeof(time_buf), "%Y%m%d-%H%M%S", &tm_local);

			std::string new_filename = get_base_filename(final_filename) + "-" + time_buf;
			wctx->activeFilename = new_filename;
			if (!ext.empty()) {
				new_filename += "." + ext;
			}
			final_path = std::string(record_folder) + "/" + new_filename;

			blog(LOG_INFO, "Duplicate filename detected.\nRenamed to\n%s", new_filename.c_str());
		} else {
			wctx->activeFilename = get_base_filename(final_filename);
		}
	}

	std::string normalized_path;
	for (char c : final_path) {
		if (c == '\\') {
			normalized_path += '/';
		} else {
			if (c == '/' && !normalized_path.empty() && normalized_path.back() == '/') {
				continue;
			}
			normalized_path += c;
		}
	}

	return bstrdup(normalized_path.c_str());
}

extern "C" void websocket_set_next_filename(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	(void)param;
	const char *source_name = obs_data_get_string(request_data, "source");
	const char *filename = obs_data_get_string(request_data, "filename");

	if (!filename || strlen(filename) == 0) {
		obs_data_set_string(response_data, "error", "Invalid Filename");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	struct source_record_filter_context *context = find_filter_context(source_name);
	if (!context) {
		obs_data_set_string(response_data, "error", "Source Not Found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	bool is_recording = context->starting_file_output || 
	                    (context->output_active && context->fileOutput && obs_output_active(context->fileOutput));

	if (is_recording) {
		obs_data_set_string(response_data, "error", "Already Recording");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	auto *wctx = static_cast<websocket_context *>(context->websocket_context);
	if (wctx) {
		std::lock_guard<std::mutex> lock(wctx->mutex);
		wctx->nextFilename = filename;
	}

	blog(LOG_INFO, "[SourceRecord]\nSetNextFilename\nSource=%s\nFilename=%s", source_name, filename);
	obs_data_set_bool(response_data, "success", true);
}

extern "C" void websocket_get_recording_status(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	(void)param;
	const char *source_name = obs_data_get_string(request_data, "source");

	struct source_record_filter_context *context = find_filter_context(source_name);
	if (!context) {
		obs_data_set_string(response_data, "error", "Source Not Found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	bool is_recording = context->output_active && context->fileOutput && obs_output_active(context->fileOutput);

	std::string filename_val;
	long long duration_ms = 0;

	auto *wctx = static_cast<websocket_context *>(context->websocket_context);
	if (wctx) {
		std::lock_guard<std::mutex> lock(wctx->mutex);
		if (is_recording) {
			filename_val = wctx->activeFilename;
		} else {
			filename_val = wctx->nextFilename;
		}
	}

	if (is_recording && context->fileOutput) {
		int totalFrames = obs_output_get_total_frames(context->fileOutput);
		video_t *video = obs_output_video(context->fileOutput);
		if (video) {
			uint64_t frameTimeNs = video_output_get_frame_time(video);
			duration_ms = util_mul_div64(totalFrames, frameTimeNs, 1000000ULL);
		}
	}

	obs_data_set_bool(response_data, "recording", is_recording);
	obs_data_set_string(response_data, "filename", filename_val.c_str());
	obs_data_set_int(response_data, "duration", duration_ms);
	obs_data_set_bool(response_data, "success", true);
}

extern "C" void websocket_context_set_next_filename(void *ctx, const char *filename)
{
	if (!ctx)
		return;
	auto *wctx = static_cast<websocket_context *>(ctx);
	std::lock_guard<std::mutex> lock(wctx->mutex);
	wctx->nextFilename = filename ? filename : "";
}

struct find_scene_data {
	const char *source_name;
	const char *scene_name;
};

static bool enum_scene_callback(void *param, obs_source_t *scene_source)
{
	auto *data = static_cast<find_scene_data *>(param);
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (scene) {
		obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, data->source_name);
		if (item) {
			data->scene_name = obs_source_get_name(scene_source);
			return false;
		}
	}
	return true;
}

static const char *get_scene_for_source(obs_source_t *parent)
{
	if (!parent)
		return "";

	if (obs_scene_from_source(parent) != nullptr) {
		return obs_source_get_name(parent);
	}

	find_scene_data data;
	data.source_name = obs_source_get_name(parent);
	data.scene_name = nullptr;
	obs_enum_scenes(enum_scene_callback, &data);

	if (data.scene_name)
		return data.scene_name;

	return "";
}

struct enum_data {
	obs_data_array_t *array;
	std::vector<obs_source_t *> filters;
};

static void enum_filter_status(obs_source_t *parent, obs_source_t *child, void *param)
{
	if (strcmp(obs_source_get_unversioned_id(child), "source_record_filter") != 0)
		return;

	auto *data = static_cast<enum_data *>(param);

	if (std::find(data->filters.begin(), data->filters.end(), child) != data->filters.end())
		return;
	data->filters.push_back(child);

	struct source_record_filter_context *context = static_cast<struct source_record_filter_context *>(obs_obj_get_data(child));
	if (!context)
		return;

	obs_data_t *item = obs_data_create();

	const char *parent_name = parent ? obs_source_get_name(parent) : "";
	obs_data_set_string(item, "source", parent_name);

	const char *scene_name = parent ? get_scene_for_source(parent) : "";
	obs_data_set_string(item, "scene", scene_name);

	obs_data_set_string(item, "filter", obs_source_get_name(child));

	obs_data_t *settings = obs_source_get_settings(child);
	long long record_mode = obs_data_get_int(settings, "record_mode");
	obs_data_release(settings);
	obs_data_set_int(item, "record_mode", record_mode);

	bool is_recording = context->output_active && context->fileOutput && obs_output_active(context->fileOutput);
	bool is_paused = is_recording && obs_output_paused(context->fileOutput);
	bool is_streaming = context->output_active && context->streamOutput && obs_output_active(context->streamOutput);
	bool is_replay_active = context->output_active && context->replayOutput && obs_output_active(context->replayOutput);

	obs_data_set_bool(item, "recording", is_recording);
	obs_data_set_bool(item, "paused", is_paused);
	obs_data_set_bool(item, "streaming", is_streaming);
	obs_data_set_bool(item, "replay_buffer", is_replay_active);

	std::string filename_val;
	auto *wctx = static_cast<websocket_context *>(context->websocket_context);
	if (wctx) {
		std::lock_guard<std::mutex> lock(wctx->mutex);
		if (is_recording) {
			filename_val = wctx->activeFilename;
		} else {
			filename_val = wctx->nextFilename;
		}
	}
	obs_data_set_string(item, "filename", filename_val.c_str());

	long long duration_ms = 0;
	if (is_recording && context->fileOutput) {
		int totalFrames = obs_output_get_total_frames(context->fileOutput);
		video_t *video = obs_output_video(context->fileOutput);
		if (video) {
			uint64_t frameTimeNs = video_output_get_frame_time(video);
			duration_ms = util_mul_div64(totalFrames, frameTimeNs, 1000000ULL);
		}
	}
	obs_data_set_int(item, "duration", duration_ms);

	obs_data_array_push_back(data->array, item);
	obs_data_release(item);
}

static bool enum_source_for_filters(void *param, obs_source_t *source)
{
	obs_source_enum_filters(source, enum_filter_status, param);
	return true;
}

extern "C" void websocket_get_active_sources_status(obs_data_t *request_data, obs_data_t *response_data, void *param)
{
	(void)request_data;
	(void)param;

	enum_data data;
	data.array = obs_data_array_create();

	obs_enum_sources(enum_source_for_filters, &data);
	obs_enum_scenes(enum_source_for_filters, &data);

	obs_data_set_array(response_data, "sources", data.array);
	obs_data_array_release(data.array);
	obs_data_set_bool(response_data, "success", true);
}

extern "C" void register_custom_websocket_requests(void *vendor)
{
	obs_websocket_vendor_register_request(static_cast<obs_websocket_vendor>(vendor), "SetNextFilename", websocket_set_next_filename, nullptr);
	obs_websocket_vendor_register_request(static_cast<obs_websocket_vendor>(vendor), "GetRecordingStatus", websocket_get_recording_status, nullptr);
	obs_websocket_vendor_register_request(static_cast<obs_websocket_vendor>(vendor), "GetActiveSourcesStatus", websocket_get_active_sources_status, nullptr);
}
