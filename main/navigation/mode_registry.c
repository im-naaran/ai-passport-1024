#include "mode_registry.h"

bool mode_registry_init(mode_registry_t *registry, mode_t badge, mode_t lightboard, mode_t settings) {
    if (!registry || badge.id == settings.id || lightboard.id == settings.id ||
        badge.id == lightboard.id) return false;
    registry->business[0] = badge;
    registry->business[1] = lightboard;
    registry->system_settings = settings;
    return true;
}
