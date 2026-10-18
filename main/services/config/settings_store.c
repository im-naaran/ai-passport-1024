#include "settings_store.h"

const uint8_t settings_capacities[] = {10, 20, 30};
const uint8_t settings_expiry_minutes[] = {1, 3, 5, 10, 30, 60};
static const uint8_t settings_screen_off_minutes[] = {1, 2, 3, 5, 10, 0};
const uint8_t settings_brightness_percent[] = {20, 40, 60, 80, 100};

static const settings_t defaults = {20, 10, 3, 60};

static bool allowed(uint8_t value, const uint8_t *values, size_t count) {
    for (size_t i = 0; i < count; ++i) if (value == values[i]) return true;
    return false;
}

bool settings_valid(settings_t v) {
    return allowed(v.capacity, settings_capacities, sizeof(settings_capacities)) &&
        allowed(v.expiry_minutes, settings_expiry_minutes, sizeof(settings_expiry_minutes)) &&
        allowed(v.screen_off_minutes, settings_screen_off_minutes, sizeof(settings_screen_off_minutes)) &&
        allowed(v.brightness_percent, settings_brightness_percent, sizeof(settings_brightness_percent));
}

settings_result_t settings_store_init(settings_store_t *s, settings_backend_t backend) {
    *s = (settings_store_t){ .value = defaults, .backend = backend };
    uint8_t record[5]; size_t length = sizeof(record);
    if (!backend.read) return SETTINGS_IO_ERROR;
    settings_result_t result = backend.read(backend.context, record, &length);
    if (result != SETTINGS_OK) return result;
    if (length == 0) return SETTINGS_DEFAULTED;
    // Explicit schema bytes preserve old settings without relying on compiler struct layout.
    if (record[0] == 1 && length == 3) {
        if (!allowed(record[1], settings_capacities, sizeof(settings_capacities)) ||
            !allowed(record[2], settings_expiry_minutes, sizeof(settings_expiry_minutes)))
            return SETTINGS_DEFAULTED;
        s->value.capacity = record[1];
        s->value.expiry_minutes = record[2];
        return SETTINGS_OK;
    }
    if (record[0] == 2 && (length == 3 || length == 4)) {
        bool defaulted = false;
        if (allowed(record[1], settings_capacities, sizeof(settings_capacities)))
            s->value.capacity = record[1];
        else defaulted = true;
        if (allowed(record[2], settings_expiry_minutes, sizeof(settings_expiry_minutes)))
            s->value.expiry_minutes = record[2];
        else defaulted = true;
        if (length == 4) {
            if (allowed(record[3], settings_screen_off_minutes, sizeof(settings_screen_off_minutes)))
                s->value.screen_off_minutes = record[3];
            else defaulted = true;
        }
        return defaulted ? SETTINGS_DEFAULTED : SETTINGS_OK;
    }
    if (record[0] == 3 && length == 5) {
        bool defaulted = false;
        uint8_t *fields[] = {&s->value.capacity, &s->value.expiry_minutes,
            &s->value.screen_off_minutes, &s->value.brightness_percent};
        const uint8_t *values[] = {settings_capacities, settings_expiry_minutes,
            settings_screen_off_minutes, settings_brightness_percent};
        const size_t counts[] = {sizeof(settings_capacities), sizeof(settings_expiry_minutes),
            sizeof(settings_screen_off_minutes), sizeof(settings_brightness_percent)};
        // A corrupt new field falls back independently so unrelated valid preferences survive.
        for (size_t i = 0; i < 4; ++i) {
            if (allowed(record[i + 1], values[i], counts[i])) *fields[i] = record[i + 1];
            else defaulted = true;
        }
        return defaulted ? SETTINGS_DEFAULTED : SETTINGS_OK;
    }
    return SETTINGS_DEFAULTED;
}

settings_result_t settings_store_save(settings_store_t *s, settings_t value) {
    if (!settings_valid(value)) return SETTINGS_INVALID;
    if (value.capacity == s->value.capacity && value.expiry_minutes == s->value.expiry_minutes &&
        value.screen_off_minutes == s->value.screen_off_minutes &&
        value.brightness_percent == s->value.brightness_percent) return SETTINGS_OK;
    if (!s->backend.save) return SETTINGS_IO_ERROR;
    const uint8_t record[] = {3, value.capacity, value.expiry_minutes,
        value.screen_off_minutes, value.brightness_percent};
    settings_result_t result = s->backend.save(s->backend.context, record, sizeof(record));
    // Failed persistence leaves the live value and the editor's draft independent.
    if (result == SETTINGS_OK) s->value = value;
    return result;
}
