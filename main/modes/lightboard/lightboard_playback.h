#pragma once
#include "services/lightboard/lightboard_record.h"

enum {
    LB_PLAYBACK_FRAME_US = 50000,
    LB_PLAYBACK_FADE_US = 120000,
    LB_PLAYBACK_STATIC_US = 3000000,
    LB_PLAYBACK_BREATH_US = 1500000,
    LB_PLAYBACK_SCREEN_WIDTH = 240,
    LB_PLAYBACK_COLOR_COUNT = 6,
};
typedef enum {
    LB_PLAYBACK_INACTIVE, LB_PLAYBACK_FADE_IN, LB_PLAYBACK_HOLDING,
    LB_PLAYBACK_SCROLLING, LB_PLAYBACK_FADE_OUT,
} lb_playback_phase_t;
typedef struct {
    lb_playback_phase_t phase;
    uint32_t revision;
    uint8_t index, color_index, core_opacity, halo_opacity;
    int16_t x, y;
} lb_playback_frame_t;
typedef struct {
    uint16_t width;
    int16_t x, y;
    bool scroll;
} lb_playback_item_t;
typedef struct {
    lb_playback_item_t items[LB_MAX_ITEMS];
    int64_t duration_us[LB_MAX_ITEMS], cycle_us, origin_us, last_now_us, last_frame_us;
    lb_playback_frame_t frame;
    uint32_t revision;
    uint8_t count;
    bool active, frame_valid;
} lb_playback_t;

// Zero-initialize before first use. Metadata is copied, with no pixel ownership or I/O.
// A revision identifies an immutable list; the same revision is a no-op. A new list
// restarts active playback, but never activates a page that was left.
bool lb_playback_set_list(lb_playback_t *, const lb_bundle_t *, uint32_t revision,
                          int64_t now_us);
bool lb_playback_enter(lb_playback_t *, int64_t now_us);
// The view, not this pure scheduler, must detach/release its resource snapshot on leave.
void lb_playback_leave(lb_playback_t *);
// Nonnegative monotonic microseconds, matching esp_timer_get_time(). Backward input
// cannot rewind playback. Returns true and writes output only when a frame is due
// and changed. Enter/new revision allow an immediate first frame; animation is 20Hz.
bool lb_playback_tick(lb_playback_t *, int64_t now_us, lb_playback_frame_t *output);
