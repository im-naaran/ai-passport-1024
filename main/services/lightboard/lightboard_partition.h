#pragma once
#include "lightboard_store.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    const esp_partition_t *partition;
    SemaphoreHandle_t mutex;
    bool owns_mutex;
} lb_partition_t;
// Share the application storage mutex with the NVS adapter.
lb_result_t lb_partition_open(lb_partition_t *, SemaphoreHandle_t shared_mutex, lb_backend_t *);
// Caller releases all snapshots/stops transactions before closing.
void lb_partition_close(lb_partition_t *);
