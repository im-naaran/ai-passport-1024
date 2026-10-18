#include "badge_http_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#endif

static badge_store_result_t store_begin(void *context, const badge_upload_meta_t *meta) {
    return badge_store_begin_update(context, meta);
}
static badge_store_result_t store_write(void *context, const uint8_t *data, size_t length) {
    return badge_store_write(context, data, length);
}
static badge_store_result_t store_finish(void *context) { return badge_store_finish(context); }
static void store_abort(void *context) { badge_store_abort(context); }

static bool parse_token(const char *text, uint64_t *value) {
    if (!text || !value || strlen(text) != 16) return false;
    uint64_t parsed = 0;
    for (size_t i = 0; i < 16; ++i) {
        unsigned digit;
        if (text[i] >= '0' && text[i] <= '9') digit = (unsigned)(text[i] - '0');
        else if (text[i] >= 'a' && text[i] <= 'f') digit = (unsigned)(text[i] - 'a' + 10);
        else if (text[i] >= 'A' && text[i] <= 'F') digit = (unsigned)(text[i] - 'A' + 10);
        else return false;
        parsed = (parsed << 4) | digit;
    }
    *value = parsed;
    return true;
}

static int send_body(badge_http_response_t response, int status, const char *type,
                     const void *body, size_t length) {
    if (!response.send || !response.send(response.context, status, type, body, length)) return -1;
    return status;
}

static int send_error(badge_http_response_t response, int status, const char *code) {
    char body[96];
    int length = snprintf(body, sizeof(body), "{\"error\":\"%s\"}", code);
    return send_body(response, status, "application/json; charset=utf-8", body, (size_t)length);
}

static const badge_http_asset_t *asset_for(const badge_http_server_t *server, const char *path) {
    size_t path_length = strcspn(path, "?");
    if (path_length == strlen("/lightboard_app.js") && !strncmp(path, "/lightboard_app.js", path_length))
        return &server->assets.lightboard_app;
    if (path_length == strlen("/lightboard_protocol.js") && !strncmp(path, "/lightboard_protocol.js", path_length))
        return &server->assets.lightboard_protocol;
    if (path_length == strlen("/lightboard.woff2") && !strncmp(path, "/lightboard.woff2", path_length))
        return &server->assets.lightboard_font;
    if (path_length == strlen("/lightboard_charset.js") && !strncmp(path, "/lightboard_charset.js", path_length))
        return &server->assets.lightboard_charset;
    if (path_length == strlen("/lightboard_layout.js") && !strncmp(path, "/lightboard_layout.js", path_length))
        return &server->assets.lightboard_layout;
    if (path_length == 1 && strncmp(path, "/", path_length) == 0)
        return &server->assets.index_html;
    if (path_length == strlen("/style.css") &&
        strncmp(path, "/style.css", path_length) == 0)
        return &server->assets.style_css;
    if (path_length == strlen("/badge_image.js") &&
        strncmp(path, "/badge_image.js", path_length) == 0)
        return &server->assets.image_js;
    if (path_length == strlen("/app.js") &&
        strncmp(path, "/app.js", path_length) == 0)
        return &server->assets.app_js;
    return NULL;
}

static size_t json_escape(char *output, size_t capacity, const char *text) {
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && used + 2 < capacity; ++p) {
        if (*p == '"' || *p == '\\') { output[used++] = '\\'; output[used++] = (char)*p; }
        else if (*p < 0x20) continue;
        else output[used++] = (char)*p;
    }
    output[used] = '\0';
    return used;
}

