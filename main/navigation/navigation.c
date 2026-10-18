#include "navigation.h"

static bool available(const mode_t *mode) {
    return !mode->is_available || mode->is_available(mode->context);
}

const mode_t *navigation_active(const navigation_t *n) {
    return n->settings_active ? n->settings : &n->modes[n->index];
}
static void enter(const mode_t *mode) { if (mode->enter) mode->enter(mode->context); }
static void leave(const mode_t *mode) { if (mode->leave) mode->leave(mode->context); }
static size_t first_available(const navigation_t *n) {
    for (size_t i = 0; i < n->count; ++i) if (available(&n->modes[i])) return i;
    return n->count;
}
static size_t return_index(const navigation_t *n) {
    for (size_t i = 0; i < n->count; ++i)
        if (n->modes[i].id == n->return_id && available(&n->modes[i])) return i;
    return first_available(n);
}
static size_t adjacent_available(const navigation_t *n, size_t from, mode_key_t key,
                                 bool allow_same) {
    // The bounded scan prevents unavailable modes from causing an endless cycle.
    for (size_t step = 1; step <= n->count; ++step) {
        size_t candidate = key == MODE_KEY_UP ?
            (from + n->count - (step % n->count)) % n->count : (from + step) % n->count;
        if ((!allow_same && candidate == from) || !available(&n->modes[candidate])) continue;
        return candidate;
    }
    return n->count;
}
bool navigation_init(navigation_t *n, const mode_t *modes, size_t count, const mode_t *settings) {
    if (!n || !modes || !count || !settings) return false;
    for (size_t i = 0; i < count; ++i) {
        if (modes[i].id == settings->id) return false;
        for (size_t j = 0; j < i; ++j) if (modes[i].id == modes[j].id) return false;
    }
    *n = (navigation_t){.modes = modes, .count = count, .settings = settings};
    for (size_t i = 0; i < count; ++i) if (modes[i].init) modes[i].init(modes[i].context);
    if (settings->init) settings->init(settings->context);
    n->index = first_available(n);
    if (n->index == n->count) return false;
    n->return_id = modes[n->index].id;
    enter(navigation_active(n));
    return true;
}
bool navigation_activate(navigation_t *n, uint32_t mode_id) {
    if (!n || !n->modes || !n->count) return false;
    size_t target = n->count;
    for (size_t i = 0; i < n->count; ++i)
        if (n->modes[i].id == mode_id && available(&n->modes[i])) { target = i; break; }
    if (target == n->count) return false;
    if (!n->settings_active && target == n->index) return true;
    // System-driven switches reuse page lifecycle hooks instead of mutating page-owned state directly.
    leave(navigation_active(n));
    n->settings_active = false;
    n->index = target;
    enter(navigation_active(n));
    return true;
}
bool navigation_key(navigation_t *n, mode_key_t key, bool long_press, int64_t now) {
    if (!n || !n->modes || !n->count || !n->settings ||
        (unsigned)key > MODE_KEY_OK)
        return false;
    const mode_t *active = navigation_active(n);
    if (n->settings_active) {
        if (long_press || !active->handle_key) return false;
        if (active->handle_key(active->context, key, now) == MODE_RETURN) {
            size_t target = return_index(n);
            if (target == n->count) return false;
            leave(active);
            n->index = target;
            n->settings_active = false;
            enter(navigation_active(n));
        }
        return true;
    }

    if (long_press) {
        // Directional long presses are intentionally consumed: display navigation is click-only.
        if (key != MODE_KEY_OK) return false;
        leave(active);
        n->return_id = active->id;
        n->settings_active = true;
        // Entering settings resets to its list and discards any unfinished editor draft.
        enter(navigation_active(n));
        return true;
    }

    if (key == MODE_KEY_OK) return false;
    size_t target = adjacent_available(n, n->index, key, false);
    // With one available display item, directional clicks are true lifecycle no-ops.
    if (target == n->count) return false;
    leave(active);
    n->index = target;
    enter(navigation_active(n));
    return true;
}

bool navigation_reconcile_active(navigation_t *n) {
    if (!n || !n->modes || !n->count || n->settings_active ||
        n->index >= n->count || available(&n->modes[n->index]))
        return false;
    size_t target = first_available(n);
    if (target == n->count) return false;
    // Stable descriptors remain in place; reconciliation only changes an invalid active item.
    leave(&n->modes[n->index]);
    n->index = target;
    enter(navigation_active(n));
    return true;
}
void navigation_render(const navigation_t *n) {
    const mode_t *active = navigation_active(n);
    if (active->render) active->render(active->context);
}
