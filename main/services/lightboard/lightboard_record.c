#include "lightboard_record.h"
#include "services/badge/badge_record.h"
#include "text_utf8.h"
#include <string.h>

static const uint8_t charset[] = {
#include "lightboard_charset.inc"
};
static uint16_t u16(const uint8_t *b) { return (uint16_t)b[0] | (uint16_t)b[1] << 8; }
static uint32_t u32(const uint8_t *b) { return u16(b) | (uint32_t)u16(b + 2) << 16; }
static void put32(uint8_t *b, uint32_t v) { for (unsigned i = 0; i < 4; ++i) b[i] = (uint8_t)(v >> (8 * i)); }
static bool zero(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; ++i) if (b[i]) return false; return true; }

bool lb_character_supported(uint32_t cp) { return cp < 65536 && (charset[cp >> 3] & (1u << (cp & 7))); }
static bool whitespace(uint32_t cp) {
    // This is the ECMAScript trim set; the browser and C normalizers use the same rule.
    return (cp >= 9 && cp <= 13) || cp == 32 || cp == 0xa0 || cp == 0x1680 ||
        (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 ||
        cp == 0x202f || cp == 0x205f || cp == 0x3000 || cp == 0xfeff;
}
lb_text_result_t lb_text_check(const uint8_t *text, size_t length) {
    if (!text || !length) return LB_TEXT_INVALID;
    if (length > LB_TEXT_BYTES) return LB_TEXT_TOO_LONG;
    size_t offset = 0, count = 0; uint32_t first = 0, last = 0;
    while (offset < length) {
        bool valid; uint32_t cp;
        size_t bytes = text_utf8_decode((const char *)text + offset, length - offset, &cp, &valid);
        if (!valid || !lb_character_supported(cp)) return LB_TEXT_INVALID;
        if (++count > LB_MAX_CHARS) return LB_TEXT_TOO_LONG;
        if (count == 1) first = cp;
        last = cp; offset += bytes;
    }
    return !whitespace(first) && !whitespace(last) ? LB_TEXT_OK : LB_TEXT_INVALID;
}
bool lb_text_valid(const uint8_t *text, size_t length) { return lb_text_check(text, length) == LB_TEXT_OK; }
bool lb_parse_lines(const char *input, size_t length, lb_text_list_t *output) {
    if (!input || !output || length > 2048) return false;
    lb_text_list_t list = {0}; size_t line = 0;
    while (line < length) {
        size_t end = line;
        while (end < length && input[end] != '\n') ++end;
        size_t bound = end;
        if (bound > line && input[bound - 1] == '\r' && end < length) --bound;
        size_t first = bound, last = line;
        for (size_t at = line; at < bound;) {
            bool valid; uint32_t cp;
            size_t n = text_utf8_decode(input + at, bound - at, &cp, &valid);
            if (!valid) return false;
            if (!whitespace(cp)) { if (first == bound) first = at; last = at + n; }
            at += n;
        }
        if (first < bound) {
            if (list.count == LB_MAX_ITEMS || !lb_text_valid((const uint8_t *)input + first, last - first)) return false;
            memcpy(list.text[list.count++], input + first, last - first);
        }
        line = end + (end < length);
    }
    if (!list.count) return false;
    *output = list; return true;
}
bool lb_item_geometry_valid(const lb_item_t *i) {
    if (!i || !i->width || !i->height || i->stride != i->width || i->mode > 1 ||
        i->font_size < 64 || i->font_size > 104 || i->x < 0 || i->y < 0 ||
        (uint32_t)i->y + i->height > 320) return false;
    if (i->mode) return i->width <= 1536 && i->height <= 96 && i->x == 0;
    return i->width <= 240 && i->height <= 320 && (uint32_t)i->x + i->width <= 240;
}
bool lb_item_decode(const uint8_t e[LB_ENTRY_BYTES], lb_item_t *i) {
    if (!e || !i || !e[0] || e[0] > LB_TEXT_BYTES || !lb_text_valid(e + 12, e[0]) ||
        !zero(e + 12 + e[0], LB_TEXT_BYTES - e[0]) || !zero(e + 90, 6)) return false;
    memset(i, 0, sizeof(*i));
    i->text_length = e[0]; i->mode = e[1]; memcpy(i->text, e + 12, e[0]);
    i->width = u16(e + 2); i->height = u16(e + 4); i->stride = u16(e + 6);
    i->x = (int16_t)u16(e + 8); i->y = (int16_t)u16(e + 10); i->font_size = u16(e + 88);
    for (unsigned layer = 0; layer < 2; ++layer) {
        i->offset[layer] = u32(e + 72 + layer * 8); i->length[layer] = u32(e + 76 + layer * 8);
    }
    return lb_item_geometry_valid(i);
}
bool lb_bundle_decode_meta(const uint8_t *b, size_t length, lb_bundle_t *output) {
    if (!b || !output || length < LB_BUNDLE_HEADER || u32(b) != 0x3150424c ||
        u16(b + 4) != 1 || !b[6] || b[6] > LB_MAX_ITEMS || b[7] != LB_FONT_VERSION ||
        u16(b + 8) != LB_LAYOUT_VERSION || u16(b + 10) || u32(b + 28) ||
        badge_crc32(b, 24) != u32(b + 24) || length != LB_BUNDLE_HEADER + (size_t)b[6] * LB_ENTRY_BYTES)
        return false;
    lb_bundle_t result = {.count = b[6], .expected_revision = u32(b + 16),
        .total_length = u32(b + 12), .body_crc = u32(b + 20)};
    if (result.total_length > LB_MAX_BUNDLE || result.total_length < length) return false;
    uint32_t next = (uint32_t)length;
    for (unsigned n = 0; n < result.count; ++n) {
        const uint8_t *e = b + LB_BUNDLE_HEADER + n * LB_ENTRY_BYTES;
        lb_item_t *i = &result.items[n];
        if (!lb_item_decode(e, i)) return false;
        for (unsigned layer = 0; layer < 2; ++layer) {
            i->offset[layer] = u32(e + 72 + 8 * layer); i->length[layer] = u32(e + 76 + 8 * layer);
            if (i->offset[layer] != next || i->length[layer] != (uint32_t)i->width * i->height ||
                i->length[layer] > result.total_length - next) return false;
            next += i->length[layer];
        }
    }
    if (next != result.total_length) return false;
    *output = result; return true;
}
bool lb_bundle_validate(const uint8_t *b, size_t length, lb_bundle_t *output) {
    if (!b || length < LB_BUNDLE_HEADER) return false;
    size_t prefix = LB_BUNDLE_HEADER + (size_t)b[6] * LB_ENTRY_BYTES;
    return prefix <= length && lb_bundle_decode_meta(b, prefix, output) && output->total_length == length &&
        badge_crc32(b + LB_BUNDLE_HEADER, length - LB_BUNDLE_HEADER) == output->body_crc;
}
bool lb_revision_newer(uint32_t candidate, uint32_t current) {
    return candidate != current && candidate - current < 0x80000000u;
}
void lb_bank_prepare(uint8_t b[LB_BANK_HEADER], uint32_t revision, uint32_t length, uint32_t crc) {
    memset(b, 0, LB_BANK_HEADER); put32(b, 0x3153424c); put32(b + 4, 1);
    put32(b + 8, revision); put32(b + 12, length); put32(b + 16, crc);
    put32(b + 20, badge_crc32(b, 20)); memset(b + 28, 0xff, 4);
}
void lb_bank_commit(uint8_t b[4]) { put32(b, 0x434d424c); }
bool lb_bank_decode(const uint8_t b[LB_BANK_HEADER], uint32_t *revision, uint32_t *length, uint32_t *crc) {
    if (!b || !revision || !length || !crc || u32(b) != 0x3153424c || u32(b + 4) != 1 || !u32(b + 8) ||
        u32(b + 12) < LB_BUNDLE_HEADER || u32(b + 12) > LB_MAX_BUNDLE ||
        u32(b + 20) != badge_crc32(b, 20) || u32(b + 24) || u32(b + 28) != 0x434d424c) return false;
    *revision = u32(b + 8); *length = u32(b + 12); *crc = u32(b + 16); return true;
}
