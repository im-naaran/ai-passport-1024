#pragma once
#include "lightboard_mode.h"
#include "ui_shell.h"

typedef struct {
    lv_obj_t *root;
    lv_image_decoder_t *decoder;
    lv_image_dsc_t images[2];
    lv_area_t bounds;
    lb_playback_frame_t frame;
    const lb_snapshot_t *snapshot;
    void *workspace;
    bool bound, error_reported;
} lightboard_view_t;

// All view and decoder access is serialized by the BSP LVGL lock.
bool lightboard_view_init(lightboard_view_t *, lightboard_mode_t *, lv_obj_t *parent);
