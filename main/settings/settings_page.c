#include "settings_page.h"

typedef struct {
    const uint8_t *values;
    size_t count;
    uint8_t saved;
} setting_options_t;

static bool setting_options(const settings_page_t *page, setting_options_t *options) {
    if (!page || !page->config || !options) return false;
    if (page->page == SETTINGS_EDIT_BRIGHTNESS) {
        *options = (setting_options_t){settings_brightness_percent, 5,
                                      page->config->value.brightness_percent};
        return true;
    }
    return false;
}

static void reset(void *context) {
    settings_page_t *page = context;
    if (settings_page_hotspot_active(page) && page->stop_hotspot)
        page->stop_hotspot(page->hotspot_context);
    page->page = SETTINGS_LIST;
    page->cursor = 0;
    page->draft_index = 0;
    page->feedback = SETTINGS_FEEDBACK_NONE;
    page->feedback_until_us = 0;
}

static void set_feedback(settings_page_t *page, settings_feedback_t feedback,
                         int64_t now_us) {
    page->feedback = feedback;
    page->feedback_until_us = now_us + 2000000;
}

static mode_action_t handle_key(void *context, mode_key_t key, int64_t now_us) {
    settings_page_t *page = context;
    page->feedback = SETTINGS_FEEDBACK_NONE;
    if (page->page == SETTINGS_WIFI_CONFIG) {
        // A dedicated hotspot page consumes the whole semantic gesture on exit.
        settings_page_exit_hotspot(page);
        return MODE_STAY;
    }
    if (page->page == SETTINGS_LIST) {
        if (key == MODE_KEY_UP && page->cursor > 0) --page->cursor;
        else if (key == MODE_KEY_DOWN && page->cursor < 2) ++page->cursor;
        else if (key == MODE_KEY_OK) {
            if (page->cursor == 2) return MODE_RETURN;
            if (page->cursor == 0) {
                if (page->start_hotspot && page->start_hotspot(page->hotspot_context))
                    page->page = SETTINGS_WIFI_CONFIG;
                else
                    set_feedback(page, SETTINGS_FEEDBACK_UNAVAILABLE, now_us);
            } else {
                page->page = SETTINGS_EDIT_BRIGHTNESS;
                setting_options_t options;
                if (!setting_options(page, &options)) return MODE_STAY;
                page->draft_index = 0;
                for (size_t i = 0; i < options.count; ++i)
                    if (options.values[i] == options.saved) page->draft_index = i;
            }
        }
        return MODE_STAY;
    }

    setting_options_t options;
    if (!setting_options(page, &options)) return MODE_STAY;
    if (key == MODE_KEY_UP && page->draft_index > 0) --page->draft_index;
    else if (key == MODE_KEY_DOWN && page->draft_index + 1 < options.count)
        ++page->draft_index;
    else if (key == MODE_KEY_OK) {
        // Start from the full live record so fields owned by another firmware survive.
        settings_t draft = page->config->value;
        draft.brightness_percent = options.values[page->draft_index];
        if (settings_store_save(page->config, draft) != SETTINGS_OK) {
            set_feedback(page, SETTINGS_FEEDBACK_SAVE_FAILED, now_us);
            return MODE_STAY;
        }
        page->page = SETTINGS_LIST;
    }
    return MODE_STAY;
}

bool settings_page_hotspot_active(const settings_page_t *page) {
    return page && page->page == SETTINGS_WIFI_CONFIG;
}

void settings_page_exit_hotspot(settings_page_t *page) {
    if (!settings_page_hotspot_active(page)) return;
    page->page = SETTINGS_LIST;
    page->cursor = 0;
    if (page->stop_hotspot) page->stop_hotspot(page->hotspot_context);
}

settings_hotspot_status_t settings_page_hotspot_status(const settings_page_t *page) {
    if (!page || !page->hotspot_status)
        return (settings_hotspot_status_t){
            .state = SETTINGS_HOTSPOT_ERROR,
            .error = SETTINGS_HOTSPOT_ERROR_RUNTIME,
            .address = "192.168.4.1",
        };
    return page->hotspot_status(page->hotspot_context);
}

const char *settings_page_hotspot_state_text(settings_hotspot_status_t status) {
    if (status.save_success) return "保存成功，可继续设置";
    switch (status.state) {
    case SETTINGS_HOTSPOT_STARTING: return "热点启动中";
    case SETTINGS_HOTSPOT_WAITING_CLIENT: return "等待手机连接";
    case SETTINGS_HOTSPOT_CLIENT_CONNECTED: return "手机已连接";
    case SETTINGS_HOTSPOT_STOPPING: return "热点关闭中";
    case SETTINGS_HOTSPOT_ERROR:
        return status.error == SETTINGS_HOTSPOT_ERROR_START ?
                   "热点启动失败" : "热点运行异常";
    default: return "热点未开启";
    }
}

settings_feedback_t settings_page_feedback(const settings_page_t *page,
                                           int64_t now_us) {
    if (!page) return SETTINGS_FEEDBACK_NONE;
    if (page->feedback != SETTINGS_FEEDBACK_NONE && now_us < page->feedback_until_us)
        return page->feedback;
    // A mode save warning survives entering settings; reset only clears editor feedback.
    return page->mode_save_failed ? SETTINGS_FEEDBACK_MODE_SAVE_FAILED : SETTINGS_FEEDBACK_NONE;
}

bool settings_page_set_battery(settings_page_t *page, int soc) {
    if (!page || soc < -1 || soc > 100 || page->battery_soc == soc) return false;
    page->battery_soc = soc;
    return true;
}

mode_t settings_page_descriptor(settings_page_t *page) {
    if (page) page->battery_soc = -1;
    return (mode_t){.id = 100, .name = "设置", .context = page,
                    .init = reset, .enter = reset, .leave = reset,
                    .handle_key = handle_key};
}
