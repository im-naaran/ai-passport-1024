#pragma once
#include "lightboard_defaults.h"

typedef enum { LB_OK, LB_DEFAULTED, LB_INVALID, LB_BUSY, LB_CONFLICT, LB_IO_ERROR } lb_result_t;
typedef struct { const uint8_t *data; uintptr_t handle; } lb_mapping_t;
typedef struct {
    void *context;
    size_t size;
    bool (*read)(void *, size_t offset, uint8_t *, size_t length);
    bool (*write)(void *, size_t offset, const uint8_t *, size_t length);
    bool (*erase)(void *, size_t offset, size_t length);
    bool (*map)(void *, size_t offset, size_t length, lb_mapping_t *);
    void (*unmap)(void *, lb_mapping_t);
    void (*lock)(void *);
    void (*unlock)(void *);
} lb_backend_t;
typedef struct lb_store {
    lb_backend_t backend;
    const uint8_t *defaults;
    lb_bundle_t active, pending;
    int active_bank, pending_bank;
    uint32_t revision, references[2], pending_crc, pending_body_crc;
    size_t written;
    bool available, busy;
} lb_store_t;
// A snapshot is an owning lease: zero-initialize it, never copy it, release once.
typedef struct {
    lb_store_t *owner;
    lb_item_t item;
    const uint8_t *planes[2];
    lb_mapping_t mapping;
    uint32_t revision;
    int bank;
    bool compressed, acquired;
} lb_snapshot_t;

lb_result_t lb_store_init(lb_store_t *, lb_backend_t, const uint8_t *defaults, size_t defaults_length);
lb_result_t lb_store_begin(lb_store_t *, const uint8_t *prefix, size_t prefix_length);
lb_result_t lb_store_write(lb_store_t *, const uint8_t *, size_t length);
lb_result_t lb_store_finish(lb_store_t *);
void lb_store_abort(lb_store_t *);
bool lb_store_metadata(lb_store_t *, lb_bundle_t *, uint32_t *revision);
bool lb_store_revision(lb_store_t *, uint32_t *revision);
lb_result_t lb_snapshot_acquire(lb_store_t *, unsigned index, lb_snapshot_t *);
void lb_snapshot_release(lb_snapshot_t *);
