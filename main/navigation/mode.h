#pragma once
#include <stdbool.h>
#include <stdint.h>

// Semantic input only; the BSP remains the sole owner of debounce and hold timing.
typedef enum { MODE_KEY_UP, MODE_KEY_DOWN, MODE_KEY_OK } mode_key_t;
typedef enum { MODE_STAY, MODE_RETURN } mode_action_t;
typedef struct {
    uint32_t id;
    const char *name;
    void *context;
    void (*init)(void *);
    void (*enter)(void *);
    void (*leave)(void *);
    mode_action_t (*handle_key)(void *, mode_key_t, int64_t now_us);
    void (*render)(void *); // Called only for the active mode, under the application's LVGL lock.
    bool (*is_available)(void *); // NULL means the mode is always available.
} mode_t;
