#include <unity.h>

#include "button_gesture.h"
#include "display_render_policy.h"

void setUp(void) {}
void tearDown(void) {}

static void test_gesture_thresholds_and_release(void) {
    button_gesture_t state = {0};
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_UP, 0));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_DOWN, 100));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_CLICK,
                      button_gesture_update(&state, BUTTON_EDGE_UP, 150));

    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_DOWN, 200));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_TICK, 999));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_LONG,
                      button_gesture_update(&state, BUTTON_EDGE_TICK, 1000));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_UP, 1001));
}

static void test_pressed_mask_starts_and_stops_shared_timer(void) {
    uint8_t mask = 0;
    TEST_ASSERT_EQUAL(BUTTON_TIMER_START,
                      button_pressed_mask_update(&mask, 1u, true));
    TEST_ASSERT_EQUAL(BUTTON_TIMER_UNCHANGED,
                      button_pressed_mask_update(&mask, 2u, true));
    TEST_ASSERT_EQUAL(BUTTON_TIMER_UNCHANGED,
                      button_pressed_mask_update(&mask, 1u, false));
    TEST_ASSERT_EQUAL(BUTTON_TIMER_STOP,
                      button_pressed_mask_update(&mask, 2u, false));
}

static void test_render_policy_coalesces_changes_without_periodic_refresh(void) {
    display_render_policy_t policy;
    display_render_policy_init(&policy, false);
    TEST_ASSERT_FALSE(display_render_policy_should_render(&policy));
    display_render_policy_request(&policy);
    display_render_policy_request(&policy);
    TEST_ASSERT_TRUE(display_render_policy_should_render(&policy));
    display_render_policy_mark_rendered(&policy);
    TEST_ASSERT_FALSE(display_render_policy_should_render(&policy));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gesture_thresholds_and_release);
    RUN_TEST(test_pressed_mask_starts_and_stops_shared_timer);
    RUN_TEST(test_render_policy_coalesces_changes_without_periodic_refresh);
    return UNITY_END();
}
