#pragma once

#include "navigation/mode.h"
#include "services/badge/badge_store.h"

typedef struct badge_view badge_view_t;

typedef struct {
    badge_store_t *store;
    badge_view_t *view;
} badge_mode_t;

mode_t badge_mode_descriptor(badge_mode_t *mode, badge_store_t *store);
const badge_profile_snapshot_t *badge_mode_snapshot(const badge_mode_t *mode);
