#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    BADGE_RECORD_HEADER_SIZE = 44,
    BADGE_NAME_MAX_BYTES = 48,
    BADGE_BIO_MAX_BYTES = 96,
    BADGE_IMAGE_WIDTH = 200,
    BADGE_IMAGE_HEIGHT = 200,
    BADGE_IMAGE_STRIDE = 400,
    BADGE_IMAGE_BYTES = 80000,
    BADGE_RECORD_PAYLOAD_CRC_OFFSET = 32,
    BADGE_RECORD_HEADER_CRC_OFFSET = 36,
    BADGE_RECORD_COMMIT_OFFSET = 40,
    BADGE_SLOT_SIZE = 0x20000,
    BADGE_PARTITION_SIZE = 0x40000,
};

typedef enum {
    BADGE_IMAGE_FORMAT_RGB565_LE = 1,
} badge_image_format_t;

typedef enum {
    BADGE_PHOTO_SHAPE_SQUARE = 0,
    BADGE_PHOTO_SHAPE_ROUNDED = 1,
    BADGE_PHOTO_SHAPE_CIRCLE = 2,
} badge_photo_shape_t;

enum { BADGE_RECORD_SCHEMA = 3 };

typedef struct {
    uint32_t sequence;
    uint16_t name_length;
    uint16_t image_format;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint32_t image_length;
    uint16_t bio_length;
    badge_photo_shape_t shape;
} badge_record_meta_t;

typedef struct {
    badge_record_meta_t meta;
    const uint8_t *name;
    const uint8_t *bio;
    const uint8_t *image;
} badge_record_view_t;

bool badge_name_valid(const uint8_t *name, size_t length);
bool badge_bio_valid(const uint8_t *bio, size_t length);
bool badge_photo_shape_valid(badge_photo_shape_t shape);
bool badge_sequence_newer(uint32_t candidate, uint32_t current);

uint32_t badge_crc32_start(void);
uint32_t badge_crc32_extend(uint32_t state, const uint8_t *data, size_t length);
uint32_t badge_crc32_finish(uint32_t state);
uint32_t badge_crc32(const uint8_t *data, size_t length);

void badge_record_prepare(uint8_t header[BADGE_RECORD_HEADER_SIZE], const badge_record_meta_t *meta);
void badge_record_finalize(uint8_t header[BADGE_RECORD_HEADER_SIZE], uint32_t payload_crc);
void badge_record_mark_committed(uint8_t commit_bytes[4]);
bool badge_record_decode(const uint8_t header[BADGE_RECORD_HEADER_SIZE], badge_record_meta_t *meta,
                         uint32_t *payload_crc);
bool badge_record_validate(const uint8_t *slot, size_t slot_size, badge_record_view_t *view);
