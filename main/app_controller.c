#include "app_controller.h"

static app_action_t request_render(app_controller_t *controller, app_action_t action) {
    display_render_policy_request(controller->render);
    return action | APP_ACTION_RENDER;
}

void app_controller_init(app_controller_t *controller, navigation_t *navigation,
                         settings_page_t *settings,
                         display_render_policy_t *render) {
    *controller = (app_controller_t){
        .navigation = navigation,
        .settings = settings,
        .render = render,
        .battery_soc = -1,
    };
    settings_page_set_battery(settings, -1);
}

app_action_t app_controller_gesture(app_controller_t *controller, mode_key_t key,
                                    bool long_press,
                                    int64_t now_us) {
    if (!controller || !controller->navigation || !controller->settings ||
        !controller->render)
        return APP_ACTION_NONE;
    if (settings_page_hotspot_active(controller->settings)) {
        const mode_t *active = navigation_active(controller->navigation);
        if (active && active->handle_key)
            active->handle_key(active->context, key, now_us);
        return request_render(controller, APP_ACTION_HOTSPOT_EXIT);
    }
    uint32_t before = controller->navigation->settings_active ? controller->navigation->return_id :
        navigation_active(controller->navigation)->id;
    if (!navigation_key(controller->navigation, key, long_press, now_us))
        return APP_ACTION_NONE;
    uint32_t after = controller->navigation->settings_active ? controller->navigation->return_id :
        navigation_active(controller->navigation)->id;
    // Settings/hotspot visits and automatic text/frame changes never write NVS.
    if (before != after && controller->mode_store)
        controller->settings->mode_save_failed =
            mode_store_save(controller->mode_store, after) != SETTINGS_OK;
    return request_render(controller, APP_ACTION_NAVIGATION);
}

void app_controller_attach_config(app_controller_t *controller, mode_store_t *store,
                                  lightboard_mode_t *lightboard) {
    controller->mode_store = store;
    controller->lightboard = lightboard;
}
app_action_t app_controller_lightboard_updated(app_controller_t *controller, uint32_t revision) {
    if (!controller || !controller->lightboard || !controller->render) return APP_ACTION_NONE;
    if (controller->lightboard_revision_known && controller->lightboard_revision == revision)
        return APP_ACTION_NONE;
    controller->lightboard_revision = revision;
    controller->lightboard_revision_known = true;
    if (!controller->lightboard->active) return APP_ACTION_NONE;
    controller->lightboard->start_pending = true;
    controller->lightboard->frame_ready = false;
    return request_render(controller, APP_ACTION_NONE);
}

app_action_t app_controller_wifi_changed(app_controller_t *controller) {
    if (!controller || !controller->render) return APP_ACTION_NONE;
    return request_render(controller, APP_ACTION_NONE);
}

app_action_t app_controller_profile_updated(app_controller_t *controller,
                                            uint32_t sequence) {
    if (!controller || !controller->render) return APP_ACTION_NONE;
    if (controller->badge_sequence_known && controller->badge_sequence == sequence)
        return APP_ACTION_NONE;
    controller->badge_sequence = sequence;
    controller->badge_sequence_known = true;
    return request_render(controller, APP_ACTION_NONE);
}

app_action_t app_controller_battery_result(app_controller_t *controller, int soc) {
    if (!controller || !controller->render || !controller->settings ||
        soc < 0 || soc > 100 ||
        controller->battery_soc == soc)
        return APP_ACTION_NONE;
    controller->battery_soc = soc;
    settings_page_set_battery(controller->settings, soc);
    const mode_t *active = controller->navigation ?
        navigation_active(controller->navigation) : NULL;
    // Business pages retain the latest snapshot without redrawing a hidden battery label.
    if (!controller->navigation || !controller->navigation->settings_active ||
        !active || active->context != controller->settings ||
        controller->settings->page != SETTINGS_LIST)
        return APP_ACTION_NONE;
    return request_render(controller, APP_ACTION_NONE);
}
