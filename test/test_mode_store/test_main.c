#include <unity.h>
#include <string.h>
#include "app_controller.h"
#include "services/config/mode_store.h"

typedef struct {
    uint8_t bytes[8]; size_t length; unsigned saves;
    settings_result_t read_result, save_result;
} fake_t;
static settings_result_t read_record(void *ctx, uint8_t *bytes, size_t *length) {
    fake_t *f = ctx;
    if (f->read_result != SETTINGS_OK) return f->read_result;
    if (f->length <= *length) memcpy(bytes, f->bytes, f->length);
    *length = f->length;
    return SETTINGS_OK;
}
static settings_result_t save_record(void *ctx, const uint8_t *bytes, size_t length) {
    fake_t *f = ctx; ++f->saves;
    if (f->save_result != SETTINGS_OK) return f->save_result;
    memcpy(f->bytes, bytes, length); f->length = length;
    return SETTINGS_OK;
}
static settings_backend_t backend(fake_t *f) { return (settings_backend_t){f, read_record, save_record}; }
void setUp(void) {}
void tearDown(void) {}
static void test_missing_invalid_legacy_ids_and_schema_restore_badge_without_writing(void) {
    fake_t f = {0}; mode_store_t store;
    TEST_ASSERT_EQUAL(SETTINGS_DEFAULTED, mode_store_init(&store, backend(&f)));
    const uint8_t ids[] = {0, 2, 5, 6, 100, 255};
    for (unsigned i = 0; i < sizeof(ids); ++i) {
        f.bytes[0] = 1; f.bytes[1] = ids[i]; f.length = 2;
        TEST_ASSERT_EQUAL(SETTINGS_DEFAULTED, mode_store_init(&store, backend(&f)));
        TEST_ASSERT_EQUAL(4, store.value);
    }
    f.bytes[0] = 2; f.bytes[1] = 7;
    TEST_ASSERT_EQUAL(SETTINGS_DEFAULTED, mode_store_init(&store, backend(&f)));
    f.bytes[0] = 1; f.length = 1;
    TEST_ASSERT_EQUAL(SETTINGS_DEFAULTED, mode_store_init(&store, backend(&f)));
    f.length = 3;
    TEST_ASSERT_EQUAL(SETTINGS_DEFAULTED, mode_store_init(&store, backend(&f)));
    TEST_ASSERT_EQUAL(0, f.saves);
}
static void test_valid_modes_roundtrip_and_repeat_save_is_noop(void) {
    fake_t f = {.bytes = {1, 7}, .length = 2}; mode_store_t store;
    TEST_ASSERT_EQUAL(SETTINGS_OK, mode_store_init(&store, backend(&f)));
    TEST_ASSERT_EQUAL(7, store.value);
    TEST_ASSERT_EQUAL(SETTINGS_OK, mode_store_save(&store, 7));
    TEST_ASSERT_EQUAL(0, f.saves);
    TEST_ASSERT_EQUAL(SETTINGS_OK, mode_store_save(&store, 4));
    TEST_ASSERT_EQUAL(1, f.saves);
    TEST_ASSERT_EQUAL(SETTINGS_OK, mode_store_init(&store, backend(&f)));
    TEST_ASSERT_EQUAL(4, store.value);
    TEST_ASSERT_EQUAL(SETTINGS_INVALID, mode_store_save(&store, 100));
    TEST_ASSERT_EQUAL(SETTINGS_INVALID, mode_store_save(&store, 2));
    TEST_ASSERT_EQUAL(1, f.saves);
}
static void test_io_errors_keep_known_committed_state_without_repair_erase(void) {
    fake_t f = {.read_result = SETTINGS_IO_ERROR}; mode_store_t store;
    TEST_ASSERT_EQUAL(SETTINGS_IO_ERROR, mode_store_init(&store, backend(&f)));
    TEST_ASSERT_EQUAL(4, store.value);
    TEST_ASSERT_EQUAL(0, f.saves);
    f.save_result = SETTINGS_IO_ERROR;
    TEST_ASSERT_EQUAL(SETTINGS_IO_ERROR, mode_store_save(&store, 7));
    TEST_ASSERT_EQUAL(4, store.value);
    TEST_ASSERT_EQUAL(1, f.saves);
    TEST_ASSERT_EQUAL(SETTINGS_IO_ERROR, mode_store_init(&store, (settings_backend_t){0}));
    TEST_ASSERT_EQUAL(SETTINGS_IO_ERROR, mode_store_save(&store, 7));
}

