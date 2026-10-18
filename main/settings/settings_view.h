#pragma once

#include "settings_page.h"
#include "ui_shell.h"

typedef struct settings_view {
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *battery;
    lv_obj_t *surface;
    lv_obj_t *highlight;
    lv_obj_t *divider;
    lv_obj_t *rows[4];
    lv_obj_t *footer;
    lv_obj_t *footer_text;
    int64_t now_us;
} settings_view_t;

void settings_view_init(settings_view_t *view, settings_page_t *page,
                        mode_t *mode, lv_obj_t *parent);
