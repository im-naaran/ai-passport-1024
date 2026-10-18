#include "lightboard_partition.h"
#include <string.h>

static bool range(lb_partition_t *p, size_t offset, size_t length) {
    return p && p->partition && offset <= p->partition->size && length <= p->partition->size - offset;
}
static bool read_bytes(void *ctx, size_t offset, uint8_t *bytes, size_t length) {
    lb_partition_t *p = ctx;
    return range(p, offset, length) && esp_partition_read(p->partition, offset, bytes, length) == ESP_OK;
}
static bool write_bytes(void *ctx, size_t offset, const uint8_t *bytes, size_t length) {
    lb_partition_t *p = ctx;
    return range(p, offset, length) && esp_partition_write(p->partition, offset, bytes, length) == ESP_OK;
}
static bool erase_bytes(void *ctx, size_t offset, size_t length) {
    lb_partition_t *p = ctx;
    return range(p, offset, length) && !(offset % 4096) && !(length % 4096) &&
        esp_partition_erase_range(p->partition, offset, length) == ESP_OK;
}
static bool map_bytes(void *ctx, size_t offset, size_t length, lb_mapping_t *mapping) {
    lb_partition_t *p = ctx; const void *data; esp_partition_mmap_handle_t handle;
    // Only a single item's contiguous core/halo planes are mapped, never both full banks.
    if (!mapping || !length || length > 2u * 1536u * 96u || !range(p, offset, length) ||
        esp_partition_mmap(p->partition, offset, length, ESP_PARTITION_MMAP_DATA, &data, &handle) != ESP_OK) return false;
    *mapping = (lb_mapping_t){.data = data, .handle = (uintptr_t)handle}; return true;
}
static void unmap_bytes(void *ctx, lb_mapping_t mapping) {
    (void)ctx; esp_partition_munmap((esp_partition_mmap_handle_t)mapping.handle);
}
static void lock(void *ctx) { xSemaphoreTake(((lb_partition_t *)ctx)->mutex, portMAX_DELAY); }
static void unlock(void *ctx) { xSemaphoreGive(((lb_partition_t *)ctx)->mutex); }
lb_result_t lb_partition_open(lb_partition_t *p, SemaphoreHandle_t shared_mutex, lb_backend_t *backend) {
    if (!p || !backend) return LB_INVALID;
    memset(p, 0, sizeof(*p));
    p->partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x42, "lightboard_data");
    if (!p->partition || p->partition->address != 0x45a000 || p->partition->size != LB_PARTITION_SIZE) return LB_IO_ERROR;
    p->mutex = shared_mutex ? shared_mutex : xSemaphoreCreateMutex(); p->owns_mutex = !shared_mutex;
    if (!p->mutex) return LB_IO_ERROR;
    *backend = (lb_backend_t){p, LB_PARTITION_SIZE, read_bytes, write_bytes, erase_bytes, map_bytes, unmap_bytes, lock, unlock};
    return LB_OK;
}
void lb_partition_close(lb_partition_t *p) {
    if (!p) return;
    if (p->owns_mutex && p->mutex) vSemaphoreDelete(p->mutex);
    memset(p, 0, sizeof(*p));
}
