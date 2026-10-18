#include "lightboard_view.h"
#include "lightboard_strip.h"
#include "esp_log.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/misc/lv_area_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    tinfl_decompressor inflater;
    lv_draw_buf_t draw;
    uint8_t pixels[LB_DEFAULT_STRIP_BYTES];
} decode_workspace_t;

// One software drawing unit consumes each strip before the other mask can reuse
// it. Multiple parallel drawing units would require a different workspace design.
_Static_assert(LV_DRAW_SW_DRAW_UNIT_CNT == 1, "lightboard requires serial software drawing");
_Static_assert(LV_DRAW_BUF_STRIDE_ALIGN == 1 && LV_DRAW_BUF_ALIGN <= 4,
               "lightboard A8 strips require the existing byte stride and 4-byte alignment");
_Static_assert(sizeof(decode_workspace_t) + sizeof(lightboard_view_t) + sizeof(lightboard_mode_t)
               + sizeof(lb_bundle_t) <= 16384, "lightboard working metadata exceeds budget");

static const uint32_t colors[] = {0xff3dbb, 0xff8a24, 0x19ddff, 0x8dff39, 0xb46cff, 0xfff143};
static const char *TAG = "lightboard";

static bool inflate_strip(void *context, const uint8_t *input, size_t length,
                          uint8_t *output, size_t expected) {
    decode_workspace_t *work = context;
    tinfl_init(&work->inflater);
    size_t in = length, out = expected;
    tinfl_status status = tinfl_decompress(&work->inflater, input, &in, output, output, &out,
                                          TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return status == TINFL_STATUS_DONE && in == length && out == expected;
}

static lv_result_t decoder_info(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc,
                                 lv_image_header_t *header) {
    lightboard_view_t *view = decoder->user_data;
    if (dsc->src_type != LV_IMAGE_SRC_VARIABLE || !view->bound ||
        (dsc->src != &view->images[0] && dsc->src != &view->images[1]) ||
        !view->snapshot->compressed) return LV_RESULT_INVALID;
    *header = ((const lv_image_dsc_t *)dsc->src)->header;
    return LV_RESULT_OK;
}
static lv_result_t decoder_open(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc) {
    (void)decoder;
    // NULL decoded selects get_area, never a full-image decoder/cache allocation.
    dsc->decoded = NULL;
    return LV_RESULT_OK;
}
static lv_result_t decoder_area(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc,
                                 const lv_area_t *full, lv_area_t *area) {
    lightboard_view_t *view = decoder->user_data;
    const lb_snapshot_t *snapshot = view->snapshot;
    if (!view->bound || !snapshot || !view->workspace || full->y1 < 0 ||
        full->y2 >= snapshot->item.height || full->x1 < 0 || full->x2 >= snapshot->item.width)
        return LV_RESULT_INVALID;
    int y = area->y1 == LV_COORD_MIN ? full->y1 / 4 * 4 : area->y2 + 1;
    if (y > full->y2) return LV_RESULT_INVALID;
    decode_workspace_t *work = view->workspace;
    unsigned layer = dsc->src == &view->images[0] ? 0 : 1;
    size_t length;
    if (!lb_strip_decode(snapshot, layer, y / 4, inflate_strip, work,
                         work->pixels, sizeof(work->pixels), &length)) {
        if (!view->error_reported) ESP_LOGE(TAG, "default mask strip decode failed");
        view->error_reported = true;
        return LV_RESULT_INVALID;
    }
    unsigned rows = length / snapshot->item.width;
    if (lv_draw_buf_init(&work->draw, snapshot->item.width, rows, LV_COLOR_FORMAT_A8,
                         snapshot->item.stride, work->pixels, sizeof(work->pixels)) != LV_RESULT_OK)
        return LV_RESULT_INVALID;
    *area = (lv_area_t){.x1 = 0, .x2 = snapshot->item.width - 1, .y1 = y, .y2 = y + rows - 1};
    dsc->decoded = &work->draw;
    return LV_RESULT_OK;
}
static void decoder_close(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc) {
    (void)decoder;
    dsc->decoded = NULL; // The view owns the shared strip, no per-open allocations.
}

static void invalidate(lightboard_view_t *view, const lv_area_t *bounds) {
    lv_area_t page, visible;
    lv_obj_get_coords(view->root, &page);
    if (lv_area_intersect(&visible, &page, bounds)) lv_obj_invalidate_area(view->root, &visible);
}
static void clear(void *context, bool leaving) {
    lightboard_view_t *view = context;
    // No draw task can run concurrently with this callback under the LVGL lock.
    // Make future draws skip these descriptors before dropping either cache and
    // before the mode releases the mapped snapshot.
    if (view->bound) {
        view->bound = false;
        invalidate(view, &view->bounds);
        for (unsigned i = 0; i < 2; ++i) {
            lv_image_cache_drop(&view->images[i]);
            lv_image_header_cache_drop(&view->images[i]);
            memset(&view->images[i], 0, sizeof(view->images[i]));
        }
    }
    view->snapshot = NULL;
    if (leaving) {
        lv_obj_add_flag(view->root, LV_OBJ_FLAG_HIDDEN);
        free(view->workspace);
        view->workspace = NULL;
        view->error_reported = false;
    } else {
        ui_page_show(view->root);
    }
}
static bool present(void *context, const lb_snapshot_t *snapshot, const lb_playback_frame_t *frame) {
    lightboard_view_t *view = context;
    ui_page_show(view->root);
    if (!view->bound) {
        if (snapshot->compressed && !view->workspace) {
            view->workspace = calloc(1, sizeof(decode_workspace_t));
            if (!view->workspace) {
                if (!view->error_reported) ESP_LOGE(TAG, "default decoder workspace unavailable");
                view->error_reported = true;
                return false;
            }
        } else if (!snapshot->compressed) {
            free(view->workspace);
            view->workspace = NULL;
        }
        for (unsigned i = 0; i < 2; ++i)
            view->images[i] = (lv_image_dsc_t){
                .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
                    .flags = snapshot->compressed ? LV_IMAGE_FLAGS_USER1 : 0,
                    .w = snapshot->item.width, .h = snapshot->item.height, .stride = snapshot->item.stride},
                .data_size = snapshot->item.length[i], .data = snapshot->planes[i],
            };
        view->snapshot = snapshot;
        view->bound = true;
    } else {
        invalidate(view, &view->bounds);
    }
    lv_area_t root;
    lv_obj_get_coords(view->root, &root);
    view->frame = *frame;
    view->bounds = (lv_area_t){.x1 = root.x1 + frame->x, .y1 = root.y1 + frame->y,
        .x2 = root.x1 + frame->x + snapshot->item.width - 1,
        .y2 = root.y1 + frame->y + snapshot->item.height - 1};
    invalidate(view, &view->bounds);
    return true;
}
static void draw(lv_event_t *event) {
    lightboard_view_t *view = lv_event_get_user_data(event);
    if (!view->bound) return;
    // Drawing directly avoids per-frame object/style churn and any implicit image
    // transforms. The two A8 masks blend in the existing display clip rectangle.
    for (int i = 1; i >= 0; --i) {
        lv_draw_image_dsc_t dsc;
        lv_draw_image_dsc_init(&dsc);
        dsc.src = &view->images[i];
        dsc.recolor = lv_color_hex(colors[view->frame.color_index]);
        dsc.recolor_opa = LV_OPA_COVER;
        dsc.opa = i ? view->frame.halo_opacity : view->frame.core_opacity;
        lv_draw_image(lv_event_get_layer(event), &dsc, &view->bounds);
    }
}
bool lightboard_view_init(lightboard_view_t *view, lightboard_mode_t *mode, lv_obj_t *parent) {
    memset(view, 0, sizeof(*view));
    view->root = ui_page_create(parent);
    if (!view->root) return false;
    lv_obj_set_style_bg_color(view->root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view->root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(view->root, 0, 0);
    lv_obj_add_event_cb(view->root, draw, LV_EVENT_DRAW_MAIN, view);
    view->decoder = lv_image_decoder_create();
    if (!view->decoder) return false;
    view->decoder->user_data = view;
    lv_image_decoder_set_info_cb(view->decoder, decoder_info);
    lv_image_decoder_set_open_cb(view->decoder, decoder_open);
    lv_image_decoder_set_get_area_cb(view->decoder, decoder_area);
    lv_image_decoder_set_close_cb(view->decoder, decoder_close);
    mode->renderer = (lightboard_renderer_t){.context = view, .clear = clear, .present = present};
    return true;
}
