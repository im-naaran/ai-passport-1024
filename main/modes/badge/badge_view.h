#pragma once

#include "badge_mode.h"
#include "ui_shell.h"

struct badge_view {
    lv_obj_t *root;
    lv_obj_t *photo;
    lv_obj_t *name;
    lv_obj_t *bio;
    lv_image_dsc_t image_dsc;
    const uint8_t *rendered_image;
    uint32_t rendered_sequence;
    bool rendered_default;
    bool has_rendered;
};

void badge_view_init(badge_view_t *view, badge_mode_t *badge, mode_t *mode, lv_obj_t *parent);
