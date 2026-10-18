#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "services/badge/badge_http_protocol.h"
#include "services/badge/badge_http_server.h"

enum { TOKEN = 0x12345678u };
static uint8_t body[BADGE_HTTP_BODY_MAX + 1];
static size_t body_length;
static unsigned begin_calls, finish_calls, abort_calls;
static size_t image_bytes;
static badge_store_result_t begin_result, write_result, finish_result;
static char received_name[BADGE_NAME_MAX_BYTES + 1];
static char received_bio[BADGE_BIO_MAX_BYTES + 1];
static badge_photo_shape_t received_shape;
static uint8_t store_flash[BADGE_PARTITION_SIZE];
static uint8_t default_image[BADGE_IMAGE_BYTES];
static badge_store_t profile_store;
static badge_wifi_service_t wifi_service;
static badge_http_server_t http_server;
static uint8_t response_body[BADGE_IMAGE_BYTES + 256];
static size_t response_length;
static int response_status;
static char response_type[48];
static bool response_fails;

static badge_store_result_t flash_erase(void *context, size_t offset, size_t length) {
    (void)context; memset(store_flash + offset, 0xff, length); return BADGE_STORE_OK;
}
static badge_store_result_t flash_write(void *context, size_t offset, const uint8_t *data, size_t length) {
    (void)context; memcpy(store_flash + offset, data, length); return BADGE_STORE_OK;
}
static badge_store_result_t flash_read(void *context, size_t offset, uint8_t *data, size_t length) {
    (void)context; memcpy(data, store_flash + offset, length); return BADGE_STORE_OK;
}
static void no_lock(void *context) { (void)context; }
static bool capture_response(void *context, int status, const char *type,
                             const uint8_t *data, size_t length) {
    (void)context;
    response_status = status; response_length = length;
    snprintf(response_type, sizeof(response_type), "%s", type);
    if (length <= sizeof(response_body)) memcpy(response_body, data, length);
    if (length < sizeof(response_body)) response_body[length] = '\0';
    return !response_fails;
}
static int64_t fixed_now(void *context) { return *(int64_t *)context; }
typedef struct { const uint8_t *data; size_t length; size_t offset; size_t chunk; int terminal; } body_reader_t;
static int read_body(void *context, uint8_t *out, size_t capacity) {
    body_reader_t *reader = context;
    if (reader->offset == reader->length) return reader->terminal;
    size_t take = reader->length - reader->offset;
    if (take > reader->chunk) take = reader->chunk;
    if (take > capacity) take = capacity;
    memcpy(out, reader->data + reader->offset, take); reader->offset += take;
    return (int)take;
}

