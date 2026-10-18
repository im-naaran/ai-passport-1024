#include <limits.h>
#include <unity.h>

#include "battery_schedule.h"

void setUp(void) {}
void tearDown(void) {}

static void test_continuously_lit_sampling_cadence(void) {
    battery_schedule_t schedule;
    battery_schedule_init(&schedule, 10);
    TEST_ASSERT_TRUE(battery_schedule_due(&schedule, 10));
    battery_schedule_sampled(&schedule, 10, true);
    TEST_ASSERT_EQUAL_INT64(30000010, schedule.next_sample_us);
    TEST_ASSERT_FALSE(battery_schedule_due(&schedule, 30000009));
    TEST_ASSERT_TRUE(battery_schedule_due(&schedule, 30000010));
}

static void test_failure_retries_then_resumes_normal_cadence(void) {
    battery_schedule_t schedule;
    battery_schedule_init(&schedule, 100);
    battery_schedule_sampled(&schedule, 100, false);
    TEST_ASSERT_EQUAL_INT64(1000100, schedule.next_sample_us);
    battery_schedule_sampled(&schedule, 1000100, true);
    TEST_ASSERT_EQUAL_INT64(31000100, schedule.next_sample_us);
}

static void test_deadline_saturates_on_overflow(void) {
    battery_schedule_t schedule;
    battery_schedule_init(&schedule, INT64_MAX - 5);
    battery_schedule_sampled(&schedule, INT64_MAX - 5, true);
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, schedule.next_sample_us);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_continuously_lit_sampling_cadence);
    RUN_TEST(test_failure_retries_then_resumes_normal_cadence);
    RUN_TEST(test_deadline_saturates_on_overflow);
    return UNITY_END();
}
