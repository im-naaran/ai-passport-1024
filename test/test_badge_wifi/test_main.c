#include <unity.h>
#include "services/badge/badge_wifi_state.h"
#include "services/badge/badge_wifi_service.h"

static badge_wifi_state_machine_t machine;

typedef struct {
    char calls[64];
    size_t call_count;
    char fail_at;
    badge_wifi_station_callback_t callback;
    void *callback_context;
} fake_adapter_t;

static fake_adapter_t fake;

static void fake_lock(void *context) { (void)context; }
static void fake_unlock(void *context) { (void)context; }

static bool fake_call(char call) {
    fake.calls[fake.call_count++] = call;
    fake.calls[fake.call_count] = '\0';
    return fake.fail_at != call;
}
static bool fake_prepare(void *context) { (void)context; return fake_call('P'); }
static void fake_release(void *context) { (void)context; fake_call('p'); }
static bool fake_suffix(void *context, uint16_t *suffix) {
    (void)context; bool ok = fake_call('M'); if (ok) *suffix = 0x12AF; return ok;
}
static bool fake_netif(void *context) { (void)context; return fake_call('N'); }
static void fake_destroy_netif(void *context) { (void)context; fake_call('n'); }
static bool fake_init_wifi(void *context, const char *ssid) {
    (void)context; TEST_ASSERT_EQUAL_STRING("AI-Passport-12AF", ssid); return fake_call('I');
}
static void fake_deinit_wifi(void *context) { (void)context; fake_call('i'); }
static bool fake_register(void *context, badge_wifi_station_callback_t callback,
                          void *callback_context) {
    (void)context; bool ok = fake_call('H');
    if (ok) { fake.callback = callback; fake.callback_context = callback_context; }
    return ok;
}
static void fake_unregister(void *context) {
    (void)context; fake_call('h'); fake.callback = NULL; fake.callback_context = NULL;
}
static bool fake_start_wifi(void *context) { (void)context; return fake_call('S'); }
static void fake_stop_wifi(void *context) { (void)context; fake_call('s'); }
static bool fake_http_start(void *context, badge_wifi_service_t *service) {
    (void)context;
    (void)service;
    return fake_call('T');
}
static void fake_http_stop(void *context) { (void)context; fake_call('t'); }
static badge_wifi_http_runtime_t fake_http(void) {
    return (badge_wifi_http_runtime_t){
        .context = &fake, .start = fake_http_start, .stop = fake_http_stop};
}
static badge_wifi_adapter_t fake_adapter(void) {
    return (badge_wifi_adapter_t){
        .context = &fake, .lock = fake_lock, .unlock = fake_unlock,
        .prepare_global = fake_prepare, .release_global = fake_release,
        .device_suffix = fake_suffix, .create_ap_netif = fake_netif,
        .destroy_ap_netif = fake_destroy_netif, .init_wifi = fake_init_wifi,
        .deinit_wifi = fake_deinit_wifi, .register_events = fake_register,
        .unregister_events = fake_unregister, .start_wifi = fake_start_wifi,
        .stop_wifi = fake_stop_wifi,
    };
}

void setUp(void) { badge_wifi_state_init(&machine); fake = (fake_adapter_t){0}; }
void tearDown(void) {}