static badge_store_result_t begin(void *context, const badge_upload_meta_t *meta) {
    (void)context; ++begin_calls;
    TEST_ASSERT_EQUAL(BADGE_IMAGE_FORMAT_RGB565_LE, meta->image_format);
    TEST_ASSERT_EQUAL(BADGE_IMAGE_WIDTH, meta->width);
    TEST_ASSERT_EQUAL(BADGE_IMAGE_HEIGHT, meta->height);
    TEST_ASSERT_EQUAL(BADGE_IMAGE_STRIDE, meta->stride);
    TEST_ASSERT_EQUAL(BADGE_IMAGE_BYTES, meta->image_length);
    TEST_ASSERT_LESS_OR_EQUAL(BADGE_NAME_MAX_BYTES, meta->name_length);
    memcpy(received_name, meta->name, meta->name_length);
    received_name[meta->name_length] = '\0';
    TEST_ASSERT_LESS_OR_EQUAL(BADGE_BIO_MAX_BYTES, meta->bio_length);
    if (meta->bio_length) memcpy(received_bio, meta->bio, meta->bio_length);
    received_bio[meta->bio_length] = '\0';
    received_shape = meta->shape;
    return begin_result;
}
static badge_store_result_t write_image(void *context, const uint8_t *data, size_t length) {
    (void)context;
    if (write_result != BADGE_STORE_OK) return write_result;
    for (size_t i = 0; i < length; ++i)
        TEST_ASSERT_EQUAL_HEX8((uint8_t)((image_bytes + i) & 0xFFu), data[i]);
    image_bytes += length;
    return BADGE_STORE_OK;
}
static badge_store_result_t finish(void *context) {
    (void)context; ++finish_calls; return finish_result;
}
static void abort_write(void *context) { (void)context; ++abort_calls; }
static badge_http_writer_t writer(void) {
    return (badge_http_writer_t){NULL, begin, write_image, finish, abort_write};
}
static size_t make_body(const uint8_t *name, size_t name_length,
                        const uint8_t *bio, size_t bio_length,
                        badge_photo_shape_t shape) {
    badge_http_envelope_header(body, (uint16_t)name_length, (uint16_t)bio_length, shape);
    memcpy(body + BADGE_HTTP_ENVELOPE_HEADER_SIZE, name, name_length);
    memcpy(body + BADGE_HTTP_ENVELOPE_HEADER_SIZE + name_length, bio, bio_length);
    for (size_t i = 0; i < BADGE_IMAGE_BYTES; ++i)
        body[BADGE_HTTP_ENVELOPE_HEADER_SIZE + name_length + bio_length + i] =
            (uint8_t)(i & 0xFFu);
    return BADGE_HTTP_ENVELOPE_HEADER_SIZE + name_length + bio_length + BADGE_IMAGE_BYTES;
}
static size_t make_legacy_body(uint16_t version, const uint8_t *name, size_t name_length) {
    const size_t header_size = version == 1 ? 12 : 14;
    body[0] = 'B'; body[1] = 'P'; body[2] = 'R'; body[3] = 'F';
    body[4] = (uint8_t)version; body[5] = 0;
    body[6] = (uint8_t)name_length; body[7] = (uint8_t)(name_length >> 8);
    if (version == 2) { body[8] = 0; body[9] = 0; }
    uint32_t image_length = BADGE_IMAGE_BYTES;
    size_t image_length_offset = version == 1 ? 8 : 10;
    for (unsigned i = 0; i < 4; ++i)
        body[image_length_offset + i] = (uint8_t)(image_length >> (i * 8));
    memcpy(body + header_size, name, name_length);
    for (size_t i = 0; i < BADGE_IMAGE_BYTES; ++i)
        body[header_size + name_length + i] = (uint8_t)(i & 0xFFu);
    return header_size + name_length + BADGE_IMAGE_BYTES;
}
static badge_http_parser_t parser(void) {
    badge_http_parser_t value;
    TEST_ASSERT_EQUAL(BADGE_HTTP_MORE,
        badge_http_parser_init(&value, body_length, TOKEN, TOKEN, writer()));
    return value;
}

void setUp(void) {
    begin_calls = finish_calls = abort_calls = 0; image_bytes = 0;
    begin_result = write_result = finish_result = BADGE_STORE_OK;
    memset(received_name, 0, sizeof(received_name));
    memset(received_bio, 0, sizeof(received_bio));
    body_length = make_body((const uint8_t *)"张三", 6,
                            (const uint8_t *)"保持好奇", 12,
                            BADGE_PHOTO_SHAPE_ROUNDED);
    memset(store_flash, 0xff, sizeof(store_flash));
    memset(default_image, 0, sizeof(default_image));
    badge_profile_snapshot_t defaults = {.name = "AI Passport", .image = default_image,
        .width = BADGE_IMAGE_WIDTH, .height = BADGE_IMAGE_HEIGHT, .stride = BADGE_IMAGE_STRIDE,
        .image_format = BADGE_IMAGE_FORMAT_RGB565_LE, .is_default = true,
        .bio = "我的 AI 身份", .shape = BADGE_PHOTO_SHAPE_SQUARE};
    badge_store_backend_t backend = {NULL, store_flash, sizeof(store_flash),
                                     flash_erase, flash_write, flash_read};
    badge_store_init(&profile_store, backend, defaults);
    badge_wifi_service_init(&wifi_service, (badge_wifi_adapter_t){.lock = no_lock, .unlock = no_lock});
    wifi_service.machine.state = BADGE_WIFI_WAITING_CLIENT;
    static const uint8_t html[] = "<html>badge</html>";
    static const uint8_t css[] = "body{}";
    static const uint8_t image_script[] = "image";
    static const uint8_t app_script[] = "app";
    badge_http_assets_t assets = {
        .index_html = {html, sizeof(html) - 1, "text/html; charset=utf-8"},
        .style_css = {css, sizeof(css) - 1, "text/css; charset=utf-8"},
        .image_js = {image_script, sizeof(image_script) - 1, "application/javascript; charset=utf-8"},
        .app_js = {app_script, sizeof(app_script) - 1, "application/javascript; charset=utf-8"},
        .lightboard_font = {css, sizeof(css) - 1, "font/woff2"},
        .lightboard_charset = {app_script, sizeof(app_script) - 1, "application/javascript; charset=utf-8"},
        .lightboard_layout = {app_script, sizeof(app_script) - 1, "application/javascript; charset=utf-8"},
        .lightboard_protocol = {app_script, sizeof(app_script) - 1, "application/javascript; charset=utf-8"},
        .lightboard_app = {app_script, sizeof(app_script) - 1, "application/javascript; charset=utf-8"},
    };
    static int64_t now = 500;
    badge_http_server_init(&http_server, &profile_store, assets, fixed_now, &now);
    badge_http_server_begin_session(&http_server, &wifi_service, TOKEN);
    response_length = 0; response_status = 0; response_type[0] = '\0'; response_fails = false;
}
void tearDown(void) {}

