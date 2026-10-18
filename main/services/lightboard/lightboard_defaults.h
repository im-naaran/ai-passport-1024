#pragma once
#include <stddef.h>
#include <stdint.h>
#include "lightboard_record.h"

// Embedded compressed default bundle, independent of user-configured raw A8 banks.
const uint8_t *lightboard_defaults_data(size_t *length);
enum { LB_DEFAULT_STRIP_BYTES = 3120 };
bool lb_defaults_decode(const uint8_t *blob, size_t length, lb_bundle_t *output);
// Return one independently deflated/sub-filtered strip; caller bounds the output buffer.
bool lb_default_strip(const uint8_t *plane, size_t length, uint16_t width, uint16_t height,
                      unsigned strip, const uint8_t **compressed, size_t *compressed_length,
                      size_t *raw_length);
