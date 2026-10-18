#pragma once
#include "mode_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Set once before opening any backend. NVS writes and lightboard bank operations
// share this application mutex; neither backend ever takes the LVGL lock.
void settings_nvs_set_storage_mutex(SemaphoreHandle_t);
