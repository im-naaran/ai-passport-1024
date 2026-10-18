#include "badge_store.h"
#include <string.h>

static badge_store_result_t refresh(badge_store_t *store) {
    badge_record_view_t views[2];
    bool valid[2];
    for (unsigned i = 0; i < 2; ++i)
        valid[i] = badge_record_validate(store->backend.mapped + i * BADGE_SLOT_SIZE,
                                         BADGE_SLOT_SIZE, &views[i]);
    int selected = !valid[0] ? (valid[1] ? 1 : -1) :
        (!valid[1] || !badge_sequence_newer(views[1].meta.sequence, views[0].meta.sequence) ? 0 : 1);
    if (selected < 0) {
        store->active_slot = -1;
        store->snapshot = store->defaults;
        store->snapshot.is_default = true;
        return BADGE_STORE_DEFAULTED;
    }
    badge_record_view_t *record = &views[selected];
    memcpy(store->active_name, record->name, record->meta.name_length);
    store->active_name[record->meta.name_length] = '\0';
    memcpy(store->active_bio, record->bio, record->meta.bio_length);
    store->active_bio[record->meta.bio_length] = '\0';
    store->active_slot = selected;
    store->snapshot = (badge_profile_snapshot_t){
        .name = store->active_name,
        .image = record->image,
        .width = record->meta.width,
        .height = record->meta.height,
        .stride = record->meta.stride,
        .image_format = record->meta.image_format,
        .sequence = record->meta.sequence,
        .is_default = false,
        .bio = store->active_bio,
        .shape = record->meta.shape,
    };
    return BADGE_STORE_OK;
}

static bool defaults_valid(const badge_profile_snapshot_t *defaults) {
    return defaults && defaults->name && defaults->image && defaults->bio &&
        badge_name_valid((const uint8_t *)defaults->name, strlen(defaults->name)) &&
        badge_bio_valid((const uint8_t *)defaults->bio, strlen(defaults->bio)) &&
        defaults->width == BADGE_IMAGE_WIDTH && defaults->height == BADGE_IMAGE_HEIGHT &&
        defaults->stride == BADGE_IMAGE_STRIDE &&
        defaults->image_format == BADGE_IMAGE_FORMAT_RGB565_LE &&
        badge_photo_shape_valid(defaults->shape);
}

badge_store_result_t badge_store_init_defaults(badge_store_t *store, badge_profile_snapshot_t defaults) {
    if (!store || !defaults_valid(&defaults)) return BADGE_STORE_INVALID;
    *store = (badge_store_t){.defaults = defaults, .snapshot = defaults, .active_slot = -1};
    store->snapshot.is_default = true;
    return BADGE_STORE_DEFAULTED;
}

badge_store_result_t badge_store_init(badge_store_t *store, badge_store_backend_t backend,
                                      badge_profile_snapshot_t defaults) {
    if (!store || !backend.mapped || backend.size != BADGE_PARTITION_SIZE || !backend.erase ||
        !backend.write || !backend.read ||
        !defaults_valid(&defaults)) return BADGE_STORE_INVALID;
    *store = (badge_store_t){.backend = backend, .defaults = defaults, .active_slot = -1};
    return refresh(store);
}

const badge_profile_snapshot_t *badge_store_snapshot(const badge_store_t *store) {
    return store ? &store->snapshot : NULL;
}

