#include "app_controller.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "modes/badge/badge_default.h"
#include "modes/badge/badge_mode.h"
#include "modes/badge/badge_view.h"
#include "modes/lightboard/lightboard_view.h"
#include "navigation/mode_registry.h"
#include "nvs_flash.h"
#include "services/badge/badge_http_server.h"
#include "services/badge/badge_partition.h"
#include "services/badge/badge_wifi_esp.h"
#include "services/config/settings_store.h"
#include "services/config/settings_nvs.h"
#include "services/display/battery_schedule.h"
#include "services/lightboard/lightboard_partition.h"
#include "settings/settings_view.h"
#include "ui_shell.h"

#include <string.h>

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} key_event_t;

static const char *TAG = "passport_badge";
static QueueHandle_t s_keys;
static QueueHandle_t s_battery_results;
static settings_store_t s_settings_store;
static mode_store_t s_mode_store;
static SemaphoreHandle_t s_storage_mutex;
static badge_partition_t s_badge_partition;
static badge_store_t s_badge_store;
static lb_partition_t s_lightboard_partition;
static lb_store_t s_lightboard_store;
static badge_wifi_service_t s_wifi;
static badge_http_server_t s_http;
static badge_mode_t s_badge_mode;
static lightboard_mode_t s_lightboard_mode;
static settings_page_t s_settings_page;
static mode_registry_t s_registry;
static navigation_t s_navigation;
static display_render_policy_t s_render_policy;
static app_controller_t s_controller;
static ui_shell_t s_ui;
static badge_view_t s_badge_view;
static lightboard_view_t s_lightboard_view;
static settings_view_t s_settings_view;

static int64_t now_us(void *context) {
    (void)context;
    return esp_timer_get_time();
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *context) {
    (void)context;
    key_event_t key = {.button = button, .event = event};
    // The timer callback only publishes a bounded event; display, storage and Wi-Fi
    // work remain serialized in the application loop.
    if (s_keys) xQueueSend(s_keys, &key, 0);
}

static bool start_hotspot(void *context) {
    return badge_wifi_service_start(context);
}

static void stop_hotspot(void *context) {
    badge_wifi_service_request_stop(context);
}

static settings_hotspot_state_t settings_hotspot_state(badge_wifi_state_t state) {
    switch (state) {
    case BADGE_WIFI_OFF: return SETTINGS_HOTSPOT_OFF;
    case BADGE_WIFI_STARTING: return SETTINGS_HOTSPOT_STARTING;
    case BADGE_WIFI_WAITING_CLIENT: return SETTINGS_HOTSPOT_WAITING_CLIENT;
    case BADGE_WIFI_CLIENT_CONNECTED: return SETTINGS_HOTSPOT_CLIENT_CONNECTED;
    case BADGE_WIFI_STOPPING: return SETTINGS_HOTSPOT_STOPPING;
    default: return SETTINGS_HOTSPOT_ERROR;
    }
}

static settings_hotspot_error_t settings_hotspot_error(badge_wifi_error_t error) {
    switch (error) {
    case BADGE_WIFI_ERROR_NONE: return SETTINGS_HOTSPOT_ERROR_NONE;
    case BADGE_WIFI_ERROR_START: return SETTINGS_HOTSPOT_ERROR_START;
    default: return SETTINGS_HOTSPOT_ERROR_RUNTIME;
    }
}

static settings_hotspot_status_t hotspot_status(void *context) {
    badge_wifi_status_t source = badge_wifi_service_status(context);
    settings_hotspot_status_t status = {
        .state = settings_hotspot_state(source.state),
        .error = settings_hotspot_error(source.error),
        .save_success = source.save_success,
        .address = source.address,
    };
    memcpy(status.ssid, source.ssid, sizeof(status.ssid));
    return status;
}

static mode_key_t mode_key(bsp_btn_t button) {
    if (button == BSP_BTN_UP) return MODE_KEY_UP;
    if (button == BSP_BTN_DOWN) return MODE_KEY_DOWN;
    return MODE_KEY_OK;
}

static void battery_task(void *context) {
    (void)context;
    bool ready = false;
    battery_schedule_t schedule;
    battery_schedule_init(&schedule, esp_timer_get_time());
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now = esp_timer_get_time();
        if (!battery_schedule_due(&schedule, now)) continue;
        // CW2017 startup may retry for seconds, so it never runs in the input/render loop.
        if (!ready) ready = bsp_battery_init() == ESP_OK;
        int soc = ready ? bsp_battery_soc() : -1;
        xQueueOverwrite(s_battery_results, &soc);
        battery_schedule_sampled(&schedule, now, soc >= 0);
    }
}

