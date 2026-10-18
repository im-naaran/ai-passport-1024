#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t next_sample_us;
} battery_schedule_t;

void battery_schedule_init(battery_schedule_t *, int64_t now_us);
bool battery_schedule_due(const battery_schedule_t *, int64_t now_us);
void battery_schedule_sampled(battery_schedule_t *, int64_t now_us, bool success);
