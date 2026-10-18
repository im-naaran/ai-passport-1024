#include "badge_default.h"

/* Neutral placeholder until the final portrait asset is supplied; it keeps the production path
 * identical to a user RGB565 profile without enabling a decoder or allocating an 80 KB buffer. */
static const uint16_t s_default_pixels[BADGE_IMAGE_WIDTH * BADGE_IMAGE_HEIGHT] = {
    [0 ... BADGE_IMAGE_WIDTH * BADGE_IMAGE_HEIGHT - 1] = 0x3186,
};

badge_profile_snapshot_t badge_default_profile(void) {
    return (badge_profile_snapshot_t){
        .name = "AI Passport",
        .image = (const uint8_t *)s_default_pixels,
        .width = BADGE_IMAGE_WIDTH,
        .height = BADGE_IMAGE_HEIGHT,
        .stride = BADGE_IMAGE_STRIDE,
        .image_format = BADGE_IMAGE_FORMAT_RGB565_LE,
        .sequence = 0,
        .is_default = true,
        .bio = BADGE_DEFAULT_TAGLINE,
        .shape = BADGE_PHOTO_SHAPE_SQUARE,
    };
}
