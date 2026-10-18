#include "badge_record.h"
#include <string.h>

enum {
    BADGE_MAGIC = 0x47444142u,
    BADGE_COMMIT_MARKER = 0xC04D17EDu,
    OFFSET_SEQUENCE = 8,
    OFFSET_NAME_LENGTH = 12,
    OFFSET_IMAGE_FORMAT = 14,
    OFFSET_WIDTH = 16,
    OFFSET_HEIGHT = 18,
    OFFSET_STRIDE = 20,
    OFFSET_BIO_LENGTH = 22,
    OFFSET_IMAGE_LENGTH = 24,
    OFFSET_SHAPE = 28,
    OFFSET_RESERVED = 30,
};

_Static_assert(BADGE_RECORD_COMMIT_OFFSET + 4 == BADGE_RECORD_HEADER_SIZE,
               "badge record header layout mismatch");

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static bool utf8_next(const uint8_t *text, size_t length, size_t *used, uint32_t *codepoint) {
    uint8_t first = text[0];
    if (first < 0x80) {
        *used = 1;
        *codepoint = first;
        return true;
    }
    unsigned count;
    uint32_t value;
    if (first >= 0xC2 && first <= 0xDF) { count = 2; value = first & 0x1Fu; }
    else if (first >= 0xE0 && first <= 0xEF) { count = 3; value = first & 0x0Fu; }
    else if (first >= 0xF0 && first <= 0xF4) { count = 4; value = first & 0x07u; }
    else return false;
    if (length < count) return false;
    for (unsigned i = 1; i < count; ++i) {
        if ((text[i] & 0xC0u) != 0x80u) return false;
        value = (value << 6) | (text[i] & 0x3Fu);
    }
    if ((count == 3 && value < 0x800) || (count == 4 && value < 0x10000) ||
        value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    *used = count;
    *codepoint = value;
    return true;
}

bool badge_name_valid(const uint8_t *name, size_t length) {
    if (!name || !length || length > BADGE_NAME_MAX_BYTES) return false;
    for (size_t pos = 0; pos < length;) {
        size_t used = 0;
        uint32_t codepoint = 0;
        if (!utf8_next(name + pos, length - pos, &used, &codepoint)) return false;
        if (codepoint < 0x20 || codepoint == 0x7F) return false;
        pos += used;
    }
    return true;
}

bool badge_bio_valid(const uint8_t *bio, size_t length) {
    if (length > BADGE_BIO_MAX_BYTES || (!bio && length)) return false;
    for (size_t pos = 0; pos < length;) {
        size_t used = 0;
        uint32_t codepoint = 0;
        if (!utf8_next(bio + pos, length - pos, &used, &codepoint)) return false;
        if (codepoint < 0x20 || codepoint == 0x7F) return false;
        pos += used;
    }
    return true;
}

bool badge_photo_shape_valid(badge_photo_shape_t shape) {
    return shape == BADGE_PHOTO_SHAPE_SQUARE || shape == BADGE_PHOTO_SHAPE_ROUNDED ||
           shape == BADGE_PHOTO_SHAPE_CIRCLE;
}

bool badge_sequence_newer(uint32_t candidate, uint32_t current) {
    // Signed subtraction keeps ordering valid across the uint32_t wrap boundary.
    return candidate != current && (int32_t)(candidate - current) > 0;
}

uint32_t badge_crc32_start(void) { return UINT32_MAX; }

uint32_t badge_crc32_extend(uint32_t state, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        state ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            state = (state >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(state & 1u));
    }
    return state;
}

uint32_t badge_crc32_finish(uint32_t state) { return state ^ UINT32_MAX; }

uint32_t badge_crc32(const uint8_t *data, size_t length) {
    return badge_crc32_finish(badge_crc32_extend(badge_crc32_start(), data, length));
}

static bool meta_valid(const badge_record_meta_t *meta) {
    return meta && meta->name_length > 0 && meta->name_length <= BADGE_NAME_MAX_BYTES &&
        meta->bio_length <= BADGE_BIO_MAX_BYTES &&
        badge_photo_shape_valid(meta->shape) &&
        meta->image_format == BADGE_IMAGE_FORMAT_RGB565_LE && meta->width == BADGE_IMAGE_WIDTH &&
        meta->height == BADGE_IMAGE_HEIGHT && meta->stride == BADGE_IMAGE_STRIDE &&
        meta->image_length == BADGE_IMAGE_BYTES &&
        BADGE_RECORD_HEADER_SIZE + (size_t)meta->name_length + meta->bio_length +
            meta->image_length <= BADGE_SLOT_SIZE;
}

