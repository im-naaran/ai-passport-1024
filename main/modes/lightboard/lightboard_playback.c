#include "lightboard_playback.h"

// 80px/s gives an exact integer duration per pixel, including the entire viewport.
#define US_PER_PIXEL INT64_C(12500)

static int64_t monotonic_now(lb_playback_t *playback, int64_t now_us) {
    if (now_us < playback->last_now_us) return playback->last_now_us;
    playback->last_now_us = now_us;
    return now_us;
}

static void restart(lb_playback_t *playback, int64_t now_us) {
    playback->origin_us = monotonic_now(playback, now_us);
    playback->frame_valid = false;
}

bool lb_playback_set_list(lb_playback_t *playback, const lb_bundle_t *list,
                          uint32_t revision, int64_t now_us) {
    if (!playback || !list || now_us < 0 || !list->count || list->count > LB_MAX_ITEMS)
        return false;
    // Validate all entries before changing any state, so a bad update preserves playback.
    for (uint8_t i = 0; i < list->count; ++i)
        if (!lb_item_geometry_valid(&list->items[i])) return false;
    if (playback->count && playback->revision == revision) return true;
    playback->cycle_us = 0;
    playback->count = list->count;
    playback->revision = revision;
    for (uint8_t i = 0; i < list->count; ++i) {
        const lb_item_t *item = &list->items[i];
        playback->items[i] = (lb_playback_item_t){
            .width = item->width, .x = item->x, .y = item->y, .scroll = item->mode == 1,
        };
        // Static 3s includes both 120ms fades; scrolling finishes its entire
        // path before fade-out. No speed or duration depends on tick frequency.
        playback->duration_us[i] = item->mode == 1
            ? 2 * LB_PLAYBACK_FADE_US + (item->width + LB_PLAYBACK_SCREEN_WIDTH) * US_PER_PIXEL
            : LB_PLAYBACK_STATIC_US;
        playback->cycle_us += playback->duration_us[i];
    }
    restart(playback, now_us);
    return true;
}

bool lb_playback_enter(lb_playback_t *playback, int64_t now_us) {
    if (!playback || !playback->count || now_us < 0) return false;
    restart(playback, now_us);
    playback->active = true;
    return true;
}

void lb_playback_leave(lb_playback_t *playback) {
    if (!playback) return;
    playback->active = false;
    playback->frame_valid = false;
    playback->frame = (lb_playback_frame_t){.phase = LB_PLAYBACK_INACTIVE};
}

static bool same_frame(const lb_playback_frame_t *a, const lb_playback_frame_t *b) {
    // Compare fields, never structure padding. Phase also matters at item boundaries.
    return a->phase == b->phase && a->revision == b->revision && a->index == b->index &&
           a->color_index == b->color_index && a->x == b->x && a->y == b->y &&
           a->core_opacity == b->core_opacity && a->halo_opacity == b->halo_opacity;
}

bool lb_playback_tick(lb_playback_t *playback, int64_t now_us, lb_playback_frame_t *output) {
    if (!playback || !output || !playback->active || now_us < 0) return false;
    now_us = monotonic_now(playback, now_us);
    if (playback->frame_valid && now_us - playback->last_frame_us < LB_PLAYBACK_FRAME_US)
        return false;

    // Skip arbitrarily many missed cycles in constant space and at most six steps;
    // late frames are never queued or replayed, even after a long settings pause.
    int64_t elapsed = (now_us - playback->origin_us) % playback->cycle_us;
    uint8_t index = 0;
    while (elapsed >= playback->duration_us[index])
        elapsed -= playback->duration_us[index++];
    const lb_playback_item_t *item = &playback->items[index];
    const int64_t duration = playback->duration_us[index];
    lb_playback_frame_t frame = {
        .revision = playback->revision, .index = index,
        .color_index = index % LB_PLAYBACK_COLOR_COUNT, .x = item->x, .y = item->y,
        .core_opacity = 255, .halo_opacity = 153,
    };
    int64_t fade = LB_PLAYBACK_FADE_US;
    if (elapsed < LB_PLAYBACK_FADE_US) {
        frame.phase = LB_PLAYBACK_FADE_IN;
        fade = elapsed;
    } else if (elapsed >= duration - LB_PLAYBACK_FADE_US) {
        frame.phase = LB_PLAYBACK_FADE_OUT;
        fade = duration - elapsed;
    } else {
        frame.phase = item->scroll ? LB_PLAYBACK_SCROLLING : LB_PLAYBACK_HOLDING;
    }
    if (item->scroll) {
        int64_t travel = elapsed > LB_PLAYBACK_FADE_US ? elapsed - LB_PLAYBACK_FADE_US : 0;
        int64_t full_travel = duration - 2 * LB_PLAYBACK_FADE_US;
        if (travel > full_travel) travel = full_travel;
        frame.x = LB_PLAYBACK_SCREEN_WIDTH - travel / US_PER_PIXEL;
    } else {
        // Smoothstep on a triangle wave: quiet 1.5s breathing, integer-only with
        // zero slope at the extrema. These are mask opacities, never backlight values.
        int64_t phase = elapsed % LB_PLAYBACK_BREATH_US;
        int64_t half = LB_PLAYBACK_BREATH_US / 2;
        int64_t triangle = phase <= half ? phase : LB_PLAYBACK_BREATH_US - phase;
        int64_t t = triangle * 1000 / half;
        int64_t breath = t * t * (3000 - 2 * t) / 1000000;
        frame.core_opacity = 204 + 51 * breath / 1000;
        frame.halo_opacity = 89 + 64 * breath / 1000;
    }
    frame.core_opacity = frame.core_opacity * fade / LB_PLAYBACK_FADE_US;
    frame.halo_opacity = frame.halo_opacity * fade / LB_PLAYBACK_FADE_US;
    if (playback->frame_valid && same_frame(&frame, &playback->frame)) return false;
    playback->frame = frame;
    playback->last_frame_us = now_us;
    playback->frame_valid = true;
    *output = frame;
    return true;
}