static int lightboard_response(badge_http_server_t *server, badge_http_response_t response, bool saved) {
    lb_bundle_t list; uint32_t revision;
    if (!lb_store_metadata(server->lightboard, &list, &revision))
        return send_error(response, 500, "storage_error");
    char body[1152], escaped[LB_TEXT_BYTES * 2 + 1];
    int n = snprintf(body, sizeof(body),
        "{%s\"schema\":1,\"revision\":%lu,\"max_items\":6,\"max_chars\":20,"
        "\"font_version\":%u,\"layout_version\":%u,\"token\":\"%016llx\",\"texts\":[",
        saved ? "\"ok\":true," : "", (unsigned long)revision, LB_FONT_VERSION, LB_LAYOUT_VERSION,
        (unsigned long long)server->session_token);
    if (n < 0 || (size_t)n >= sizeof(body)) return send_error(response, 500, "storage_error");
    size_t used = n;
    for (uint8_t i = 0; i < list.count; ++i) {
        json_escape(escaped, sizeof(escaped), list.items[i].text);
        n = snprintf(body + used, sizeof(body) - used, "%s\"%s\"", i ? "," : "", escaped);
        if (n < 0 || (size_t)n >= sizeof(body) - used)
            return send_error(response, 500, "storage_error");
        used += n;
    }
    if (used + 2 > sizeof(body)) return send_error(response, 500, "storage_error");
    body[used++] = ']'; body[used++] = '}';
    return send_body(response, 200, "application/json; charset=utf-8", body, used);
}

static int lightboard_status(lb_result_t result, const char **code) {
    switch (result) {
    case LB_BUSY: *code = "busy"; return 409;
    case LB_CONFLICT: *code = "revision_conflict"; return 409;
    case LB_IO_ERROR: *code = "storage_error"; return 500;
    default: *code = "invalid_bundle"; return 400;
    }
}
static int read_exact(const badge_http_request_t *request, uint8_t *buffer, size_t length) {
    size_t used = 0;
    while (used < length) {
        int count = request->read ? request->read(request->context, buffer + used, length - used) :
                                  BADGE_HTTP_READ_ERROR;
        if (count == BADGE_HTTP_READ_TIMEOUT) return 408;
        if (count <= 0 || (size_t)count > length - used) return 400;
        used += count;
    }
    return 0;
}
static int handle_lightboard_post(badge_http_server_t *server, const badge_http_request_t *request,
                                  badge_http_response_t response) {
    if (!server->lightboard) return send_error(response, 503, "storage_unavailable");
    if (!request->content_type || strcmp(request->content_type, "application/octet-stream"))
        return send_error(response, 415, "invalid_content_type");
    uint64_t supplied;
    if (!parse_token(request->session_token, &supplied) || !server->session_token ||
        supplied != server->session_token) return send_error(response, 403, "invalid_token");
    if (request->content_length > LIGHTBOARD_HTTP_BODY_MAX) return send_error(response, 413, "too_large");
    if (request->content_length < LB_BUNDLE_HEADER + LB_ENTRY_BYTES)
        return send_error(response, 400, "invalid_length");
    if (!badge_wifi_service_begin_upload(server->wifi)) return send_error(response, 409, "busy");

    // One bounded heap chunk avoids placing a 4KiB buffer on the HTTP task's
    // stack alongside socket/Flash verification frames. No whole-body allocation.
    uint8_t *buffer = malloc(LIGHTBOARD_HTTP_CHUNK_BYTES);
    uint8_t prefix[LB_META_BYTES];
    bool began = false;
    int status = 500;
    const char *code = "storage_error";
    if (!buffer) goto failed;
    status = read_exact(request, prefix, LB_BUNDLE_HEADER);
    if (status) { code = status == 408 ? "timeout" : "invalid_length"; goto failed; }
    size_t prefix_length = LB_BUNDLE_HEADER + (size_t)prefix[6] * LB_ENTRY_BYTES;
    if (prefix[6] > LB_MAX_ITEMS) { status = 413; code = "too_many_items"; goto failed; }
    if (!prefix[6] || prefix_length > request->content_length) {
        status = 400; code = "invalid_bundle"; goto failed;
    }
    status = read_exact(request, prefix + LB_BUNDLE_HEADER, prefix_length - LB_BUNDLE_HEADER);
    if (status) { code = status == 408 ? "timeout" : "invalid_length"; goto failed; }
    for (unsigned i = 0; i < prefix[6]; ++i) {
        const uint8_t *entry = prefix + LB_BUNDLE_HEADER + i * LB_ENTRY_BYTES;
        // Reuse the record's UTF-8/codepoint checker to distinguish capacity from
        // malformed input. Never read past the entry's fixed text storage.
        if (entry[0] > LB_TEXT_BYTES || lb_text_check(entry + 12, entry[0]) == LB_TEXT_TOO_LONG) {
            status = 413; code = "text_too_long"; goto failed;
        }
    }
    lb_bundle_t meta;
    if (!lb_bundle_decode_meta(prefix, prefix_length, &meta) || meta.total_length != request->content_length) {
        status = 400; code = "invalid_bundle"; goto failed;
    }
    lb_result_t result = lb_store_begin(server->lightboard, prefix, prefix_length);
    if (result != LB_OK) { status = lightboard_status(result, &code); goto failed; }
    began = true;
    for (size_t received = prefix_length; received < request->content_length;) {
        size_t chunk = request->content_length - received;
        if (chunk > LIGHTBOARD_HTTP_CHUNK_BYTES) chunk = LIGHTBOARD_HTTP_CHUNK_BYTES;
        status = read_exact(request, buffer, chunk);
        if (status) { code = status == 408 ? "timeout" : "invalid_length"; goto failed; }
        result = lb_store_write(server->lightboard, buffer, chunk);
        if (result != LB_OK) { status = lightboard_status(result, &code); goto failed; }
        received += chunk;
    }
    result = lb_store_finish(server->lightboard);
    if (result != LB_OK) { status = lightboard_status(result, &code); goto failed; }
    free(buffer);
    // Publish only after durable commit, even when the response is lost. The
    // application also polls the committed revision to recover coalesced events.
    badge_wifi_service_finish_upload(server->wifi, true);
    badge_wifi_service_lightboard_updated(server->wifi);
    int sent = lightboard_response(server, response, true);
    if (sent == 200 && server->now_us)
        badge_wifi_service_response_finished(server->wifi, server->now_us(server->clock_context));
    return sent;
failed:
    if (began) lb_store_abort(server->lightboard);
    free(buffer);
    badge_wifi_service_finish_upload(server->wifi, false);
    return send_error(response, status, code);
}

