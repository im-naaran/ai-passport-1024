#pragma once

#include "navigation/mode.h"
#include "settings_store.h"

typedef enum {
    SETTINGS_LIST,
    SETTINGS_EDIT_BRIGHTNESS,
    SETTINGS_WIFI_CONFIG,
} settings_page_kind_t;

typedef enum {
    SETTINGS_FEEDBACK_NONE,
    SETTINGS_FEEDBACK_UNAVAILABLE,
    SETTINGS_FEEDBACK_SAVE_FAILED,
    SETTINGS_FEEDBACK_MODE_SAVE_FAILED,
} settings_feedback_t;

typedef enum {
    SETTINGS_HOTSPOT_OFF,
    SETTINGS_HOTSPOT_STARTING,
    SETTINGS_HOTSPOT_WAITING_CLIENT,
    SETTINGS_HOTSPOT_CLIENT_CONNECTED,
    SETTINGS_HOTSPOT_STOPPING,
    SETTINGS_HOTSPOT_ERROR,
} settings_hotspot_state_t;

typedef enum {
    SETTINGS_HOTSPOT_ERROR_NONE,
    SETTINGS_HOTSPOT_ERROR_START,
    SETTINGS_HOTSPOT_ERROR_RUNTIME,
} settings_hotspot_error_t;

typedef struct {
    settings_hotspot_state_t state;
    settings_hotspot_error_t error;
    bool save_success;
    char ssid[24];
    const char *address;
} settings_hotspot_status_t;

typedef struct settings_page {
    struct settings_view *view;
    settings_store_t *config;
    settings_page_kind_t page;
    size_t cursor;
    size_t draft_index;
    settings_feedback_t feedback;
    int64_t feedback_until_us;
    int battery_soc;
    bool mode_save_failed;
    void *hotspot_context;
    bool (*start_hotspot)(void *context);
    void (*stop_hotspot)(void *context);
    settings_hotspot_status_t (*hotspot_status)(void *context);
} settings_page_t;

mode_t settings_page_descriptor(settings_page_t *page);
bool settings_page_set_battery(settings_page_t *page, int soc);
settings_feedback_t settings_page_feedback(const settings_page_t *page,
                                           int64_t now_us);
bool settings_page_hotspot_active(const settings_page_t *page);
void settings_page_exit_hotspot(settings_page_t *page);
settings_hotspot_status_t settings_page_hotspot_status(const settings_page_t *page);
const char *settings_page_hotspot_state_text(settings_hotspot_status_t status);
