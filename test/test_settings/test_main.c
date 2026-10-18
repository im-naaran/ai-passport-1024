#include <unity.h>

#include "settings_store.h"
#include "settings/settings_page.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    uint8_t record[8];
    size_t length;
    settings_result_t read_result;
    settings_result_t save_result;
    unsigned saves;
} fake_settings_t;

static settings_result_t fake_read(void *context, uint8_t *bytes, size_t *length) {
    fake_settings_t *fake = context;
    if (fake->read_result != SETTINGS_OK) return fake->read_result;
    if (*length < fake->length) return SETTINGS_IO_ERROR;
    memcpy(bytes, fake->record, fake->length);
    *length = fake->length;
    return SETTINGS_OK;
}

static settings_result_t fake_save(void *context, const uint8_t *bytes, size_t length) {
    fake_settings_t *fake = context;
    ++fake->saves;
    if (fake->save_result != SETTINGS_OK) return fake->save_result;
    memcpy(fake->record, bytes, length);
    fake->length = length;
    return SETTINGS_OK;
}

static settings_backend_t backend(fake_settings_t *fake) {
    return (settings_backend_t){fake, fake_read, fake_save};
}

typedef struct {
    bool start_ok;
    unsigned starts;
    unsigned stops;
    settings_hotspot_status_t status;
} fake_hotspot_t;

static bool start_hotspot(void *context) {
    fake_hotspot_t *hotspot = context;
    ++hotspot->starts;
    return hotspot->start_ok;
}

static void stop_hotspot(void *context) {
    ++((fake_hotspot_t *)context)->stops;
}

static settings_hotspot_status_t hotspot_status(void *context) {
    return ((fake_hotspot_t *)context)->status;
}

static mode_action_t page_key(mode_t *mode, mode_key_t key, int64_t now_us) {
    return mode->handle_key(mode->context, key, now_us);
}

static void test_defaults_and_old_schemas_preserve_common_fields(void) {
    settings_store_t store;
    fake_settings_t missing = {.read_result = SETTINGS_DEFAULTED};
    TEST_ASSERT_EQUAL(SETTINGS_DEFAULTED, settings_store_init(&store, backend(&missing)));
    TEST_ASSERT_EQUAL(3, store.value.screen_off_minutes);
    TEST_ASSERT_EQUAL(60, store.value.brightness_percent);

    fake_settings_t schema1 = {
        .record = {1, 30, 5}, .length = 3,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_OK,
    };
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&schema1)));
    TEST_ASSERT_EQUAL(30, store.value.capacity);
    TEST_ASSERT_EQUAL(5, store.value.expiry_minutes);
    TEST_ASSERT_EQUAL(3, store.value.screen_off_minutes);
    TEST_ASSERT_EQUAL(60, store.value.brightness_percent);

    fake_settings_t schema2 = {
        .record = {2, 10, 1, 5}, .length = 4,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_OK,
    };
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&schema2)));
    TEST_ASSERT_EQUAL(5, store.value.screen_off_minutes);
    TEST_ASSERT_EQUAL(60, store.value.brightness_percent);
}

static void test_corrupt_fields_default_independently(void) {
    settings_store_t store;
    fake_settings_t fake = {
        .record = {3, 30, 99, 10, 80}, .length = 5,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_OK,
    };
    TEST_ASSERT_EQUAL(SETTINGS_DEFAULTED, settings_store_init(&store, backend(&fake)));
    TEST_ASSERT_EQUAL(30, store.value.capacity);
    TEST_ASSERT_EQUAL(10, store.value.expiry_minutes);
    TEST_ASSERT_EQUAL(10, store.value.screen_off_minutes);
    TEST_ASSERT_EQUAL(80, store.value.brightness_percent);
}

static void test_save_commits_before_updating_live_value(void) {
    settings_store_t store;
    fake_settings_t fake = {
        .record = {3, 20, 10, 3, 60}, .length = 5,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_IO_ERROR,
    };
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&fake)));
    settings_t changed = store.value;
    changed.brightness_percent = 80;
    TEST_ASSERT_EQUAL(SETTINGS_IO_ERROR, settings_store_save(&store, changed));
    TEST_ASSERT_EQUAL(60, store.value.brightness_percent);
    TEST_ASSERT_EQUAL(1, fake.saves);

    fake.save_result = SETTINGS_OK;
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_save(&store, changed));
    TEST_ASSERT_EQUAL(80, store.value.brightness_percent);
    TEST_ASSERT_EQUAL(2, fake.saves);
    const uint8_t expected[] = {3, 20, 10, 3, 80};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, fake.record, 5);
}

