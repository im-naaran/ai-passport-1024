#include "badge_wifi_service.h"
#include <stdio.h>
#include <string.h>

static badge_wifi_status_t make_status(const badge_wifi_service_t *service) {
    badge_wifi_status_t status = {
        .state = service->machine.state,
        .error = service->machine.error,
        .client_count = service->machine.client_count,
        .upload_in_progress = service->machine.upload_in_progress,
        .stop_requested = service->machine.stop_requested,
        .save_success = service->machine.save_success,
        .feedback_until_us = service->machine.feedback_until_us,
        .address = "192.168.4.1",
    };
    memcpy(status.ssid, service->ssid, sizeof(status.ssid));
    return status;
}

static void publish(badge_wifi_service_t *service, badge_wifi_event_type_t type) {
    badge_wifi_event_t event = {.type = type, .status = make_status(service)};
    if (service->event_count == BADGE_WIFI_EVENT_QUEUE_CAPACITY) {
        uint8_t last = (uint8_t)((service->event_head + service->event_count - 1) %
                                 BADGE_WIFI_EVENT_QUEUE_CAPACITY);
        service->events[last] = event;
        return;
    }
    uint8_t tail = (uint8_t)((service->event_head + service->event_count) %
                             BADGE_WIFI_EVENT_QUEUE_CAPACITY);
    service->events[tail] = event;
    service->event_count++;
}

static void cleanup_resources(badge_wifi_service_t *service) {
    /* HTTP must stop accepting sockets before radio teardown; remaining stages unwind in reverse. */
    if (service->http_started) {
        service->http.stop(service->http.context);
        service->http_started = false;
    }
    if (service->wifi_started) {
        service->adapter.stop_wifi(service->adapter.context);
        service->wifi_started = false;
    }
    if (service->events_ready) {
        service->adapter.unregister_events(service->adapter.context);
        service->events_ready = false;
    }
    if (service->wifi_ready) {
        service->adapter.deinit_wifi(service->adapter.context);
        service->wifi_ready = false;
    }
    if (service->netif_ready) {
        service->adapter.destroy_ap_netif(service->adapter.context);
        service->netif_ready = false;
    }
    if (service->global_ready) {
        service->adapter.release_global(service->adapter.context);
        service->global_ready = false;
    }
}

static bool adapter_complete(const badge_wifi_adapter_t *adapter) {
    return adapter->lock && adapter->unlock && adapter->prepare_global &&
        adapter->release_global && adapter->device_suffix &&
        adapter->create_ap_netif && adapter->destroy_ap_netif && adapter->init_wifi &&
        adapter->deinit_wifi && adapter->register_events && adapter->unregister_events &&
        adapter->start_wifi && adapter->stop_wifi;
}

static void fail_start(badge_wifi_service_t *service) {
    cleanup_resources(service);
    service->adapter.lock(service->adapter.context);
    badge_wifi_state_failed(&service->machine, BADGE_WIFI_ERROR_START);
    publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
}

static void station_callback(void *context, bool connected) {
    badge_wifi_service_on_station(context, connected);
}

void badge_wifi_service_init(badge_wifi_service_t *service, badge_wifi_adapter_t adapter) {
    if (!service) return;
    *service = (badge_wifi_service_t){.adapter = adapter};
    badge_wifi_state_init(&service->machine);
}

void badge_wifi_service_attach_http(badge_wifi_service_t *service,
                                    badge_wifi_http_runtime_t runtime) {
    if (!service || service->machine.state != BADGE_WIFI_OFF) return;
    service->http = runtime;
}

bool badge_wifi_service_start(badge_wifi_service_t *service) {
    if (!service || !adapter_complete(&service->adapter)) return false;
    service->adapter.lock(service->adapter.context);
    badge_wifi_action_t action = badge_wifi_state_request_start(&service->machine);
    if (action != BADGE_WIFI_ACTION_NONE) publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
    if (action == BADGE_WIFI_ACTION_NONE) return false;
    uint16_t suffix = 0;
    if (!service->adapter.prepare_global(service->adapter.context)) return fail_start(service), false;
    service->global_ready = true;
    if (!service->adapter.device_suffix(service->adapter.context, &suffix)) return fail_start(service), false;
    snprintf(service->ssid, sizeof(service->ssid), "AI-Passport-%04X", suffix);
    if (!service->adapter.create_ap_netif(service->adapter.context)) return fail_start(service), false;
    service->netif_ready = true;
    if (!service->adapter.init_wifi(service->adapter.context, service->ssid)) return fail_start(service), false;
    service->wifi_ready = true;
    if (!service->adapter.register_events(service->adapter.context, station_callback, service))
        return fail_start(service), false;
    service->events_ready = true;
    if (!service->adapter.start_wifi(service->adapter.context)) return fail_start(service), false;
    service->wifi_started = true;
    if (service->http.start && service->http.stop) {
        if (!service->http.start(service->http.context, service)) return fail_start(service), false;
        service->http_started = true;
    }
    service->adapter.lock(service->adapter.context);
    badge_wifi_state_started(&service->machine);
    publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
    return true;
}

