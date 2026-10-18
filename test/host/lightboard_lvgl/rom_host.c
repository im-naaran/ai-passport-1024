// Host substitute only: zlib exercises production strip/LVGL adapters. It cannot
// establish ESP32-C3 ROM ABI correctness or device execution time.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include <zlib.h>
#include <string.h>
tinfl_status tinfl_decompress(tinfl_decompressor *r, const mz_uint8 *input, size_t *in,
                              mz_uint8 *start, mz_uint8 *output, size_t *out, const mz_uint32 flags) {
    (void)r; (void)start;
    if (flags != TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) return TINFL_STATUS_FAILED;
    z_stream stream; memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)input; stream.avail_in = (uInt)*in;
    stream.next_out = output; stream.avail_out = (uInt)*out;
    if (inflateInit2(&stream, -15) != Z_OK) return TINFL_STATUS_FAILED;
    int result = inflate(&stream, Z_FINISH);
    *in = stream.total_in; *out = stream.total_out;
    inflateEnd(&stream);
    return result == Z_STREAM_END ? TINFL_STATUS_DONE : TINFL_STATUS_FAILED;
}
