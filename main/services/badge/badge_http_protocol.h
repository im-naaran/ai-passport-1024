#pragma once

#include "badge_store.h"

enum {
    BADGE_HTTP_ENVELOPE_HEADER_SIZE = 16,
    BADGE_HTTP_ENVELOPE_VERSION = 3,
    BADGE_HTTP_BODY_MAX = BADGE_HTTP_ENVELOPE_HEADER_SIZE + BADGE_NAME_MAX_BYTES +
                          BADGE_BIO_MAX_BYTES + BADGE_IMAGE_BYTES,
};

typedef enum {
    BADGE_HTTP_MORE,
    BADGE_HTTP_COMPLETE,
    BADGE_HTTP_ERROR_TOKEN,
    BADGE_HTTP_ERROR_LENGTH,
    BADGE_HTTP_ERROR_PROTOCOL,
    BADGE_HTTP_ERROR_NAME,
    BADGE_HTTP_ERROR_BIO,
    BADGE_HTTP_ERROR_SHAPE,
    BADGE_HTTP_ERROR_BUSY,
    BADGE_HTTP_ERROR_STORAGE,
    BADGE_HTTP_ERROR_TRUNCATED,
    BADGE_HTTP_ERROR_TIMEOUT,
    BADGE_HTTP_ERROR_CANCELLED,
    BADGE_HTTP_ERROR_EXTRA_DATA,
} badge_http_result_t;

typedef enum {
    BADGE_HTTP_END_DISCONNECTED,
    BADGE_HTTP_END_TIMEOUT,
    BADGE_HTTP_END_CANCELLED,
} badge_http_end_reason_t;

typedef struct {
    void *context;
    badge_store_result_t (*begin)(void *context, const badge_upload_meta_t *meta);
    badge_store_result_t (*write)(void *context, const uint8_t *data, size_t length);
    badge_store_result_t (*finish)(void *context);
    void (*abort)(void *context);
} badge_http_writer_t;

typedef struct {
    badge_http_writer_t writer;
    size_t content_length;
    size_t received;
    uint8_t header[BADGE_HTTP_ENVELOPE_HEADER_SIZE];
    size_t header_received;
    char name[BADGE_NAME_MAX_BYTES + 1];
    size_t name_length;
    size_t name_received;
    char bio[BADGE_BIO_MAX_BYTES + 1];
    size_t bio_length;
    size_t bio_received;
    badge_photo_shape_t shape;
    size_t image_received;
    bool writer_started;
    badge_http_result_t result;
} badge_http_parser_t;

void badge_http_envelope_header(uint8_t output[BADGE_HTTP_ENVELOPE_HEADER_SIZE],
                                uint16_t name_length, uint16_t bio_length,
                                badge_photo_shape_t shape);
badge_http_result_t badge_http_parser_init(badge_http_parser_t *parser, size_t content_length,
                                           uint64_t supplied_token, uint64_t expected_token,
                                           badge_http_writer_t writer);
badge_http_result_t badge_http_parser_feed(badge_http_parser_t *parser, const uint8_t *data,
                                           size_t length);
badge_http_result_t badge_http_parser_end(badge_http_parser_t *parser,
                                          badge_http_end_reason_t reason);
