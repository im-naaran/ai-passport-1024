#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Legacy fields remain in the serialized record for compatibility; screen-off is never applied.
typedef struct {
    uint8_t capacity, expiry_minutes, screen_off_minutes, brightness_percent;
} settings_t;
typedef enum { SETTINGS_OK, SETTINGS_DEFAULTED, SETTINGS_IO_ERROR, SETTINGS_INVALID } settings_result_t;
typedef struct {
    void *context;
    settings_result_t (*read)(void *, uint8_t *, size_t *);
    // Must return OK only after the entire record has been committed.
    settings_result_t (*save)(void *, const uint8_t *, size_t);
} settings_backend_t;
typedef struct { settings_t value; settings_backend_t backend; } settings_store_t;

extern const uint8_t settings_capacities[3];
extern const uint8_t settings_expiry_minutes[6];
extern const uint8_t settings_brightness_percent[5];
bool settings_valid(settings_t value);
settings_result_t settings_store_init(settings_store_t *, settings_backend_t);
settings_result_t settings_store_save(settings_store_t *, settings_t);
// NVS must be initialized by application startup; this adapter never erases storage.
settings_backend_t settings_nvs_backend(void);
