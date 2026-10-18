#include "badge_partition.h"
#include <string.h>

enum { BADGE_PARTITION_SUBTYPE = 0x40 };

static badge_store_result_t erase(void *context, size_t offset, size_t length) {
    badge_partition_t *owner = context;
    return esp_partition_erase_range(owner->partition, offset, length) == ESP_OK ?
        BADGE_STORE_OK : BADGE_STORE_IO_ERROR;
}

static badge_store_result_t write(void *context, size_t offset, const uint8_t *data, size_t length) {
    badge_partition_t *owner = context;
    return esp_partition_write(owner->partition, offset, data, length) == ESP_OK ?
        BADGE_STORE_OK : BADGE_STORE_IO_ERROR;
}

static badge_store_result_t read(void *context, size_t offset, uint8_t *data, size_t length) {
    badge_partition_t *owner = context;
    return esp_partition_read(owner->partition, offset, data, length) == ESP_OK ?
        BADGE_STORE_OK : BADGE_STORE_IO_ERROR;
}

badge_store_result_t badge_partition_open(badge_partition_t *owner, badge_store_backend_t *backend) {
    if (!owner || !backend) return BADGE_STORE_INVALID;
    memset(owner, 0, sizeof(*owner));
    owner->partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        (esp_partition_subtype_t)BADGE_PARTITION_SUBTYPE, "badge_data");
    if (!owner->partition || owner->partition->size != BADGE_PARTITION_SIZE) return BADGE_STORE_IO_ERROR;
    const void *mapped = NULL;
    if (esp_partition_mmap(owner->partition, 0, owner->partition->size, ESP_PARTITION_MMAP_DATA,
                           &mapped, &owner->mmap_handle) != ESP_OK) return BADGE_STORE_IO_ERROR;
    owner->mapped = mapped;
    *backend = (badge_store_backend_t){owner, owner->mapped, owner->partition->size,
        erase, write, read};
    return BADGE_STORE_OK;
}