void badge_record_prepare(uint8_t header[BADGE_RECORD_HEADER_SIZE], const badge_record_meta_t *meta) {
    memset(header, 0xFF, BADGE_RECORD_HEADER_SIZE);
    write_u32(header, BADGE_MAGIC);
    write_u16(header + 4, BADGE_RECORD_SCHEMA);
    write_u16(header + 6, BADGE_RECORD_HEADER_SIZE);
    write_u32(header + OFFSET_SEQUENCE, meta->sequence);
    write_u16(header + OFFSET_NAME_LENGTH, meta->name_length);
    write_u16(header + OFFSET_IMAGE_FORMAT, meta->image_format);
    write_u16(header + OFFSET_WIDTH, meta->width);
    write_u16(header + OFFSET_HEIGHT, meta->height);
    write_u16(header + OFFSET_STRIDE, meta->stride);
    write_u16(header + OFFSET_BIO_LENGTH, meta->bio_length);
    write_u32(header + OFFSET_IMAGE_LENGTH, meta->image_length);
    write_u16(header + OFFSET_SHAPE, (uint16_t)meta->shape);
    write_u16(header + OFFSET_RESERVED, 0);
}

void badge_record_finalize(uint8_t header[BADGE_RECORD_HEADER_SIZE], uint32_t payload_crc) {
    write_u32(header + BADGE_RECORD_PAYLOAD_CRC_OFFSET, payload_crc);
    // Header CRC covers stable metadata and payload CRC, but excludes itself and the final commit marker.
    write_u32(header + BADGE_RECORD_HEADER_CRC_OFFSET,
              badge_crc32(header, BADGE_RECORD_HEADER_CRC_OFFSET));
}

void badge_record_mark_committed(uint8_t commit_bytes[4]) {
    write_u32(commit_bytes, BADGE_COMMIT_MARKER);
}

bool badge_record_decode(const uint8_t header[BADGE_RECORD_HEADER_SIZE], badge_record_meta_t *meta,
                         uint32_t *payload_crc) {
    if (!header || read_u32(header) != BADGE_MAGIC ||
        read_u16(header + 6) != BADGE_RECORD_HEADER_SIZE ||
        read_u32(header + BADGE_RECORD_COMMIT_OFFSET) != BADGE_COMMIT_MARKER ||
        read_u32(header + BADGE_RECORD_HEADER_CRC_OFFSET) !=
            badge_crc32(header, BADGE_RECORD_HEADER_CRC_OFFSET)) return false;
    if (read_u16(header + 4) != BADGE_RECORD_SCHEMA ||
        read_u16(header + OFFSET_RESERVED) != 0) return false;
    badge_record_meta_t decoded = {
        .sequence = read_u32(header + OFFSET_SEQUENCE),
        .name_length = read_u16(header + OFFSET_NAME_LENGTH),
        .image_format = read_u16(header + OFFSET_IMAGE_FORMAT),
        .width = read_u16(header + OFFSET_WIDTH),
        .height = read_u16(header + OFFSET_HEIGHT),
        .stride = read_u16(header + OFFSET_STRIDE),
        .image_length = read_u32(header + OFFSET_IMAGE_LENGTH),
        .bio_length = read_u16(header + OFFSET_BIO_LENGTH),
        .shape = (badge_photo_shape_t)read_u16(header + OFFSET_SHAPE),
    };
    if (!meta_valid(&decoded)) return false;
    if (meta) *meta = decoded;
    if (payload_crc) *payload_crc = read_u32(header + BADGE_RECORD_PAYLOAD_CRC_OFFSET);
    return true;
}

bool badge_record_validate(const uint8_t *slot, size_t slot_size, badge_record_view_t *view) {
    badge_record_meta_t meta;
    uint32_t expected_crc;
    if (!slot || slot_size < BADGE_SLOT_SIZE || !badge_record_decode(slot, &meta, &expected_crc)) return false;
    const uint8_t *name = slot + BADGE_RECORD_HEADER_SIZE;
    const uint8_t *bio = name + meta.name_length;
    const uint8_t *image = bio + meta.bio_length;
    if (!badge_name_valid(name, meta.name_length)) return false;
    if (!badge_bio_valid(bio, meta.bio_length)) return false;
    uint32_t state = badge_crc32_extend(badge_crc32_start(), name, meta.name_length);
    state = badge_crc32_extend(state, bio, meta.bio_length);
    state = badge_crc32_extend(state, image, meta.image_length);
    if (badge_crc32_finish(state) != expected_crc) return false;
    if (view) *view = (badge_record_view_t){.meta = meta, .name = name,
                                            .bio = bio, .image = image};
    return true;
}
