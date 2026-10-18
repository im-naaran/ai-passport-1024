#include <unity.h>
#include <limits.h>
#include <string.h>
#include "modes/lightboard/lightboard_playback.h"

void setUp(void) {}
void tearDown(void) {}

static lb_bundle_t list_of(uint8_t count, bool scroll) {
    lb_bundle_t list = {.count = count};
    for (uint8_t i = 0; i < count && i < LB_MAX_ITEMS; ++i)
        list.items[i] = (lb_item_t){.mode = scroll, .width = scroll ? 780 : 200,
            .height = scroll ? 80 : 240, .stride = scroll ? 780 : 200,
            .x = scroll ? 0 : 10 + i, .y = scroll ? 60 + i * 24 : 20 + i,
            .font_size = 64};
    return list;
}

static lb_playback_frame_t at(const lb_bundle_t *list, int64_t time) {
    lb_playback_t p = {0};
    lb_playback_frame_t f;
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, list, 7, 0));
    TEST_ASSERT_TRUE(lb_playback_enter(&p, 0));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, time, &f));
    return f;
}

static void test_static_exact_boundaries_include_both_fades_in_three_seconds(void) {
    lb_bundle_t list = list_of(2, false);
    const int64_t times[] = {0, 119999, 120000, 2879999, 2880000, 2999999, 3000000};
    const lb_playback_phase_t phases[] = {LB_PLAYBACK_FADE_IN, LB_PLAYBACK_FADE_IN,
        LB_PLAYBACK_HOLDING, LB_PLAYBACK_HOLDING, LB_PLAYBACK_FADE_OUT,
        LB_PLAYBACK_FADE_OUT, LB_PLAYBACK_FADE_IN};
    for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        lb_playback_frame_t f = at(&list, times[i]);
        TEST_ASSERT_EQUAL(phases[i], f.phase);
        TEST_ASSERT_EQUAL(times[i] == 3000000 ? 1 : 0, f.index);
        TEST_ASSERT_EQUAL(10 + f.index, f.x);
        TEST_ASSERT_EQUAL(20 + f.index, f.y);
    }
    TEST_ASSERT_EQUAL(0, at(&list, 0).core_opacity);
    TEST_ASSERT_EQUAL(0, at(&list, 3000000).halo_opacity);
}

static void test_scroll_travels_from_right_to_complete_tail_exit_at_80px_per_second(void) {
    lb_bundle_t list = list_of(2, true);
    int64_t travel = (780 + 240) * INT64_C(12500), end = 120000 + travel;
    TEST_ASSERT_EQUAL(240, at(&list, 0).x);
    TEST_ASSERT_EQUAL(240, at(&list, 120000).x);
    TEST_ASSERT_EQUAL(LB_PLAYBACK_SCROLLING, at(&list, 120000).phase);
    TEST_ASSERT_EQUAL(160, at(&list, 1120000).x);
    TEST_ASSERT_EQUAL(-779, at(&list, end - 1).x);
    lb_playback_frame_t f = at(&list, end);
    TEST_ASSERT_EQUAL(-780, f.x);
    TEST_ASSERT_EQUAL(LB_PLAYBACK_FADE_OUT, f.phase);
    TEST_ASSERT_EQUAL(0, f.index);
    TEST_ASSERT_EQUAL(-780, at(&list, end + 119999).x);
    f = at(&list, end + 120000);
    TEST_ASSERT_EQUAL(1, f.index);
    TEST_ASSERT_EQUAL(240, f.x);
}

static void test_single_item_repeats_and_enter_always_restarts(void) {
    for (unsigned scrolling = 0; scrolling < 2; ++scrolling) {
        lb_bundle_t list = list_of(1, scrolling);
        lb_playback_t p = {0}; lb_playback_frame_t f;
        TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 0, 500));
        TEST_ASSERT_TRUE(lb_playback_enter(&p, 500));
        TEST_ASSERT_TRUE(lb_playback_tick(&p, 500 + p.cycle_us * 11, &f));
        TEST_ASSERT_EQUAL(0, f.index);
        TEST_ASSERT_EQUAL(LB_PLAYBACK_FADE_IN, f.phase);
        TEST_ASSERT_EQUAL(0, f.core_opacity);
        TEST_ASSERT_TRUE(lb_playback_enter(&p, 500 + p.cycle_us * 11 + 100));
        TEST_ASSERT_TRUE(lb_playback_tick(&p, 500 + p.cycle_us * 11 + 100, &f));
        TEST_ASSERT_EQUAL(0, f.core_opacity);
    }
}

