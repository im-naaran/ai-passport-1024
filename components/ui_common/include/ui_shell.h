#pragma once
#include "ui_font.h"
#include "text_layout.h"
typedef struct {
    lv_obj_t *content;
} ui_shell_t;
// All calls require the BSP LVGL lock. Objects live for the application lifetime.
void ui_shell_init(ui_shell_t *);
lv_obj_t *ui_page_create(lv_obj_t *parent);
void ui_page_show(lv_obj_t *page);
lv_obj_t *ui_label_create(lv_obj_t *parent, int x, int y, int width, int height);
void ui_label_update(lv_obj_t *, const char *text);
text_layout_result_t ui_label_format(lv_obj_t *, const char *text, bool truncated, unsigned lines);
