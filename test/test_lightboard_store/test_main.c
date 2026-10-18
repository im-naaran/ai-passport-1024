#include <unity.h>
#include "services/lightboard/lightboard_store.h"
#include "services/badge/badge_record.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint8_t *flash, *defaults;
static size_t defaults_length;
static uint8_t bundle[512];
static size_t bundle_length;
static lb_store_t store;
static unsigned writes, reads, erases, fail_write, fail_read, fail_erase;
static unsigned maps[2], lock_depth;
static size_t torn_bytes;
static bool map_fails, commit_uncertain;
static uint32_t u32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void put32(uint8_t *b, uint32_t v) { for (unsigned i = 0; i < 4; ++i) b[i] = v >> (8 * i); }
static void lock(void *ctx) { (void)ctx; TEST_ASSERT_EQUAL(0, lock_depth); lock_depth++; }
static void unlock(void *ctx) { (void)ctx; TEST_ASSERT_EQUAL(1, lock_depth); lock_depth--; }
static void bounds(size_t offset, size_t length) {
    TEST_ASSERT_EQUAL(1, lock_depth); TEST_ASSERT_TRUE(offset <= LB_PARTITION_SIZE && length <= LB_PARTITION_SIZE - offset);
}
static bool read_bytes(void *ctx, size_t offset, uint8_t *out, size_t length) {
    (void)ctx; bounds(offset, length);
    if (++reads == fail_read) return false;
    memcpy(out, flash + offset, length); return true;
}
static bool write_bytes(void *ctx, size_t offset, const uint8_t *bytes, size_t length) {
    (void)ctx; bounds(offset, length); ++writes;
    bool failed = writes == fail_write;
    size_t written = failed && torn_bytes < length ? torn_bytes : length;
    for (size_t i = 0; i < written; ++i) {
        TEST_ASSERT_EQUAL_HEX8(bytes[i], flash[offset + i] & bytes[i]);
        flash[offset + i] &= bytes[i];
    }
    if (commit_uncertain && offset % LB_BANK_SIZE == 28) { fail_read = reads + 1; return false; }
    return !failed;
}
static bool erase_bytes(void *ctx, size_t offset, size_t length) {
    (void)ctx; bounds(offset, length); TEST_ASSERT_EQUAL(LB_BANK_SIZE, length);
    TEST_ASSERT_EQUAL(0, maps[offset / LB_BANK_SIZE]);
    bool failed = ++erases == fail_erase;
    memset(flash + offset, 0xff, failed ? length / 2 : length); return !failed;
}
static bool map_bytes(void *ctx, size_t offset, size_t length, lb_mapping_t *map) {
    (void)ctx; bounds(offset, length);
    if (map_fails) return false;
    TEST_ASSERT_LESS_OR_EQUAL(2 * 1536 * 96, length);
    unsigned bank = offset / LB_BANK_SIZE; ++maps[bank];
    *map = (lb_mapping_t){flash + offset, bank}; return true;
}
static void unmap_bytes(void *ctx, lb_mapping_t map) {
    (void)ctx; TEST_ASSERT_EQUAL(1, lock_depth); TEST_ASSERT_GREATER_THAN(0, maps[map.handle]); --maps[map.handle];
}
static lb_backend_t backend(void) {
    return (lb_backend_t){NULL, LB_PARTITION_SIZE, read_bytes, write_bytes, erase_bytes, map_bytes, unmap_bytes, lock, unlock};
}
static void reset_faults(void) {
    writes = reads = erases = fail_write = fail_read = fail_erase = 0;
    torn_bytes = 0; map_fails = commit_uncertain = false;
}
static lb_result_t reload(lb_store_t *s) { return lb_store_init(s, backend(), defaults, defaults_length); }
static size_t prefix_length(void) { return LB_BUNDLE_HEADER + bundle[6] * LB_ENTRY_BYTES; }
static void revision(uint32_t value) { put32(bundle + 16, value); put32(bundle + 24, badge_crc32(bundle, 24)); }
static lb_result_t save(lb_store_t *s) {
    size_t prefix = prefix_length(); lb_result_t result = lb_store_begin(s, bundle, prefix);
    if (result != LB_OK) return result;
    for (size_t at = prefix; at < bundle_length;) {
        size_t count = bundle_length - at; if (count > 17) count = 17;
        result = lb_store_write(s, bundle + at, count); if (result != LB_OK) return result;
        at += count;
    }
    return lb_store_finish(s);
}
void setUp(void) {
    flash = malloc(LB_PARTITION_SIZE); TEST_ASSERT_NOT_NULL(flash); memset(flash, 0xff, LB_PARTITION_SIZE);
    FILE *f = fopen("main/resources/lightboard_defaults.bin", "rb"); TEST_ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END); defaults_length = (size_t)ftell(f); rewind(f);
    defaults = malloc(defaults_length); TEST_ASSERT_NOT_NULL(defaults);
    TEST_ASSERT_EQUAL(defaults_length, fread(defaults, 1, defaults_length, f)); fclose(f);
    f = fopen("test/fixtures/lightboard/bundle.bin", "rb"); TEST_ASSERT_NOT_NULL(f);
    bundle_length = fread(bundle, 1, sizeof(bundle), f); fclose(f); TEST_ASSERT_EQUAL(352, bundle_length);
    maps[0] = maps[1] = lock_depth = 0; reset_faults();
    TEST_ASSERT_EQUAL(LB_DEFAULTED, reload(&store)); revision(0); reset_faults();
}
void tearDown(void) { TEST_ASSERT_EQUAL(0, lock_depth); TEST_ASSERT_EQUAL(0, maps[0] + maps[1]); free(flash); free(defaults); }

