#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LB_MAX_ITEMS = 6, LB_MAX_CHARS = 20, LB_TEXT_BYTES = 60,
    LB_BUNDLE_HEADER = 32, LB_ENTRY_BYTES = 96, LB_META_BYTES = 608,
    LB_BANK_HEADER = 32, LB_BANK_SIZE = 0x1c0000, LB_PARTITION_SIZE = 0x380000,
    LB_MAX_BUNDLE = LB_BANK_SIZE - LB_BANK_HEADER,
    LB_FONT_VERSION = 1, LB_LAYOUT_VERSION = 1,
};
typedef struct {
    char text[LB_TEXT_BYTES + 1];
    uint8_t text_length, mode;
    uint16_t width, height, stride, font_size;
    int16_t x, y;
    uint32_t offset[2], length[2];
} lb_item_t;
typedef struct {
    uint8_t count;
    uint32_t expected_revision, total_length, body_crc;
    lb_item_t items[LB_MAX_ITEMS];
} lb_bundle_t;
typedef struct { uint8_t count; char text[LB_MAX_ITEMS][LB_TEXT_BYTES + 1]; } lb_text_list_t;

bool lb_character_supported(uint32_t codepoint);
typedef enum { LB_TEXT_OK, LB_TEXT_INVALID, LB_TEXT_TOO_LONG } lb_text_result_t;
lb_text_result_t lb_text_check(const uint8_t *text, size_t length);
bool lb_text_valid(const uint8_t *text, size_t length);
bool lb_parse_lines(const char *input, size_t length, lb_text_list_t *output);
bool lb_item_geometry_valid(const lb_item_t *item);
bool lb_item_decode(const uint8_t entry[LB_ENTRY_BYTES], lb_item_t *item);
// Prefix is exactly header + count entries. Pixel CRC is checked by the streamed store.
bool lb_bundle_decode_meta(const uint8_t *prefix, size_t length, lb_bundle_t *output);
bool lb_bundle_validate(const uint8_t *bytes, size_t length, lb_bundle_t *output);
bool lb_revision_newer(uint32_t candidate, uint32_t current);
void lb_bank_prepare(uint8_t bytes[LB_BANK_HEADER], uint32_t revision,
                     uint32_t bundle_length, uint32_t bundle_crc);
bool lb_bank_decode(const uint8_t bytes[LB_BANK_HEADER], uint32_t *revision,
                    uint32_t *bundle_length, uint32_t *bundle_crc);
void lb_bank_commit(uint8_t bytes[4]);
