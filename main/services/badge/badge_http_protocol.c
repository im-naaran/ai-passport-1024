#include "badge_http_protocol.h"
#include <string.h>

enum { BADGE_HTTP_MAGIC = 0x46525042u };

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

void badge_http_envelope_header(uint8_t output[BADGE_HTTP_ENVELOPE_HEADER_SIZE],
                                uint16_t name_length, uint16_t bio_length,
                                badge_photo_shape_t shape) {
    write_u32(output, BADGE_HTTP_MAGIC);
    write_u16(output + 4, BADGE_HTTP_ENVELOPE_VERSION);
    write_u16(output + 6, name_length);
    write_u16(output + 8, bio_length);
    output[10] = (uint8_t)shape;
    output[11] = 0;
    write_u32(output + 12, BADGE_IMAGE_BYTES);
}

static badge_http_result_t map_store_result(badge_store_result_t result) {
    return result == BADGE_STORE_BUSY ? BADGE_HTTP_ERROR_BUSY : BADGE_HTTP_ERROR_STORAGE;
}

static badge_http_result_t fail(badge_http_parser_t *parser, badge_http_result_t result) {
    if (parser->writer_started && parser->writer.abort) parser->writer.abort(parser->writer.context);
    parser->writer_started = false;
    parser->result = result;
    return result;
}

badge_http_result_t badge_http_parser_init(badge_http_parser_t *parser, size_t content_length,
                                           uint64_t supplied_token, uint64_t expected_token,
                                           badge_http_writer_t writer) {
    if (!parser) return BADGE_HTTP_ERROR_PROTOCOL;
    *parser = (badge_http_parser_t){.writer = writer, .content_length = content_length,
                                    .result = BADGE_HTTP_MORE};
    /* A session token rejects stale/cross-origin submissions; it is not authentication on an open AP. */
    if (supplied_token != expected_token) parser->result = BADGE_HTTP_ERROR_TOKEN;
    /* Bound the declared body before parsing so fixed header/name buffers are the only RAM staging. */
    else if (content_length < BADGE_HTTP_ENVELOPE_HEADER_SIZE + 1 + BADGE_IMAGE_BYTES ||
             content_length > BADGE_HTTP_BODY_MAX) parser->result = BADGE_HTTP_ERROR_LENGTH;
    else if (!writer.begin || !writer.write || !writer.finish || !writer.abort)
        parser->result = BADGE_HTTP_ERROR_PROTOCOL;
    return parser->result;
}

static badge_http_result_t parse_header(badge_http_parser_t *parser) {
    if (read_u32(parser->header) != BADGE_HTTP_MAGIC ||
        read_u16(parser->header + 4) != BADGE_HTTP_ENVELOPE_VERSION ||
        parser->header[11] != 0) return fail(parser, BADGE_HTTP_ERROR_PROTOCOL);
    parser->name_length = read_u16(parser->header + 6);
    parser->bio_length = read_u16(parser->header + 8);
    parser->shape = (badge_photo_shape_t)parser->header[10];
    uint32_t image_length = read_u32(parser->header + 12);
    if (!badge_photo_shape_valid(parser->shape))
        return fail(parser, BADGE_HTTP_ERROR_SHAPE);
    if (!parser->name_length || parser->name_length > BADGE_NAME_MAX_BYTES ||
        parser->bio_length > BADGE_BIO_MAX_BYTES || image_length != BADGE_IMAGE_BYTES)
        return fail(parser, BADGE_HTTP_ERROR_LENGTH);
    size_t expected = BADGE_HTTP_ENVELOPE_HEADER_SIZE + parser->name_length +
                      parser->bio_length + image_length;
    if (expected != parser->content_length) return fail(parser, BADGE_HTTP_ERROR_LENGTH);
    return BADGE_HTTP_MORE;
}