static void test_valid_body_one_byte_chunks_crosses_every_boundary(void) {
    badge_http_parser_t p = parser();
    for (size_t i = 0; i < body_length; ++i) {
        badge_http_result_t expected = i + 1 == body_length ? BADGE_HTTP_COMPLETE : BADGE_HTTP_MORE;
        TEST_ASSERT_EQUAL(expected, badge_http_parser_feed(&p, body + i, 1));
    }
    TEST_ASSERT_EQUAL(1, begin_calls); TEST_ASSERT_EQUAL(1, finish_calls);
    TEST_ASSERT_EQUAL(0, abort_calls); TEST_ASSERT_EQUAL(BADGE_IMAGE_BYTES, image_bytes);
    TEST_ASSERT_EQUAL_STRING("张三", received_name);
    TEST_ASSERT_EQUAL_STRING("保持好奇", received_bio);
    TEST_ASSERT_EQUAL(BADGE_PHOTO_SHAPE_ROUNDED, received_shape);
    TEST_ASSERT_EQUAL(BADGE_HTTP_COMPLETE, badge_http_parser_end(&p, BADGE_HTTP_END_DISCONNECTED));
}

static void test_header_name_and_bio_split_positions(void) {
    size_t boundary_end = BADGE_HTTP_ENVELOPE_HEADER_SIZE + 6 + 12;
    for (size_t split = 0; split <= boundary_end; ++split) {
        begin_calls = finish_calls = abort_calls = 0; image_bytes = 0;
        badge_http_parser_t p = parser();
        TEST_ASSERT_EQUAL(BADGE_HTTP_MORE, badge_http_parser_feed(&p, body, split));
        TEST_ASSERT_EQUAL(BADGE_HTTP_COMPLETE,
                          badge_http_parser_feed(&p, body + split, body_length - split));
        TEST_ASSERT_EQUAL(1, begin_calls); TEST_ASSERT_EQUAL(1, finish_calls);
        TEST_ASSERT_EQUAL(0, abort_calls);
    }
}

static void test_all_shapes_reach_writer(void) {
    for (badge_photo_shape_t shape = BADGE_PHOTO_SHAPE_SQUARE;
         shape <= BADGE_PHOTO_SHAPE_CIRCLE; shape = (badge_photo_shape_t)(shape + 1)) {
        setUp();
        body_length = make_body((const uint8_t *)"张三", 6,
                                (const uint8_t *)"", 0, shape);
        badge_http_parser_t p = parser();
        TEST_ASSERT_EQUAL(BADGE_HTTP_COMPLETE,
                          badge_http_parser_feed(&p, body, body_length));
        TEST_ASSERT_EQUAL(shape, received_shape);
    }
}

static void test_token_and_content_length_rejected_before_writer(void) {
    badge_http_parser_t p;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_TOKEN,
        badge_http_parser_init(&p, body_length, TOKEN + 1, TOKEN, writer()));
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_TOKEN, badge_http_parser_feed(&p, body, body_length));
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_LENGTH,
        badge_http_parser_init(&p, BADGE_HTTP_ENVELOPE_HEADER_SIZE + BADGE_IMAGE_BYTES,
                               TOKEN, TOKEN, writer()));
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_LENGTH,
        badge_http_parser_init(&p, BADGE_HTTP_BODY_MAX + 1, TOKEN, TOKEN, writer()));
    TEST_ASSERT_EQUAL(0, begin_calls); TEST_ASSERT_EQUAL(0, abort_calls);
}

