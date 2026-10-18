#pragma once
#include "services/lightboard/lightboard_store.h"

typedef bool (*lb_strip_inflate_t)(void *, const uint8_t *, size_t, uint8_t *, size_t);
// Decode exactly one independent default strip. Inflate must consume all input
// and produce exactly output_length, rejecting truncation, trailing data and overrun.
bool lb_strip_decode(const lb_snapshot_t *, unsigned layer, unsigned strip,
                     lb_strip_inflate_t, void *context, uint8_t *output, size_t capacity,
                     size_t *output_length);