static void start(void) {
    TEST_ASSERT_BITS_HIGH(BADGE_WIFI_ACTION_START_RESOURCES,
                          badge_wifi_state_request_start(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_STARTING, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED, badge_wifi_state_started(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
}

static void test_start_connect_disconnect_and_duplicate_events(void) {
    TEST_ASSERT_EQUAL(BADGE_WIFI_OFF, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_started(&machine));
    start();
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_request_start(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_client_connected(&machine));
    TEST_ASSERT_EQUAL(1, machine.client_count);
    TEST_ASSERT_EQUAL(BADGE_WIFI_CLIENT_CONNECTED, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_client_connected(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_client_disconnected(&machine));
    TEST_ASSERT_EQUAL(0, machine.client_count);
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_client_disconnected(&machine));
}

static void test_no_inactivity_timeout_and_failed_upload_returns_active(void) {
    start();
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_tick(&machine, INT64_MAX));
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
    TEST_ASSERT_TRUE(badge_wifi_state_begin_upload(&machine));
    TEST_ASSERT_FALSE(badge_wifi_state_accepts_upload(&machine));
    TEST_ASSERT_FALSE(badge_wifi_state_begin_upload(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_finish_upload(&machine, false));
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
    TEST_ASSERT_TRUE(badge_wifi_state_accepts_upload(&machine));
}

static void test_success_feedback_expires_without_stopping_and_allows_another_save(void) {
    start();
    TEST_ASSERT_TRUE(badge_wifi_state_begin_upload(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_finish_upload(&machine, true));
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
    TEST_ASSERT_TRUE(machine.save_success);
    TEST_ASSERT_TRUE(badge_wifi_state_accepts_upload(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_tick(&machine, INT64_MAX));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_response_finished(&machine, 123));
    TEST_ASSERT_EQUAL_INT64(3000123, machine.feedback_until_us);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_tick(&machine, 3000122));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED, badge_wifi_state_tick(&machine, 3000123));
    TEST_ASSERT_FALSE(machine.save_success);
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
    TEST_ASSERT_TRUE(badge_wifi_state_begin_upload(&machine));
    badge_wifi_state_finish_upload(&machine, true);
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
}

static void test_stop_during_upload_waits_for_transaction(void) {
    start();
    badge_wifi_state_client_connected(&machine);
    TEST_ASSERT_TRUE(badge_wifi_state_begin_upload(&machine));
    badge_wifi_action_t action = badge_wifi_state_request_stop(&machine);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED, action);
    TEST_ASSERT_TRUE(machine.stop_requested);
    TEST_ASSERT_TRUE(machine.upload_in_progress);
    TEST_ASSERT_EQUAL(BADGE_WIFI_CLIENT_CONNECTED, machine.state);
    TEST_ASSERT_FALSE(badge_wifi_state_accepts_upload(&machine));
    action = badge_wifi_state_finish_upload(&machine, true);
    TEST_ASSERT_BITS_HIGH(BADGE_WIFI_ACTION_STOP_RESOURCES, action);
    TEST_ASSERT_EQUAL(BADGE_WIFI_STOPPING, machine.state);
}

static void test_early_stop_error_and_restart_boundaries(void) {
    start();
    badge_wifi_action_t action = badge_wifi_state_request_stop(&machine);
    TEST_ASSERT_BITS_HIGH(BADGE_WIFI_ACTION_STOP_RESOURCES, action);
    TEST_ASSERT_EQUAL(BADGE_WIFI_STOPPING, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_request_stop(&machine));
    badge_wifi_state_stopped(&machine);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_stopped(&machine));

    badge_wifi_state_request_start(&machine);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_failed(&machine, BADGE_WIFI_ERROR_START));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ERROR, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ERROR_START, machine.error);
    action = badge_wifi_state_request_start(&machine);
    TEST_ASSERT_BITS_HIGH(BADGE_WIFI_ACTION_START_RESOURCES, action);
    TEST_ASSERT_EQUAL(BADGE_WIFI_STARTING, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ERROR_NONE, machine.error);
    badge_wifi_state_failed(&machine, BADGE_WIFI_ERROR_START);
    action = badge_wifi_state_request_stop(&machine);
    TEST_ASSERT_BITS_HIGH(BADGE_WIFI_ACTION_STOP_RESOURCES, action);
    badge_wifi_state_stopped(&machine);
    TEST_ASSERT_EQUAL(BADGE_WIFI_OFF, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ERROR_NONE, machine.error);
}

