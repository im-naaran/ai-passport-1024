#pragma once
#include "settings_store.h"

enum { DISPLAY_MODE_BADGE = 4, DISPLAY_MODE_LIGHTBOARD = 7 };
typedef struct {
    uint32_t value;
    settings_backend_t backend;
    bool known;
} mode_store_t;
settings_result_t mode_store_init(mode_store_t *, settings_backend_t);
settings_result_t mode_store_save(mode_store_t *, uint32_t mode_id);
settings_backend_t mode_nvs_backend(void);