static bool render_frame(int64_t now) {
    if (!display_render_policy_should_render(&s_render_policy)) return true;
    if (!bsp_lvgl_lock(1000)) return false;
    s_settings_view.now_us = now;
    // Input hooks only stop state. Detach old LVGL sources/cache and release
    // their mapping here, before another page can render under the same lock.
    lightboard_mode_sync_locked(&s_lightboard_mode);
    navigation_render(&s_navigation);
    bsp_lvgl_unlock();
    display_render_policy_mark_rendered(&s_render_policy);
    return true;
}

static bool init_storage(void) {
    s_storage_mutex = xSemaphoreCreateMutex();
    settings_nvs_set_storage_mutex(s_storage_mutex);
    esp_err_t nvs = nvs_flash_init();
    // Other firmware roles share this NVS partition. Failure degrades to defaults;
    // this firmware never erases NVS as an automatic recovery step.
    settings_result_t settings = settings_store_init(&s_settings_store,
                                                       settings_nvs_backend());
    if (nvs != ESP_OK || settings == SETTINGS_IO_ERROR)
        ESP_LOGW(TAG, "settings unavailable: nvs=%d settings=%d", nvs, settings);
    if (mode_store_init(&s_mode_store, mode_nvs_backend()) == SETTINGS_IO_ERROR)
        ESP_LOGW(TAG, "display mode unavailable; restoring badge");

    badge_store_backend_t backend;
    badge_store_result_t partition = badge_partition_open(&s_badge_partition, &backend);
    badge_store_result_t badge = partition == BADGE_STORE_OK ?
        badge_store_init(&s_badge_store, backend, badge_default_profile()) :
        badge_store_init_defaults(&s_badge_store, badge_default_profile());
    if (partition != BADGE_STORE_OK)
        ESP_LOGW(TAG, "badge partition unavailable: %d; using read-only default", partition);

    size_t defaults_length;
    const uint8_t *defaults = lightboard_defaults_data(&defaults_length);
    lb_backend_t lightboard_backend = {0};
    lb_result_t lightboard_partition = s_storage_mutex ?
        lb_partition_open(&s_lightboard_partition, s_storage_mutex, &lightboard_backend) : LB_IO_ERROR;
    lb_result_t lightboard = lb_store_init(&s_lightboard_store, lightboard_backend, defaults, defaults_length);
    // A missing/unreadable data partition leaves the embedded default list read-only.
    if (lightboard_partition != LB_OK || (lightboard != LB_OK && lightboard != LB_DEFAULTED))
        ESP_LOGW(TAG, "lightboard storage degraded: partition=%d store=%d", lightboard_partition, lightboard);
    return badge != BADGE_STORE_INVALID && badge != BADGE_STORE_IO_ERROR;
}

static bool init_ui_and_services(void) {
    badge_wifi_service_init(&s_wifi, badge_wifi_esp_adapter());
    badge_http_server_init(&s_http, &s_badge_store, badge_http_embedded_assets(),
                           now_us, NULL);
    s_http.lightboard = &s_lightboard_store;
    badge_wifi_service_attach_http(&s_wifi, badge_http_server_runtime(&s_http));

    s_settings_page = (settings_page_t){
        .config = &s_settings_store,
        .hotspot_context = &s_wifi,
        .start_hotspot = start_hotspot,
        .stop_hotspot = stop_hotspot,
        .hotspot_status = hotspot_status,
    };
    mode_t badge = badge_mode_descriptor(&s_badge_mode, &s_badge_store);
    mode_t lightboard = lightboard_mode_descriptor(&s_lightboard_mode, &s_lightboard_store);
    mode_t settings = settings_page_descriptor(&s_settings_page);
    if (!mode_registry_init(&s_registry, badge, lightboard, settings) ||
        !navigation_init(&s_navigation, s_registry.business,
                         MODE_REGISTRY_BUSINESS_COUNT,
                         &s_registry.system_settings))
        return false;

    display_render_policy_init(&s_render_policy, false);
    app_controller_init(&s_controller, &s_navigation, &s_settings_page,
                        &s_render_policy);
    app_controller_attach_config(&s_controller, &s_mode_store, &s_lightboard_mode);

    // Every LVGL object is created and subsequently updated only while holding this lock.
    if (!bsp_lvgl_lock(1000)) return false;
    ui_shell_init(&s_ui);
    badge_view_init(&s_badge_view, &s_badge_mode, &s_registry.business[0], s_ui.content);
    settings_view_init(&s_settings_view, &s_settings_page,
                       &s_registry.system_settings, s_ui.content);
    if (!lightboard_view_init(&s_lightboard_view, &s_lightboard_mode, s_ui.content)) {
        bsp_lvgl_unlock();
        return false;
    }
    // Select the committed business mode before the first visible frame. Navigation
    // init's badge enter is logic-only; no badge frame is drawn during a lamp restore.
    navigation_activate(&s_navigation, s_mode_store.value);
    uint32_t revision;
    if (lb_store_revision(&s_lightboard_store, &revision))
        app_controller_lightboard_updated(&s_controller, revision);
    lightboard_mode_tick(&s_lightboard_mode, esp_timer_get_time());
    lightboard_mode_sync_locked(&s_lightboard_mode);
    navigation_render(&s_navigation);
    esp_err_t refresh = bsp_lvgl_refresh_now();
    bsp_lvgl_unlock();
    if (refresh != ESP_OK) return false;
    bsp_display_backlight(s_settings_store.value.brightness_percent);
    return true;
}

