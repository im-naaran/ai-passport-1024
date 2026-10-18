#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modes/lightboard/lightboard_view.h"
#include "modes/lightboard/lightboard_strip.h"
#include "modes/badge/badge_view.h"
#include "settings/settings_view.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

static uint16_t framebuffer[240 * 320], expected[240 * 320];
static uint16_t dma_sized_buffer[240 * 8];
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    unsigned width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        assert(y >= 0 && y < 320 && area->x1 >= 0 && area->x2 < 240);
        memcpy(framebuffer + y * 240 + area->x1, pixels, width * 2);
        pixels += width * 2;
    }
    lv_display_flush_ready(display);
}
static bool inflate_raw(void *ctx, const uint8_t *input, size_t length, uint8_t *output, size_t expected_len) {
    tinfl_decompressor *state = ctx;
    tinfl_init(state);
    size_t in = length, out = expected_len;
    return tinfl_decompress(state, input, &in, output, output, &out,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) == TINFL_STATUS_DONE && in == length && out == expected_len;
}
static uint8_t *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); long size = ftell(file); assert(size > 0);
    rewind(file); uint8_t *bytes = malloc(size); assert(bytes);
    assert(fread(bytes, 1, size, file) == (size_t)size); fclose(file);
    *length = size; return bytes;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    size_t length; uint8_t *defaults = read_file(argv[1], &length);
    lb_store_t store;
    assert(lb_store_init(&store, (lb_backend_t){0}, defaults, length) == LB_IO_ERROR);
    lv_init();
    const uint32_t warning[] = {0x6a21, 0x5f0f, 0x4fdd, 0x5b58, 0x5931, 0x8d25};
    for (unsigned i = 0; i < sizeof(warning) / sizeof(warning[0]); ++i) {
        lv_font_glyph_dsc_t glyph;
        assert(lv_font_get_glyph_dsc(&passport_font_16, &glyph, warning[i], 0));
        assert(!glyph.is_placeholder);
    }
    lv_display_t *display = lv_display_create(240, 320); assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, dma_sized_buffer, NULL, sizeof(dma_sized_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    ui_shell_t ui; ui_shell_init(&ui);
    // Keep the existing two pages resident to exercise the unchanged 24KiB pool.
    badge_mode_t badge; badge_view_t badge_view;
    mode_t badge_descriptor = badge_mode_descriptor(&badge, NULL);
    badge_view_init(&badge_view, &badge, &badge_descriptor, ui.content);
    settings_store_t config = {.value = {.brightness_percent = 60}};
    settings_page_t settings = {.config = &config};
    settings_view_t settings_view; mode_t settings_descriptor = settings_page_descriptor(&settings);
    settings_view_init(&settings_view, &settings, &settings_descriptor, ui.content);
    lightboard_mode_t mode; lightboard_view_t view;
    mode_t descriptor = lightboard_mode_descriptor(&mode, &store);
    assert(lightboard_view_init(&view, &mode, ui.content));
    descriptor.enter(descriptor.context);
    tinfl_decompressor *inflate_state = calloc(1, sizeof(*inflate_state)); assert(inflate_state);
    unsigned strip_count = 0, frames = 0;
    for (unsigned index = 0; index < store.active.count; ++index) {
        lb_snapshot_t snapshot = {0}; assert(lb_snapshot_acquire(&store, index, &snapshot) == LB_OK);
        size_t plane_size = snapshot.item.width * snapshot.item.height;
        uint8_t *raw[2] = {malloc(plane_size), malloc(plane_size)}; assert(raw[0] && raw[1]);
        for (unsigned layer = 0; layer < 2; ++layer) {
            for (unsigned strip = 0; strip < (snapshot.item.height + 3u) / 4; ++strip) {
                size_t out = 0, at = strip * 4 * snapshot.item.width;
                assert(lb_strip_decode(&snapshot, layer, strip, inflate_raw, inflate_state,
                    raw[layer] + at, plane_size - at, &out));
                assert(out > 0 && out <= LB_DEFAULT_STRIP_BYTES); ++strip_count;
                size_t untouched = 123;
                assert(!lb_strip_decode(&snapshot, layer, strip, inflate_raw, inflate_state,
                    raw[layer] + at, out - 1, &untouched));
                assert(untouched == 123);
            }
        }
        // Unknown layer/strip, truncated stream, trailing bytes and decoded size mismatch.
        uint8_t scratch[LB_DEFAULT_STRIP_BYTES]; size_t out = 0;
        assert(!lb_strip_decode(&snapshot, 2, 0, inflate_raw, inflate_state, scratch, sizeof(scratch), &out));
        assert(!lb_strip_decode(&snapshot, 0, 1000, inflate_raw, inflate_state, scratch, sizeof(scratch), &out));
        const uint8_t *compressed; size_t compressed_len, raw_len;
        assert(lb_default_strip(snapshot.planes[0], snapshot.item.length[0], snapshot.item.width,
            snapshot.item.height, 0, &compressed, &compressed_len, &raw_len));
        assert(!inflate_raw(inflate_state, compressed, compressed_len - 1, scratch, raw_len));
        uint8_t *trailing = malloc(compressed_len + 1); assert(trailing);
        memcpy(trailing, compressed, compressed_len); trailing[compressed_len] = 0;
        assert(!inflate_raw(inflate_state, trailing, compressed_len + 1, scratch, raw_len));
        assert(!inflate_raw(inflate_state, compressed, compressed_len, scratch, raw_len - 1));
        free(trailing);
        for (unsigned sample = 0; sample < 3; ++sample) {
            lb_playback_frame_t frame = {.index = index, .color_index = index,
                .core_opacity = sample == 1 ? 204 : 255, .halo_opacity = sample == 1 ? 89 : 153,
                .x = snapshot.item.mode ? 80 - (int)sample * 160 : snapshot.item.x,
                .y = snapshot.item.y};
            assert(mode.renderer.present(mode.renderer.context, &snapshot, &frame));
            lv_tick_inc(50); lv_refr_now(display);
            memcpy(expected, framebuffer, sizeof(expected));
            unsigned lit = 0; for (unsigned p = 0; p < 240 * 320; ++p) lit += expected[p] != 0;
            assert(lit > 0);
            mode.renderer.clear(mode.renderer.context, false);
            lb_snapshot_t raw_snapshot = snapshot; // Test-owned default, no mmap lease transfer.
            raw_snapshot.compressed = false;
            for (unsigned layer = 0; layer < 2; ++layer) {
                raw_snapshot.planes[layer] = raw[layer]; raw_snapshot.item.length[layer] = plane_size;
            }
            assert(mode.renderer.present(mode.renderer.context, &raw_snapshot, &frame));
            lv_tick_inc(50); lv_refr_now(display);
            assert(memcmp(expected, framebuffer, sizeof(expected)) == 0);
            mode.renderer.clear(mode.renderer.context, false);
            ++frames;
        }
        lb_snapshot_release(&snapshot); free(raw[0]); free(raw[1]);
    }
    // Largest legal user resources stay on the direct A8 path: no default decoder workspace.
    const unsigned widths[] = {1536, 240}, heights[] = {96, 320};
    for (unsigned i = 0; i < 2; ++i) {
        size_t size = widths[i] * heights[i];
        uint8_t *core = malloc(size), *halo = malloc(size); assert(core && halo);
        memset(core, 255, size); memset(halo, 128, size);
        lb_snapshot_t raw = {.acquired = true, .item = {.width = widths[i], .height = heights[i],
            .stride = widths[i], .length = {size, size}}, .planes = {core, halo}};
        lb_playback_frame_t frame = {.x = i ? 0 : -1000, .y = i ? 0 : 100,
            .core_opacity = 255, .halo_opacity = 153};
        assert(mode.renderer.present(mode.renderer.context, &raw, &frame));
        assert(!view.workspace);
        lv_tick_inc(50); lv_refr_now(display);
        for (unsigned y = 0; y < 320; ++y) for (unsigned x = 0; x < 240; ++x) {
            bool lit = framebuffer[y * 240 + x] != 0;
            assert(lit == (i || (y >= 100 && y < 196)));
        }
        mode.renderer.clear(mode.renderer.context, false);
        free(core); free(halo);
    }
    // Repeated enter/leave frees the external decoder workspace and invalidates
    // both descriptor caches while retaining only resident page objects.
    size_t stable_free = 0;
    for (unsigned cycle = 0; cycle < 25; ++cycle) {
        descriptor.enter(descriptor.context);
        assert(lightboard_mode_tick(&mode, cycle * INT64_C(10000000)));
        lightboard_mode_sync_locked(&mode); descriptor.render(descriptor.context);
        lv_tick_inc(50); lv_refr_now(display);
        assert(view.workspace && view.bound);
        descriptor.leave(descriptor.context); lightboard_mode_sync_locked(&mode);
        assert(!view.workspace && !view.bound && !mode.snapshot.acquired);
        ui_page_show(badge_view.root);
        lv_tick_inc(50); lv_refr_now(display);
        lv_mem_monitor_t memory; lv_mem_monitor(&memory);
        if (cycle == 0) stable_free = memory.free_size;
        else assert(memory.free_size == stable_free);
    }
    printf("LVGL host: %u strips, %u pixel comparisons, 2 maximum raw resources, 25 lifecycle cycles; LVGL free=%zu/24576\n",
           strip_count, frames, stable_free);
    free(inflate_state); free(defaults);
    lv_deinit();
    return 0;
}
