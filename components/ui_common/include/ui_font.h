#pragma once
#include "lvgl.h"
LV_FONT_DECLARE(passport_font_16);
// Adapter for text_layout; callers set LVGL letter spacing to zero and disable extra wrapping.
int ui_font_glyph_width(void *context, uint32_t codepoint);
