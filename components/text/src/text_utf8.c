#include "text_utf8.h"
#include <stdint.h>
#include <string.h>

static size_t span(const uint8_t *p, size_t length, bool *incomplete) {
    *incomplete = false;
    if (!length) return 0;
    size_t width = p[0] < 0x80 && p[0] != 0 ? 1 :
                   p[0] >= 0xC2 && p[0] <= 0xDF ? 2 :
                   p[0] >= 0xE0 && p[0] <= 0xEF ? 3 :
                   p[0] >= 0xF0 && p[0] <= 0xF4 ? 4 : 0;
    size_t available = length < width ? length : width;
    for (size_t i = 1; i < available; ++i) if ((p[i] & 0xC0) != 0x80) return 0;
    if (width > 1 && available > 1 &&
        ((p[0] == 0xE0 && p[1] < 0xA0) || (p[0] == 0xED && p[1] >= 0xA0) ||
         (p[0] == 0xF0 && p[1] < 0x90) || (p[0] == 0xF4 && p[1] >= 0x90))) return 0;
    if (width > length) { *incomplete = true; return 0; }
    return width;
}
size_t text_utf8_decode(const char *text, size_t length, uint32_t *codepoint, bool *valid) {
    bool incomplete;
    size_t width = text ? span((const uint8_t *)text, length, &incomplete) : 0;
    *valid = width != 0;
    if (!width) { *codepoint = '?'; return length ? 1 : 0; }
    const uint8_t *p = (const uint8_t *)text;
    uint32_t cp = width == 1 ? p[0] : p[0] & ((1u << (7 - width)) - 1);
    for (size_t i = 1; i < width; ++i) cp = (cp << 6) | (p[i] & 0x3f);
    *codepoint = cp;
    return width;
}
size_t text_utf8_clean(char *text, size_t length, bool truncated) {
    if (!text) return 0;
    size_t read = 0, write = 0;
    while (read < length) {
        bool incomplete;
        size_t width = span((const uint8_t *)text + read, length - read, &incomplete);
        if (incomplete && truncated) break;
        if (!width) { text[write++] = '?'; ++read; }
        else { memmove(text + write, text + read, width); write += width; read += width; }
    }
    text[write] = '\0';
    return write;
}
