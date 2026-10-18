// components/bsp/src/bsp_button.c
// 移植自 trae_card/components/platform/platform_esp32/src/btn_iot_button.c
#include "bsp_button.h"
#include "bsp_pins.h"
#include "button_gesture.h"
#include <stdint.h>
#include <string.h>
#include "iot_button.h"
#include "button_adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bsp_btn";

static const uint16_t BTN_MV[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;

static button_handle_t s_btn[BSP_BTN_COUNT];
static bsp_btn_cb_t    s_cb;
static void           *s_user;

// ADC1 是 unit 级独占资源:iot_button 与 bsp_button_read_mv() 必须共用同一个 oneshot
// 句柄。谁第二个调 adc_oneshot_new_unit() 谁就拿到 "adc1 is already in use"。
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;

// 电压读取的衰减档必须与 button 组件内部的 ADC_BUTTON_ATTEN 一致 —— 通道只被配置一次
// (由组件在 iot_button_new_adc_device() 里下发),两边对不上会让读数与按键阈值错位。
// managed_components/espressif__button/button_adc.c:26 在 C3 上取 ADC_ATTEN_DB_6+1。
#define BSP_BTN_ATTEN  ADC_ATTEN_DB_12       // 量程约 0~3100mV,覆盖松开态

static button_gesture_t s_gestures[BSP_BTN_COUNT];
static esp_timer_handle_t s_gesture_timer;
static uint8_t s_pressed_mask;
static bool s_gesture_timer_running;

static void apply_timer_action(button_timer_action_t action) {
    if (action == BUTTON_TIMER_START && s_gesture_timer && !s_gesture_timer_running) {
        esp_err_t err = esp_timer_start_periodic(s_gesture_timer, 10000);
        if (err == ESP_OK) s_gesture_timer_running = true;
        else ESP_LOGE(TAG, "手势 timer 启动失败 (%s)", esp_err_to_name(err));
    } else if (action == BUTTON_TIMER_STOP && s_gesture_timer_running) {
        esp_err_t err = esp_timer_stop(s_gesture_timer);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) s_gesture_timer_running = false;
        else ESP_LOGE(TAG, "手势 timer 停止失败 (%s)", esp_err_to_name(err));
    }
}

static void on_edge(void *arg, void *user, button_edge_t edge) {
    (void)arg;
    int index = (int)(intptr_t)user;
    uint8_t key_mask = (uint8_t)(1u << index);
    button_gesture_event_t event = button_gesture_update(&s_gestures[index], edge,
                                                          (uint64_t)(esp_timer_get_time() / 1000));
    if (edge == BUTTON_EDGE_DOWN)
        apply_timer_action(button_pressed_mask_update(&s_pressed_mask, key_mask, true));
    if (s_cb && (edge == BUTTON_EDGE_DOWN || edge == BUTTON_EDGE_UP))
        s_cb((bsp_btn_t)index, edge == BUTTON_EDGE_DOWN ? BSP_BTN_EVENT_DOWN : BSP_BTN_EVENT_UP, s_user);
    if (s_cb && event != BUTTON_GESTURE_NONE) {
        s_cb((bsp_btn_t)index, event == BUTTON_GESTURE_LONG ? BSP_BTN_LONG : BSP_BTN_CLICK, s_user);
    }
    // The last UP updates gesture state and emits its result before stopping the shared timer.
    if (edge == BUTTON_EDGE_UP)
        apply_timer_action(button_pressed_mask_update(&s_pressed_mask, key_mask, false));
}
static void cb_down(void *a, void *u) { on_edge(a, u, BUTTON_EDGE_DOWN); }
static void cb_up(void *a, void *u) { on_edge(a, u, BUTTON_EDGE_UP); }
static void gesture_tick(void *arg) {
    (void)arg;
    // Same ESP_TIMER_TASK as the button component: gesture state needs no cross-task lock.
    for (int i = 0; i < BSP_BTN_COUNT; ++i)
        if (s_pressed_mask & (1u << i)) on_edge(NULL, (void *)(intptr_t)i, BUTTON_EDGE_TICK);
}

