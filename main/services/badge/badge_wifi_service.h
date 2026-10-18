#pragma once

#include "badge_wifi_state.h"
#include <stddef.h>

enum { BADGE_WIFI_SSID_CAPACITY = 24, BADGE_WIFI_EVENT_QUEUE_CAPACITY = 8 };

typedef void (*badge_wifi_station_callback_t)(void *context, bool connected);

typedef struct {
    void *context;
    void (*lock)(void *context);
    void (*unlock)(void *context);
    bool (*prepare_global)(void *context);
    void (*release_global)(void *context);
    bool (*device_suffix)(void *context, uint16_t *suffix);
    bool (*create_ap_netif)(void *context);
    void (*destroy_ap_netif)(void *context);
    bool (*init_wifi)(void *context, const char *ssid);
    void (*deinit_wifi)(void *context);
    bool (*register_events)(void *context, badge_wifi_station_callback_t callback,
                            void *callback_context);
    void (*unregister_events)(void *context);
    bool (*start_wifi)(void *context);
    void (*stop_wifi)(void *context);
} badge_wifi_adapter_t;

typedef struct {
    badge_wifi_state_t state;
    badge_wifi_error_t error;
    uint8_t client_count;
    bool upload_in_progress;
    bool stop_requested;
    bool save_success;
    int64_t feedback_until_us;
    char ssid[BADGE_WIFI_SSID_CAPACITY];
    const char *address;
} badge_wifi_status_t;

typedef enum {
    BADGE_WIFI_EVENT_STATUS_CHANGED,
    BADGE_WIFI_EVENT_CLIENT_CONNECTED,
    BADGE_WIFI_EVENT_CLIENT_DISCONNECTED,
    BADGE_WIFI_EVENT_PROFILE_UPDATED,
    BADGE_WIFI_EVENT_LIGHTBOARD_UPDATED,
} badge_wifi_event_type_t;

typedef struct {
    badge_wifi_event_type_t type;
    badge_wifi_status_t status;
} badge_wifi_event_t;

typedef struct badge_wifi_service badge_wifi_service_t;

typedef struct {
    void *context;
    bool (*start)(void *context, badge_wifi_service_t *service);
    void (*stop)(void *context);
} badge_wifi_http_runtime_t;

struct badge_wifi_service {
    badge_wifi_state_machine_t machine;
    badge_wifi_adapter_t adapter;
    badge_wifi_http_runtime_t http;
    char ssid[BADGE_WIFI_SSID_CAPACITY];
    bool global_ready;
    bool netif_ready;
    bool wifi_ready;
    bool events_ready;
    bool wifi_started;
    bool http_started;
    bool cleanup_pending;
    badge_wifi_event_t events[BADGE_WIFI_EVENT_QUEUE_CAPACITY];
    uint8_t event_head;
    uint8_t event_count;
};

void badge_wifi_service_init(badge_wifi_service_t *service, badge_wifi_adapter_t adapter);
void badge_wifi_service_attach_http(badge_wifi_service_t *service,
                                    badge_wifi_http_runtime_t runtime);
bool badge_wifi_service_start(badge_wifi_service_t *service);
void badge_wifi_service_request_stop(badge_wifi_service_t *service);
void badge_wifi_service_process(badge_wifi_service_t *service);
void badge_wifi_service_tick(badge_wifi_service_t *service, int64_t now_us);
badge_wifi_status_t badge_wifi_service_status(const badge_wifi_service_t *service);
bool badge_wifi_service_poll(badge_wifi_service_t *service, badge_wifi_event_t *event);
bool badge_wifi_service_begin_upload(badge_wifi_service_t *service);
void badge_wifi_service_finish_upload(badge_wifi_service_t *service, bool committed);
void badge_wifi_service_response_finished(badge_wifi_service_t *service, int64_t now_us);
void badge_wifi_service_on_station(badge_wifi_service_t *service, bool connected);
void badge_wifi_service_profile_updated(badge_wifi_service_t *service);
void badge_wifi_service_lightboard_updated(badge_wifi_service_t *service);
