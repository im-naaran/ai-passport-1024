#include "lightboard_store.h"
#include "services/badge/badge_record.h"
#include <string.h>

static void lock(lb_store_t *s) { if (s->backend.lock) s->backend.lock(s->backend.context); }
static void unlock(lb_store_t *s) { if (s->backend.unlock) s->backend.unlock(s->backend.context); }
static size_t bank_offset(int bank) { return (size_t)bank * LB_BANK_SIZE; }

static bool read_bundle(lb_store_t *s, int bank, uint32_t length, uint32_t expected_crc,
                         lb_bundle_t *meta, bool *io_error) {
    uint8_t prefix[LB_META_BYTES], chunk[512];
    size_t base = bank_offset(bank) + LB_BANK_HEADER;
    if (!s->backend.read(s->backend.context, base, prefix, LB_BUNDLE_HEADER)) { *io_error = true; return false; }
    if (!prefix[6] || prefix[6] > LB_MAX_ITEMS) return false;
    size_t n = LB_BUNDLE_HEADER + (size_t)prefix[6] * LB_ENTRY_BYTES;
    if (n > length) return false;
    if (!s->backend.read(s->backend.context, base, prefix, n)) { *io_error = true; return false; }
    if (!lb_bundle_decode_meta(prefix, n, meta) || meta->total_length != length) return false;
    uint32_t crc = badge_crc32_start(), body_crc = badge_crc32_start();
    for (size_t offset = 0; offset < length;) {
        size_t count = length - offset; if (count > sizeof(chunk)) count = sizeof(chunk);
        if (!s->backend.read(s->backend.context, base + offset, chunk, count)) { *io_error = true; return false; }
        crc = badge_crc32_extend(crc, chunk, count);
        size_t skip = offset < LB_BUNDLE_HEADER ? LB_BUNDLE_HEADER - offset : 0;
        if (count > skip) body_crc = badge_crc32_extend(body_crc, chunk + skip, count - skip);
        offset += count;
    }
    return badge_crc32_finish(crc) == expected_crc && badge_crc32_finish(body_crc) == meta->body_crc;
}
lb_result_t lb_store_init(lb_store_t *s, lb_backend_t backend, const uint8_t *defaults, size_t defaults_length) {
    if (!s) return LB_INVALID;
    *s = (lb_store_t){.backend = backend, .defaults = defaults, .active_bank = -1};
    if (!lb_defaults_decode(defaults, defaults_length, &s->active)) return LB_INVALID;
    if (backend.size != LB_PARTITION_SIZE || !backend.read || !backend.write || !backend.erase ||
        !backend.map || !backend.unmap || !backend.lock || !backend.unlock) return LB_IO_ERROR;
    lock(s); bool io_error = false;
    for (int bank = 0; bank < 2; ++bank) {
        uint8_t header[LB_BANK_HEADER]; uint32_t revision, length, crc; lb_bundle_t candidate;
        if (!backend.read(backend.context, bank_offset(bank), header, sizeof(header))) { io_error = true; continue; }
        if (!lb_bank_decode(header, &revision, &length, &crc) || !read_bundle(s, bank, length, crc, &candidate, &io_error)) continue;
        // Equal or half-range revisions have no unique newest record; preserve both read-only.
        if (s->active_bank >= 0 && (revision == s->revision ||
            revision - s->revision == 0x80000000u)) io_error = true;
        if (s->active_bank < 0 || lb_revision_newer(revision, s->revision)) {
            s->active_bank = bank; s->revision = revision; s->active = candidate;
        }
    }
    // An unreadable bank might contain a newer committed version: do not erase it blind.
    s->available = !io_error; unlock(s);
    return io_error ? LB_IO_ERROR : s->active_bank < 0 ? LB_DEFAULTED : LB_OK;
}
lb_result_t lb_store_begin(lb_store_t *s, const uint8_t *prefix, size_t length) {
    if (!s || !prefix || length > LB_META_BYTES) return LB_INVALID;
    lb_bundle_t meta;
    if (!lb_bundle_decode_meta(prefix, length, &meta)) return LB_INVALID;
    lock(s);
    lb_result_t result = LB_OK;
    int target = s->active_bank == 0 ? 1 : 0;
    if (!s->available) result = LB_IO_ERROR;
    else if (s->busy || s->references[target]) result = LB_BUSY;
    else if (meta.expected_revision != s->revision) result = LB_CONFLICT;
    else if (!s->backend.erase(s->backend.context, bank_offset(target), LB_BANK_SIZE) ||
             !s->backend.write(s->backend.context, bank_offset(target) + LB_BANK_HEADER, prefix, length)) result = LB_IO_ERROR;
    else {
        s->pending = meta; s->pending_bank = target; s->busy = true; s->written = length;
        s->pending_crc = badge_crc32_extend(badge_crc32_start(), prefix, length);
        s->pending_body_crc = badge_crc32_extend(badge_crc32_start(), prefix + LB_BUNDLE_HEADER, length - LB_BUNDLE_HEADER);
    }
    unlock(s); return result;
}
lb_result_t lb_store_write(lb_store_t *s, const uint8_t *bytes, size_t length) {
    if (!s || !bytes || !length) return LB_INVALID;
    lock(s); lb_result_t result = LB_OK;
    if (!s->busy || length > s->pending.total_length - s->written) result = LB_INVALID;
    else if (!s->backend.write(s->backend.context, bank_offset(s->pending_bank) + LB_BANK_HEADER + s->written, bytes, length)) {
        s->busy = false; result = LB_IO_ERROR;
    } else {
        s->pending_crc = badge_crc32_extend(s->pending_crc, bytes, length);
        s->pending_body_crc = badge_crc32_extend(s->pending_body_crc, bytes, length); s->written += length;
    }
    unlock(s); return result;
}
lb_result_t lb_store_finish(lb_store_t *s) {
    if (!s) return LB_INVALID;
    lock(s);
    if (!s->busy || s->written != s->pending.total_length) { unlock(s); return LB_INVALID; }
    bool io_error = false; lb_bundle_t checked;
    uint32_t crc = badge_crc32_finish(s->pending_crc);
    // A bad incoming payload is a client error; a readback mismatch after valid
    // input is a storage error. Keep these distinct for the HTTP failure response.
    if (badge_crc32_finish(s->pending_body_crc) != s->pending.body_crc) {
        s->busy = false; unlock(s); return LB_INVALID;
    }
    if (!read_bundle(s, s->pending_bank, s->pending.total_length, crc, &checked, &io_error) ||
        checked.expected_revision != s->pending.expected_revision || checked.count != s->pending.count) {
        s->busy = false; unlock(s); return LB_IO_ERROR;
    }
    uint32_t next_revision = s->revision + 1;
    if (!next_revision) next_revision = 1; // Zero belongs to the built-in default list.
    uint8_t header[LB_BANK_HEADER], verify[LB_BANK_HEADER], marker[4];
    lb_bank_prepare(header, next_revision, s->pending.total_length, crc);
    size_t base = bank_offset(s->pending_bank);
    if (!s->backend.write(s->backend.context, base, header, sizeof(header)) ||
        !s->backend.read(s->backend.context, base, verify, sizeof(verify)) || memcmp(header, verify, sizeof(header))) {
        s->busy = false; unlock(s); return LB_IO_ERROR;
    }
    // Commit is the final mutation. The current list and all leased planes remain live until here.
    lb_bank_commit(marker);
    if (!s->backend.write(s->backend.context, base + 28, marker, sizeof(marker))) {
        uint32_t r, length, c;
        bool read_ok = s->backend.read(s->backend.context, base, verify, sizeof(verify));
        if (!read_ok || !lb_bank_decode(verify, &r, &length, &c) || r != next_revision ||
            length != s->pending.total_length || c != crc) {
            if (!read_ok) s->available = false;
            s->busy = false; unlock(s); return LB_IO_ERROR;
        }
    }
    s->active_bank = s->pending_bank; s->revision = next_revision; s->active = s->pending;
    s->busy = false; unlock(s); return LB_OK;
}
void lb_store_abort(lb_store_t *s) { if (s) { lock(s); s->busy = false; unlock(s); } }
bool lb_store_metadata(lb_store_t *s, lb_bundle_t *meta, uint32_t *revision) {
    if (!s || !meta || !revision || !s->defaults) return false;
    lock(s); *meta = s->active; *revision = s->revision; unlock(s); return meta->count != 0;
}
bool lb_store_revision(lb_store_t *s, uint32_t *revision) {
    if (!s || !revision || !s->defaults) return false;
    lock(s); *revision = s->revision; bool valid = s->active.count != 0; unlock(s);
    return valid;
}
lb_result_t lb_snapshot_acquire(lb_store_t *s, unsigned index, lb_snapshot_t *out) {
    if (!s || !out || out->acquired) return LB_INVALID;
    lock(s);
    if (index >= s->active.count) { unlock(s); return LB_INVALID; }
    lb_snapshot_t result = {.owner = s, .item = s->active.items[index], .revision = s->revision,
        .bank = s->active_bank, .compressed = s->active_bank < 0, .acquired = true};
    if (result.compressed) {
        for (unsigned layer = 0; layer < 2; ++layer) result.planes[layer] = s->defaults + result.item.offset[layer];
    } else {
        if (s->references[result.bank] == UINT32_MAX ||
            !s->backend.map(s->backend.context, bank_offset(result.bank) + LB_BANK_HEADER + result.item.offset[0],
                            (size_t)result.item.length[0] + result.item.length[1], &result.mapping)) {
            unlock(s); return LB_IO_ERROR;
        }
        ++s->references[result.bank]; result.planes[0] = result.mapping.data;
        result.planes[1] = result.mapping.data + result.item.length[0];
    }
    *out = result; unlock(s); return LB_OK;
}
void lb_snapshot_release(lb_snapshot_t *snapshot) {
    if (!snapshot || !snapshot->acquired || !snapshot->owner) return;
    lb_store_t *s = snapshot->owner; lock(s);
    if (snapshot->bank >= 0) {
        s->backend.unmap(s->backend.context, snapshot->mapping); --s->references[snapshot->bank];
    }
    memset(snapshot, 0, sizeof(*snapshot)); unlock(s);
}
