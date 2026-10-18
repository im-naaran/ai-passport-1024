#include "lightboard_mode.h"

static void enter(void *context) {
    lightboard_mode_t *mode = context;
    mode->active = mode->start_pending = true;
    mode->frame_ready = false;
}
static void leave(void *context) {
    lightboard_mode_t *mode = context;
    mode->active = mode->start_pending = mode->frame_ready = false;
    lb_playback_leave(&mode->playback);
}
static void clear(lightboard_mode_t *mode, bool leaving) {
    if (mode->renderer.clear) mode->renderer.clear(mode->renderer.context, leaving);
    // Descriptor/cache detachment precedes the lease release, including failures.
    lb_snapshot_release(&mode->snapshot);
}
void lightboard_mode_sync_locked(lightboard_mode_t *mode) {
    if (mode && !mode->active) clear(mode, true);
    else if (mode && !mode->snapshot.acquired && mode->renderer.clear)
        mode->renderer.clear(mode->renderer.context, false);
}
static void render(void *context) {
    lightboard_mode_t *mode = context;
    if (!mode->active || !mode->frame_ready || !mode->renderer.present) return;
    if (mode->snapshot.acquired && (mode->snapshot_index != mode->frame.index ||
        mode->snapshot.revision != mode->frame.revision)) clear(mode, false);
    if (!mode->snapshot.acquired) {
        if (lb_snapshot_acquire(mode->store, mode->frame.index, &mode->snapshot) != LB_OK)
            return;
        // A commit may race between metadata selection and acquire. Never bind
        // a new list's pixels to an old frame; the update event will restart it.
        if (mode->snapshot.revision != mode->frame.revision) {
            clear(mode, false);
            return;
        }
        mode->snapshot_index = mode->frame.index;
    }
    if (!mode->renderer.present(mode->renderer.context, &mode->snapshot, &mode->frame))
        clear(mode, false);
}
mode_t lightboard_mode_descriptor(lightboard_mode_t *mode, lb_store_t *store) {
    *mode = (lightboard_mode_t){.store = store};
    return (mode_t){.id = LIGHTBOARD_MODE_ID, .name = "灯牌", .context = mode,
        .enter = enter, .leave = leave, .render = render};
}
bool lightboard_mode_tick(lightboard_mode_t *mode, int64_t now_us) {
    if (!mode || !mode->active) return false;
    if (mode->start_pending) {
        lb_bundle_t list; uint32_t revision;
        if (!lb_store_metadata(mode->store, &list, &revision) ||
            !lb_playback_set_list(&mode->playback, &list, revision, now_us) ||
            !lb_playback_enter(&mode->playback, now_us)) return false;
        mode->start_pending = false;
    }
    if (!lb_playback_tick(&mode->playback, now_us, &mode->frame)) return false;
    mode->frame_ready = true;
    return true;
}
