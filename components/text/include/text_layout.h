#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Return advance in pixels; negative means a missing glyph. No kerning/letter spacing.
typedef int (*text_glyph_width_fn)(void *context, uint32_t codepoint);
typedef struct { size_t bytes, lines; bool truncated, replaced; } text_layout_result_t;
// Output is always NUL-terminated when capacity > 0. Input/output must not overlap.
// Newlines consume rows; unsupported/invalid codepoints become '?'.
text_layout_result_t text_layout_format(const char *input, size_t length, bool pre_truncated,
    unsigned width, unsigned max_lines, text_glyph_width_fn glyph, void *context,
    char *output, size_t capacity);
