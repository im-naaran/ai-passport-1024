#pragma once

#include <stdbool.h>
#include "navigation.h"

enum {
    MODE_REGISTRY_BUSINESS_COUNT = 2,
};

typedef struct {
    mode_t business[MODE_REGISTRY_BUSINESS_COUNT];
    mode_t system_settings;
} mode_registry_t;

// Fixed array positions define the user-visible order; stable IDs define settings return targets.
bool mode_registry_init(mode_registry_t *registry, mode_t badge,
                        mode_t lightboard, mode_t settings);
