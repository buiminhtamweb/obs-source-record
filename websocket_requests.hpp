#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration of the filter context from source-record.h
struct source_record_filter_context;
struct obs_data;
typedef struct obs_data obs_data_t;

// Register custom websocket requests
void register_custom_websocket_requests(void *vendor);
void websocket_get_active_sources_status(obs_data_t *request_data, obs_data_t *response_data, void *param);
char *websocket_context_generate_path(void *ctx, const char *record_folder, const char *extension, const char *filename_formatting, bool is_websocket_mode);
char *websocket_context_generate_split_path(void *ctx, const char *record_folder, const char *extension);
const char *websocket_context_get_active_filename(void *ctx);

#ifdef __cplusplus
}
#endif
