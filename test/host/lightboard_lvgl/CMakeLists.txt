cmake_minimum_required(VERSION 3.20)
project(lightboard_lvgl_host C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT ESP_IDF_PATH)
    message(FATAL_ERROR "Pass -DESP_IDF_PATH=/path/to/framework-espidf for the ROM header")
endif()
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(lightboard_lvgl_test main.c rom_host.c
    "${ROOT}/main/modes/badge/badge_mode.c"
    "${ROOT}/main/modes/badge/badge_view.c"
    "${ROOT}/main/settings/settings_page.c"
    "${ROOT}/main/settings/settings_view.c"
    "${ROOT}/main/services/config/settings_store.c"
    "${ROOT}/main/modes/lightboard/lightboard_view.c"
    "${ROOT}/main/modes/lightboard/lightboard_mode.c"
    "${ROOT}/main/modes/lightboard/lightboard_playback.c"
    "${ROOT}/main/modes/lightboard/lightboard_strip.c"
    "${ROOT}/main/services/lightboard/lightboard_defaults.c"
    "${ROOT}/main/services/lightboard/lightboard_record.c"
    "${ROOT}/main/services/lightboard/lightboard_store.c"
    "${ROOT}/main/services/badge/badge_record.c"
    "${ROOT}/main/services/badge/badge_store.c"
    "${ROOT}/components/text/src/text_utf8.c"
    "${ROOT}/components/text/src/text_layout.c"
    "${ROOT}/components/ui_common/src/ui_shell.c"
    "${ROOT}/components/ui_common/src/ui_theme.c"
    "${ROOT}/components/ui_common/src/ui_font.c"
    "${ROOT}/components/ui_common/fonts/passport_font_16.c")
target_include_directories(lightboard_lvgl_test PRIVATE . "${ROOT}/main"
    "${ROOT}/test/host/stubs" "${ROOT}/main/services/config"
    "${ROOT}/components/text/include" "${ROOT}/components/ui_common/include"
    )
# The SDK ROM header redefines 32-bit tuning macros on LP64 hosts. Treat it as
# an external header; the host replacement still cannot validate the ROM ABI.
target_include_directories(lightboard_lvgl_test SYSTEM PRIVATE
    "${ESP_IDF_PATH}/components/esp_rom/include" "${ESP_IDF_PATH}/components/esp_rom/esp32c3")
target_link_libraries(lightboard_lvgl_test PRIVATE lvgl z)
target_compile_options(lightboard_lvgl_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
if(APPLE)
    target_compile_options(lightboard_lvgl_test PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/host_compat.h")
endif()
