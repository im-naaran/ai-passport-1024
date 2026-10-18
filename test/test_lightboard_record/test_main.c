#include <unity.h>
#include "services/lightboard/lightboard_record.h"
#include "services/badge/badge_record.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "text_vectors.inc"
static uint8_t golden[512];
static size_t golden_length;
static void put16(uint8_t *b, uint16_t v) { b[0] = v; b[1] = v >> 8; }
static void put32(uint8_t *b, uint32_t v) { put16(b, v); put16(b + 2, v >> 16); }
static void seal(uint8_t *b, size_t length) {
    put32(b + 20, badge_crc32(b + 32, length - 32)); put32(b + 24, badge_crc32(b, 24));
}
void setUp(void) {
    FILE *f = fopen("test/fixtures/lightboard/bundle.bin", "rb");
    TEST_ASSERT_NOT_NULL(f); golden_length = fread(golden, 1, sizeof(golden), f); fclose(f);
    TEST_ASSERT_EQUAL(352, golden_length);
}
void tearDown(void) {}

static void test_shared_text_vectors(void) {
    for (size_t n = 0; n < sizeof(vectors) / sizeof(vectors[0]); ++n) {
        lb_text_list_t list;
        TEST_ASSERT_EQUAL(vectors[n].valid, lb_parse_lines(vectors[n].input, strlen(vectors[n].input), &list));
        if (vectors[n].valid) {
            TEST_ASSERT_EQUAL(vectors[n].count, list.count);
            for (size_t i = 0; i < list.count; ++i) TEST_ASSERT_EQUAL_STRING(vectors[n].texts[i], list.text[i]);
        }
    }
    const uint8_t nul[] = {'a', 0, 'b'};
    TEST_ASSERT_FALSE(lb_text_valid(nul, sizeof(nul)));
    TEST_ASSERT_FALSE(lb_text_valid((const uint8_t *)" a", 2));
    TEST_ASSERT_FALSE(lb_text_valid((const uint8_t *)"a ", 2));
}
static void test_js_golden_bundle_and_crc(void) {
    lb_bundle_t bundle;
    TEST_ASSERT_TRUE(lb_bundle_validate(golden, golden_length, &bundle));
    TEST_ASSERT_EQUAL(7, bundle.expected_revision); TEST_ASSERT_EQUAL(2, bundle.count);
    TEST_ASSERT_EQUAL_STRING("中A1！", bundle.items[1].text);
    TEST_ASSERT_EQUAL(32, bundle.items[0].length[0]);
    golden[golden_length - 1] ^= 1;
    TEST_ASSERT_FALSE(lb_bundle_validate(golden, golden_length, &bundle));
    TEST_ASSERT_FALSE(lb_bundle_validate(golden, golden_length - 1, &bundle));
}
static void test_semantic_corruption_is_rejected_with_valid_checksums(void) {
    const size_t offsets[] = {4, 7, 8, 10, 28, 32, 33, 34, 38, 40, 44, 104, 108, 122};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        uint8_t bytes[512]; memcpy(bytes, golden, golden_length);
        if (offsets[i] == 34) bytes[34] = 0;
        else if (offsets[i] == 40 || offsets[i] == 44) bytes[offsets[i]] = 0xff;
        else ++bytes[offsets[i]];
        seal(bytes, golden_length); lb_bundle_t bundle;
        TEST_ASSERT_FALSE(lb_bundle_validate(bytes, golden_length, &bundle));
    }
    uint8_t bytes[512]; memcpy(bytes, golden, golden_length);
    put32(bytes + 104, UINT32_MAX); seal(bytes, golden_length);
    lb_bundle_t bundle; TEST_ASSERT_FALSE(lb_bundle_validate(bytes, golden_length, &bundle));
}
static void test_twenty_chinese_and_maximum_metadata(void) {
    uint8_t bytes[LB_META_BYTES] = {0}; memcpy(bytes, golden, 32); bytes[6] = 6;
    uint32_t next = LB_META_BYTES;
    for (unsigned i = 0; i < 6; ++i) {
        uint8_t *e = bytes + 32 + i * 96;
        memcpy(e, golden + 32, 96); e[0] = 60; e[1] = 1;
        for (unsigned n = 0; n < 20; ++n) memcpy(e + 12 + n * 3, "中", 3);
        put16(e + 2, 1536); put16(e + 4, 96); put16(e + 6, 1536); put16(e + 8, 0);
        for (unsigned layer = 0; layer < 2; ++layer) {
            put32(e + 72 + 8 * layer, next); put32(e + 76 + 8 * layer, 1536 * 96); next += 1536 * 96;
        }
    }
    put32(bytes + 12, next); put32(bytes + 24, badge_crc32(bytes, 24));
    lb_bundle_t bundle; TEST_ASSERT_TRUE(lb_bundle_decode_meta(bytes, sizeof(bytes), &bundle));
    TEST_ASSERT_EQUAL(1770080, next); TEST_ASSERT_LESS_OR_EQUAL(LB_MAX_BUNDLE, next);
    bytes[6] = 7; put32(bytes + 24, badge_crc32(bytes, 24));
    TEST_ASSERT_FALSE(lb_bundle_decode_meta(bytes, sizeof(bytes), &bundle));
}
static void test_bank_commit_and_wrapped_revisions(void) {
    uint8_t header[32]; uint32_t revision, length, crc;
    lb_bank_prepare(header, UINT32_MAX, 352, 123);
    TEST_ASSERT_FALSE(lb_bank_decode(header, &revision, &length, &crc));
    lb_bank_commit(header + 28);
    TEST_ASSERT_TRUE(lb_bank_decode(header, &revision, &length, &crc));
    TEST_ASSERT_EQUAL(UINT32_MAX, revision); TEST_ASSERT_EQUAL(352, length); TEST_ASSERT_EQUAL(123, crc);
    TEST_ASSERT_TRUE(lb_revision_newer(1, UINT32_MAX)); TEST_ASSERT_FALSE(lb_revision_newer(UINT32_MAX, 1));
    TEST_ASSERT_FALSE(lb_revision_newer(3, 3));
    header[20] ^= 1; TEST_ASSERT_FALSE(lb_bank_decode(header, &revision, &length, &crc));
}
int main(void) {
    UNITY_BEGIN(); RUN_TEST(test_shared_text_vectors); RUN_TEST(test_js_golden_bundle_and_crc);
    RUN_TEST(test_semantic_corruption_is_rejected_with_valid_checksums);
    RUN_TEST(test_twenty_chinese_and_maximum_metadata); RUN_TEST(test_bank_commit_and_wrapped_revisions);
    return UNITY_END();
}
