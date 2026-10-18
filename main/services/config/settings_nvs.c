#include "settings_nvs.h"
#include "nvs.h"

static SemaphoreHandle_t storage_mutex;
void settings_nvs_set_storage_mutex(SemaphoreHandle_t mutex) { storage_mutex = mutex; }

static settings_result_t read_record(void *context, uint8_t *bytes, size_t *length) {
    if (!storage_mutex) return SETTINGS_IO_ERROR;
    xSemaphoreTake(storage_mutex, portMAX_DELAY);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("passport_cfg", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_blob(handle, (const char *)context, bytes, length);
        nvs_close(handle);
    }
    xSemaphoreGive(storage_mutex);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_NVS_TYPE_MISMATCH)
        return SETTINGS_DEFAULTED;
    return err == ESP_OK ? SETTINGS_OK : SETTINGS_IO_ERROR;
}
static settings_result_t save_record(void *context, const uint8_t *bytes, size_t length) {
    if (!storage_mutex) return SETTINGS_IO_ERROR;
    xSemaphoreTake(storage_mutex, portMAX_DELAY);
    nvs_handle_t handle;
    // This product namespace remains independent from data owned by other firmware roles.
    esp_err_t err = nvs_open("passport_cfg", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, (const char *)context, bytes, length);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    xSemaphoreGive(storage_mutex);
    return err == ESP_OK ? SETTINGS_OK : SETTINGS_IO_ERROR;
}
settings_backend_t settings_nvs_backend(void) {
    return (settings_backend_t){ .context = "settings", .read = read_record, .save = save_record };
}
settings_backend_t mode_nvs_backend(void) {
    return (settings_backend_t){ .context = "display_mode", .read = read_record, .save = save_record };
}
