#include "mode_store.h"

static bool valid(uint32_t id) {
    return id == DISPLAY_MODE_BADGE || id == DISPLAY_MODE_LIGHTBOARD;
}
settings_result_t mode_store_init(mode_store_t *store, settings_backend_t backend) {
    if (!store) return SETTINGS_INVALID;
    *store = (mode_store_t){.value = DISPLAY_MODE_BADGE, .backend = backend};
    uint8_t bytes[2]; size_t length = sizeof(bytes);
    if (!backend.read) return SETTINGS_IO_ERROR;
    settings_result_t result = backend.read(backend.context, bytes, &length);
    store->known = result == SETTINGS_OK || result == SETTINGS_DEFAULTED;
    if (result != SETTINGS_OK) return result;
    // Missing/old image/system IDs never become restore targets or trigger a repair write.
    if (length != 2 || bytes[0] != 1 || !valid(bytes[1])) return SETTINGS_DEFAULTED;
    store->value = bytes[1];
    return SETTINGS_OK;
}
settings_result_t mode_store_save(mode_store_t *store, uint32_t id) {
    if (!store || !valid(id)) return SETTINGS_INVALID;
    if (store->known && store->value == id) return SETTINGS_OK;
    if (!store->backend.save) return SETTINGS_IO_ERROR;
    uint8_t bytes[] = {1, (uint8_t)id};
    settings_result_t result = store->backend.save(store->backend.context, bytes, sizeof(bytes));
    // Live navigation may succeed on an I/O error, but this value means committed state only.
    // A failed commit is uncertain: a later real switch must write even when it
    // returns to the last known value, instead of treating it as a proven no-op.
    store->known = result == SETTINGS_OK;
    if (result == SETTINGS_OK) store->value = id;
    return result;
}