badge_store_result_t badge_store_begin_update(badge_store_t *store, const badge_upload_meta_t *meta) {
    if (!store || !meta) return BADGE_STORE_INVALID;
    if (store->busy) return BADGE_STORE_BUSY;
    if (!store->backend.mapped || !store->backend.erase || !store->backend.write ||
        !store->backend.read) return BADGE_STORE_IO_ERROR;
    if (!badge_name_valid((const uint8_t *)meta->name, meta->name_length) ||
        !badge_bio_valid((const uint8_t *)meta->bio, meta->bio_length) ||
        meta->image_format != BADGE_IMAGE_FORMAT_RGB565_LE || meta->width != BADGE_IMAGE_WIDTH ||
        meta->height != BADGE_IMAGE_HEIGHT || meta->stride != BADGE_IMAGE_STRIDE ||
        meta->image_length != BADGE_IMAGE_BYTES || !badge_photo_shape_valid(meta->shape))
        return BADGE_STORE_INVALID;
    store->pending_slot = store->active_slot == 0 ? 1 : 0;
    store->pending_meta = (badge_record_meta_t){
        .sequence = store->snapshot.is_default ? 1 : store->snapshot.sequence + 1,
        .name_length = (uint16_t)meta->name_length,
        .image_format = meta->image_format,
        .width = meta->width,
        .height = meta->height,
        .stride = meta->stride,
        .image_length = meta->image_length,
        .bio_length = (uint16_t)meta->bio_length,
        .shape = meta->shape,
    };
    memcpy(store->pending_name, meta->name, meta->name_length);
    store->pending_name[meta->name_length] = '\0';
    if (meta->bio_length) memcpy(store->pending_bio, meta->bio, meta->bio_length);
    store->pending_bio[meta->bio_length] = '\0';
    size_t slot_offset = (size_t)store->pending_slot * BADGE_SLOT_SIZE;
    badge_store_result_t result = store->backend.erase(store->backend.context, slot_offset, BADGE_SLOT_SIZE);
    if (result != BADGE_STORE_OK) return result;
    uint8_t header[BADGE_RECORD_HEADER_SIZE];
    badge_record_prepare(header, &store->pending_meta);
    result = store->backend.write(store->backend.context, slot_offset, header, sizeof(header));
    if (result == BADGE_STORE_OK)
        result = store->backend.write(store->backend.context, slot_offset + BADGE_RECORD_HEADER_SIZE,
                                      (const uint8_t *)store->pending_name, meta->name_length);
    if (result == BADGE_STORE_OK && meta->bio_length)
        result = store->backend.write(store->backend.context,
            slot_offset + BADGE_RECORD_HEADER_SIZE + meta->name_length,
            (const uint8_t *)store->pending_bio, meta->bio_length);
    if (result != BADGE_STORE_OK) return result;
    store->pending_image_written = 0;
    store->pending_crc = badge_crc32_extend(badge_crc32_start(),
                                            (const uint8_t *)store->pending_name, meta->name_length);
    store->pending_crc = badge_crc32_extend(store->pending_crc,
                                            (const uint8_t *)store->pending_bio, meta->bio_length);
    store->busy = true;
    return BADGE_STORE_OK;
}

badge_store_result_t badge_store_write(badge_store_t *store, const uint8_t *data, size_t length) {
    if (!store || !store->busy || (!data && length)) return BADGE_STORE_INVALID;
    if (length > store->pending_meta.image_length - store->pending_image_written) return BADGE_STORE_INVALID;
    size_t offset = (size_t)store->pending_slot * BADGE_SLOT_SIZE + BADGE_RECORD_HEADER_SIZE +
        store->pending_meta.name_length + store->pending_meta.bio_length +
        store->pending_image_written;
    badge_store_result_t result = length ?
        store->backend.write(store->backend.context, offset, data, length) : BADGE_STORE_OK;
    if (result != BADGE_STORE_OK) return result;
    store->pending_crc = badge_crc32_extend(store->pending_crc, data, length);
    store->pending_image_written += length;
    return BADGE_STORE_OK;
}

badge_store_result_t badge_store_finish(badge_store_t *store) {
    if (!store || !store->busy || store->pending_image_written != store->pending_meta.image_length)
        return BADGE_STORE_INVALID;
    size_t slot_offset = (size_t)store->pending_slot * BADGE_SLOT_SIZE;
    uint8_t header[BADGE_RECORD_HEADER_SIZE];
    badge_record_prepare(header, &store->pending_meta);
    badge_record_finalize(header, badge_crc32_finish(store->pending_crc));
    badge_store_result_t result = store->backend.write(store->backend.context,
        slot_offset + BADGE_RECORD_PAYLOAD_CRC_OFFSET,
        header + BADGE_RECORD_PAYLOAD_CRC_OFFSET, 8);
    uint8_t verify[1024];
    uint32_t crc_state = badge_crc32_start();
    size_t payload_size = store->pending_meta.name_length + store->pending_meta.bio_length +
        store->pending_meta.image_length;
    for (size_t read = 0; result == BADGE_STORE_OK && read < payload_size;) {
        size_t chunk = payload_size - read < sizeof(verify) ? payload_size - read : sizeof(verify);
        result = store->backend.read(store->backend.context,
            slot_offset + BADGE_RECORD_HEADER_SIZE + read, verify, chunk);
        if (result == BADGE_STORE_OK) crc_state = badge_crc32_extend(crc_state, verify, chunk);
        read += chunk;
    }
    if (result == BADGE_STORE_OK && badge_crc32_finish(crc_state) !=
        badge_crc32_finish(store->pending_crc)) result = BADGE_STORE_IO_ERROR;
    uint8_t committed[4];
    badge_record_mark_committed(committed);
    if (result == BADGE_STORE_OK)
        result = store->backend.write(store->backend.context,
                                      slot_offset + BADGE_RECORD_COMMIT_OFFSET,
                                      committed, sizeof(committed));
    store->busy = false;
    if (result != BADGE_STORE_OK) return result;
    return refresh(store) == BADGE_STORE_OK ? BADGE_STORE_OK : BADGE_STORE_IO_ERROR;
}

void badge_store_abort(badge_store_t *store) {
    if (!store) return;
    // The uncommitted slot is deliberately left invalid; the next update erases it before reuse.
    store->busy = false;
    store->pending_image_written = 0;
}