static void test_bad_header_lengths_and_invalid_name(void) {
    badge_http_parser_t p = parser();
    body[0] ^= 1;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_PROTOCOL,
                      badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE));

    setUp(); p = parser(); body[4] = 2;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_PROTOCOL,
                      badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE));

    setUp(); p = parser(); body[6] = BADGE_NAME_MAX_BYTES + 1;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_LENGTH,
                      badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE));

    setUp(); p = parser(); body[12] = 0;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_LENGTH,
                      badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE));

    setUp(); p = parser(); body[10] = 3;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_SHAPE,
                      badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE));

    setUp(); p = parser(); body[11] = 1;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_PROTOCOL,
                      badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE));

    const uint8_t bad_name[] = {'a', '\n'};
    body_length = make_body(bad_name, sizeof(bad_name), (const uint8_t *)"", 0,
                            BADGE_PHOTO_SHAPE_SQUARE); p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_NAME,
        badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE + sizeof(bad_name)));
    TEST_ASSERT_EQUAL(0, begin_calls);

    const uint8_t bad_bio[] = {'a', '\n'};
    body_length = make_body((const uint8_t *)"张三", 6, bad_bio, sizeof(bad_bio),
                            BADGE_PHOTO_SHAPE_SQUARE); p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_BIO,
        badge_http_parser_feed(&p, body,
            BADGE_HTTP_ENVELOPE_HEADER_SIZE + 6 + sizeof(bad_bio)));
    TEST_ASSERT_EQUAL(0, begin_calls);
}

static void test_v1_and_v2_bodies_are_rejected_before_writer(void) {
    for (uint16_t version = 1; version <= 2; ++version) {
        body_length = make_legacy_body(version, (const uint8_t *)"旧网页", 9);
        badge_http_parser_t p = parser();
        TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_PROTOCOL,
                          badge_http_parser_feed(&p, body, body_length));
        TEST_ASSERT_EQUAL(0, begin_calls);
        TEST_ASSERT_EQUAL(0, abort_calls);
    }
}

static void test_truncated_timeout_cancel_and_extra_data(void) {
    badge_http_parser_t p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_MORE,
        badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE + 6 + 12 + 100));
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_TRUNCATED,
                      badge_http_parser_end(&p, BADGE_HTTP_END_DISCONNECTED));
    TEST_ASSERT_EQUAL(1, abort_calls);

    setUp(); p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_MORE,
        badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE + 6 + 12));
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_TIMEOUT,
                      badge_http_parser_end(&p, BADGE_HTTP_END_TIMEOUT));
    TEST_ASSERT_EQUAL(1, abort_calls);

    setUp(); p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_CANCELLED,
                      badge_http_parser_end(&p, BADGE_HTTP_END_CANCELLED));
    TEST_ASSERT_EQUAL(0, abort_calls);

    setUp(); p = parser(); body[body_length] = 0;
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_EXTRA_DATA,
                      badge_http_parser_feed(&p, body, body_length + 1));
    TEST_ASSERT_EQUAL(0, begin_calls);
}

static void test_busy_write_and_finish_errors_abort(void) {
    badge_http_parser_t p;
    begin_result = BADGE_STORE_BUSY; p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_BUSY,
        badge_http_parser_feed(&p, body, BADGE_HTTP_ENVELOPE_HEADER_SIZE + 6 + 12));
    TEST_ASSERT_EQUAL(1, begin_calls); TEST_ASSERT_EQUAL(0, abort_calls);

    setUp(); write_result = BADGE_STORE_IO_ERROR; p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_STORAGE, badge_http_parser_feed(&p, body, body_length));
    TEST_ASSERT_EQUAL(1, abort_calls); TEST_ASSERT_EQUAL(0, finish_calls);

    setUp(); finish_result = BADGE_STORE_IO_ERROR; p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_STORAGE, badge_http_parser_feed(&p, body, body_length));
    TEST_ASSERT_EQUAL(1, finish_calls); TEST_ASSERT_EQUAL(1, abort_calls);
}

static void test_complete_then_out_of_contract_extra_is_rejected(void) {
    badge_http_parser_t p = parser();
    TEST_ASSERT_EQUAL(BADGE_HTTP_COMPLETE, badge_http_parser_feed(&p, body, body_length));
    TEST_ASSERT_EQUAL(BADGE_HTTP_ERROR_EXTRA_DATA, badge_http_parser_feed(&p, body, 1));
    TEST_ASSERT_EQUAL(1, finish_calls); TEST_ASSERT_EQUAL(0, abort_calls);
}

static badge_http_request_t get_request(const char *path) {
    return (badge_http_request_t){.method = BADGE_HTTP_GET, .path = path};
}

