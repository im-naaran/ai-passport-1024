#include "ui_font.h"
int ui_font_glyph_width(void *context, uint32_t codepoint) {
    const lv_font_t *font = context ? context : &passport_font_16;
    lv_font_glyph_dsc_t glyph;
    if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder) return -1;
    return glyph.adv_w;
}
