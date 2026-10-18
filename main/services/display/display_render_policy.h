#pragma once

#include <stdbool.h>

typedef struct {
    bool pending;
} display_render_policy_t;

void display_render_policy_init(display_render_policy_t *, bool pending);
void display_render_policy_request(display_render_policy_t *);
bool display_render_policy_should_render(const display_render_policy_t *);
void display_render_policy_mark_rendered(display_render_policy_t *);
