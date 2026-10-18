idf_component_register(
    SRCS
        "app_main.c"
        "app_controller.c"
        "navigation/navigation.c"
        "navigation/mode_registry.c"
        "modes/badge/badge_default.c"
        "modes/badge/badge_mode.c"
        "modes/badge/badge_view.c"
        "modes/lightboard/lightboard_playback.c"
        "modes/lightboard/lightboard_mode.c"
        "modes/lightboard/lightboard_strip.c"
        "modes/lightboard/lightboard_view.c"
        "settings/settings_page.c"
        "settings/settings_view.c"
        "services/config/settings_store.c"
        "services/config/mode_store.c"
        "services/config/settings_nvs.c"
        "services/badge/badge_record.c"
        "services/badge/badge_store.c"
        "services/badge/badge_partition.c"
        "services/badge/badge_wifi_state.c"
        "services/badge/badge_wifi_service.c"
        "services/badge/badge_wifi_esp.c"
        "services/badge/badge_http_protocol.c"
        "services/badge/badge_http_server.c"
        "services/badge/badge_assets.c"
        "services/lightboard/lightboard_record.c"
        "services/lightboard/lightboard_defaults.c"
        "services/lightboard/lightboard_store.c"
        "services/lightboard/lightboard_partition.c"
        "services/display/display_render_policy.c"
        "services/display/battery_schedule.c"
    INCLUDE_DIRS "." "services/config"
    REQUIRES bsp esp_timer text ui_common nvs_flash esp_partition esp_wifi
             esp_netif esp_event esp_http_server
)

# Rebuild the embedded asset object whenever an offline page resource changes.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BADGE_INDEX_HTML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/index.html"
    BADGE_STYLE_CSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/style.css"
    BADGE_IMAGE_JS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/badge_image.js"
    BADGE_APP_JS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/app.js"
    LIGHTBOARD_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard.woff2"
    LIGHTBOARD_FONT_LICENSE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard-font-OFL.txt"
    LIGHTBOARD_CHARSET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_charset.js"
    LIGHTBOARD_PROTOCOL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_protocol.js"
    LIGHTBOARD_APP_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_app.js"
    LIGHTBOARD_LAYOUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_layout.js"
    LIGHTBOARD_DEFAULTS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/lightboard_defaults.bin"
)
# ESP-IDF normalizes component sources to absolute paths before creating the build rule. Attach
# the web dependencies to that same source identity so incremental PlatformIO builds re-run the
# .incbin assembler whenever an embedded page resource changes.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/services/badge/badge_assets.c"
    APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/index.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/style.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/badge_image.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/app.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard.woff2"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard-font-OFL.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_charset.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_protocol.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_app.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/badge/lightboard_layout.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/lightboard_defaults.bin"
)
