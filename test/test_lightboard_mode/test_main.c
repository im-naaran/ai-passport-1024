#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "modes/lightboard/lightboard_mode.h"
#include "navigation/mode_registry.h"

static uint8_t bytes[1024];
static lb_store_t store;
static lightboard_mode_t mode;
static mode_t descriptor;
static unsigned maps, unmaps, presents, clears;
static bool ui_locked, detached, fail_map, fail_present;
static lb_mapping_t mapping;
static bool map(void *ctx, size_t offset, size_t length, lb_mapping_t *out) {
    (void)ctx;
    if (fail_map || offset < LB_BANK_HEADER || offset - LB_BANK_HEADER + length > sizeof(bytes)) return false;
    mapping = (lb_mapping_t){.data = bytes + offset - LB_BANK_HEADER, .handle = ++maps};
    *out = mapping;
    return true;
}
static void unmap(void *ctx, lb_mapping_t value) {
    (void)ctx;
    TEST_ASSERT_TRUE(ui_locked);
    TEST_ASSERT_TRUE(detached);
    TEST_ASSERT_EQUAL_PTR(mapping.data, value.data);
    ++unmaps;
}
static void clear(void *ctx, bool leaving) {
    (void)ctx; (void)leaving;
    TEST_ASSERT_TRUE(ui_locked);
    // clear sees a valid lease; the production view drops descriptors/cache here.
    if (mode.snapshot.acquired) {
        TEST_ASSERT_EQUAL(1, store.references[0]);
        TEST_ASSERT_NOT_NULL(mode.snapshot.planes[0]);
    }
    detached = true; ++clears;
}
static bool present(void *ctx, const lb_snapshot_t *snapshot, const lb_playback_frame_t *frame) {
    (void)ctx;
    TEST_ASSERT_TRUE(ui_locked);
    TEST_ASSERT_TRUE(snapshot->acquired);
    TEST_ASSERT_EQUAL(1, store.references[0]);
    TEST_ASSERT_EQUAL(snapshot->revision, frame->revision);
    detached = false; ++presents;
    return !fail_present;
}
static void locked_render(void) {
    ui_locked = true;
    lightboard_mode_sync_locked(&mode);
    descriptor.render(descriptor.context);
    ui_locked = false;
}
void setUp(void) {
    FILE *file = fopen("test/fixtures/lightboard/bundle.bin", "rb");
    TEST_ASSERT_NOT_NULL(file);
    size_t size = fread(bytes, 1, sizeof(bytes), file); fclose(file);
    store = (lb_store_t){.active_bank = 0, .revision = 7, .defaults = bytes,
        .backend = {.map = map, .unmap = unmap}};
    TEST_ASSERT_TRUE(lb_bundle_validate(bytes, size, &store.active));
    descriptor = lightboard_mode_descriptor(&mode, &store);
    mode.renderer = (lightboard_renderer_t){.clear = clear, .present = present};
    maps = unmaps = presents = clears = 0;
    ui_locked = fail_map = fail_present = false;
    detached = true;
}
void tearDown(void) {
    descriptor.leave(descriptor.context);
    locked_render();
    TEST_ASSERT_EQUAL(0, store.references[0]);
    TEST_ASSERT_EQUAL(maps, unmaps);
}
static void test_navigation_stops_state_before_locked_cache_detach_and_unmap(void) {
    mode_t badge = {.id = 4}, settings = {.id = 100};
    mode_registry_t registry; navigation_t navigation;
    TEST_ASSERT_TRUE(mode_registry_init(&registry, badge, descriptor, settings));
    TEST_ASSERT_TRUE(navigation_init(&navigation, registry.business, 2, &registry.system_settings));
    TEST_ASSERT_EQUAL_STRING("灯牌", descriptor.name);
    TEST_ASSERT_TRUE(navigation_key(&navigation, MODE_KEY_UP, false, 0));
    TEST_ASSERT_TRUE(mode.active);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 0));
    TEST_ASSERT_EQUAL(0, maps); // tick has no UI or mapping side effects.
    locked_render();
    TEST_ASSERT_EQUAL(1, maps);
    TEST_ASSERT_FALSE(navigation_key(&navigation, MODE_KEY_OK, false, 1));
    TEST_ASSERT_TRUE(navigation_key(&navigation, MODE_KEY_OK, true, 2));
    TEST_ASSERT_FALSE(mode.active);
    TEST_ASSERT_FALSE(lightboard_mode_tick(&mode, 1000000));
    TEST_ASSERT_EQUAL(0, unmaps); // input callback cannot unmap an in-flight source.
    TEST_ASSERT_EQUAL(1, store.references[0]);
    locked_render();
    TEST_ASSERT_EQUAL(1, unmaps);
    TEST_ASSERT_FALSE(mode.snapshot.acquired);
}
static void test_repeated_frames_reuse_mapping_item_switch_detaches_first(void) {
    descriptor.enter(descriptor.context);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 0)); locked_render();
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 1000000)); locked_render();
    TEST_ASSERT_EQUAL(1, maps); TEST_ASSERT_EQUAL(0, unmaps);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 3000000)); locked_render();
    TEST_ASSERT_EQUAL(1, mode.frame.index);
    TEST_ASSERT_EQUAL(2, maps); TEST_ASSERT_EQUAL(1, unmaps);
}
static void test_render_failure_clears_sources_before_releasing_lease_and_can_retry(void) {
    descriptor.enter(descriptor.context);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 0));
    fail_present = true; locked_render();
    TEST_ASSERT_FALSE(mode.snapshot.acquired);
    TEST_ASSERT_EQUAL(1, maps); TEST_ASSERT_EQUAL(1, unmaps);
    fail_present = false;
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 50000)); locked_render();
    TEST_ASSERT_TRUE(mode.snapshot.acquired);
    TEST_ASSERT_EQUAL(2, maps);
}
static void test_mapping_failure_does_not_publish_dangling_source(void) {
    descriptor.enter(descriptor.context);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 0));
    fail_map = true; locked_render();
    TEST_ASSERT_EQUAL(0, presents);
    TEST_ASSERT_FALSE(mode.snapshot.acquired);
    TEST_ASSERT_EQUAL(0, store.references[0]);
    fail_map = false;
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 50000)); locked_render();
    TEST_ASSERT_EQUAL(1, presents);
}
static void test_metadata_acquire_revision_race_does_not_mix_frames_and_pixels(void) {
    descriptor.enter(descriptor.context);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 0));
    store.revision = 8;
    locked_render();
    TEST_ASSERT_EQUAL(0, presents);
    TEST_ASSERT_FALSE(mode.snapshot.acquired);
    TEST_ASSERT_EQUAL(1, unmaps);
    descriptor.leave(descriptor.context);
    descriptor.enter(descriptor.context);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 1000000)); locked_render();
    TEST_ASSERT_EQUAL(8, mode.frame.revision);
    TEST_ASSERT_EQUAL(1, presents);
}
static void test_rapid_leave_reenter_restarts_first_frame_with_valid_lease(void) {
    descriptor.enter(descriptor.context);
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 0)); locked_render();
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 1000000)); locked_render();
    descriptor.leave(descriptor.context);
    descriptor.enter(descriptor.context); // UI lock was unavailable between gestures.
    TEST_ASSERT_TRUE(lightboard_mode_tick(&mode, 1000010)); locked_render();
    TEST_ASSERT_EQUAL(0, mode.frame.index);
    TEST_ASSERT_EQUAL(0, mode.frame.core_opacity);
    TEST_ASSERT_EQUAL(1, maps);
    TEST_ASSERT_TRUE(mode.snapshot.acquired);
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_navigation_stops_state_before_locked_cache_detach_and_unmap);
    RUN_TEST(test_repeated_frames_reuse_mapping_item_switch_detaches_first);
    RUN_TEST(test_render_failure_clears_sources_before_releasing_lease_and_can_retry);
    RUN_TEST(test_mapping_failure_does_not_publish_dangling_source);
    RUN_TEST(test_metadata_acquire_revision_race_does_not_mix_frames_and_pixels);
    RUN_TEST(test_rapid_leave_reenter_restarts_first_frame_with_valid_lease);
    return UNITY_END();
}
