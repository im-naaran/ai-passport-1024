#pragma once
#include <stddef.h>
#include "mode.h"

typedef struct {
    const mode_t *modes, *settings;
    size_t count, index;
    uint32_t return_id;
    bool settings_active;
} navigation_t;
bool navigation_init(navigation_t *, const mode_t *, size_t count, const mode_t *settings);
const mode_t *navigation_active(const navigation_t *);
bool navigation_activate(navigation_t *, uint32_t mode_id);
bool navigation_key(navigation_t *, mode_key_t, bool long_press, int64_t now_us);
bool navigation_reconcile_active(navigation_t *);
void navigation_render(const navigation_t *);
