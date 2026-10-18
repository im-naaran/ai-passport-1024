#include "ui_theme.h"
#include "ui_font.h"

lv_color_t ui_color(ui_color_role_t role) {
    static const uint32_t colors[] = {
        [UI_COLOR_ROLE_HEADER] = 0x000000,
        [UI_COLOR_ROLE_CANVAS] = 0x0B0F0E,
        [UI_COLOR_ROLE_SURFACE] = 0x151B19,
        [UI_COLOR_ROLE_SURFACE_ACTIVE] = 0x1C2522,
        [UI_COLOR_ROLE_BORDER] = 0x2B3732,
        [UI_COLOR_ROLE_TEXT_PRIMARY] = 0xF2F5F3,
        [UI_COLOR_ROLE_TEXT_SECONDARY] = 0xB6C0BC,
        [UI_COLOR_ROLE_TEXT_MUTED] = 0x78837F,
        [UI_COLOR_ROLE_ACCENT] = 0x2FD39A,
        [UI_COLOR_ROLE_INFO] = 0x62AFFF,
        [UI_COLOR_ROLE_WARNING] = 0xF0B84B,
        [UI_COLOR_ROLE_DANGER] = 0xEF6262,
    };
    return lv_color_hex(colors[role]);
}

void ui_theme_screen(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, ui_color(UI_COLOR_ROLE_CANVAS), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_theme_page(lv_obj_t *page) {
    lv_obj_set_style_bg_color(page, ui_color(UI_COLOR_ROLE_CANVAS), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
}

void ui_theme_surface(lv_obj_t *surface) {
    lv_obj_set_style_bg_color(surface, ui_color(UI_COLOR_ROLE_SURFACE), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(surface, ui_color(UI_COLOR_ROLE_BORDER), 0);
    lv_obj_set_style_border_width(surface, UI_BORDER_WIDTH, 0);
    lv_obj_set_style_radius(surface, UI_RADIUS_MD, 0);
}

void ui_theme_label(lv_obj_t *label, ui_text_role_t role) {
    static const ui_color_role_t colors[] = {
        [UI_TEXT_PRIMARY] = UI_COLOR_ROLE_TEXT_PRIMARY,
        [UI_TEXT_SECONDARY] = UI_COLOR_ROLE_TEXT_SECONDARY,
        [UI_TEXT_MUTED] = UI_COLOR_ROLE_TEXT_MUTED,
        [UI_TEXT_ACCENT] = UI_COLOR_ROLE_ACCENT,
    };
    lv_obj_set_style_text_font(label, &passport_font_16, 0);
    lv_obj_set_style_text_color(label, ui_color(colors[role]), 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
}