static void test_feedback_deadline_overflow_saturates_without_closing(void) {
    start();
    TEST_ASSERT_TRUE(badge_wifi_state_begin_upload(&machine));
    badge_wifi_state_finish_upload(&machine, true);
    badge_wifi_state_response_finished(&machine, INT64_MAX - 1);
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, machine.feedback_until_us);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_tick(&machine, INT64_MAX - 1));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_tick(&machine, INT64_MAX));
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
}

static void test_illegal_events_do_not_change_state(void) {
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_client_connected(&machine));
    TEST_ASSERT_FALSE(badge_wifi_state_begin_upload(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_finish_upload(&machine, true));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_response_finished(&machine, 1));
    TEST_ASSERT_EQUAL(BADGE_WIFI_OFF, machine.state);

    badge_wifi_state_request_start(&machine);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_client_connected(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_tick(&machine, INT64_MAX));
    TEST_ASSERT_FALSE(badge_wifi_state_begin_upload(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_STARTING, machine.state);

    badge_wifi_state_started(&machine);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_started(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_stopped(&machine));
    badge_wifi_state_request_stop(&machine);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_NONE, badge_wifi_state_client_connected(&machine));
    TEST_ASSERT_FALSE(badge_wifi_state_begin_upload(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_STOPPING, machine.state);

    badge_wifi_state_stopped(&machine);
    badge_wifi_state_request_start(&machine);
    badge_wifi_state_failed(&machine, BADGE_WIFI_ERROR_START);
    TEST_ASSERT_BITS_HIGH(BADGE_WIFI_ACTION_START_RESOURCES,
                          badge_wifi_state_request_start(&machine));
    TEST_ASSERT_FALSE(badge_wifi_state_begin_upload(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_STARTING, machine.state);
    TEST_ASSERT_EQUAL(BADGE_WIFI_ACTION_STATUS_CHANGED,
                      badge_wifi_state_started(&machine));
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, machine.state);
}

static void drain_events(badge_wifi_service_t *service) {
    badge_wifi_event_t event;
    while (badge_wifi_service_poll(service, &event)) {}
}

static void test_service_start_station_events_and_stop_order(void) {
    badge_wifi_service_t service;
    badge_wifi_service_init(&service, fake_adapter());
    badge_wifi_service_attach_http(&service, fake_http());
    TEST_ASSERT_TRUE(badge_wifi_service_start(&service));
    TEST_ASSERT_EQUAL_STRING("PMNIHST", fake.calls);
    badge_wifi_status_t status = badge_wifi_service_status(&service);
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, status.state);
    TEST_ASSERT_EQUAL_STRING("AI-Passport-12AF", status.ssid);
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", status.address);
    drain_events(&service);

    fake.callback(fake.callback_context, true);
    fake.callback(fake.callback_context, true);
    badge_wifi_event_t event;
    TEST_ASSERT_TRUE(badge_wifi_service_poll(&service, &event));
    TEST_ASSERT_EQUAL(BADGE_WIFI_EVENT_CLIENT_CONNECTED, event.type);
    TEST_ASSERT_EQUAL(1, event.status.client_count);
    TEST_ASSERT_FALSE(badge_wifi_service_poll(&service, &event));
    fake.callback(fake.callback_context, false);
    TEST_ASSERT_TRUE(badge_wifi_service_poll(&service, &event));
    TEST_ASSERT_EQUAL(BADGE_WIFI_EVENT_CLIENT_DISCONNECTED, event.type);

    badge_wifi_service_request_stop(&service);
    TEST_ASSERT_EQUAL_STRING("PMNIHSTtshinp", fake.calls);
    TEST_ASSERT_EQUAL(BADGE_WIFI_OFF, badge_wifi_service_status(&service).state);
    badge_wifi_service_request_stop(&service);
    TEST_ASSERT_EQUAL_STRING("PMNIHSTtshinp", fake.calls);
}

