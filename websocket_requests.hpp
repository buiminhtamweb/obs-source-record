#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration of the filter context from source-record.h
struct source_record_filter_context;

// Register custom websocket requests
void register_custom_websocket_requests(void *vendor);

#ifdef __cplusplus
}
#endif
