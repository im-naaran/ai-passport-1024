#include "badge_wifi_state.h"
#include <limits.h>

enum { SAVE_FEEDBACK_US = 3000000 };

static bool active_state(badge_wifi_state_t state) {
    return state == BADGE_WIFI_WAITING_CLIENT || state == BADGE_WIFI_CLIENT_CONNECTED;
}

static void reset_runtime(badge_wifi_state_machine_t *machine) {
    machine->client_count = 0;
    machine->upload_in_progress = false;
    machine->stop_requested = false;
    machine->save_success = false;
    machine->feedback_until_us = 0;
}

void badge_wifi_state_init(badge_wifi_state_machine_t *machine) {
    *machine = (badge_wifi_state_machine_t){.state = BADGE_WIFI_OFF};
}

badge_wifi_action_t badge_wifi_state_request_start(badge_wifi_state_machine_t *machine) {
    if (!machine || (machine->state != BADGE_WIFI_OFF && machine->state != BADGE_WIFI_ERROR))
        return BADGE_WIFI_ACTION_NONE;
    // A failed start has already unwound every owned resource. Let the settings
    // page's retry text perform a real second attempt instead of trapping ERROR.
    reset_runtime(machine);
    machine->error = BADGE_WIFI_ERROR_NONE;
    machine->state = BADGE_WIFI_STARTING;
    return BADGE_WIFI_ACTION_STATUS_CHANGED | BADGE_WIFI_ACTION_START_RESOURCES;
}

badge_wifi_action_t badge_wifi_state_started(badge_wifi_state_machine_t *machine) {
    if (!machine || machine->state != BADGE_WIFI_STARTING) return BADGE_WIFI_ACTION_NONE;
    machine->state = BADGE_WIFI_WAITING_CLIENT;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}

badge_wifi_action_t badge_wifi_state_failed(badge_wifi_state_machine_t *machine,
                                            badge_wifi_error_t error) {
    if (!machine || machine->state == BADGE_WIFI_OFF || machine->state == BADGE_WIFI_STOPPING ||
        error == BADGE_WIFI_ERROR_NONE) return BADGE_WIFI_ACTION_NONE;
    reset_runtime(machine);
    machine->state = BADGE_WIFI_ERROR;
    machine->error = error;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}

badge_wifi_action_t badge_wifi_state_client_connected(badge_wifi_state_machine_t *machine) {
    if (!machine || !active_state(machine->state) || machine->client_count == 1)
        return BADGE_WIFI_ACTION_NONE;
    machine->client_count = 1;
    machine->state = BADGE_WIFI_CLIENT_CONNECTED;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}

badge_wifi_action_t badge_wifi_state_client_disconnected(badge_wifi_state_machine_t *machine) {
    if (!machine || !active_state(machine->state) || machine->client_count == 0)
        return BADGE_WIFI_ACTION_NONE;
    machine->client_count = 0;
    machine->state = BADGE_WIFI_WAITING_CLIENT;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}

bool badge_wifi_state_accepts_upload(const badge_wifi_state_machine_t *machine) {
    return machine && (machine->state == BADGE_WIFI_WAITING_CLIENT ||
        machine->state == BADGE_WIFI_CLIENT_CONNECTED) && !machine->upload_in_progress &&
        !machine->stop_requested;
}

bool badge_wifi_state_begin_upload(badge_wifi_state_machine_t *machine) {
    if (!badge_wifi_state_accepts_upload(machine)) return false;
    machine->upload_in_progress = true;
    machine->save_success = false;
    machine->feedback_until_us = 0;
    return true;
}

badge_wifi_action_t badge_wifi_state_finish_upload(badge_wifi_state_machine_t *machine,
                                                   bool committed) {
    if (!machine || !machine->upload_in_progress) return BADGE_WIFI_ACTION_NONE;
    machine->upload_in_progress = false;
    if (machine->stop_requested) {
        machine->state = BADGE_WIFI_STOPPING;
        machine->save_success = false;
        machine->feedback_until_us = 0;
        return BADGE_WIFI_ACTION_STATUS_CHANGED | BADGE_WIFI_ACTION_STOP_RESOURCES;
    }
    machine->state = machine->client_count ? BADGE_WIFI_CLIENT_CONNECTED :
        BADGE_WIFI_WAITING_CLIENT;
    machine->save_success = committed;
    machine->feedback_until_us = 0;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}

badge_wifi_action_t badge_wifi_state_response_finished(badge_wifi_state_machine_t *machine,
                                                       int64_t now_us) {
    if (!machine || !active_state(machine->state) || !machine->save_success ||
        machine->feedback_until_us != 0)
        return BADGE_WIFI_ACTION_NONE;
    // Save feedback is cosmetic. It never changes the active hotspot lifecycle.
    machine->feedback_until_us = now_us > INT64_MAX - SAVE_FEEDBACK_US ? INT64_MAX :
        now_us + SAVE_FEEDBACK_US;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}

badge_wifi_action_t badge_wifi_state_request_stop(badge_wifi_state_machine_t *machine) {
    if (!machine || machine->state == BADGE_WIFI_OFF || machine->state == BADGE_WIFI_STOPPING)
        return BADGE_WIFI_ACTION_NONE;
    machine->stop_requested = true;
    machine->save_success = false;
    machine->feedback_until_us = 0;
    if (machine->upload_in_progress) {
        // UI may leave immediately; resource teardown waits until the Flash transaction is safe.
        return BADGE_WIFI_ACTION_STATUS_CHANGED;
    }
    machine->state = BADGE_WIFI_STOPPING;
    return BADGE_WIFI_ACTION_STATUS_CHANGED | BADGE_WIFI_ACTION_STOP_RESOURCES;
}

badge_wifi_action_t badge_wifi_state_tick(badge_wifi_state_machine_t *machine, int64_t now_us) {
    if (!machine || !machine->save_success || machine->feedback_until_us == 0 ||
        now_us < machine->feedback_until_us) return BADGE_WIFI_ACTION_NONE;
    machine->save_success = false;
    machine->feedback_until_us = 0;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}

badge_wifi_action_t badge_wifi_state_stopped(badge_wifi_state_machine_t *machine) {
    if (!machine || (machine->state != BADGE_WIFI_STOPPING && machine->state != BADGE_WIFI_ERROR))
        return BADGE_WIFI_ACTION_NONE;
    reset_runtime(machine);
    machine->error = BADGE_WIFI_ERROR_NONE;
    machine->state = BADGE_WIFI_OFF;
    return BADGE_WIFI_ACTION_STATUS_CHANGED;
}
