#pragma once

#include <obs.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_AUDIO_MIXES
#define MAX_AUDIO_MIXES 6
#endif

struct source_record_filter_context {
	obs_source_t *source;
	video_t *video_output;
	audio_t *audio_output;
	bool output_active;
	uint32_t width;
	uint32_t height;
	uint64_t last_frame_time_ns;
	obs_view_t *view;
	bool starting_file_output;
	bool starting_stream_output;
	bool starting_replay_output;
	bool restart;
	obs_output_t *fileOutput;
	obs_output_t *streamOutput;
	obs_output_t *replayOutput;
	obs_encoder_t *encoder;
	obs_encoder_t *audioEncoder[MAX_AUDIO_MIXES];
	obs_service_t *service;
	bool record;
	bool stream;
	bool replayBuffer;
	obs_hotkey_pair_id enableHotkey;
	obs_hotkey_pair_id pauseHotkeys;
	obs_hotkey_id splitHotkey;
	obs_hotkey_id chapterHotkey;
	int audio_track;
	obs_weak_source_t *audio_source;
	bool closing;
	bool exiting;
	long long replay_buffer_duration;
	struct vec4 backgroundColor;
	bool remove_after_record;
	long long record_max_seconds;
	int last_frontend_event;
	void *websocket_context;
	bool websocket_recording;
	bool last_is_recording;
	bool last_is_paused;
	bool last_is_streaming;
	bool last_is_replay_active;
	bool status_initialized;

	bool recording;
	uint64_t recording_start_ns;
	uint64_t last_sent_second;
	struct obs_timer_t *status_timer;
};

// WebSocket integration helper functions
struct obs_timer_t;
struct obs_timer_t *obs_timer_create(void (*callback)(void *), void *param, uint32_t interval_ms);
void obs_timer_destroy(struct obs_timer_t *timer);

void websocket_emit_status_event(void *ctx, obs_source_t *parent, bool recording, uint64_t elapsed_ms, uint64_t elapsed_sec, const char *file_name);

void *websocket_context_create(void);
void websocket_context_destroy(void *ctx);
void websocket_context_clear_next_filename(void *ctx);
void websocket_context_set_next_filename(void *ctx, const char *filename);
char *websocket_context_generate_path(void *ctx, const char *record_folder, const char *extension, const char *filename_formatting, bool is_websocket_mode);
struct source_record_filter_context *find_filter_context(const char *source_name);

#ifdef __cplusplus
}
#endif