static void test_invalid_and_unchanged_values_do_not_write(void) {
    settings_store_t store;
    fake_settings_t fake = {
        .record = {3, 20, 10, 3, 60}, .length = 5,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_OK,
    };
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&fake)));
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_save(&store, store.value));
    TEST_ASSERT_EQUAL(0, fake.saves);
    settings_t invalid = store.value;
    invalid.brightness_percent = 50;
    TEST_ASSERT_EQUAL(SETTINGS_INVALID, settings_store_save(&store, invalid));
    TEST_ASSERT_EQUAL(0, fake.saves);
}

static void test_page_only_edits_brightness_and_preserves_legacy_fields(void) {
    for (size_t timeout = 0; timeout < 2; ++timeout) {
        fake_settings_t fake = {
            .record = {3, 20, 10, timeout ? 0 : 1, 60}, .length = 5,
            .read_result = SETTINGS_OK, .save_result = SETTINGS_OK,
        };
        settings_store_t store;
        TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&fake)));
        settings_page_t page = {.config = &store};
        mode_t mode = settings_page_descriptor(&page);
        mode.init(mode.context);
        page_key(&mode, MODE_KEY_DOWN, 1);
        page_key(&mode, MODE_KEY_OK, 2);
        TEST_ASSERT_EQUAL(SETTINGS_EDIT_BRIGHTNESS, page.page);
        TEST_ASSERT_EQUAL(2, page.draft_index);
        page_key(&mode, MODE_KEY_DOWN, 3);
        page_key(&mode, MODE_KEY_OK, 4);
        TEST_ASSERT_EQUAL(SETTINGS_LIST, page.page);
        TEST_ASSERT_EQUAL(80, store.value.brightness_percent);
        TEST_ASSERT_EQUAL(timeout ? 0 : 1, store.value.screen_off_minutes);
        TEST_ASSERT_EQUAL(20, store.value.capacity);
        TEST_ASSERT_EQUAL(10, store.value.expiry_minutes);
        TEST_ASSERT_EQUAL(1, fake.saves);
        settings_store_t reloaded;
        TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&reloaded, backend(&fake)));
        TEST_ASSERT_EQUAL(80, reloaded.value.brightness_percent);
    }
}

static void test_page_save_failure_is_finite_and_keeps_live_value(void) {
    fake_settings_t fake = {
        .record = {3, 20, 10, 3, 60}, .length = 5,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_IO_ERROR,
    };
    settings_store_t store;
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&fake)));
    settings_page_t page = {.config = &store};
    mode_t mode = settings_page_descriptor(&page);
    mode.init(mode.context);
    page_key(&mode, MODE_KEY_DOWN, 1);
    page_key(&mode, MODE_KEY_OK, 3);
    page_key(&mode, MODE_KEY_DOWN, 4);
    page_key(&mode, MODE_KEY_OK, 100);
    TEST_ASSERT_EQUAL(SETTINGS_EDIT_BRIGHTNESS, page.page);
    TEST_ASSERT_EQUAL(60, store.value.brightness_percent);
    TEST_ASSERT_EQUAL(SETTINGS_FEEDBACK_SAVE_FAILED,
                      settings_page_feedback(&page, 200));
    TEST_ASSERT_EQUAL(SETTINGS_FEEDBACK_NONE,
                      settings_page_feedback(&page, 2000100));
}