typedef struct {
    fake_t modes, brightness;
    mode_store_t saved;
    settings_store_t config;
    settings_page_t settings;
    lightboard_mode_t lamp;
    mode_t modes_desc[2], settings_desc;
    navigation_t navigation;
    display_render_policy_t render;
    app_controller_t controller;
} fixture_t;
static bool start_hotspot(void *ctx) { (void)ctx; return true; }
static void stop_hotspot(void *ctx) { (void)ctx; }
static void init(fixture_t *f) {
    memset(f, 0, sizeof(*f));
    mode_store_init(&f->saved, backend(&f->modes));
    settings_store_init(&f->config, backend(&f->brightness));
    f->settings = (settings_page_t){.config = &f->config,
        .start_hotspot = start_hotspot, .stop_hotspot = stop_hotspot};
    f->modes_desc[0] = (mode_t){.id = 4};
    f->modes_desc[1] = lightboard_mode_descriptor(&f->lamp, NULL);
    f->settings_desc = settings_page_descriptor(&f->settings);
    TEST_ASSERT_TRUE(navigation_init(&f->navigation, f->modes_desc, 2, &f->settings_desc));
    display_render_policy_init(&f->render, false);
    app_controller_init(&f->controller, &f->navigation, &f->settings, &f->render);
    app_controller_attach_config(&f->controller, &f->saved, &f->lamp);
}
static void key(fixture_t *f, mode_key_t key, bool long_press) {
    app_controller_gesture(&f->controller, key, long_press, 1000);
}
static void test_only_business_changes_persist_settings_and_hotspot_keep_restore_target(void) {
    fixture_t f; init(&f);
    key(&f, MODE_KEY_DOWN, false);
    TEST_ASSERT_EQUAL(7, f.saved.value); TEST_ASSERT_EQUAL(1, f.modes.saves);
    key(&f, MODE_KEY_OK, false); key(&f, MODE_KEY_UP, true);
    TEST_ASSERT_EQUAL(1, f.modes.saves);
    key(&f, MODE_KEY_OK, true); // settings
    TEST_ASSERT_FALSE(f.lamp.active);
    key(&f, MODE_KEY_OK, false); // hotspot
    TEST_ASSERT_EQUAL(SETTINGS_WIFI_CONFIG, f.settings.page);
    key(&f, MODE_KEY_DOWN, false); // consumes exit, does not change mode
    TEST_ASSERT_TRUE(f.navigation.settings_active);
    TEST_ASSERT_EQUAL(7, f.saved.value);
    key(&f, MODE_KEY_DOWN, false); key(&f, MODE_KEY_DOWN, false); key(&f, MODE_KEY_OK, false);
    TEST_ASSERT_TRUE(f.lamp.active); TEST_ASSERT_EQUAL(1, f.modes.saves);
    key(&f, MODE_KEY_UP, false);
    TEST_ASSERT_EQUAL(4, f.saved.value); TEST_ASSERT_EQUAL(2, f.modes.saves);
}
static void test_failed_mode_save_remains_live_and_warning_survives_settings_enter(void) {
    fixture_t f; init(&f); f.modes.save_result = SETTINGS_IO_ERROR;
    key(&f, MODE_KEY_DOWN, false);
    TEST_ASSERT_EQUAL(7, navigation_active(&f.navigation)->id);
    TEST_ASSERT_EQUAL(4, f.saved.value);
    key(&f, MODE_KEY_OK, true);
    TEST_ASSERT_EQUAL(SETTINGS_FEEDBACK_MODE_SAVE_FAILED, settings_page_feedback(&f.settings, 1000));
    TEST_ASSERT_EQUAL(1, f.modes.saves);
    f.settings.cursor = 2; key(&f, MODE_KEY_OK, false);
    f.modes.save_result = SETTINGS_OK;
    // A failed commit is uncertain. A later real switch must persist even when
    // it returns to the last known value, rather than claim a stale no-op succeeded.
    key(&f, MODE_KEY_UP, false);
    TEST_ASSERT_FALSE(f.settings.mode_save_failed);
    TEST_ASSERT_EQUAL(2, f.modes.saves);
}
static void test_list_updates_coalesce_restart_only_active_lamp_and_never_write_nvs(void) {
    fixture_t f; init(&f);
    TEST_ASSERT_EQUAL(APP_ACTION_NONE, app_controller_lightboard_updated(&f.controller, 0));
    TEST_ASSERT_FALSE(f.lamp.active);
    key(&f, MODE_KEY_DOWN, false);
    f.lamp.start_pending = false; f.lamp.frame_ready = true;
    TEST_ASSERT_BITS_HIGH(APP_ACTION_RENDER, app_controller_lightboard_updated(&f.controller, 1));
    TEST_ASSERT_TRUE(f.lamp.start_pending); TEST_ASSERT_FALSE(f.lamp.frame_ready);
    f.lamp.start_pending = false;
    TEST_ASSERT_EQUAL(APP_ACTION_NONE, app_controller_lightboard_updated(&f.controller, 1));
    TEST_ASSERT_FALSE(f.lamp.start_pending);
    key(&f, MODE_KEY_OK, true);
    TEST_ASSERT_EQUAL(APP_ACTION_NONE, app_controller_lightboard_updated(&f.controller, 2));
    TEST_ASSERT_TRUE(f.navigation.settings_active);
    TEST_ASSERT_EQUAL(1, f.modes.saves);
}
static void test_brightness_legacy_fields_and_failure_are_independent_from_mode_key(void) {
    fake_t f = {.bytes = {3,20,10,1,80}, .length = 5}; settings_store_t store;
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&f)));
    TEST_ASSERT_EQUAL(80, store.value.brightness_percent);
    settings_t draft = store.value; draft.brightness_percent = 100;
    f.save_result = SETTINGS_IO_ERROR;
    TEST_ASSERT_EQUAL(SETTINGS_IO_ERROR, settings_store_save(&store, draft));
    TEST_ASSERT_EQUAL(80, store.value.brightness_percent);
    f.save_result = SETTINGS_OK;
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_save(&store, draft));
    settings_store_t restored;
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&restored, backend(&f)));
    TEST_ASSERT_EQUAL(100, restored.value.brightness_percent);
    TEST_ASSERT_EQUAL(1, restored.value.screen_off_minutes);
}
static unsigned badge_frames, lamp_frames;
static void badge_frame(void *ctx) { (void)ctx; ++badge_frames; }
static void lamp_frame(void *ctx) { (void)ctx; ++lamp_frames; }
static void test_boot_restore_selects_lamp_before_first_visible_render(void) {
    fixture_t f; init(&f);
    f.modes.bytes[0] = 1; f.modes.bytes[1] = 7; f.modes.length = 2;
    TEST_ASSERT_EQUAL(SETTINGS_OK, mode_store_init(&f.saved, backend(&f.modes)));
    f.modes_desc[0].render = badge_frame;
    f.modes_desc[1].render = lamp_frame;
    badge_frames = lamp_frames = 0;
    TEST_ASSERT_TRUE(navigation_activate(&f.navigation, f.saved.value));
    navigation_render(&f.navigation);
    TEST_ASSERT_EQUAL(0, badge_frames);
    TEST_ASSERT_EQUAL(1, lamp_frames);
    TEST_ASSERT_EQUAL(0, f.modes.saves);
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_restore_selects_lamp_before_first_visible_render);
    RUN_TEST(test_missing_invalid_legacy_ids_and_schema_restore_badge_without_writing);
    RUN_TEST(test_valid_modes_roundtrip_and_repeat_save_is_noop);
    RUN_TEST(test_io_errors_keep_known_committed_state_without_repair_erase);
    RUN_TEST(test_only_business_changes_persist_settings_and_hotspot_keep_restore_target);
    RUN_TEST(test_failed_mode_save_remains_live_and_warning_survives_settings_enter);
    RUN_TEST(test_list_updates_coalesce_restart_only_active_lamp_and_never_write_nvs);
    RUN_TEST(test_brightness_legacy_fields_and_failure_are_independent_from_mode_key);
    return UNITY_END();
}
