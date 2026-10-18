#include "display_render_policy.h"

void display_render_policy_init(display_render_policy_t *policy, bool pending) {
    policy->pending = pending;
}

void display_render_policy_request(display_render_policy_t *policy) {
    policy->pending = true;
}

bool display_render_policy_should_render(const display_render_policy_t *policy) {
    // A continuously lit static page still redraws only after an effective change.
    return policy->pending;
}

void display_render_policy_mark_rendered(display_render_policy_t *policy) {
    policy->pending = false;
}