void app_main(void) {
    ESP_LOGI(TAG, "starting firmware role=badge/display");
    if (!init_storage()) ESP_LOGW(TAG, "badge storage degraded to default profile");

    s_keys = xQueueCreate(24, sizeof(key_event_t));
    s_battery_results = xQueueCreate(1, sizeof(int));
    if (!s_keys || !s_battery_results ||
        bsp_display_init() != ESP_OK || !bsp_lvgl_init() ||
        !init_ui_and_services()) {
        ESP_LOGE(TAG, "display/application initialization failed");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK)
        ESP_LOGW(TAG, "buttons unavailable; display remains active");
    if (xTaskCreate(battery_task, "battery", 2048, NULL, 1, NULL) != pdPASS)
        ESP_LOGW(TAG, "battery worker unavailable");

    uint8_t applied_brightness = s_settings_store.value.brightness_percent;
    for (;;) {
        key_event_t key;
        if (xQueueReceive(s_keys, &key, pdMS_TO_TICKS(25)) == pdTRUE &&
            (key.event == BSP_BTN_CLICK || key.event == BSP_BTN_LONG)) {
            // All pages remain lit: the first semantic gesture acts immediately.
            app_controller_gesture(&s_controller, mode_key(key.button),
                                   key.event == BSP_BTN_LONG, esp_timer_get_time());
        }

        badge_wifi_service_process(&s_wifi);
        badge_wifi_event_t wifi_event;
        while (badge_wifi_service_poll(&s_wifi, &wifi_event)) {
            if (wifi_event.type == BADGE_WIFI_EVENT_PROFILE_UPDATED) {
                const badge_profile_snapshot_t *snapshot = badge_store_snapshot(&s_badge_store);
                if (snapshot) app_controller_profile_updated(&s_controller, snapshot->sequence);
            } else if (wifi_event.type == BADGE_WIFI_EVENT_LIGHTBOARD_UPDATED) {
                uint32_t revision;
                if (lb_store_revision(&s_lightboard_store, &revision))
                    app_controller_lightboard_updated(&s_controller, revision);
                app_controller_wifi_changed(&s_controller);
            } else {
                app_controller_wifi_changed(&s_controller);
            }
        }

        int soc;
        if (xQueueReceive(s_battery_results, &soc, 0) == pdTRUE)
            app_controller_battery_result(&s_controller, soc);

        // Revision is authoritative even if the bounded Wi-Fi event queue coalesces
        // an update away, or a committed HTTP response is lost. This reads metadata
        // under the shared storage mutex, never scans or writes Flash per frame.
        uint32_t revision;
        if (lb_store_revision(&s_lightboard_store, &revision))
            app_controller_lightboard_updated(&s_controller, revision);

        if (applied_brightness != s_settings_store.value.brightness_percent) {
            applied_brightness = s_settings_store.value.brightness_percent;
            bsp_display_backlight(applied_brightness);
        }

        // The pure scheduler coalesces late frames and requests at most 20Hz;
        // static badge/settings pages keep their existing event-driven redraws.
        if (lightboard_mode_tick(&s_lightboard_mode, esp_timer_get_time()))
            display_render_policy_request(&s_render_policy);
        if (!render_frame(esp_timer_get_time())) continue;
        int64_t now = esp_timer_get_time();
        badge_wifi_service_tick(&s_wifi, now);
    }
}