static const char *shape_name(badge_photo_shape_t shape) {
    switch (shape) {
    case BADGE_PHOTO_SHAPE_SQUARE: return "square";
    case BADGE_PHOTO_SHAPE_ROUNDED: return "rounded";
    case BADGE_PHOTO_SHAPE_CIRCLE: return "circle";
    default: return NULL;
    }
}

static int handle_get(badge_http_server_t *server, const badge_http_request_t *request,
                      badge_http_response_t response) {
    const badge_http_asset_t *asset = asset_for(server, request->path);
    if (asset) {
        if (!asset->data || !asset->length) return send_error(response, 503, "assets_unavailable");
        return send_body(response, 200, asset->content_type, asset->data, asset->length);
    }
    if (strcmp(request->path, "/api/lightboard") == 0)
        return lightboard_response(server, response, false);
    badge_wifi_status_t status = badge_wifi_service_status(server->wifi);
    if (strcmp(request->path, "/api/status") == 0) {
        char body[192];
        int length = snprintf(body, sizeof(body),
            "{\"token\":\"%016llx\",\"width\":200,\"height\":200,\"imageBytes\":80000,"
            "\"nameMaxBytes\":48,\"connected\":%s}",
            (unsigned long long)server->session_token, status.client_count ? "true" : "false");
        return send_body(response, 200, "application/json; charset=utf-8", body, (size_t)length);
    }
    const badge_profile_snapshot_t *profile = badge_store_snapshot(server->store);
    if (!profile) return send_error(response, 500, "storage_error");
    if (strcmp(request->path, "/api/profile") == 0) {
        const char *shape = shape_name(profile->shape);
        if (!shape) return send_error(response, 500, "storage_error");
        char escaped[BADGE_NAME_MAX_BYTES * 2 + 1];
        char escaped_bio[BADGE_BIO_MAX_BYTES * 2 + 1];
        json_escape(escaped, sizeof(escaped), profile->name);
        json_escape(escaped_bio, sizeof(escaped_bio), profile->bio);
        char body[BADGE_NAME_MAX_BYTES * 2 + BADGE_BIO_MAX_BYTES * 2 + 128];
        int length = snprintf(body, sizeof(body),
            "{\"name\":\"%s\",\"bio\":\"%s\",\"shape\":\"%s\",\"width\":%u,\"height\":%u,\"isDefault\":%s}",
            escaped, escaped_bio, shape, profile->width, profile->height,
            profile->is_default ? "true" : "false");
        return send_body(response, 200, "application/json; charset=utf-8", body, (size_t)length);
    }
    if (strcmp(request->path, "/api/photo") == 0)
        return send_body(response, 200, "application/octet-stream", profile->image,
                         BADGE_IMAGE_BYTES);
    return send_error(response, 404, "not_found");
}