static void test_server_get_routes_content_types_and_not_found(void) {
    badge_http_response_t response = {NULL, capture_response};
    const char *paths[] = {"/", "/style.css?v=20260928", "/badge_image.js?v=20260928",
                           "/app.js?v=20260928", "/api/status", "/api/profile",
                           "/lightboard.woff2", "/lightboard_charset.js?v=1",
                           "/lightboard_layout.js?v=1", "/lightboard_protocol.js?v=1", "/lightboard_app.js?v=1", "/api/photo"};
    const char *types[] = {"text/html", "text/css", "application/javascript", "application/javascript",
                           "application/json", "application/json", "font/woff2", "application/javascript",
                           "application/javascript", "application/javascript", "application/javascript", "application/octet-stream"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        badge_http_request_t request = get_request(paths[i]);
        TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response));
        TEST_ASSERT_EQUAL(BADGE_WIFI_CLIENT_CONNECTED, wifi_service.machine.state);
        TEST_ASSERT_NOT_NULL(strstr(response_type, types[i]));
        if (strcmp(paths[i], "/api/profile") == 0) {
            TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "\"bio\":\"我的 AI 身份\""));
            TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "\"shape\":\"square\""));
        }
    }
    TEST_ASSERT_EQUAL(BADGE_IMAGE_BYTES, response_length);
    badge_http_request_t missing = get_request("/missing");
    TEST_ASSERT_EQUAL(404, badge_http_server_handle(&http_server, &missing, response));
    TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "not_found"));
}

static void test_server_profile_returns_all_shape_names(void) {
    static const char *const names[] = {"square", "rounded", "circle"};
    badge_http_response_t response = {NULL, capture_response};
    badge_http_request_t request = get_request("/api/profile");
    for (badge_photo_shape_t shape = BADGE_PHOTO_SHAPE_SQUARE;
         shape <= BADGE_PHOTO_SHAPE_CIRCLE; shape = (badge_photo_shape_t)(shape + 1)) {
        profile_store.snapshot.shape = shape;
        TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response));
        char expected[32];
        snprintf(expected, sizeof(expected), "\"shape\":\"%s\"", names[shape]);
        TEST_ASSERT_NOT_NULL(strstr((char *)response_body, expected));
    }
}

static badge_http_request_t post_request(body_reader_t *reader, const char *token,
                                         const char *type, size_t declared) {
    return (badge_http_request_t){.method = BADGE_HTTP_POST, .path = "/api/profile",
        .content_type = type, .session_token = token, .content_length = declared,
        .context = reader, .read = read_body};
}

static void test_server_success_streams_commits_and_keeps_hotspot_active(void) {
    body_reader_t reader = {body, body_length, 0, 317, 0};
    badge_http_request_t request = post_request(&reader, "0000000012345678",
                                                "application/octet-stream", body_length);
    badge_http_response_t response = {NULL, capture_response};
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response));
    TEST_ASSERT_EQUAL_STRING("张三", badge_store_snapshot(&profile_store)->name);
    TEST_ASSERT_EQUAL_STRING("保持好奇", badge_store_snapshot(&profile_store)->bio);
    TEST_ASSERT_EQUAL(BADGE_PHOTO_SHAPE_ROUNDED,
                      badge_store_snapshot(&profile_store)->shape);
    TEST_ASSERT_EQUAL(BADGE_WIFI_CLIENT_CONNECTED, wifi_service.machine.state);
    TEST_ASSERT_TRUE(wifi_service.machine.save_success);

    badge_http_request_t get_profile = get_request("/api/profile");
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &get_profile, response));
    TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "\"shape\":\"rounded\""));

    body_reader_t second_reader = {body, body_length, 0, 997, 0};
    badge_http_request_t second_request = post_request(&second_reader, "0000000012345678",
                                                       "application/octet-stream", body_length);
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &second_request, response));
    TEST_ASSERT_EQUAL(BADGE_WIFI_CLIENT_CONNECTED, wifi_service.machine.state);
    TEST_ASSERT_TRUE(wifi_service.machine.save_success);
    bool profile_event = false;
    badge_wifi_event_t event;
    while (badge_wifi_service_poll(&wifi_service, &event))
        if (event.type == BADGE_WIFI_EVENT_PROFILE_UPDATED) profile_event = true;
    TEST_ASSERT_TRUE(profile_event);
}

