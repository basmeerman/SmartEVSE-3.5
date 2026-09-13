/*
 * ota_upload.c — Pure C bookkeeping for a chunked firmware upload.
 * See ota_upload.h for why finalising twice had to be made impossible.
 */

#include "ota_upload.h"
#include <stddef.h>

void ota_upload_begin(ota_upload_t *st, long size) {
    if (!st) {
        return;
    }
    st->active = (size > 0);
    st->finalized = false;
    st->size = size;
}

bool ota_upload_is_last_chunk(const ota_upload_t *st, long offset, long chunk_len) {
    if (!st || !st->active) {
        return false;
    }
    if (offset < 0 || chunk_len < 0) {
        return false;
    }
    return offset + chunk_len >= st->size;
}

bool ota_upload_take_finalize(ota_upload_t *st, long offset, long chunk_len) {
    if (!ota_upload_is_last_chunk(st, offset, chunk_len)) {
        return false;
    }
    if (st->finalized) {
        return false;
    }
    st->finalized = true;
    return true;
}

bool ota_upload_is_finalized(const ota_upload_t *st) {
    return st != NULL && st->finalized;
}