static void test_default_snapshot_and_strict_strip_bounds(void) {
    lb_snapshot_t snapshot = {0}; TEST_ASSERT_EQUAL(LB_OK, lb_snapshot_acquire(&store, 0, &snapshot));
    TEST_ASSERT_TRUE(snapshot.compressed); TEST_ASSERT_EQUAL(0, snapshot.revision);
    TEST_ASSERT_EQUAL_STRING("需求不改，指定能橙", snapshot.item.text);
    const uint8_t *data; size_t length, raw;
    TEST_ASSERT_TRUE(lb_default_strip(snapshot.planes[0], snapshot.item.length[0], snapshot.item.width,
        snapshot.item.height, 0, &data, &length, &raw));
    TEST_ASSERT_EQUAL(snapshot.item.width * 4, raw);
    TEST_ASSERT_FALSE(lb_default_strip(snapshot.planes[0], 1, snapshot.item.width,
        snapshot.item.height, 0, &data, &length, &raw));
    lb_snapshot_release(&snapshot); lb_snapshot_release(&snapshot);
    lb_bundle_t metadata;
    uint8_t *bad = malloc(defaults_length); TEST_ASSERT_NOT_NULL(bad); memcpy(bad, defaults, defaults_length);
    put32(bad + u32(bad + 16 + 72), UINT32_MAX);
    put32(bad + 12, badge_crc32(bad + 16, defaults_length - 16));
    TEST_ASSERT_FALSE(lb_defaults_decode(bad, defaults_length, &metadata)); free(bad);
}
static void test_save_reload_overwrite_and_corrupt_newest_fallback(void) {
    TEST_ASSERT_EQUAL(LB_OK, save(&store)); TEST_ASSERT_EQUAL(1, store.revision);
    lb_store_t boot; TEST_ASSERT_EQUAL(LB_OK, reload(&boot)); TEST_ASSERT_EQUAL(1, boot.revision);
    revision(1); TEST_ASSERT_EQUAL(LB_OK, save(&boot)); TEST_ASSERT_EQUAL(2, boot.revision);
    flash[LB_BANK_SIZE + LB_BANK_HEADER + bundle_length - 1] ^= 1;
    TEST_ASSERT_EQUAL(LB_OK, reload(&boot)); TEST_ASSERT_EQUAL(1, boot.revision);
    flash[LB_BANK_HEADER + bundle_length - 1] ^= 1;
    TEST_ASSERT_EQUAL(LB_DEFAULTED, reload(&boot)); TEST_ASSERT_EQUAL(0, boot.revision);
}
static void test_leased_old_bank_cannot_be_erased_or_reused(void) {
    TEST_ASSERT_EQUAL(LB_OK, save(&store));
    lb_snapshot_t old = {0}; TEST_ASSERT_EQUAL(LB_OK, lb_snapshot_acquire(&store, 0, &old));
    const uint8_t *address = old.planes[0]; uint8_t first = *address;
    TEST_ASSERT_EQUAL(1, maps[0]);
    revision(1); TEST_ASSERT_EQUAL(LB_OK, save(&store)); TEST_ASSERT_EQUAL(2, store.revision);
    TEST_ASSERT_EQUAL(first, *address); TEST_ASSERT_EQUAL(1, old.revision);
    lb_snapshot_t current = {0}; TEST_ASSERT_EQUAL(LB_OK, lb_snapshot_acquire(&store, 1, &current));
    revision(2); unsigned previous_erases = erases;
    TEST_ASSERT_EQUAL(LB_BUSY, lb_store_begin(&store, bundle, prefix_length())); TEST_ASSERT_EQUAL(previous_erases, erases);
    lb_snapshot_release(&old); TEST_ASSERT_EQUAL(LB_OK, save(&store));
    TEST_ASSERT_EQUAL(2, current.revision); lb_snapshot_release(&current);
}
static void test_busy_conflict_truncation_and_map_failure_are_bounded(void) {
    revision(1); TEST_ASSERT_EQUAL(LB_CONFLICT, save(&store)); revision(0);
    TEST_ASSERT_EQUAL(LB_OK, lb_store_begin(&store, bundle, prefix_length()));
    TEST_ASSERT_EQUAL(LB_BUSY, lb_store_begin(&store, bundle, prefix_length()));
    TEST_ASSERT_EQUAL(LB_INVALID, lb_store_finish(&store)); lb_store_abort(&store);
    TEST_ASSERT_EQUAL(0, store.revision); TEST_ASSERT_EQUAL(LB_OK, save(&store));
    lb_snapshot_t snapshot = {0}; map_fails = true;
    TEST_ASSERT_EQUAL(LB_IO_ERROR, lb_snapshot_acquire(&store, 0, &snapshot));
    TEST_ASSERT_FALSE(snapshot.acquired); TEST_ASSERT_EQUAL(0, maps[0] + maps[1]);
    TEST_ASSERT_EQUAL(LB_INVALID, lb_snapshot_acquire(&store, 6, &snapshot));
}
static void test_every_write_and_read_failure_restores_a_complete_version(void) {
    TEST_ASSERT_EQUAL(LB_OK, save(&store));
    uint8_t *backup = malloc(LB_PARTITION_SIZE); TEST_ASSERT_NOT_NULL(backup); memcpy(backup, flash, LB_PARTITION_SIZE);
    reset_faults(); revision(1); TEST_ASSERT_EQUAL(LB_OK, save(&store));
    unsigned total_writes = writes, total_reads = reads;
    for (unsigned kind = 0; kind < 3; ++kind) {
        unsigned count = kind == 0 ? total_writes : kind == 1 ? total_reads : 1;
        for (unsigned failure = 1; failure <= count; ++failure) for (unsigned partial = 0; partial < 3; ++partial) {
            memcpy(flash, backup, LB_PARTITION_SIZE); reset_faults(); TEST_ASSERT_EQUAL(LB_OK, reload(&store));
            reset_faults();
            if (kind == 0) fail_write = failure;
            else if (kind == 1) fail_read = failure;
            else fail_erase = failure;
            torn_bytes = partial == 0 ? 0 : partial == 1 ? 2 : SIZE_MAX;
            lb_result_t result = save(&store);
            TEST_ASSERT_TRUE(result == LB_OK || result == LB_IO_ERROR);
            TEST_ASSERT_EQUAL(result == LB_OK ? 2 : 1, store.revision);
            reset_faults(); lb_store_t boot;
            TEST_ASSERT_EQUAL(LB_OK, reload(&boot)); TEST_ASSERT_TRUE(boot.revision == 1 || boot.revision == 2);
            lb_snapshot_t snapshot = {0}; TEST_ASSERT_EQUAL(LB_OK, lb_snapshot_acquire(&boot, 0, &snapshot));
            TEST_ASSERT_EQUAL_STRING("Hello 1024", snapshot.item.text); lb_snapshot_release(&snapshot);
        }
    }
    free(backup);
}
static void test_bad_checksum_abort_and_interrupted_upload_never_publish(void) {
    TEST_ASSERT_EQUAL(LB_OK, save(&store)); revision(1);
    TEST_ASSERT_EQUAL(LB_OK, lb_store_begin(&store, bundle, prefix_length()));
    TEST_ASSERT_EQUAL(LB_OK, lb_store_write(&store, bundle + prefix_length(), 1));
    lb_store_t boot; TEST_ASSERT_EQUAL(LB_OK, reload(&boot)); TEST_ASSERT_EQUAL(1, boot.revision);
    lb_store_abort(&store);
    bundle[bundle_length - 1] ^= 1;
    TEST_ASSERT_EQUAL(LB_INVALID, save(&store)); TEST_ASSERT_EQUAL(1, store.revision);
}
static void test_wrapped_sequence_and_uncertain_commit_stop_new_writes(void) {
    TEST_ASSERT_EQUAL(LB_OK, save(&store));
    uint8_t header[32]; lb_bank_prepare(header, UINT32_MAX, bundle_length, badge_crc32(bundle, bundle_length));
    lb_bank_commit(header + 28); memcpy(flash, header, sizeof(header));
    TEST_ASSERT_EQUAL(LB_OK, reload(&store)); TEST_ASSERT_EQUAL(UINT32_MAX, store.revision);
    revision(UINT32_MAX); TEST_ASSERT_EQUAL(LB_OK, save(&store)); TEST_ASSERT_EQUAL(1, store.revision);
    reset_faults(); revision(1); commit_uncertain = true;
    TEST_ASSERT_EQUAL(LB_IO_ERROR, save(&store)); TEST_ASSERT_FALSE(store.available);
    TEST_ASSERT_EQUAL(LB_IO_ERROR, lb_store_begin(&store, bundle, prefix_length()));
    reset_faults(); TEST_ASSERT_EQUAL(LB_OK, reload(&store)); TEST_ASSERT_EQUAL(2, store.revision);
}
static void test_unreadable_bank_disables_writes_but_keeps_defaults(void) {
    reset_faults(); fail_read = 1;
    TEST_ASSERT_EQUAL(LB_IO_ERROR, reload(&store)); TEST_ASSERT_FALSE(store.available);
    TEST_ASSERT_EQUAL(6, store.active.count); TEST_ASSERT_EQUAL(LB_IO_ERROR, save(&store));
}
static void test_full_width_resource_streams_across_readback_chunks(void) {
    const size_t plane = 1536u * 96u, prefix = LB_META_BYTES, length = prefix + 12 * plane;
    uint8_t *large = calloc(1, length); TEST_ASSERT_NOT_NULL(large);
    memcpy(large, bundle, 32); large[6] = 6;
    put32(large + 12, (uint32_t)length);
    for (unsigned item = 0; item < 6; ++item) {
        uint8_t *entry = large + 32 + 96 * item;
        memcpy(entry, bundle + 32, 96); entry[1] = 1;
        entry[2] = 0; entry[3] = 6; entry[4] = 96; entry[5] = 0;
        entry[6] = 0; entry[7] = 6; entry[8] = entry[9] = 0;
        put32(entry + 72, prefix + 2 * item * plane); put32(entry + 76, plane);
        put32(entry + 80, prefix + (2 * item + 1) * plane); put32(entry + 84, plane);
    }
    for (size_t n = prefix; n < length; ++n) large[n] = (uint8_t)(n * 17);
    put32(large + 20, badge_crc32(large + 32, length - 32)); put32(large + 24, badge_crc32(large, 24));
    TEST_ASSERT_EQUAL(LB_OK, lb_store_begin(&store, large, prefix));
    for (size_t offset = prefix; offset < length;) {
        size_t count = length - offset; if (count > 4096) count = 4096;
        TEST_ASSERT_EQUAL(LB_OK, lb_store_write(&store, large + offset, count)); offset += count;
    }
    TEST_ASSERT_EQUAL(LB_OK, lb_store_finish(&store));
    lb_store_t boot; TEST_ASSERT_EQUAL(LB_OK, reload(&boot));
    lb_snapshot_t snapshot = {0}; TEST_ASSERT_EQUAL(LB_OK, lb_snapshot_acquire(&boot, 5, &snapshot));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(large + prefix + 10 * plane, snapshot.planes[0], plane);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(large + prefix + 11 * plane, snapshot.planes[1], plane);
    lb_snapshot_release(&snapshot); free(large);
}
static void test_ambiguous_valid_sequences_are_read_only(void) {
    TEST_ASSERT_EQUAL(LB_OK, save(&store)); revision(1); TEST_ASSERT_EQUAL(LB_OK, save(&store));
    uint8_t header[32];
    lb_bank_prepare(header, 1, bundle_length, badge_crc32(bundle, bundle_length));
    lb_bank_commit(header + 28); memcpy(flash + LB_BANK_SIZE, header, sizeof(header));
    TEST_ASSERT_EQUAL(LB_IO_ERROR, reload(&store)); TEST_ASSERT_FALSE(store.available);
    // Both records remain intact; automatic recovery must not erase either candidate.
    revision(store.revision); unsigned count = erases;
    TEST_ASSERT_EQUAL(LB_IO_ERROR, save(&store)); TEST_ASSERT_EQUAL(count, erases);
}
int main(void) {
    UNITY_BEGIN(); RUN_TEST(test_default_snapshot_and_strict_strip_bounds);
    RUN_TEST(test_save_reload_overwrite_and_corrupt_newest_fallback);
    RUN_TEST(test_leased_old_bank_cannot_be_erased_or_reused);
    RUN_TEST(test_busy_conflict_truncation_and_map_failure_are_bounded);
    RUN_TEST(test_every_write_and_read_failure_restores_a_complete_version);
    RUN_TEST(test_bad_checksum_abort_and_interrupted_upload_never_publish);
    RUN_TEST(test_wrapped_sequence_and_uncertain_commit_stop_new_writes);
    RUN_TEST(test_unreadable_bank_disables_writes_but_keeps_defaults);
    RUN_TEST(test_full_width_resource_streams_across_readback_chunks);
    RUN_TEST(test_ambiguous_valid_sequences_are_read_only);
    return UNITY_END();
}
