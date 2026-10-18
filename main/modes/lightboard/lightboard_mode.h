#pragma once
#include "lightboard_playback.h"
#include "navigation/mode.h"
#include "services/lightboard/lightboard_store.h"

enum { LIGHTBOARD_MODE_ID = 7 };
// Renderer calls and snapshot release occur only in sync/render under the LVGL
// lock. clear must detach descriptors and evict caches before it returns.
typedef struct {
    void *context;
    void (*clear)(void *, bool leaving);
    bool (*present)(void *, const lb_snapshot_t *, const lb_playback_frame_t *);
} lightboard_renderer_t;
typedef struct {
    lb_store_t *store;
    lb_playback_t playback;
    lb_playback_frame_t frame;
    lb_snapshot_t snapshot;
    lightboard_renderer_t renderer;
    uint8_t snapshot_index;
    bool active, start_pending, frame_ready;
} lightboard_mode_t;

mode_t lightboard_mode_descriptor(lightboard_mode_t *, lb_store_t *);
bool lightboard_mode_tick(lightboard_mode_t *, int64_t now_us);
// Called for every requested application render, including when another page is
// active, so leave can defer LVGL cleanup safely instead of freeing mapped pixels
// from an input callback. No-op while active; render binds the current frame.
void lightboard_mode_sync_locked(lightboard_mode_t *);
