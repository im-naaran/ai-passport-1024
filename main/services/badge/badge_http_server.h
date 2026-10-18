#pragma once

#include "badge_http_protocol.h"
#include "badge_wifi_service.h"
#include "services/lightboard/lightboard_store.h"

typedef struct {
    const uint8_t *data;
    size_t length;
    const char *content_type;
} badge_http_asset_t;

typedef struct {
    badge_http_asset_t index_html;
    badge_http_asset_t style_css;
    badge_http_asset_t image_js;
    badge_http_asset_t app_js;
    badge_http_asset_t lightboard_font;
    badge_http_asset_t lightboard_charset;
    badge_http_asset_t lightboard_layout;
    badge_http_asset_t lightboard_protocol;
    badge_http_asset_t lightboard_app;
} badge_http_assets_t;

typedef enum { BADGE_HTTP_GET, BADGE_HTTP_POST } badge_http_method_t;
enum {
    BADGE_HTTP_READ_ERROR = -1,
    BADGE_HTTP_READ_TIMEOUT = -2,
    BADGE_HTTP_SERVER_TASK_STACK_BYTES = 8192,
    LIGHTBOARD_HTTP_CHUNK_BYTES = 4096,
    LIGHTBOARD_HTTP_BODY_MAX = LB_META_BYTES + LB_MAX_ITEMS * 2 * 1536 * 96,
};

typedef struct {
    badge_http_method_t method;
    const char *path;
    const char *content_type;
    const char *session_token;
    size_t content_length;
    void *context;
    int (*read)(void *context, uint8_t *buffer, size_t capacity);
} badge_http_request_t;

typedef struct {
    void *context;
    bool (*send)(void *context, int status, const char *content_type,
                 const uint8_t *body, size_t length);
} badge_http_response_t;

typedef struct badge_http_server {
    badge_store_t *store;
    lb_store_t *lightboard;
    badge_wifi_service_t *wifi;
    badge_http_assets_t assets;
    uint64_t session_token;
    int64_t (*now_us)(void *context);
    void *clock_context;
    void *platform_handle;
} badge_http_server_t;

void badge_http_server_init(badge_http_server_t *server, badge_store_t *store,
                            badge_http_assets_t assets,
                            int64_t (*now_us)(void *context), void *clock_context);
void badge_http_server_begin_session(badge_http_server_t *server,
                                     badge_wifi_service_t *wifi, uint64_t token);
int badge_http_server_handle(badge_http_server_t *server,
                             const badge_http_request_t *request,
                             badge_http_response_t response);
badge_wifi_http_runtime_t badge_http_server_runtime(badge_http_server_t *server);
badge_http_assets_t badge_http_embedded_assets(void);
