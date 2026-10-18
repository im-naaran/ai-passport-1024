#include "button_gesture.h"

button_gesture_event_t button_gesture_update(button_gesture_t *s, button_edge_t edge, uint64_t now_ms) {
    if (!s) return BUTTON_GESTURE_NONE;
    if (edge == BUTTON_EDGE_DOWN) {
        if (!s->pressed) { s->pressed = true; s->long_sent = false; s->pressed_at_ms = now_ms; }
        return BUTTON_GESTURE_NONE;
    }
    if (!s->pressed) return BUTTON_GESTURE_NONE;
    button_gesture_event_t event = BUTTON_GESTURE_NONE;
    // Own timing: button 4.2.0 does not emit LONG_START for a held repeat press.
    if (!s->long_sent && now_ms - s->pressed_at_ms >= BUTTON_LONG_PRESS_MS) {
        s->long_sent = true;
        event = BUTTON_GESTURE_LONG;
    }
    if (edge == BUTTON_EDGE_UP) {
        s->pressed = false;
        if (!s->long_sent && now_ms - s->pressed_at_ms >= BUTTON_SHORT_PRESS_MS)
            event = BUTTON_GESTURE_CLICK;
    }
    return event;
}

button_timer_action_t button_pressed_mask_update(uint8_t *mask, uint8_t key_mask, bool pressed) {
    if (!mask || !key_mask) return BUTTON_TIMER_UNCHANGED;
    uint8_t before = *mask;
    if (pressed) *mask |= key_mask;
    else *mask &= (uint8_t)~key_mask;
    if (!before && *mask) return BUTTON_TIMER_START;
    if (before && !*mask) return BUTTON_TIMER_STOP;
    return BUTTON_TIMER_UNCHANGED;
}
