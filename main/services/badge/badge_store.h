#pragma once

#include "badge_record.h"

typedef enum {
    BADGE_STORE_OK,
    BADGE_STORE_DEFAULTED,
    BADGE_STORE_INVALID,
    BADGE_STORE_BUSY,
    BADGE_STORE_IO_ERROR,
} badge_store_result_t;

typedef struct {
    const char *name;
    const uint8_t *image;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t image_format;
    uint32_t sequence;
    bool is_default;
    const char *bio;
    badge_photo_shape_t shape;
} badge_profile_snapshot_t;

typedef struct {
    void *context;
    const uint8_t *mapped;
    size_t size;
    badge_store_result_t (*erase)(void *context, size_t offset, size_t length);
    badge_store_result_t (*write)(void *context, size_t offset, const uint8_t *data, size_t length);
    badge_store_result_t (*read)(void *context, size_t offset, uint8_t *data, size_t length);
} badge_store_backend_t;

typedef struct {
    const char *name;
    size_t name_length;
    uint16_t image_format;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint32_t image_length;
    const char *bio;
    size_t bio_length;
    badge_photo_shape_t shape;
} badge_upload_meta_t;

typedef struct badge_store {
    badge_store_backend_t backend;
    badge_profile_snapshot_t defaults;
    badge_profile_snapshot_t snapshot;
    char active_name[BADGE_NAME_MAX_BYTES + 1];
    char active_bio[BADGE_BIO_MAX_BYTES + 1];
    int active_slot;
    bool busy;
    int pending_slot;
    badge_record_meta_t pending_meta;
    char pending_name[BADGE_NAME_MAX_BYTES + 1];
    char pending_bio[BADGE_BIO_MAX_BYTES + 1];
    size_t pending_image_written;
    uint32_t pending_crc;
} badge_store_t;

badge_store_result_t badge_store_init(badge_store_t *store, badge_store_backend_t backend,
                                      badge_profile_snapshot_t defaults);
badge_store_result_t badge_store_init_defaults(badge_store_t *store, badge_profile_snapshot_t defaults);
const badge_profile_snapshot_t *badge_store_snapshot(const badge_store_t *store);
badge_store_result_t badge_store_begin_update(badge_store_t *store, const badge_upload_meta_t *meta);
badge_store_result_t badge_store_write(badge_store_t *store, const uint8_t *data, size_t length);
badge_store_result_t badge_store_finish(badge_store_t *store);
void badge_store_abort(badge_store_t *store);
