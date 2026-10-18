#include "badge_http_server.h"
#include "services/lightboard/lightboard_defaults.h"

#ifdef ESP_PLATFORM
/* Link the source files directly into read-only application flash. This avoids a filesystem and
 * also avoids PlatformIO treating ESP-IDF's generated EMBED_FILES assembly as a SCons source. */
__asm__(
    ".section .rodata.badge_assets,\"a\",@progbits\n"
    ".balign 4\n.global index_html_start\nindex_html_start:\n"
    ".incbin \"" BADGE_INDEX_HTML_PATH "\"\n.global index_html_end\nindex_html_end:\n"
    ".balign 4\n.global style_css_start\nstyle_css_start:\n"
    ".incbin \"" BADGE_STYLE_CSS_PATH "\"\n.global style_css_end\nstyle_css_end:\n"
    ".balign 4\n.global badge_image_js_start\nbadge_image_js_start:\n"
    ".incbin \"" BADGE_IMAGE_JS_PATH "\"\n.global badge_image_js_end\nbadge_image_js_end:\n"
    ".balign 4\n.global app_js_start\napp_js_start:\n"
    ".incbin \"" BADGE_APP_JS_PATH "\"\n.global app_js_end\napp_js_end:\n"
    ".balign 4\n.global lightboard_font_start\nlightboard_font_start:\n"
    ".incbin \"" LIGHTBOARD_FONT_PATH "\"\n.global lightboard_font_end\nlightboard_font_end:\n"
    /* Retain the full OFL notice in standalone firmware distributions as well
     * as the source tree; it shares this retained read-only asset section. */
    ".balign 4\nlightboard_font_license:\n"
    ".incbin \"" LIGHTBOARD_FONT_LICENSE_PATH "\"\n"
    ".balign 4\n.global lightboard_charset_start\nlightboard_charset_start:\n"
    ".incbin \"" LIGHTBOARD_CHARSET_PATH "\"\n.global lightboard_charset_end\nlightboard_charset_end:\n"
    ".balign 4\n.global lightboard_layout_start\nlightboard_layout_start:\n"
    ".incbin \"" LIGHTBOARD_LAYOUT_PATH "\"\n.global lightboard_layout_end\nlightboard_layout_end:\n"
    ".balign 4\n.global lightboard_defaults_start\nlightboard_defaults_start:\n"
    ".incbin \"" LIGHTBOARD_DEFAULTS_PATH "\"\n.global lightboard_defaults_end\nlightboard_defaults_end:\n"
    ".balign 4\n.global lightboard_protocol_start\nlightboard_protocol_start:\n"
    ".incbin \"" LIGHTBOARD_PROTOCOL_PATH "\"\n.global lightboard_protocol_end\nlightboard_protocol_end:\n"
    ".balign 4\n.global lightboard_app_start\nlightboard_app_start:\n"
    ".incbin \"" LIGHTBOARD_APP_PATH "\"\n.global lightboard_app_end\nlightboard_app_end:\n"
    ".previous\n");

extern const uint8_t index_html_start[], index_html_end[];
extern const uint8_t style_css_start[], style_css_end[];
extern const uint8_t badge_image_js_start[], badge_image_js_end[];
extern const uint8_t app_js_start[], app_js_end[];
extern const uint8_t lightboard_font_start[], lightboard_font_end[];
extern const uint8_t lightboard_charset_start[], lightboard_charset_end[];
extern const uint8_t lightboard_layout_start[], lightboard_layout_end[];
extern const uint8_t lightboard_defaults_start[], lightboard_defaults_end[];
extern const uint8_t lightboard_protocol_start[], lightboard_protocol_end[];
extern const uint8_t lightboard_app_start[], lightboard_app_end[];

const uint8_t *lightboard_defaults_data(size_t *length) {
    if (length) *length = (size_t)(lightboard_defaults_end - lightboard_defaults_start);
    return lightboard_defaults_start;
}

static badge_http_asset_t asset(const uint8_t *start, const uint8_t *end,
                                const char *content_type) {
    return (badge_http_asset_t){.data = start, .length = (size_t)(end - start),
                                .content_type = content_type};
}

badge_http_assets_t badge_http_embedded_assets(void) {
    return (badge_http_assets_t){
        .index_html = asset(index_html_start, index_html_end, "text/html; charset=utf-8"),
        .style_css = asset(style_css_start, style_css_end, "text/css; charset=utf-8"),
        .image_js = asset(badge_image_js_start, badge_image_js_end,
                          "application/javascript; charset=utf-8"),
        .app_js = asset(app_js_start, app_js_end, "application/javascript; charset=utf-8"),
        .lightboard_protocol = asset(lightboard_protocol_start, lightboard_protocol_end, "application/javascript; charset=utf-8"),
        .lightboard_app = asset(lightboard_app_start, lightboard_app_end, "application/javascript; charset=utf-8"),
        .lightboard_font = asset(lightboard_font_start, lightboard_font_end, "font/woff2"),
        .lightboard_charset = asset(lightboard_charset_start, lightboard_charset_end, "application/javascript; charset=utf-8"),
        .lightboard_layout = asset(lightboard_layout_start, lightboard_layout_end, "application/javascript; charset=utf-8"),
    };
}
#endif
