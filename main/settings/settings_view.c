#include "settings_view.h"
#include "ui_theme.h"

#include <stdio.h>

enum { SETTINGS_ROW_COUNT = 4, SETTINGS_LIST_ROW_COUNT = 3, SETTINGS_ROW_STEP = 52, SETTINGS_SURFACE_H = 232 };

static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static void visible(lv_obj_t *object, bool show) {
    if (show) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static void place(lv_obj_t *object, int x, int y, int width, int height) {
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
}

static void tone(lv_obj_t *object, ui_color_role_t role) {
    lv_obj_set_style_text_color(object, ui_color(role), 0);
}

static void render_list(settings_view_t *view, const settings_page_t *page) {
    static const char *names[SETTINGS_LIST_ROW_COUNT] = {
        "Wi-Fi 热点", "屏幕亮度", "返回"};
    char values[SETTINGS_ROW_COUNT][24] = {{0}};
    char rows[SETTINGS_ROW_COUNT][48] = {{0}};
    settings_hotspot_status_t hotspot = settings_page_hotspot_status(page);
    const char *wifi = "进入设置";
    if (hotspot.state == SETTINGS_HOTSPOT_STARTING) wifi = "启动中";
    else if (hotspot.state == SETTINGS_HOTSPOT_WAITING_CLIENT ||
             hotspot.state == SETTINGS_HOTSPOT_CLIENT_CONNECTED) wifi = "配置中";
    else if (hotspot.state == SETTINGS_HOTSPOT_STOPPING) wifi = "关闭中";
    else if (hotspot.state == SETTINGS_HOTSPOT_ERROR) wifi = "启动失败";
    snprintf(values[0], sizeof(values[0]), "%s", wifi);
    snprintf(values[1], sizeof(values[1]), "%u%%",
             page->config->value.brightness_percent);

    visible(view->rows[3], false);
    char battery[8];
    bool battery_valid = page->battery_soc >= 0 && page->battery_soc <= 100;
    if (battery_valid) snprintf(battery, sizeof(battery), "%d%%", page->battery_soc);
    else snprintf(battery, sizeof(battery), "--%%");
    visible(view->battery, true);
    ui_label_update(view->battery, battery);
    tone(view->battery, !battery_valid ? UI_COLOR_ROLE_TEXT_MUTED :
         page->battery_soc <= 15 ? UI_COLOR_ROLE_DANGER :
         page->battery_soc <= 30 ? UI_COLOR_ROLE_WARNING : UI_COLOR_ROLE_TEXT_PRIMARY);

    visible(view->divider, false);
    visible(view->highlight, page->cursor < SETTINGS_LIST_ROW_COUNT);
    place(view->highlight, 5, 5 + (int)page->cursor * SETTINGS_ROW_STEP, 214, 46);
    lv_obj_set_style_bg_color(view->highlight,
                              ui_color(UI_COLOR_ROLE_SURFACE_ACTIVE), 0);
    lv_obj_set_style_border_width(view->highlight, 3, 0);
    for (size_t i = 0; i < SETTINGS_LIST_ROW_COUNT; ++i) {
        snprintf(rows[i], sizeof(rows[i]), "%s  %s", names[i], values[i]);
        visible(view->rows[i], true);
        place(view->rows[i], 17, 16 + (int)i * SETTINGS_ROW_STEP, 190, 26);
        lv_obj_set_style_text_align(view->rows[i], LV_TEXT_ALIGN_LEFT, 0);
        ui_label_update(view->rows[i], rows[i]);
        tone(view->rows[i], page->cursor == i ? UI_COLOR_ROLE_TEXT_PRIMARY :
                                               UI_COLOR_ROLE_TEXT_SECONDARY);
    }
}

static void render_hotspot(settings_view_t *view, const settings_page_t *page) {
    settings_hotspot_status_t status = settings_page_hotspot_status(page);
    char connect[64];
    char address[48];
    snprintf(connect, sizeof(connect), "请先连接 %s",
             status.ssid[0] ? status.ssid : "AI-Passport-....");
    snprintf(address, sizeof(address), "访问 %s",
             status.address && status.address[0] ? status.address : "192.168.4.1");
    visible(view->highlight, true);
    place(view->highlight, 0, 0, 3, SETTINGS_SURFACE_H);
    lv_obj_set_style_border_width(view->highlight, 0, 0);
    lv_obj_set_style_bg_color(view->highlight, ui_color(UI_COLOR_ROLE_ACCENT), 0);
    visible(view->divider, false);
    const int y[] = {28, 82, 148};
    const char *text[] = {
        connect,
        address,
        settings_page_hotspot_state_text(status),
    };
    for (size_t i = 0; i < 3; ++i) {
        visible(view->rows[i], true);
        place(view->rows[i], 4, y[i], 216, 28);
        lv_obj_set_style_text_align(view->rows[i], LV_TEXT_ALIGN_CENTER, 0);
        ui_label_update(view->rows[i], text[i]);
        tone(view->rows[i], i == 0 ? UI_COLOR_ROLE_ACCENT :
             (i == 2 && status.state == SETTINGS_HOTSPOT_ERROR) ?
                 UI_COLOR_ROLE_DANGER : UI_COLOR_ROLE_TEXT_PRIMARY);
    }
    visible(view->rows[3], false);
}

static void render_editor(settings_view_t *view, const settings_page_t *page) {
    const uint8_t *values = settings_brightness_percent;
    const size_t count = 5;
    const uint8_t saved = page->config->value.brightness_percent;
    const bool valid = page->draft_index < count;
    char draft[24] = "--";
    char position[24] = "-- / --";
    char saved_text[40] = "已保存：--";
    if (valid) {
        snprintf(draft, sizeof(draft), "%u%%", values[page->draft_index]);
        snprintf(position, sizeof(position), "%u / %u",
                 (unsigned)page->draft_index + 1, (unsigned)count);
    }
    char saved_value[24];
    snprintf(saved_value, sizeof(saved_value), "%u%%", saved);
    snprintf(saved_text, sizeof(saved_text), "已保存：%s", saved_value);

    visible(view->highlight, true);
    place(view->highlight, 0, 0, 3, SETTINGS_SURFACE_H);
    lv_obj_set_style_border_width(view->highlight, 0, 0);
    lv_obj_set_style_bg_color(view->highlight, ui_color(UI_COLOR_ROLE_ACCENT), 0);
    visible(view->divider, true);
    const char *texts[] = {"屏幕亮度",
                           draft, position, saved_text};
    const int y[] = {10, 70, 112, 158};
    for (size_t i = 0; i < SETTINGS_ROW_COUNT; ++i) {
        visible(view->rows[i], true);
        place(view->rows[i], 12, y[i], 200, 28);
        lv_obj_set_style_text_align(view->rows[i], LV_TEXT_ALIGN_CENTER, 0);
        ui_label_update(view->rows[i], texts[i]);
        tone(view->rows[i], i == 1 ? UI_COLOR_ROLE_TEXT_PRIMARY :
                                     UI_COLOR_ROLE_TEXT_SECONDARY);
    }
}

static void render_footer(settings_view_t *view, const settings_page_t *page) {
    static const char *context[SETTINGS_LIST_ROW_COUNT] = {
        "创建热点并用手机配置工牌",
        "调整屏幕背光亮度", "返回上一模块"};
    settings_feedback_t feedback = settings_page_feedback(page, view->now_us);
    const char *text = "";
    ui_color_role_t role = UI_COLOR_ROLE_TEXT_MUTED;
    if (feedback == SETTINGS_FEEDBACK_UNAVAILABLE) {
        text = "热点启动失败，请重试";
        role = UI_COLOR_ROLE_WARNING;
    } else if (feedback == SETTINGS_FEEDBACK_MODE_SAVE_FAILED) {
        text = "模式保存失败";
        role = UI_COLOR_ROLE_DANGER;
    } else if (feedback == SETTINGS_FEEDBACK_SAVE_FAILED) {
        text = "保存失败，请重试";
        role = UI_COLOR_ROLE_DANGER;
    } else if (page->page == SETTINGS_LIST && page->cursor < SETTINGS_LIST_ROW_COUNT) {
        text = context[page->cursor];
    } else if (page->page == SETTINGS_WIFI_CONFIG) {
        text = "按任意键退出 Wi-Fi 设置";
    } else {
        text = "上下选择  确认保存";
    }
    ui_label_update(view->footer_text, text);
    tone(view->footer_text, role);
}

static void render(void *context) {
    settings_page_t *page = context;
    settings_view_t *view = page->view;
    ui_page_show(view->root);
    visible(view->title, true);
    if (page->page != SETTINGS_LIST) visible(view->battery, false);
    if (page->page == SETTINGS_LIST) render_list(view, page);
    else if (page->page == SETTINGS_WIFI_CONFIG) render_hotspot(view, page);
    else render_editor(view, page);
    render_footer(view, page);
}

void settings_view_init(settings_view_t *view, settings_page_t *page,
                        mode_t *mode, lv_obj_t *parent) {
    view->root = ui_page_create(parent);
    view->title = ui_label_create(view->root, 12, 10, 152, 24);
    ui_label_update(view->title, "设置");
    view->battery = ui_label_create(view->root, 176, 10, 52, 24);
    lv_obj_set_style_text_align(view->battery, LV_TEXT_ALIGN_RIGHT, 0);
    view->surface = plain(view->root, 8, 44, 224, SETTINGS_SURFACE_H);
    ui_theme_surface(view->surface);
    view->highlight = plain(view->surface, 5, 5, 214, 46);
    lv_obj_set_style_bg_opa(view->highlight, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(view->highlight, ui_color(UI_COLOR_ROLE_ACCENT), 0);
    lv_obj_set_style_border_side(view->highlight, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(view->highlight, UI_RADIUS_SM, 0);
    for (size_t i = 0; i < SETTINGS_ROW_COUNT; ++i)
        view->rows[i] = ui_label_create(view->surface, 17,
            16 + (int)i * SETTINGS_ROW_STEP, 190, 26);
    view->divider = plain(view->surface, 12, 46, 200, 1);
    lv_obj_set_style_bg_color(view->divider, ui_color(UI_COLOR_ROLE_BORDER), 0);
    lv_obj_set_style_bg_opa(view->divider, LV_OPA_COVER, 0);
    view->footer = plain(view->root, 8, 282, 224, 30);
    lv_obj_set_style_bg_color(view->footer,
                              ui_color(UI_COLOR_ROLE_SURFACE_ACTIVE), 0);
    lv_obj_set_style_bg_opa(view->footer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(view->footer, UI_RADIUS_SM, 0);
    view->footer_text = ui_label_create(view->footer, 8, 3, 208, 24);
    lv_obj_set_style_text_align(view->footer_text, LV_TEXT_ALIGN_CENTER, 0);
    page->view = view;
    mode->render = render;
}
