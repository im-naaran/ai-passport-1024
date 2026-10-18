#include "text_layout.h"
#include "text_utf8.h"
#include <string.h>

text_layout_result_t text_layout_format(const char *input, size_t length, bool pre_truncated,
    unsigned width, unsigned max_lines, text_glyph_width_fn glyph, void *context,
    char *output, size_t capacity) {
    text_layout_result_t result = {0};
    if (!output || !capacity) { result.truncated = length || pre_truncated; return result; }
    output[0] = '\0';
    if (!glyph || !width || !max_lines || (!input && length)) {
        result.truncated = length || pre_truncated; return result;
    }
    size_t read = 0, end = 0, line_start = 0;
    unsigned row = 1, pixels = 0;
    while (read < length) {
        uint32_t cp; bool valid;
        size_t bytes = text_utf8_decode(input + read, length - read, &cp, &valid);
        if (cp == '\r' || cp == '\n') {
            if (cp == '\r' && read + bytes < length && input[read + bytes] == '\n') ++bytes;
            if (row == max_lines || end + 1 >= capacity) break;
            output[end++] = '\n'; line_start = end; pixels = 0; ++row; read += bytes;
            continue;
        }
        int advance = glyph(context, cp);
        bool replace = !valid || advance < 0;
        if (replace) { cp = '?'; advance = glyph(context, cp); result.replaced = true; }
        if (advance < 0) break;
        if ((unsigned)advance > width - pixels) {
            if (!pixels || row == max_lines || end + 1 >= capacity) break;
            output[end++] = '\n'; line_start = end; pixels = 0; ++row;
            continue; // Retry this complete codepoint on the next row.
        }
        size_t written = replace ? 1 : bytes;
        if (written >= capacity - end) break;
        if (replace) output[end] = '?';
        else memcpy(output + end, input + read, written);
        end += written; pixels += (unsigned)advance; read += bytes;
    }
    result.truncated = read < length || pre_truncated;
    if (result.truncated) {
        int dot = glyph(context, '.');
        unsigned dots = dot >= 0 && (unsigned)dot <= width / 3 ? (unsigned)dot * 3 : width;
        bool can_mark = dot >= 0 && (unsigned)dot <= width / 3;
        // Only the last visible row gives up text for '...'; never split a UTF-8 glyph.
        while (end > line_start && (!can_mark || pixels > width - dots || capacity - end < 4)) {
            size_t start = end - 1;
            while (start > line_start && ((unsigned char)output[start] & 0xc0) == 0x80) --start;
            uint32_t cp; bool valid;
            text_utf8_decode(output + start, end - start, &cp, &valid);
            int advance = glyph(context, cp);
            if (advance > 0) pixels -= (unsigned)advance;
            end = start;
        }
        if (can_mark && capacity - end >= 4) { memcpy(output + end, "...", 3); end += 3; }
    }
    output[end] = '\0';
    result.bytes = end; result.lines = end ? row : 0;
    return result;
}
