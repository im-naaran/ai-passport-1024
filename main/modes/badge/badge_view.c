#include "badge_view.h"
#include "ui_theme.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <string.h>

static void render(void *context) {
    badge_mode_t *badge = context;
    badge_view_t *view = badge->view;
    const badge_profile_snapshot_t *snapshot = badge_mode_snapshot(badge);
    ui_page_show(view->root);
    if (!snapshot) return;
    bool image_changed = !view->has_rendered || view->rendered_image != snapshot->image ||
        view->rendered_sequence != snapshot->sequence || view->rendered_default != snapshot->is_default;
    if (image_changed) {
        if (view->has_rendered) lv_image_cache_drop(&view->image_dsc);
        // The descriptor and mapped/default pixels outlive LVGL; no full image copy enters the heap.
        view->image_dsc = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                .w = snapshot->width, .h = snapshot->height, .stride = snapshot->stride},
            .data_size = (uint32_t)snapshot->stride * snapshot->height,
            .data = snapshot->image,
        };
        lv_image_set_src(view->photo, &view->image_dsc);
        view->rendered_image = snapshot->image;
        view->rendered_sequence = snapshot->sequence;
        view->rendered_default = snapshot->is_default;
        view->has_rendered = true;
    }
    ui_label_format(view->name, snapshot->name, false, 1);
    // The bio object remains resident; an empty value only clears its text.
    ui_label_format(view->bio, snapshot->bio ? snapshot->bio : "", false, 2);
}

void badge_view_init(badge_view_t *view, badge_mode_t *badge, mode_t *mode, lv_obj_t *parent) {
    memset(view, 0, sizeof(*view));
    view->root = ui_page_create(parent);
    view->photo = lv_image_create(view->root);
    lv_obj_set_pos(view->photo, 20, 8);
    lv_obj_set_size(view->photo, BADGE_IMAGE_WIDTH, BADGE_IMAGE_HEIGHT);
    lv_obj_clear_flag(view->photo, LV_OBJ_FLAG_CLICKABLE);
    view->name = ui_label_create(view->root, 12, 216, 216, 30);
    lv_obj_set_style_text_align(view->name, LV_TEXT_ALIGN_CENTER, 0);
    ui_theme_label(view->name, UI_TEXT_PRIMARY);
    view->bio = ui_label_create(view->root, 12, 250, 216, 54);
    lv_obj_set_style_text_align(view->bio, LV_TEXT_ALIGN_CENTER, 0);
    ui_theme_label(view->bio, UI_TEXT_SECONDARY);
    lv_obj_set_style_text_line_space(view->bio, 6, 0);
    badge->view = view;
    mode->render = render;
}
