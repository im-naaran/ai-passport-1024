#include "battery_schedule.h"
#include <limits.h>

#define BATTERY_PERIOD_US 30000000LL
#define BATTERY_RETRY_PERIOD_US 1000000LL

static int64_t add_period(int64_t now, int64_t period) {
    return now > INT64_MAX - period ? INT64_MAX : now + period;
}

void battery_schedule_init(battery_schedule_t *s, int64_t now_us) {
    *s = (battery_schedule_t){.next_sample_us = now_us};
}

bool battery_schedule_due(const battery_schedule_t *s, int64_t now_us) {
    return now_us >= s->next_sample_us;
}

void battery_schedule_sampled(battery_schedule_t *s, int64_t now_us, bool success) {
    int64_t period = success ? BATTERY_PERIOD_US
                             : BATTERY_RETRY_PERIOD_US;
    s->next_sample_us = add_period(now_us, period);
}