static void test_hotspot_start_status_and_any_key_exit(void) {
    fake_settings_t fake = {
        .record = {3, 20, 10, 3, 60}, .length = 5,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_OK,
    };
    settings_store_t store;
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&fake)));
    fake_hotspot_t hotspot = {
        .status = {.state = SETTINGS_HOTSPOT_WAITING_CLIENT,
                   .error = SETTINGS_HOTSPOT_ERROR_NONE,
                   .address = "192.168.4.1"},
    };
    strcpy(hotspot.status.ssid, "AI-Passport-1234");
    settings_page_t page = {
        .config = &store, .hotspot_context = &hotspot,
        .start_hotspot = start_hotspot, .stop_hotspot = stop_hotspot,
        .hotspot_status = hotspot_status,
    };
    mode_t mode = settings_page_descriptor(&page);
    mode.init(mode.context);
    page_key(&mode, MODE_KEY_OK, 10);
    TEST_ASSERT_EQUAL(SETTINGS_FEEDBACK_UNAVAILABLE,
                      settings_page_feedback(&page, 11));
    TEST_ASSERT_EQUAL(SETTINGS_LIST, page.page);

    hotspot.start_ok = true;
    page_key(&mode, MODE_KEY_OK, 20);
    TEST_ASSERT_TRUE(settings_page_hotspot_active(&page));
    TEST_ASSERT_EQUAL_STRING("等待手机连接",
        settings_page_hotspot_state_text(settings_page_hotspot_status(&page)));
    TEST_ASSERT_NULL(strstr(settings_page_hotspot_state_text(hotspot.status), "蓝牙"));
    TEST_ASSERT_EQUAL(MODE_STAY, page_key(&mode, MODE_KEY_UP, 21));
    TEST_ASSERT_EQUAL(SETTINGS_LIST, page.page);
    TEST_ASSERT_EQUAL(1, hotspot.stops);
}

static void test_page_return_is_third_row(void) {
    fake_settings_t fake = {
        .record = {3, 20, 10, 3, 60}, .length = 5,
        .read_result = SETTINGS_OK, .save_result = SETTINGS_OK,
    };
    settings_store_t store;
    TEST_ASSERT_EQUAL(SETTINGS_OK, settings_store_init(&store, backend(&fake)));
    settings_page_t page = {.config = &store};
    mode_t mode = settings_page_descriptor(&page);
    mode.init(mode.context);
    for (int i = 0; i < 8; ++i) page_key(&mode, MODE_KEY_DOWN, i);
    TEST_ASSERT_EQUAL(2, page.cursor);
    TEST_ASSERT_EQUAL(MODE_RETURN, page_key(&mode, MODE_KEY_OK, 10));
}

static void test_battery_snapshot_is_read_only_and_validated(void) {
    settings_page_t page = {0};
    mode_t mode = settings_page_descriptor(&page);
    TEST_ASSERT_EQUAL(-1, page.battery_soc);
    TEST_ASSERT_FALSE(settings_page_set_battery(&page, -2));
    TEST_ASSERT_FALSE(settings_page_set_battery(&page, 101));
    TEST_ASSERT_TRUE(settings_page_set_battery(&page, 0));
    TEST_ASSERT_EQUAL(0, page.battery_soc);
    TEST_ASSERT_FALSE(settings_page_set_battery(&page, 0));
    TEST_ASSERT_TRUE(settings_page_set_battery(&page, 15));
    TEST_ASSERT_TRUE(settings_page_set_battery(&page, 16));
    TEST_ASSERT_TRUE(settings_page_set_battery(&page, 30));
    TEST_ASSERT_TRUE(settings_page_set_battery(&page, 31));
    TEST_ASSERT_TRUE(settings_page_set_battery(&page, 100));
    TEST_ASSERT_TRUE(settings_page_set_battery(&page, -1));
    TEST_ASSERT_EQUAL(-1, page.battery_soc);
    TEST_ASSERT_EQUAL(0, page.cursor);
    TEST_ASSERT_EQUAL(SETTINGS_LIST, page.page);
    (void)mode;
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_and_old_schemas_preserve_common_fields);
    RUN_TEST(test_corrupt_fields_default_independently);
    RUN_TEST(test_save_commits_before_updating_live_value);
    RUN_TEST(test_invalid_and_unchanged_values_do_not_write);
    RUN_TEST(test_page_only_edits_brightness_and_preserves_legacy_fields);
    RUN_TEST(test_page_save_failure_is_finite_and_keeps_live_value);
    RUN_TEST(test_hotspot_start_status_and_any_key_exit);
    RUN_TEST(test_page_return_is_third_row);
    RUN_TEST(test_battery_snapshot_is_read_only_and_validated);
    return UNITY_END();
}
