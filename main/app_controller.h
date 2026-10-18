#pragma once

#include "navigation/navigation.h"
#include "services/display/display_render_policy.h"
#include "settings/settings_page.h"
#include "services/config/mode_store.h"
#include "modes/lightboard/lightboard_mode.h"

typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_RENDER = 1u << 1,
    APP_ACTION_HOTSPOT_EXIT = 1u << 2,
    APP_ACTION_NAVIGATION = 1u << 3,
} app_action_t;

typedef struct {
    navigation_t *navigation;
    settings_page_t *settings;
    display_render_policy_t *render;
    int battery_soc;
    uint32_t badge_sequence;
    bool badge_sequence_known;
    mode_store_t *mode_store;
    lightboard_mode_t *lightboard;
    uint32_t lightboard_revision;
    bool lightboard_revision_known;
} app_controller_t;

void app_controller_init(app_controller_t *controller, navigation_t *navigation,
                         settings_page_t *settings,
                         display_render_policy_t *render);
app_action_t app_controller_gesture(app_controller_t *controller, mode_key_t key,
                                    bool long_press,
                                    int64_t now_us);
app_action_t app_controller_wifi_changed(app_controller_t *controller);
app_action_t app_controller_profile_updated(app_controller_t *controller,
                                            uint32_t sequence);
app_action_t app_controller_battery_result(app_controller_t *controller, int soc);
void app_controller_attach_config(app_controller_t *, mode_store_t *, lightboard_mode_t *);
app_action_t app_controller_lightboard_updated(app_controller_t *, uint32_t revision);
