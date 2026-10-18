#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Decode one bounded codepoint; invalid input consumes one byte and returns '?'.
size_t text_utf8_decode(const char *text, size_t length, uint32_t *codepoint, bool *valid);

// Cleans a bounded buffer in place, never expands it. Caller reserves byte length+1.
// Invalid bytes/NUL become '?'; a cut trailing codepoint is dropped on truncation.
size_t text_utf8_clean(char *text, size_t length, bool truncated);
