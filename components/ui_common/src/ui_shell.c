#include "ui_shell.h"
#include "ui_theme.h"
#include "text_layout.h"
#include <string.h>
lv_obj_t *ui_page_create(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    if (!page) return NULL;
    lv_obj_remove_style_all(page);
    ui_theme_page(page);
    lv_obj_set_size(page, 240, 320);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
}
lv_obj_t *ui_label_create(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y); lv_obj_set_size(label, width, height);
    ui_theme_label(label, UI_TEXT_PRIMARY);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, "");
    return label;
}
void ui_label_update(lv_obj_t *label, const char *text) {
    // Avoid invalidating unchanged regions on every background-service tick.
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
text_layout_result_t ui_label_format(lv_obj_t *label, const char *text, bool truncated, unsigned lines) {
    // Shared scratch is safe because rendering is serialized under the LVGL lock.
    static char formatted[1200];
    text_layout_result_t result = text_layout_format(text, strlen(text), truncated,
        lv_obj_get_style_width(label, 0), lines, ui_font_glyph_width, NULL,
        formatted, sizeof(formatted));
    ui_label_update(label, formatted);
    return result;
}
void ui_page_show(lv_obj_t *page) {
    lv_obj_t *parent = lv_obj_get_parent(page);
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (child == page) lv_obj_remove_flag(child, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
}
void ui_shell_init(ui_shell_t *s) {
    lv_obj_t *screen = lv_screen_active();
    ui_theme_screen(screen);
    s->content = ui_page_create(screen);
    lv_obj_set_pos(s->content, 0, 0);
    lv_obj_remove_flag(s->content, LV_OBJ_FLAG_HIDDEN);
}