static void cleanup(void) {
    s_cb = NULL;
    if (s_gesture_timer) {
        if (s_gesture_timer_running) esp_timer_stop(s_gesture_timer);
        esp_timer_delete(s_gesture_timer); s_gesture_timer = NULL;
    }
    s_gesture_timer_running = false;
    s_pressed_mask = 0;
    for (int i = 0; i < BSP_BTN_COUNT; ++i) {
        if (s_btn[i]) { iot_button_delete(s_btn[i]); s_btn[i] = NULL; }
    }
    if (s_cali) { adc_cali_delete_scheme_curve_fitting(s_cali); s_cali = NULL; }
    if (s_adc) { adc_oneshot_del_unit(s_adc); s_adc = NULL; }
    memset(s_gestures, 0, sizeof(s_gestures));
}

esp_err_t bsp_button_init(bsp_btn_cb_t cb, void *user) {
    if (s_adc) return ESP_ERR_INVALID_STATE;
    s_cb = cb; s_user = user;

    // 先由 BSP 建 unit,再把句柄交给 button 组件(button_adc.h:adc_handle 非 NULL 即复用),
    // 这样本文件的 bsp_button_read_mv() 也能读同一路 ADC。
    const adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = BSP_BTN_ADC_UNIT };
    esp_err_t ae = adc_oneshot_new_unit(&ucfg, &s_adc);
    if (ae != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit 创建失败 (%s)", esp_err_to_name(ae));
        s_adc = NULL;
        return ae;
    }

    const esp_timer_create_args_t timer = {
        .callback = gesture_tick, .dispatch_method = ESP_TIMER_TASK, .name = "key_gesture",
    };
    esp_err_t err = esp_timer_create(&timer, &s_gesture_timer);
    if (err != ESP_OK) { cleanup(); return err; }

    for (int i = 0; i < BSP_BTN_COUNT; i++) {
        const button_adc_config_t ac = {
            .adc_handle   = &s_adc,          // 复用上面这一个,别让组件自建
            .unit_id      = BSP_BTN_ADC_UNIT,
            .adc_channel  = BSP_BTN_ADC_CHANNEL,
            .button_index = i,
            .min          = BTN_MV[i][0],
            .max          = BTN_MV[i][1],
        };
        const button_config_t bc = { .long_press_time = 800, .short_press_time = 50 };
        esp_err_t e = iot_button_new_adc_device(&bc, &ac, &s_btn[i]);
        if (e != ESP_OK || !s_btn[i]) {
            ESP_LOGE(TAG, "按键 %d 创建失败 (%s) —— 检查 GPIO%d 的 ADC 配置与分压电阻",
                     i, esp_err_to_name(e), BSP_BTN_ADC_CHANNEL);
            cleanup();
            return e == ESP_OK ? ESP_FAIL : e;
        }
        void *idx = (void *)(intptr_t)i;
        // PRESS_UP occurs for every physical release, even during a multi-click sequence.
        e = iot_button_register_cb(s_btn[i], BUTTON_PRESS_DOWN, NULL, cb_down, idx);
        if (e == ESP_OK) e = iot_button_register_cb(s_btn[i], BUTTON_PRESS_UP, NULL, cb_up, idx);
        if (e != ESP_OK) { cleanup(); return e; }
    }

    // 通道已由组件配置好,这里只补一份校准句柄给 bsp_button_read_mv() 用。
    // 失败不致命:按键照常工作,只是读不出电压(标定分压电阻时才需要)。
    const adc_cali_curve_fitting_config_t cal = {
        .unit_id  = BSP_BTN_ADC_UNIT,
        .chan     = BSP_BTN_ADC_CHANNEL,
        .atten    = BSP_BTN_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC 校准创建失败,无法读取按键电压");
        s_cali = NULL;
    }

    // The 10 ms gesture timer runs only while at least one key is physically held.
    ESP_LOGI(TAG, "按键就绪:ADC1_CH%d 三键分压", BSP_BTN_ADC_CHANNEL);
    return ESP_OK;
}

int bsp_button_read_mv(void) {
    // 读的是 bsp_button_init() 建好、并与 iot_button 共用的那一路 ADC。
    // 单次采样与组件的按键轮询互不干扰(oneshot 内部自带锁)。
    if (!s_adc || !s_cali) return -1;

    int raw = 0, mv = 0;
    if (adc_oneshot_read(s_adc, BSP_BTN_ADC_CHANNEL, &raw) != ESP_OK) return -1;
    if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) return -1;
    return mv;
}