static void test_server_rejects_headers_busy_and_truncated_body(void) {
    badge_http_response_t response = {NULL, capture_response};
    body_reader_t reader = {body, body_length, 0, 1024, 0};
    badge_http_request_t request = post_request(&reader, "bad", "application/octet-stream", body_length);
    TEST_ASSERT_EQUAL(403, badge_http_server_handle(&http_server, &request, response));
    request = post_request(&reader, "0000000012345678", "multipart/form-data", body_length);
    TEST_ASSERT_EQUAL(415, badge_http_server_handle(&http_server, &request, response));

    TEST_ASSERT_TRUE(badge_wifi_service_begin_upload(&wifi_service));
    request = post_request(&reader, "0000000012345678", "application/octet-stream", body_length);
    TEST_ASSERT_EQUAL(409, badge_http_server_handle(&http_server, &request, response));
    badge_wifi_service_finish_upload(&wifi_service, false);

    reader = (body_reader_t){body, 100, 0, 17, 0};
    request = post_request(&reader, "0000000012345678", "application/octet-stream", body_length);
    TEST_ASSERT_EQUAL(400, badge_http_server_handle(&http_server, &request, response));
    TEST_ASSERT_TRUE(badge_store_snapshot(&profile_store)->is_default);
}

static void test_server_maps_invalid_shape_without_starting_store(void) {
    body[10] = 3;
    body_reader_t reader = {body, body_length, 0, 1024, 0};
    badge_http_request_t request = post_request(&reader, "0000000012345678",
                                                "application/octet-stream", body_length);
    badge_http_response_t response = {NULL, capture_response};
    TEST_ASSERT_EQUAL(422, badge_http_server_handle(&http_server, &request, response));
    TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "invalid_shape"));
    TEST_ASSERT_TRUE(badge_store_snapshot(&profile_store)->is_default);
}

static void test_server_failed_success_response_does_not_close_hotspot(void) {
    body_reader_t reader = {body, body_length, 0, 1024, 0};
    badge_http_request_t request = post_request(&reader, "0000000012345678",
                                                "application/octet-stream", body_length);
    response_fails = true;
    TEST_ASSERT_EQUAL(-1, badge_http_server_handle(&http_server, &request,
                                                   (badge_http_response_t){NULL, capture_response}));
    TEST_ASSERT_EQUAL(BADGE_WIFI_CLIENT_CONNECTED, wifi_service.machine.state);
    TEST_ASSERT_TRUE(wifi_service.machine.save_success);
    TEST_ASSERT_EQUAL_INT64(0, wifi_service.machine.feedback_until_us);
}

static void test_removed_gallery_routes_cannot_read_or_mutate_storage(void) {
    const char *paths[] = {"/api/personalization", "/api/personalization/photo/1",
                          "/api/personalization/slot/1"};
    badge_http_response_t response = {NULL, capture_response};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        for (int method = BADGE_HTTP_GET; method <= BADGE_HTTP_POST; ++method) {
            badge_http_request_t request = {.method = (badge_http_method_t)method,
                .path = paths[i], .session_token = "0000000012345678"};
            TEST_ASSERT_EQUAL(404, badge_http_server_handle(&http_server, &request, response));
            TEST_ASSERT_FALSE(wifi_service.machine.upload_in_progress);
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_removed_gallery_routes_cannot_read_or_mutate_storage);
    RUN_TEST(test_valid_body_one_byte_chunks_crosses_every_boundary);
    RUN_TEST(test_header_name_and_bio_split_positions);
    RUN_TEST(test_all_shapes_reach_writer);
    RUN_TEST(test_v1_and_v2_bodies_are_rejected_before_writer);
    RUN_TEST(test_token_and_content_length_rejected_before_writer);
    RUN_TEST(test_bad_header_lengths_and_invalid_name);
    RUN_TEST(test_truncated_timeout_cancel_and_extra_data);
    RUN_TEST(test_busy_write_and_finish_errors_abort);
    RUN_TEST(test_complete_then_out_of_contract_extra_is_rejected);
    RUN_TEST(test_server_get_routes_content_types_and_not_found);
    RUN_TEST(test_server_profile_returns_all_shape_names);
    RUN_TEST(test_server_success_streams_commits_and_keeps_hotspot_active);
    RUN_TEST(test_server_rejects_headers_busy_and_truncated_body);
    RUN_TEST(test_server_maps_invalid_shape_without_starting_store);
    RUN_TEST(test_server_failed_success_response_does_not_close_hotspot);
    return UNITY_END();
}