static int result_status(badge_http_result_t result, const char **code) {
    switch (result) {
    case BADGE_HTTP_ERROR_TOKEN: *code = "invalid_token"; return 403;
    case BADGE_HTTP_ERROR_BUSY: *code = "busy"; return 409;
    case BADGE_HTTP_ERROR_NAME: *code = "invalid_name"; return 422;
    case BADGE_HTTP_ERROR_BIO: *code = "invalid_bio"; return 422;
    case BADGE_HTTP_ERROR_SHAPE: *code = "invalid_shape"; return 422;
    case BADGE_HTTP_ERROR_LENGTH:
    case BADGE_HTTP_ERROR_EXTRA_DATA: *code = "invalid_length"; return 400;
    case BADGE_HTTP_ERROR_TIMEOUT: *code = "timeout"; return 408;
    case BADGE_HTTP_ERROR_STORAGE: *code = "storage_error"; return 500;
    default: *code = "invalid_request"; return 400;
    }
}

static int handle_post(badge_http_server_t *server, const badge_http_request_t *request,
                       badge_http_response_t response) {
    if (strcmp(request->path, "/api/lightboard") == 0)
        return handle_lightboard_post(server, request, response);
    if (strcmp(request->path, "/api/profile") != 0) return send_error(response, 404, "not_found");
    if (!request->content_type || strcmp(request->content_type, "application/octet-stream") != 0)
        return send_error(response, 415, "invalid_content_type");
    uint64_t supplied = 0;
    if (!parse_token(request->session_token, &supplied))
        return send_error(response, 403, "invalid_token");
    if (!badge_wifi_service_begin_upload(server->wifi)) return send_error(response, 409, "busy");

    badge_http_writer_t writer = {server->store, store_begin, store_write, store_finish, store_abort};
    badge_http_parser_t parser;
    badge_http_result_t result = badge_http_parser_init(&parser, request->content_length,
                                                        supplied, server->session_token, writer);
    uint8_t buffer[1024];
    while (result == BADGE_HTTP_MORE && parser.received < request->content_length) {
        int count = request->read ? request->read(request->context, buffer, sizeof(buffer)) :
                                    BADGE_HTTP_READ_ERROR;
        if (count > 0) result = badge_http_parser_feed(&parser, buffer, (size_t)count);
        else result = badge_http_parser_end(&parser, count == BADGE_HTTP_READ_TIMEOUT ?
                                             BADGE_HTTP_END_TIMEOUT : BADGE_HTTP_END_DISCONNECTED);
    }
    if (result == BADGE_HTTP_MORE) result = badge_http_parser_end(&parser, BADGE_HTTP_END_DISCONNECTED);
    if (result != BADGE_HTTP_COMPLETE) {
        badge_wifi_service_finish_upload(server->wifi, false);
        const char *code;
        int status = result_status(result, &code);
        return send_error(response, status, code);
    }

    badge_wifi_service_finish_upload(server->wifi, true);
    badge_wifi_service_profile_updated(server->wifi);
    static const char saved[] = "{\"ok\":true}";
    int sent = send_body(response, 200, "application/json; charset=utf-8", saved, sizeof(saved) - 1);
    if (sent == 200 && server->now_us)
        badge_wifi_service_response_finished(server->wifi, server->now_us(server->clock_context));
    return sent;
}