void badge_wifi_service_process(badge_wifi_service_t *service) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    bool cleanup = service->cleanup_pending && service->machine.state == BADGE_WIFI_STOPPING;
    service->cleanup_pending = false;
    service->adapter.unlock(service->adapter.context);
    if (!cleanup) return;
    cleanup_resources(service);
    service->adapter.lock(service->adapter.context);
    badge_wifi_state_stopped(&service->machine);
    service->ssid[0] = '\0';
    publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
}

void badge_wifi_service_request_stop(badge_wifi_service_t *service) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    badge_wifi_action_t action = badge_wifi_state_request_stop(&service->machine);
    if (action != BADGE_WIFI_ACTION_NONE) publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
    if (action == BADGE_WIFI_ACTION_NONE) return;
    if (action & BADGE_WIFI_ACTION_STOP_RESOURCES) {
        service->adapter.lock(service->adapter.context);
        service->cleanup_pending = true;
        service->adapter.unlock(service->adapter.context);
        badge_wifi_service_process(service);
    }
}

void badge_wifi_service_tick(badge_wifi_service_t *service, int64_t now_us) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    badge_wifi_action_t action = badge_wifi_state_tick(&service->machine, now_us);
    if (action != BADGE_WIFI_ACTION_NONE) publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
    if (action == BADGE_WIFI_ACTION_NONE) return;
    if (action & BADGE_WIFI_ACTION_STOP_RESOURCES) {
        service->adapter.lock(service->adapter.context);
        service->cleanup_pending = true;
        service->adapter.unlock(service->adapter.context);
        badge_wifi_service_process(service);
    }
}

badge_wifi_status_t badge_wifi_service_status(const badge_wifi_service_t *service) {
    if (!service) return (badge_wifi_status_t){.state = BADGE_WIFI_OFF,
                                               .address = "192.168.4.1"};
    service->adapter.lock(service->adapter.context);
    badge_wifi_status_t status = make_status(service);
    service->adapter.unlock(service->adapter.context);
    return status;
}

bool badge_wifi_service_poll(badge_wifi_service_t *service, badge_wifi_event_t *event) {
    if (!service || !event) return false;
    service->adapter.lock(service->adapter.context);
    if (!service->event_count) {
        service->adapter.unlock(service->adapter.context);
        return false;
    }
    *event = service->events[service->event_head];
    service->event_head = (uint8_t)((service->event_head + 1) % BADGE_WIFI_EVENT_QUEUE_CAPACITY);
    service->event_count--;
    service->adapter.unlock(service->adapter.context);
    return true;
}

bool badge_wifi_service_begin_upload(badge_wifi_service_t *service) {
    if (!service) return false;
    service->adapter.lock(service->adapter.context);
    bool started = badge_wifi_state_begin_upload(&service->machine);
    service->adapter.unlock(service->adapter.context);
    return started;
}

void badge_wifi_service_finish_upload(badge_wifi_service_t *service, bool committed) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    badge_wifi_action_t action = badge_wifi_state_finish_upload(&service->machine, committed);
    if (action != BADGE_WIFI_ACTION_NONE) publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
    if (action == BADGE_WIFI_ACTION_NONE) return;
    if (action & BADGE_WIFI_ACTION_STOP_RESOURCES) {
        service->adapter.lock(service->adapter.context);
        // HTTP workers never tear down their own server; the application loop reaps this flag.
        service->cleanup_pending = true;
        service->adapter.unlock(service->adapter.context);
    }
}

void badge_wifi_service_response_finished(badge_wifi_service_t *service, int64_t now_us) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    if (badge_wifi_state_response_finished(&service->machine, now_us) != BADGE_WIFI_ACTION_NONE)
        publish(service, BADGE_WIFI_EVENT_STATUS_CHANGED);
    service->adapter.unlock(service->adapter.context);
}

void badge_wifi_service_on_station(badge_wifi_service_t *service, bool connected) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    badge_wifi_action_t action = connected ? badge_wifi_state_client_connected(&service->machine) :
                                             badge_wifi_state_client_disconnected(&service->machine);
    if (action != BADGE_WIFI_ACTION_NONE)
        publish(service, connected ? BADGE_WIFI_EVENT_CLIENT_CONNECTED :
                                     BADGE_WIFI_EVENT_CLIENT_DISCONNECTED);
    service->adapter.unlock(service->adapter.context);
}

void badge_wifi_service_profile_updated(badge_wifi_service_t *service) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    publish(service, BADGE_WIFI_EVENT_PROFILE_UPDATED);
    service->adapter.unlock(service->adapter.context);
}
void badge_wifi_service_lightboard_updated(badge_wifi_service_t *service) {
    if (!service) return;
    service->adapter.lock(service->adapter.context);
    publish(service, BADGE_WIFI_EVENT_LIGHTBOARD_UPDATED);
    service->adapter.unlock(service->adapter.context);
}
