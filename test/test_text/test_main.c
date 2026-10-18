#include <unity.h>

#include "text_layout.h"
#include "text_utf8.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static int test_glyph_width(void *context, uint32_t codepoint) {
    (void)context;
    if (codepoint == '?' || codepoint < 0x80) return 1;
    if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) return 2;
    return -1;
}

static void test_utf8_clean_preserves_valid_and_bounds_invalid_input(void) {
    char valid[] = "A中";
    TEST_ASSERT_EQUAL(strlen("A中"), text_utf8_clean(valid, strlen(valid), false));
    TEST_ASSERT_EQUAL_STRING("A中", valid);

    char invalid[] = {'a', 0, (char)0xC0, (char)0xAF, 0};
    TEST_ASSERT_EQUAL(4, text_utf8_clean(invalid, 4, false));
    TEST_ASSERT_EQUAL_STRING("a???", invalid);

    char cut[] = {'a', (char)0xE4, (char)0xB8, 0};
    TEST_ASSERT_EQUAL(1, text_utf8_clean(cut, 3, true));
    TEST_ASSERT_EQUAL_STRING("a", cut);
}

static void test_layout_wraps_only_at_codepoint_boundaries(void) {
    char output[32];
    text_layout_result_t result = text_layout_format(
        "ab中c", strlen("ab中c"), false, 3, 2, test_glyph_width, NULL,
        output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("ab\n中c", output);
    TEST_ASSERT_EQUAL(2, result.lines);
    TEST_ASSERT_FALSE(result.truncated);
}

static void test_layout_replaces_missing_glyphs(void) {
    char output[32];
    text_layout_result_t result = text_layout_format(
        "A😀B", strlen("A😀B"), false, 8, 1, test_glyph_width, NULL,
        output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("A?B", output);
    TEST_ASSERT_TRUE(result.replaced);
    TEST_ASSERT_FALSE(result.truncated);
}

static void test_layout_reserves_last_row_for_ellipsis(void) {
    char output[8];
    text_layout_result_t result = text_layout_format(
        "abcdef", 6, false, 4, 1, test_glyph_width, NULL,
        output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("a...", output);
    TEST_ASSERT_TRUE(result.truncated);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_utf8_clean_preserves_valid_and_bounds_invalid_input);
    RUN_TEST(test_layout_wraps_only_at_codepoint_boundaries);
    RUN_TEST(test_layout_replaces_missing_glyphs);
    RUN_TEST(test_layout_reserves_last_row_for_ellipsis);
    return UNITY_END();
}