void badge_http_server_init(badge_http_server_t *server, badge_store_t *store,
                            badge_http_assets_t assets,
                            int64_t (*now_us)(void *context), void *clock_context) {
    if (!server) return;
    *server = (badge_http_server_t){.store = store, .assets = assets, .now_us = now_us,
                                   .clock_context = clock_context};
}

void badge_http_server_begin_session(badge_http_server_t *server,
                                     badge_wifi_service_t *wifi, uint64_t token) {
    if (!server) return;
    server->wifi = wifi;
    server->session_token = token ? token : 1;
}

int badge_http_server_handle(badge_http_server_t *server,
                             const badge_http_request_t *request,
                             badge_http_response_t response) {
    if (!server || !server->store || !server->wifi || !request || !request->path)
        return send_error(response, 500, "server_error");
    /* A queued DHCP ACK only proves that the server handed a frame to the Wi-Fi stack.
     * Actual HTTP traffic proves that the phone accepted its address and can reach us. */
    badge_wifi_service_on_station(server->wifi, true);
    switch (request->method) {
    case BADGE_HTTP_GET: return handle_get(server, request, response);
    case BADGE_HTTP_POST: return handle_post(server, request, response);
    default: return send_error(response, 405, "method_not_allowed");
    }
}

__attribute__((weak)) badge_http_assets_t badge_http_embedded_assets(void) {
    return (badge_http_assets_t){0};
}

#ifdef ESP_PLATFORM
static const char *TAG = "badge_http";

typedef struct { httpd_req_t *request; } esp_request_context_t;

static int esp_read_body(void *context, uint8_t *buffer, size_t capacity) {
    esp_request_context_t *request = context;
    int result = httpd_req_recv(request->request, (char *)buffer, capacity);
    return result == HTTPD_SOCK_ERR_TIMEOUT ? BADGE_HTTP_READ_TIMEOUT : result;
}

static bool esp_send(void *context, int status, const char *content_type,
                     const uint8_t *body, size_t length) {
    httpd_req_t *request = context;
    const char *status_text = "500 Internal Server Error";
    switch (status) {
        case 200: status_text = "200 OK"; break;
        case 400: status_text = "400 Bad Request"; break;
        case 401: status_text = "401 Unauthorized"; break;
        case 403: status_text = "403 Forbidden"; break;
        case 404: status_text = "404 Not Found"; break;
        case 405: status_text = "405 Method Not Allowed"; break;
        case 408: status_text = "408 Request Timeout"; break;
        case 409: status_text = "409 Conflict"; break;
        case 413: status_text = "413 Payload Too Large"; break;
        case 415: status_text = "415 Unsupported Media Type"; break;
        case 422: status_text = "422 Unprocessable Content"; break;
        case 500: status_text = "500 Internal Server Error"; break;
        case 503: status_text = "503 Service Unavailable"; break;
    }
    httpd_resp_set_status(request, status_text);
    httpd_resp_set_type(request, content_type);
    /* The configuration page is firmware-owned and may change between flashes. iOS captive
     * portals otherwise reuse stale HTML/JS for the same fixed AP address. */
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(request, "Pragma", "no-cache");
    httpd_resp_set_hdr(request, "Expires", "0");
    esp_err_t result = httpd_resp_send(request, (const char *)body, length);
    if (result != ESP_OK)
        ESP_LOGW(TAG, "response failed uri=%s status=%d bytes=%u error=%s",
                 request->uri, status, (unsigned)length, esp_err_to_name(result));
    return result == ESP_OK;
}

