#include "badge_mode.h"

mode_t badge_mode_descriptor(badge_mode_t *badge, badge_store_t *store) {
    *badge = (badge_mode_t){.store = store};
    return (mode_t){.id = 4, .name = "工牌", .context = badge};
}

const badge_profile_snapshot_t *badge_mode_snapshot(const badge_mode_t *badge) {
    return badge && badge->store ? badge_store_snapshot(badge->store) : NULL;
}
