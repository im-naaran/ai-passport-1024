#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BADGE_WIFI_OFF,
    BADGE_WIFI_STARTING,
    BADGE_WIFI_WAITING_CLIENT,
    BADGE_WIFI_CLIENT_CONNECTED,
    BADGE_WIFI_STOPPING,
    BADGE_WIFI_ERROR,
} badge_wifi_state_t;

typedef enum {
    BADGE_WIFI_ERROR_NONE,
    BADGE_WIFI_ERROR_START,
    BADGE_WIFI_ERROR_RUNTIME,
} badge_wifi_error_t;

typedef enum {
    BADGE_WIFI_ACTION_NONE = 0,
    BADGE_WIFI_ACTION_STATUS_CHANGED = 1u << 0,
    BADGE_WIFI_ACTION_START_RESOURCES = 1u << 1,
    BADGE_WIFI_ACTION_STOP_RESOURCES = 1u << 2,
} badge_wifi_action_t;

typedef struct {
    badge_wifi_state_t state;
    badge_wifi_error_t error;
    uint8_t client_count;
    bool upload_in_progress;
    bool stop_requested;
    bool save_success;
    int64_t feedback_until_us;
} badge_wifi_state_machine_t;

void badge_wifi_state_init(badge_wifi_state_machine_t *machine);
badge_wifi_action_t badge_wifi_state_request_start(badge_wifi_state_machine_t *machine);
badge_wifi_action_t badge_wifi_state_started(badge_wifi_state_machine_t *machine);
badge_wifi_action_t badge_wifi_state_failed(badge_wifi_state_machine_t *machine,
                                            badge_wifi_error_t error);
badge_wifi_action_t badge_wifi_state_client_connected(badge_wifi_state_machine_t *machine);
badge_wifi_action_t badge_wifi_state_client_disconnected(badge_wifi_state_machine_t *machine);
bool badge_wifi_state_begin_upload(badge_wifi_state_machine_t *machine);
badge_wifi_action_t badge_wifi_state_finish_upload(badge_wifi_state_machine_t *machine,
                                                   bool committed);
badge_wifi_action_t badge_wifi_state_response_finished(badge_wifi_state_machine_t *machine,
                                                       int64_t now_us);
badge_wifi_action_t badge_wifi_state_request_stop(badge_wifi_state_machine_t *machine);
badge_wifi_action_t badge_wifi_state_tick(badge_wifi_state_machine_t *machine, int64_t now_us);
badge_wifi_action_t badge_wifi_state_stopped(badge_wifi_state_machine_t *machine);
bool badge_wifi_state_accepts_upload(const badge_wifi_state_machine_t *machine);