static esp_err_t esp_handler(httpd_req_t *request) {
    badge_http_server_t *server = request->user_ctx;
    if ((request->method == HTTP_GET && strcmp(request->uri, "/") == 0) ||
        request->method == HTTP_POST) {
        const char *method = request->method == HTTP_GET ? "GET" :
            "POST";
        ESP_LOGI(TAG, "%s %s content_len=%u",
                 method, request->uri, (unsigned)request->content_len);
    }
    char content_type[40] = {0};
    char token[32] = {0};
    if (httpd_req_get_hdr_value_len(request, "Content-Type") < sizeof(content_type))
        httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type));
    if (httpd_req_get_hdr_value_len(request, "X-Passport-Session") < sizeof(token))
        httpd_req_get_hdr_value_str(request, "X-Passport-Session", token, sizeof(token));
    esp_request_context_t read_context = {.request = request};
    badge_http_request_t core = {
        .method = request->method == HTTP_GET ? BADGE_HTTP_GET :
            BADGE_HTTP_POST,
        .path = request->uri,
        .content_type = content_type,
        .session_token = token,
        .content_length = request->content_len,
        .context = &read_context,
        .read = esp_read_body,
    };
    badge_http_response_t response = {.context = request, .send = esp_send};
    int status = badge_http_server_handle(server, &core, response);
    // Failed POSTs may leave an unread binary body. Close after sending the error
    // so those bytes cannot be interpreted as the next keep-alive request.
    return status < 0 || (request->method == HTTP_POST && status != 200) ? ESP_FAIL : ESP_OK;
}

static bool runtime_start(void *context, badge_wifi_service_t *wifi) {
    badge_http_server_t *server = context;
    uint64_t token = ((uint64_t)esp_random() << 32) | esp_random();
    badge_http_server_begin_session(server, wifi, token);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* A raw image upload nests socket receive and transactional Flash verification.
       Keep explicit headroom above ESP-IDF's 4 KiB default for the worst path. */
    config.stack_size = BADGE_HTTP_SERVER_TASK_STACK_BYTES;
    config.max_uri_handlers = 15;
    config.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t handle = NULL;
    esp_err_t result = httpd_start(&handle, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(result));
        return false;
    }
    static const struct { const char *path; httpd_method_t method; } routes[] = {
        {"/", HTTP_GET}, {"/style.css", HTTP_GET}, {"/badge_image.js", HTTP_GET},
        {"/app.js", HTTP_GET}, {"/api/status", HTTP_GET}, {"/api/profile", HTTP_GET},
        {"/api/photo", HTTP_GET}, {"/api/profile", HTTP_POST},
        {"/lightboard.woff2", HTTP_GET}, {"/lightboard_charset.js", HTTP_GET},
        {"/lightboard_layout.js", HTTP_GET}, {"/lightboard_protocol.js", HTTP_GET},
        {"/api/lightboard", HTTP_GET}, {"/api/lightboard", HTTP_POST},
        {"/lightboard_app.js", HTTP_GET},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        httpd_uri_t uri = {.uri = routes[i].path, .method = routes[i].method,
                           .handler = esp_handler, .user_ctx = server};
        result = httpd_register_uri_handler(handle, &uri);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "route registration failed at %u: %s",
                     (unsigned)i, esp_err_to_name(result));
            httpd_stop(handle);
            return false;
        }
    }
    server->platform_handle = handle;
    ESP_LOGI(TAG, "HTTP ready at http://192.168.4.1:%u/", config.server_port);
    return true;
}

static void runtime_stop(void *context) {
    badge_http_server_t *server = context;
    if (server->platform_handle) httpd_stop(server->platform_handle);
    server->platform_handle = NULL;
    server->session_token = 0;
    server->wifi = NULL;
}
#else
static bool runtime_start(void *context, badge_wifi_service_t *wifi) {
    badge_http_server_begin_session(context, wifi, 1);
    return true;
}
static void runtime_stop(void *context) {
    badge_http_server_t *server = context;
    server->session_token = 0; server->wifi = NULL;
}
#endif

badge_wifi_http_runtime_t badge_http_server_runtime(badge_http_server_t *server) {
    return (badge_wifi_http_runtime_t){.context = server, .start = runtime_start,
                                       .stop = runtime_stop};
}