static void test_six_mixed_items_keep_order_color_and_geometry_across_cycles(void) {
    lb_bundle_t list = list_of(6, false), long_list = list_of(6, true);
    for (unsigned i = 1; i < 6; i += 2) list.items[i] = long_list.items[i];
    lb_playback_t p = {0}; lb_playback_frame_t f;
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 42, 0));
    TEST_ASSERT_TRUE(lb_playback_enter(&p, 0));
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        int64_t t = cycle * p.cycle_us;
        for (unsigned i = 0; i < 6; ++i) {
            TEST_ASSERT_TRUE(lb_playback_tick(&p, t + 120000, &f));
            TEST_ASSERT_EQUAL(i, f.index);
            TEST_ASSERT_EQUAL(i, f.color_index);
            TEST_ASSERT_EQUAL(42, f.revision);
            TEST_ASSERT_EQUAL(list.items[i].y, f.y);
            TEST_ASSERT_EQUAL(i % 2 ? 240 : list.items[i].x, f.x);
            t += p.duration_us[i];
        }
    }
}

static void test_late_ticks_jump_directly_to_current_frame_without_catchup(void) {
    lb_bundle_t list = list_of(6, true);
    lb_playback_t p = {0}; lb_playback_frame_t f;
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 7, 0));
    TEST_ASSERT_TRUE(lb_playback_enter(&p, 0));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, 0, &f));
    int64_t now = p.cycle_us * INT64_C(10000000) + p.duration_us[0] * 4 + 1120000;
    TEST_ASSERT_TRUE(lb_playback_tick(&p, now, &f));
    TEST_ASSERT_EQUAL(4, f.index);
    TEST_ASSERT_EQUAL(160, f.x);
    TEST_ASSERT_FALSE(lb_playback_tick(&p, now, &f));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, now + 49999, &f));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, now + 50000, &f));
    TEST_ASSERT_EQUAL(156, f.x);
    // The clock's full positive range remains safe; no signed overflow or per-cycle loop.
    lb_playback_frame_t expected = at(&list, INT64_MAX);
    TEST_ASSERT_TRUE(lb_playback_tick(&p, INT64_MAX, &f));
    TEST_ASSERT_EQUAL(expected.index, f.index);
    TEST_ASSERT_EQUAL(expected.x, f.x);
}

static void test_polling_every_25ms_requests_no_more_than_one_frame_per_50ms(void) {
    lb_bundle_t list = list_of(6, false);
    lb_playback_t p = {0}; lb_playback_frame_t f;
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 7, 0));
    TEST_ASSERT_TRUE(lb_playback_enter(&p, 0));
    int64_t previous = -LB_PLAYBACK_FRAME_US;
    unsigned requests = 0;
    for (int64_t now = 0; now <= 20000000; now += 25000) {
        if (!lb_playback_tick(&p, now, &f)) continue;
        TEST_ASSERT_GREATER_OR_EQUAL_INT64(LB_PLAYBACK_FRAME_US, now - previous);
        previous = now; ++requests;
    }
    TEST_ASSERT_LESS_OR_EQUAL(401, requests);
    TEST_ASSERT_GREATER_THAN(300, requests);
}

static void test_identical_frame_and_backward_clock_never_request_redraw(void) {
    lb_bundle_t list = list_of(1, true);
    lb_playback_t p = {0}; lb_playback_frame_t f;
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 7, 0));
    TEST_ASSERT_TRUE(lb_playback_enter(&p, 0));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, 120000, &f));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, 120000 + p.cycle_us, &f));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, 119999, &f));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, 120000 + p.cycle_us, &f));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, 170000 + p.cycle_us, &f));
    TEST_ASSERT_EQUAL(236, f.x);
}

static void test_static_breathing_range_extrema_and_transition_envelopes(void) {
    lb_bundle_t list = list_of(1, false);
    for (int64_t now = 120000; now < 2880000; now += 1000) {
        lb_playback_frame_t f = at(&list, now);
        TEST_ASSERT_INT_WITHIN(26, 230, f.core_opacity);
        TEST_ASSERT_GREATER_OR_EQUAL(204, f.core_opacity);
        TEST_ASSERT_LESS_OR_EQUAL(255, f.core_opacity);
        TEST_ASSERT_GREATER_OR_EQUAL(89, f.halo_opacity);
        TEST_ASSERT_LESS_OR_EQUAL(153, f.halo_opacity);
    }
    TEST_ASSERT_EQUAL(255, at(&list, 750000).core_opacity);
    TEST_ASSERT_EQUAL(153, at(&list, 750000).halo_opacity);
    TEST_ASSERT_EQUAL(204, at(&list, 1500000).core_opacity);
    TEST_ASSERT_EQUAL(89, at(&list, 1500000).halo_opacity);
    TEST_ASSERT_LESS_THAN(128, at(&list, 60000).core_opacity);
    TEST_ASSERT_LESS_THAN(128, at(&list, 2940000).core_opacity);
    lb_playback_frame_t a = at(&list, 2879999), b = at(&list, 2880000);
    TEST_ASSERT_INT_WITHIN(1, a.core_opacity, b.core_opacity);
}

