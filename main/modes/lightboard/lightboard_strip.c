#include "lightboard_strip.h"

bool lb_strip_decode(const lb_snapshot_t *snapshot, unsigned layer, unsigned strip,
                     lb_strip_inflate_t inflate, void *context, uint8_t *output,
                     size_t capacity, size_t *output_length) {
    if (!snapshot || !snapshot->acquired || !snapshot->compressed || layer > 1 ||
        !inflate || !output || !output_length || !lb_item_geometry_valid(&snapshot->item))
        return false;
    const uint8_t *compressed; size_t length, raw_length;
    if (!lb_default_strip(snapshot->planes[layer], snapshot->item.length[layer],
                          snapshot->item.width, snapshot->item.height, strip,
                          &compressed, &length, &raw_length) || raw_length > capacity ||
        !inflate(context, compressed, length, output, raw_length)) return false;
    // Undo the per-row Sub filter only after exact bounded decompression succeeds.
    for (size_t i = 0; i < raw_length; ++i)
        if (i % snapshot->item.width) output[i] += output[i - 1];
    *output_length = raw_length;
    return true;
}