static badge_http_result_t begin_writer(badge_http_parser_t *parser) {
    parser->name[parser->name_length] = '\0';
    parser->bio[parser->bio_length] = '\0';
    if (!badge_name_valid((const uint8_t *)parser->name, parser->name_length))
        return fail(parser, BADGE_HTTP_ERROR_NAME);
    if (!badge_bio_valid((const uint8_t *)parser->bio, parser->bio_length))
        return fail(parser, BADGE_HTTP_ERROR_BIO);
    badge_upload_meta_t meta = {
        .name = parser->name,
        .name_length = parser->name_length,
        .image_format = BADGE_IMAGE_FORMAT_RGB565_LE,
        .width = BADGE_IMAGE_WIDTH,
        .height = BADGE_IMAGE_HEIGHT,
        .stride = BADGE_IMAGE_STRIDE,
        .image_length = BADGE_IMAGE_BYTES,
        .bio = parser->bio,
        .bio_length = parser->bio_length,
        .shape = parser->shape,
    };
    badge_store_result_t result = parser->writer.begin(parser->writer.context, &meta);
    if (result != BADGE_STORE_OK) return fail(parser, map_store_result(result));
    parser->writer_started = true;
    return BADGE_HTTP_MORE;
}

badge_http_result_t badge_http_parser_feed(badge_http_parser_t *parser, const uint8_t *data,
                                           size_t length) {
    if (!parser || (!data && length)) return BADGE_HTTP_ERROR_PROTOCOL;
    if (parser->result == BADGE_HTTP_COMPLETE)
        return length ? BADGE_HTTP_ERROR_EXTRA_DATA : BADGE_HTTP_COMPLETE;
    if (parser->result != BADGE_HTTP_MORE) return parser->result;
    if (length > parser->content_length - parser->received)
        return fail(parser, BADGE_HTTP_ERROR_EXTRA_DATA);
    parser->received += length;
    while (length) {
        if (parser->header_received < BADGE_HTTP_ENVELOPE_HEADER_SIZE) {
            size_t remaining = BADGE_HTTP_ENVELOPE_HEADER_SIZE - parser->header_received;
            size_t take = length < remaining ? length : remaining;
            memcpy(parser->header + parser->header_received, data, take);
            parser->header_received += take; data += take; length -= take;
            if (parser->header_received == BADGE_HTTP_ENVELOPE_HEADER_SIZE &&
                parse_header(parser) != BADGE_HTTP_MORE) return parser->result;
            continue;
        }
        if (parser->name_received < parser->name_length) {
            size_t remaining = parser->name_length - parser->name_received;
            size_t take = length < remaining ? length : remaining;
            memcpy(parser->name + parser->name_received, data, take);
            parser->name_received += take; data += take; length -= take;
            if (parser->name_received == parser->name_length && !parser->bio_length &&
                begin_writer(parser) != BADGE_HTTP_MORE) return parser->result;
            continue;
        }
        if (parser->bio_received < parser->bio_length) {
            size_t remaining = parser->bio_length - parser->bio_received;
            size_t take = length < remaining ? length : remaining;
            memcpy(parser->bio + parser->bio_received, data, take);
            parser->bio_received += take; data += take; length -= take;
            if (parser->bio_received == parser->bio_length &&
                begin_writer(parser) != BADGE_HTTP_MORE) return parser->result;
            continue;
        }
        size_t remaining = BADGE_IMAGE_BYTES - parser->image_received;
        size_t take = length < remaining ? length : remaining;
        badge_store_result_t write_result = parser->writer.write(parser->writer.context, data, take);
        if (write_result != BADGE_STORE_OK) return fail(parser, map_store_result(write_result));
        parser->image_received += take; data += take; length -= take;
    }
    if (parser->received == parser->content_length) {
        if (!parser->writer_started || parser->image_received != BADGE_IMAGE_BYTES)
            return fail(parser, BADGE_HTTP_ERROR_TRUNCATED);
        badge_store_result_t finish_result = parser->writer.finish(parser->writer.context);
        if (finish_result != BADGE_STORE_OK) return fail(parser, map_store_result(finish_result));
        parser->writer_started = false;
        parser->result = BADGE_HTTP_COMPLETE;
    }
    return parser->result;
}

badge_http_result_t badge_http_parser_end(badge_http_parser_t *parser,
                                          badge_http_end_reason_t reason) {
    if (!parser) return BADGE_HTTP_ERROR_PROTOCOL;
    if (parser->result == BADGE_HTTP_COMPLETE || parser->result != BADGE_HTTP_MORE)
        return parser->result;
    badge_http_result_t result = reason == BADGE_HTTP_END_TIMEOUT ? BADGE_HTTP_ERROR_TIMEOUT :
        reason == BADGE_HTTP_END_CANCELLED ? BADGE_HTTP_ERROR_CANCELLED : BADGE_HTTP_ERROR_TRUNCATED;
    return fail(parser, result);
}