static void test_service_each_start_failure_unwinds_and_can_restart(void) {
    const char failures[] = {'P', 'M', 'N', 'I', 'H', 'S', 'T'};
    const char *expected[] = {"P", "PMp", "PMNp", "PMNInp", "PMNIHinp",
                              "PMNIHShinp", "PMNIHSTshinp"};
    for (size_t index = 0; index < sizeof(failures); ++index) {
        badge_wifi_service_t service;
        badge_wifi_service_init(&service, fake_adapter());
        badge_wifi_service_attach_http(&service, fake_http());
        fake.fail_at = failures[index];
        TEST_ASSERT_FALSE(badge_wifi_service_start(&service));
        TEST_ASSERT_EQUAL_STRING(expected[index], fake.calls);
        TEST_ASSERT_EQUAL(BADGE_WIFI_ERROR, badge_wifi_service_status(&service).state);
        fake = (fake_adapter_t){0};
        service.adapter = fake_adapter();
        TEST_ASSERT_TRUE(badge_wifi_service_start(&service));
        badge_wifi_service_request_stop(&service);
        fake = (fake_adapter_t){0};
    }
}

static void test_service_defers_stop_during_upload_and_keeps_hotspot_after_success(void) {
    badge_wifi_service_t service;
    badge_wifi_service_init(&service, fake_adapter());
    badge_wifi_service_attach_http(&service, fake_http());
    TEST_ASSERT_TRUE(badge_wifi_service_start(&service));
    drain_events(&service);
    TEST_ASSERT_TRUE(badge_wifi_service_begin_upload(&service));
    badge_wifi_service_request_stop(&service);
    TEST_ASSERT_EQUAL_STRING("PMNIHST", fake.calls);
    TEST_ASSERT_TRUE(badge_wifi_service_status(&service).stop_requested);
    badge_wifi_service_finish_upload(&service, false);
    TEST_ASSERT_EQUAL(BADGE_WIFI_STOPPING, badge_wifi_service_status(&service).state);
    badge_wifi_service_process(&service);
    TEST_ASSERT_EQUAL_STRING("PMNIHSTtshinp", fake.calls);
    TEST_ASSERT_EQUAL(BADGE_WIFI_OFF, badge_wifi_service_status(&service).state);

    fake = (fake_adapter_t){0};
    badge_wifi_service_init(&service, fake_adapter());
    badge_wifi_service_attach_http(&service, fake_http());
    TEST_ASSERT_TRUE(badge_wifi_service_start(&service));
    TEST_ASSERT_TRUE(badge_wifi_service_begin_upload(&service));
    badge_wifi_service_finish_upload(&service, true);
    badge_wifi_service_response_finished(&service, 100);
    badge_wifi_service_tick(&service, 3000099);
    TEST_ASSERT_TRUE(badge_wifi_service_status(&service).save_success);
    badge_wifi_service_tick(&service, 3000100);
    TEST_ASSERT_EQUAL(BADGE_WIFI_WAITING_CLIENT, badge_wifi_service_status(&service).state);
    TEST_ASSERT_FALSE(badge_wifi_service_status(&service).save_success);
    TEST_ASSERT_TRUE(badge_wifi_service_begin_upload(&service));
    TEST_ASSERT_EQUAL_STRING("PMNIHST", fake.calls);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_start_connect_disconnect_and_duplicate_events);
    RUN_TEST(test_no_inactivity_timeout_and_failed_upload_returns_active);
    RUN_TEST(test_success_feedback_expires_without_stopping_and_allows_another_save);
    RUN_TEST(test_stop_during_upload_waits_for_transaction);
    RUN_TEST(test_early_stop_error_and_restart_boundaries);
    RUN_TEST(test_feedback_deadline_overflow_saturates_without_closing);
    RUN_TEST(test_illegal_events_do_not_change_state);
    RUN_TEST(test_service_start_station_events_and_stop_order);
    RUN_TEST(test_service_each_start_failure_unwinds_and_can_restart);
    RUN_TEST(test_service_defers_stop_during_upload_and_keeps_hotspot_after_success);
    return UNITY_END();
}
