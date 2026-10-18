#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BUTTON_SHORT_PRESS_MS 50u
#define BUTTON_LONG_PRESS_MS 800u

typedef enum { BUTTON_EDGE_DOWN, BUTTON_EDGE_UP, BUTTON_EDGE_TICK } button_edge_t;
typedef enum { BUTTON_GESTURE_NONE, BUTTON_GESTURE_CLICK, BUTTON_GESTURE_LONG } button_gesture_event_t;
typedef enum { BUTTON_TIMER_UNCHANGED, BUTTON_TIMER_START, BUTTON_TIMER_STOP } button_timer_action_t;
typedef struct { bool pressed; bool long_sent; uint64_t pressed_at_ms; } button_gesture_t;
button_gesture_event_t button_gesture_update(button_gesture_t *state, button_edge_t edge, uint64_t now_ms);
button_timer_action_t button_pressed_mask_update(uint8_t *pressed_mask, uint8_t key_mask, bool pressed);
