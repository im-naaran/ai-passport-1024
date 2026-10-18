#pragma once
#include "lvgl.h"

#define UI_SPACE_XS 4
#define UI_SPACE_SM 8
#define UI_SPACE_MD 12
#define UI_RADIUS_SM 4
#define UI_RADIUS_MD 6
#define UI_BORDER_WIDTH 1

typedef enum {
    UI_COLOR_ROLE_HEADER,
    UI_COLOR_ROLE_CANVAS,
    UI_COLOR_ROLE_SURFACE,
    UI_COLOR_ROLE_SURFACE_ACTIVE,
    UI_COLOR_ROLE_BORDER,
    UI_COLOR_ROLE_TEXT_PRIMARY,
    UI_COLOR_ROLE_TEXT_SECONDARY,
    UI_COLOR_ROLE_TEXT_MUTED,
    UI_COLOR_ROLE_ACCENT,
    UI_COLOR_ROLE_INFO,
    UI_COLOR_ROLE_WARNING,
    UI_COLOR_ROLE_DANGER,
} ui_color_role_t;

typedef enum {
    UI_TEXT_PRIMARY,
    UI_TEXT_SECONDARY,
    UI_TEXT_MUTED,
    UI_TEXT_ACCENT,
} ui_text_role_t;

lv_color_t ui_color(ui_color_role_t role);
void ui_theme_screen(lv_obj_t *screen);
void ui_theme_page(lv_obj_t *page);
void ui_theme_surface(lv_obj_t *surface);
void ui_theme_label(lv_obj_t *label, ui_text_role_t role);