static void test_new_revision_restarts_first_item_same_revision_preserves_progress(void) {
    lb_bundle_t list = list_of(6, false);
    lb_playback_t p = {0}; lb_playback_frame_t f;
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, UINT32_MAX, 0));
    TEST_ASSERT_TRUE(lb_playback_enter(&p, 0));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, 4000000, &f));
    TEST_ASSERT_EQUAL(1, f.index);
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, UINT32_MAX, 4000000));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, 4000000, &f));
    list = list_of(1, true);
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 1, 4000000));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, 4000000, &f));
    TEST_ASSERT_EQUAL(0, f.index);
    TEST_ASSERT_EQUAL(1, f.revision);
    TEST_ASSERT_EQUAL(240, f.x);
    TEST_ASSERT_EQUAL(0, f.core_opacity);
    TEST_ASSERT_EQUAL(LB_PLAYBACK_FADE_IN, f.phase);
}

static void test_leave_stops_ticks_updates_do_not_activate_and_reentry_resets(void) {
    lb_bundle_t list = list_of(6, true);
    lb_playback_t p = {0}; lb_playback_frame_t f;
    TEST_ASSERT_FALSE(lb_playback_enter(&p, 0));
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 0, 0));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, 0, &f));
    for (unsigned i = 0; i < 40; ++i) {
        int64_t now = i * INT64_C(10000000);
        TEST_ASSERT_TRUE(lb_playback_enter(&p, now));
        TEST_ASSERT_TRUE(lb_playback_tick(&p, now, &f));
        TEST_ASSERT_EQUAL(0, f.index);
        TEST_ASSERT_EQUAL(0, f.core_opacity);
        TEST_ASSERT_TRUE(lb_playback_tick(&p, now + 1000000, &f));
        lb_playback_leave(&p);
        TEST_ASSERT_EQUAL(LB_PLAYBACK_INACTIVE, p.frame.phase);
        TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, i + 1, now + 2000000));
        TEST_ASSERT_FALSE(lb_playback_tick(&p, now + 3000000, &f));
    }
}

static void test_invalid_update_preserves_active_list_and_no_pixel_pointer_is_retained(void) {
    lb_bundle_t list = list_of(2, false);
    lb_playback_t p = {0}; lb_playback_frame_t f;
    TEST_ASSERT_TRUE(lb_playback_set_list(&p, &list, 7, 0));
    TEST_ASSERT_TRUE(lb_playback_enter(&p, 0));
    lb_playback_t before = p;
    list.items[1].width = 1537;
    TEST_ASSERT_FALSE(lb_playback_set_list(&p, &list, 8, 0));
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(p));
    list = list_of(7, false); // count rejected before reading the fixed-size entries.
    TEST_ASSERT_FALSE(lb_playback_set_list(&p, &list, 8, 0));
    list.count = 0;
    TEST_ASSERT_FALSE(lb_playback_set_list(&p, &list, 8, 0));
    memset(&list, 0, sizeof(list));
    TEST_ASSERT_TRUE(lb_playback_tick(&p, 3120000, &f));
    TEST_ASSERT_EQUAL(1, f.index);
    TEST_ASSERT_EQUAL(11, f.x);
    TEST_ASSERT_FALSE(lb_playback_set_list(NULL, &list, 1, 0));
    TEST_ASSERT_FALSE(lb_playback_set_list(&p, NULL, 1, 0));
    TEST_ASSERT_FALSE(lb_playback_enter(&p, -1));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, -1, &f));
    TEST_ASSERT_FALSE(lb_playback_tick(&p, 4000000, NULL));
    lb_playback_leave(NULL);
}

static void test_minimum_and_maximum_scroll_geometry_reach_exact_tail(void) {
    const unsigned widths[] = {1, 240, 241, 1536};
    for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        lb_bundle_t list = list_of(1, true);
        list.items[0].width = list.items[0].stride = widths[i];
        int64_t end = 120000 + (widths[i] + 240) * INT64_C(12500);
        TEST_ASSERT_EQUAL(-(int)widths[i], at(&list, end).x);
        TEST_ASSERT_EQUAL(240, at(&list, end + 120000).x);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_static_exact_boundaries_include_both_fades_in_three_seconds);
    RUN_TEST(test_scroll_travels_from_right_to_complete_tail_exit_at_80px_per_second);
    RUN_TEST(test_single_item_repeats_and_enter_always_restarts);
    RUN_TEST(test_six_mixed_items_keep_order_color_and_geometry_across_cycles);
    RUN_TEST(test_late_ticks_jump_directly_to_current_frame_without_catchup);
    RUN_TEST(test_polling_every_25ms_requests_no_more_than_one_frame_per_50ms);
    RUN_TEST(test_identical_frame_and_backward_clock_never_request_redraw);
    RUN_TEST(test_static_breathing_range_extrema_and_transition_envelopes);
    RUN_TEST(test_new_revision_restarts_first_item_same_revision_preserves_progress);
    RUN_TEST(test_leave_stops_ticks_updates_do_not_activate_and_reentry_resets);
    RUN_TEST(test_invalid_update_preserves_active_list_and_no_pixel_pointer_is_retained);
    RUN_TEST(test_minimum_and_maximum_scroll_geometry_reach_exact_tail);
    return UNITY_END();
}
