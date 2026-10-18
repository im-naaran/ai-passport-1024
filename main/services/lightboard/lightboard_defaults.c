#include "lightboard_defaults.h"
#include "services/badge/badge_record.h"

static uint32_t u32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
bool lb_default_strip(const uint8_t *plane, size_t length, uint16_t width, uint16_t height,
                      unsigned strip, const uint8_t **compressed, size_t *compressed_length,
                      size_t *raw_length) {
    unsigned count = ((unsigned)height + 3) / 4;
    size_t table = (count + 1u) * 4u;
    if (!plane || !width || !height || strip >= count || length < table ||
        !compressed || !compressed_length || !raw_length || u32(plane) != table ||
        u32(plane + count * 4u) != length) return false;
    uint32_t start = u32(plane + strip * 4u), end = u32(plane + (strip + 1u) * 4u);
    if (start < table || start >= end || end > length) return false;
    unsigned rows = height - strip * 4u;
    if (rows > 4) rows = 4;
    *raw_length = (size_t)rows * width;
    if (*raw_length > LB_DEFAULT_STRIP_BYTES) return false;
    *compressed = plane + start; *compressed_length = end - start; return true;
}
bool lb_defaults_decode(const uint8_t *b, size_t length, lb_bundle_t *output) {
    if (!b || !output || length < 16 || u32(b) != 0x3144424c || b[4] != 1 || b[5] ||
        !b[6] || b[6] > LB_MAX_ITEMS || b[7] || u32(b + 8) != length ||
        length < 16u + (size_t)b[6] * LB_ENTRY_BYTES || badge_crc32(b + 16, length - 16) != u32(b + 12))
        return false;
    lb_bundle_t result = {.count = b[6], .total_length = (uint32_t)length};
    size_t next = 16u + result.count * LB_ENTRY_BYTES;
    for (unsigned n = 0; n < result.count; ++n) {
        lb_item_t *item = &result.items[n];
        if (!lb_item_decode(b + 16 + n * LB_ENTRY_BYTES, item)) return false;
        for (unsigned layer = 0; layer < 2; ++layer) {
            if (item->offset[layer] != next || item->length[layer] > length - next) return false;
            const uint8_t *plane = b + next;
            for (unsigned strip = 0; strip < ((unsigned)item->height + 3) / 4; ++strip) {
                const uint8_t *data; size_t compressed_length, raw_length;
                if (!lb_default_strip(plane, item->length[layer], item->width, item->height,
                                      strip, &data, &compressed_length, &raw_length)) return false;
            }
            next += item->length[layer];
        }
    }
    if (next != length) return false;
    *output = result; return true;
}
