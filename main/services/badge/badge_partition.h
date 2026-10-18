#pragma once

#include "badge_store.h"
#include "esp_partition.h"

typedef struct {
    const esp_partition_t *partition;
    const uint8_t *mapped;
    esp_partition_mmap_handle_t mmap_handle;
} badge_partition_t;

badge_store_result_t badge_partition_open(badge_partition_t *partition, badge_store_backend_t *backend);
