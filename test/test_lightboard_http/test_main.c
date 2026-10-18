#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/badge/badge_http_server.h"

static uint8_t *flash, *defaults, *body;
static size_t defaults_length, body_length;
static lb_store_t store;
static badge_store_t badge;
static badge_wifi_service_t wifi;
static badge_http_server_t server;
static unsigned writes, reads, erases, fail_write, fail_read, fail_erase, depth;
static size_t max_write, max_read;
static char response_body[1600];
static bool response_fail, request_stop;
static int status;
static void lock(void *ctx) { (void)ctx; TEST_ASSERT_EQUAL(0, depth); ++depth; }
static void unlock(void *ctx) { (void)ctx; TEST_ASSERT_EQUAL(1, depth); --depth; }
static void no_lock(void *ctx) { (void)ctx; }
static void bounds(size_t offset, size_t length) {
    TEST_ASSERT_EQUAL(1, depth);
    TEST_ASSERT_TRUE(offset <= LB_PARTITION_SIZE && length <= LB_PARTITION_SIZE - offset);
}
static bool read_flash(void *ctx, size_t at, uint8_t *out, size_t length) {
    (void)ctx; bounds(at, length);
    if (++reads == fail_read) return false;
    memcpy(out, flash + at, length); return true;
}
static bool write_flash(void *ctx, size_t at, const uint8_t *bytes, size_t length) {
    (void)ctx; bounds(at, length);
    if (length > max_write) max_write = length;
    if (++writes == fail_write) return false;
    for (size_t i = 0; i < length; ++i) {
        TEST_ASSERT_EQUAL_HEX8(bytes[i], flash[at + i] & bytes[i]); flash[at + i] &= bytes[i];
    }
    return true;
}
static bool erase_flash(void *ctx, size_t at, size_t length) {
    (void)ctx; bounds(at, length);
    if (++erases == fail_erase) return false;
    TEST_ASSERT_EQUAL(0, store.references[at / LB_BANK_SIZE]);
    memset(flash + at, 255, length); return true;
}
static bool map_flash(void *ctx, size_t at, size_t length, lb_mapping_t *out) {
    (void)ctx; bounds(at, length); *out = (lb_mapping_t){flash + at, at / LB_BANK_SIZE}; return true;
}
static void unmap_flash(void *ctx, lb_mapping_t mapping) { (void)ctx; (void)mapping; TEST_ASSERT_EQUAL(1, depth); }
static lb_backend_t backend(void) {
    return (lb_backend_t){NULL, LB_PARTITION_SIZE, read_flash, write_flash, erase_flash,
                          map_flash, unmap_flash, lock, unlock};
}
static uint8_t *file_bytes(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb"); TEST_ASSERT_NOT_NULL(file);
    fseek(file, 0, SEEK_END); long n = ftell(file); rewind(file);
    TEST_ASSERT_GREATER_THAN(0, n);
    uint8_t *bytes = malloc(n); TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL(n, fread(bytes, 1, n, file)); fclose(file); *length = n;
    return bytes;
}
static void put32(uint8_t *p, uint32_t value) { for (unsigned i = 0; i < 4; ++i) p[i] = value >> (i * 8); }
static void put16(uint8_t *p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
static void seal(void) {
    put32(body + 20, badge_crc32(body + LB_BUNDLE_HEADER, body_length - LB_BUNDLE_HEADER));
    put32(body + 24, badge_crc32(body, 24));
}
static void expect_revision(uint32_t revision) { put32(body + 16, revision); seal(); }
static void reset(void) {
    memset(flash, 255, LB_PARTITION_SIZE);
    writes = reads = erases = fail_write = fail_read = fail_erase = depth = 0;
    max_write = max_read = 0;
    TEST_ASSERT_EQUAL(LB_DEFAULTED, lb_store_init(&store, backend(), defaults, defaults_length));
    badge_wifi_service_init(&wifi, (badge_wifi_adapter_t){.lock = no_lock, .unlock = no_lock});
    wifi.machine.state = BADGE_WIFI_CLIENT_CONNECTED; wifi.machine.client_count = 1;
    badge_http_server_init(&server, &badge, (badge_http_assets_t){0}, NULL, NULL);
    server.lightboard = &store;
    badge_http_server_begin_session(&server, &wifi, 0x12345678);
    response_fail = request_stop = false;
    response_body[0] = 0; status = 0;
}
void setUp(void) {
    flash = malloc(LB_PARTITION_SIZE); TEST_ASSERT_NOT_NULL(flash);
    defaults = file_bytes("main/resources/lightboard_defaults.bin", &defaults_length);
    body = file_bytes("test/fixtures/lightboard/bundle.bin", &body_length);
    expect_revision(0); reset();
}
void tearDown(void) {
    TEST_ASSERT_EQUAL(0, depth);
    free(body); free(defaults); free(flash);
}
typedef struct { size_t offset, length, chunk, break_at; int terminal; bool over_return; } reader_t;
static int read_body(void *ctx, uint8_t *out, size_t capacity) {
    reader_t *r = ctx;
    TEST_ASSERT_EQUAL(0, depth); // No storage/LVGL lock while waiting for socket data.
    TEST_ASSERT_LESS_OR_EQUAL(LIGHTBOARD_HTTP_CHUNK_BYTES, capacity);
    if (capacity > max_read) max_read = capacity;
    if (r->over_return) return (int)capacity + 1;
    if (r->offset >= r->length || r->offset >= r->break_at) return r->terminal;
    size_t n = r->length - r->offset;
    if (n > r->chunk) n = r->chunk;
    if (n > capacity) n = capacity;
    if (n > r->break_at - r->offset) n = r->break_at - r->offset;
    memcpy(out, body + r->offset, n); r->offset += n;
    if (request_stop && r->offset > LB_META_BYTES) badge_wifi_service_request_stop(&wifi);
    return n;
}
static bool send_response(void *ctx, int code, const char *type, const uint8_t *bytes, size_t length) {
    (void)ctx; TEST_ASSERT_EQUAL(0, depth);
    TEST_ASSERT_EQUAL_STRING("application/json; charset=utf-8", type);
    TEST_ASSERT_LESS_THAN(sizeof(response_body), length);
    status = code; memcpy(response_body, bytes, length); response_body[length] = 0;
    return !response_fail;
}
static int post(reader_t *reader, const char *token, size_t length) {
    badge_http_request_t request = {.method = BADGE_HTTP_POST, .path = "/api/lightboard",
        .content_type = "application/octet-stream", .session_token = token,
        .content_length = length, .context = reader, .read = read_body};
    return badge_http_server_handle(&server, &request, (badge_http_response_t){NULL, send_response});
}
static reader_t reader(size_t chunk) { return (reader_t){.length = body_length, .chunk = chunk, .break_at = SIZE_MAX}; }
static int get(void) {
    badge_http_request_t request = {.method = BADGE_HTTP_GET, .path = "/api/lightboard"};
    return badge_http_server_handle(&server, &request, (badge_http_response_t){NULL, send_response});
}
static unsigned updates(void) {
    badge_wifi_event_t event; unsigned count = 0;
    while (badge_wifi_service_poll(&wifi, &event)) count += event.type == BADGE_WIFI_EVENT_LIGHTBOARD_UPDATED;
    return count;
}
static void test_get_defaults_and_success_return_revision_limits_texts_and_token(void) {
    TEST_ASSERT_EQUAL(200, get());
    TEST_ASSERT_NOT_NULL(strstr(response_body, "\"revision\":0"));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "\"max_items\":6,\"max_chars\":20"));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "\"font_version\":1,\"layout_version\":1"));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "\"token\":\"0000000012345678\""));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "今天不修 BUG"));
    reader_t r = reader(4096);
    TEST_ASSERT_EQUAL(200, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "\"revision\":1"));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "Hello 1024"));
    TEST_ASSERT_EQUAL(1, updates());
    TEST_ASSERT_FALSE(wifi.machine.upload_in_progress);
}
static void test_one_byte_and_header_boundary_short_reads_commit_exactly_once(void) {
    const unsigned chunks[] = {1,31,32,33,95,96,97};
    for (unsigned i = 0; i < sizeof(chunks)/sizeof(chunks[0]); ++i) {
        reset(); reader_t r = reader(chunks[i]);
        TEST_ASSERT_EQUAL(200, post(&r, "0000000012345678", body_length));
        TEST_ASSERT_EQUAL(body_length, r.offset);
        TEST_ASSERT_EQUAL(1, store.revision); TEST_ASSERT_EQUAL(1, updates());
    }
}
static void test_token_type_and_oversize_rejected_before_body_or_erase(void) {
    const char *tokens[] = {NULL,"", "not-a-token", "0000000012345679"};
    for (unsigned i = 0; i < sizeof(tokens)/sizeof(tokens[0]); ++i) {
        reader_t r = reader(1);
        TEST_ASSERT_EQUAL(403, post(&r, tokens[i], body_length)); TEST_ASSERT_EQUAL(0, r.offset);
    }
    reader_t r = reader(1);
    TEST_ASSERT_EQUAL(413, post(&r, "0000000012345678", LIGHTBOARD_HTTP_BODY_MAX + 1u));
    TEST_ASSERT_EQUAL(400, post(&r, "0000000012345678", 32));
    badge_http_request_t request = {.method = BADGE_HTTP_POST, .path = "/api/lightboard",
        .content_type = "text/plain", .session_token = "0000000012345678", .content_length = body_length};
    TEST_ASSERT_EQUAL(415, badge_http_server_handle(&server, &request, (badge_http_response_t){NULL, send_response}));
    TEST_ASSERT_EQUAL(0, erases); TEST_ASSERT_EQUAL(0, updates());
}
static void test_disconnect_timeout_and_over_return_keep_old_list_and_abort_transaction(void) {
    const size_t cuts[] = {0, 31, 40, 223, 224, 300, 351};
    for (unsigned i = 0; i < sizeof(cuts)/sizeof(cuts[0]); ++i) for (unsigned timeout = 0; timeout < 2; ++timeout) {
        reset(); reader_t r = reader(7); r.break_at = cuts[i];
        r.terminal = timeout ? BADGE_HTTP_READ_TIMEOUT : BADGE_HTTP_READ_ERROR;
        TEST_ASSERT_EQUAL(timeout ? 408 : 400, post(&r, "0000000012345678", body_length));
        TEST_ASSERT_EQUAL(0, store.revision); TEST_ASSERT_FALSE(store.busy);
        TEST_ASSERT_FALSE(wifi.machine.upload_in_progress); TEST_ASSERT_EQUAL(0, updates());
    }
    reset(); reader_t r = reader(1); r.over_return = true;
    TEST_ASSERT_EQUAL(400, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_EQUAL(0, erases);
}
static void test_bad_versions_text_geometry_offsets_crc_and_http_length_never_publish(void) {
    uint8_t original[512]; memcpy(original, body, body_length);
    for (unsigned variant = 0; variant < 8; ++variant) {
        reset(); memcpy(body, original, body_length);
        uint8_t *entry = body + LB_BUNDLE_HEADER;
        switch (variant) {
        case 0: body[7] = 2; break;
        case 1: entry[0] = 21; memset(entry + 12, 0, LB_TEXT_BYTES); memset(entry + 12, 'A', 21); break;
        case 2: entry[12] = 0xc0; break;
        case 3: put16(entry + 2, 1537); break;
        case 4: put32(entry + 72, 0); break;
        case 5: body[4] = 2; break;
        case 6: body[6] = 7; break;
        case 7: body[body_length - 1] ^= 1; break;
        }
        if (variant != 7) seal();
        reader_t r = reader(4096);
        TEST_ASSERT_EQUAL(variant == 1 || variant == 6 ? 413 : 400,
                          post(&r, "0000000012345678", body_length));
        TEST_ASSERT_EQUAL(0, store.revision); TEST_ASSERT_EQUAL(0, updates());
        TEST_ASSERT_FALSE(store.busy);
    }
    reset(); memcpy(body, original, body_length); reader_t r = reader(4096);
    TEST_ASSERT_EQUAL(400, post(&r, "0000000012345678", body_length + 1));
    TEST_ASSERT_EQUAL(0, erases);
}
static void test_revision_conflict_and_busy_do_not_abort_someone_elses_transaction(void) {
    expect_revision(7); reader_t r = reader(4096);
    TEST_ASSERT_EQUAL(409, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "revision_conflict")); TEST_ASSERT_EQUAL(0, erases);
    expect_revision(0); store.busy = true; r = reader(4096);
    TEST_ASSERT_EQUAL(409, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_TRUE(store.busy); store.busy = false;
    wifi.machine.upload_in_progress = true; r = reader(4096);
    TEST_ASSERT_EQUAL(409, post(&r, "0000000012345678", body_length)); TEST_ASSERT_EQUAL(0, r.offset);
    wifi.machine.upload_in_progress = false;
}
static void test_flash_erase_write_readback_and_commit_failures_return_storage_error(void) {
    for (unsigned i = 0; i < 6; ++i) {
        reset();
        if (i < 4) fail_write = i + 1;
        else if (i == 4) fail_erase = 1;
        else fail_read = reads + 1;
        reader_t r = reader(4096);
        TEST_ASSERT_EQUAL(500, post(&r, "0000000012345678", body_length));
        TEST_ASSERT_NOT_NULL(strstr(response_body, "storage_error"));
        TEST_ASSERT_EQUAL(0, store.revision); TEST_ASSERT_FALSE(store.busy);
        TEST_ASSERT_EQUAL(0, updates());
    }
}
static void test_committed_response_loss_is_observable_by_get_without_duplicate_commit(void) {
    response_fail = true; reader_t r = reader(4096);
    TEST_ASSERT_EQUAL(-1, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_EQUAL(1, store.revision); TEST_ASSERT_EQUAL(1, updates());
    response_fail = false;
    TEST_ASSERT_EQUAL(200, get()); TEST_ASSERT_NOT_NULL(strstr(response_body, "\"revision\":1"));
    r = reader(4096); TEST_ASSERT_EQUAL(409, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_EQUAL(1, store.revision); TEST_ASSERT_EQUAL(0, updates());
}
static void test_referenced_old_bank_returns_busy_without_erasing_live_source(void) {
    reader_t r = reader(4096); TEST_ASSERT_EQUAL(200, post(&r, "0000000012345678", body_length));
    lb_snapshot_t snapshot = {0}; TEST_ASSERT_EQUAL(LB_OK, lb_snapshot_acquire(&store, 0, &snapshot));
    const uint8_t pixel = snapshot.planes[0][0];
    expect_revision(1); r = reader(4096); TEST_ASSERT_EQUAL(200, post(&r, "0000000012345678", body_length));
    expect_revision(2); r = reader(4096); TEST_ASSERT_EQUAL(409, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_EQUAL(2, store.revision); TEST_ASSERT_EQUAL(2, erases);
    TEST_ASSERT_EQUAL(pixel, snapshot.planes[0][0]);
    lb_snapshot_release(&snapshot);
}
static void test_coalesced_event_queue_still_exposes_authoritative_committed_revision(void) {
    for (unsigned i = 0; i < BADGE_WIFI_EVENT_QUEUE_CAPACITY; ++i) badge_wifi_service_profile_updated(&wifi);
    reader_t r = reader(4096); TEST_ASSERT_EQUAL(200, post(&r, "0000000012345678", body_length));
    badge_wifi_service_profile_updated(&wifi); // replaces the last, coalesced lamp event.
    TEST_ASSERT_EQUAL(0, updates());
    uint32_t revision;
    TEST_ASSERT_TRUE(lb_store_revision(&store, &revision)); TEST_ASSERT_EQUAL(1, revision);
    TEST_ASSERT_EQUAL(200, get()); TEST_ASSERT_NOT_NULL(strstr(response_body, "\"revision\":1"));
}
static void test_six_maximum_resources_stream_in_4k_chunks_and_stop_request_keeps_whole_commit(void) {
    free(body); body_length = LIGHTBOARD_HTTP_BODY_MAX;
    body = calloc(1, body_length); TEST_ASSERT_NOT_NULL(body);
    put32(body, 0x3150424c); put16(body + 4, 1); body[6] = 6; body[7] = 1;
    put16(body + 8, 1); put32(body + 12, body_length);
    size_t at = LB_META_BYTES, plane = 1536 * 96;
    for (unsigned i = 0; i < 6; ++i) {
        uint8_t *entry = body + LB_BUNDLE_HEADER + i * LB_ENTRY_BYTES;
        entry[0] = 20; entry[1] = 1; memset(entry + 12, 'W', 20);
        put16(entry + 2, 1536); put16(entry + 4, 96); put16(entry + 6, 1536);
        put16(entry + 10, 112); put16(entry + 88, 64);
        for (unsigned layer = 0; layer < 2; ++layer) {
            put32(entry + 72 + 8 * layer, at); put32(entry + 76 + 8 * layer, plane);
            memset(body + at, 30 + i * 20 + layer, plane); at += plane;
        }
    }
    seal(); request_stop = true; reader_t r = reader(4096);
    TEST_ASSERT_EQUAL(200, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_EQUAL(body_length, r.offset); TEST_ASSERT_EQUAL(4096, max_read);
    TEST_ASSERT_LESS_OR_EQUAL(4096, max_write);
    TEST_ASSERT_EQUAL(1, store.revision); TEST_ASSERT_EQUAL(6, store.active.count);
    TEST_ASSERT_EQUAL(1, updates());
    lb_snapshot_t snapshot = {0}; TEST_ASSERT_EQUAL(LB_OK, lb_snapshot_acquire(&store, 5, &snapshot));
    TEST_ASSERT_EQUAL(130, snapshot.planes[0][plane - 1]); lb_snapshot_release(&snapshot);
}
static void test_json_escaping_preserves_literal_quotes_and_backslashes(void) {
    uint8_t *entry = body + LB_BUNDLE_HEADER;
    memset(entry + 12, 0, LB_TEXT_BYTES); entry[0] = 4; memcpy(entry + 12, "A\"\\B", 4); seal();
    reader_t r = reader(4096); TEST_ASSERT_EQUAL(200, post(&r, "0000000012345678", body_length));
    TEST_ASSERT_NOT_NULL(strstr(response_body, "A\\\"\\\\B"));
    TEST_ASSERT_EQUAL(200, get()); TEST_ASSERT_NOT_NULL(strstr(response_body, "A\\\"\\\\B"));
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_defaults_and_success_return_revision_limits_texts_and_token);
    RUN_TEST(test_one_byte_and_header_boundary_short_reads_commit_exactly_once);
    RUN_TEST(test_token_type_and_oversize_rejected_before_body_or_erase);
    RUN_TEST(test_disconnect_timeout_and_over_return_keep_old_list_and_abort_transaction);
    RUN_TEST(test_bad_versions_text_geometry_offsets_crc_and_http_length_never_publish);
    RUN_TEST(test_revision_conflict_and_busy_do_not_abort_someone_elses_transaction);
    RUN_TEST(test_flash_erase_write_readback_and_commit_failures_return_storage_error);
    RUN_TEST(test_committed_response_loss_is_observable_by_get_without_duplicate_commit);
    RUN_TEST(test_referenced_old_bank_returns_busy_without_erasing_live_source);
    RUN_TEST(test_coalesced_event_queue_still_exposes_authoritative_committed_revision);
    RUN_TEST(test_six_maximum_resources_stream_in_4k_chunks_and_stop_request_keeps_whole_commit);
    RUN_TEST(test_json_escaping_preserves_literal_quotes_and_backslashes);
    return UNITY_END();
}
