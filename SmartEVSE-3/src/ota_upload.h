/*
 * ota_upload.h — Pure C bookkeeping for a chunked firmware upload.
 *
 * /update receives a firmware image as a series of POSTs, each carrying an
 * offset and the total size. The handler decides from those two numbers alone
 * when the last byte has arrived, and then finalises: flush the partition,
 * check the signature, switch the boot partition.
 *
 * That decision is not idempotent, and it used to be made inline with no
 * memory of having been made before. Finalising a second time is destructive
 * rather than merely wasteful: the handler starts by pointing the boot
 * partition back at the running image, then finds Update.end() fails because
 * the update is already closed, concludes the upload was bad, and erases the
 * partition it just wrote. A completed, signature-verified update is silently
 * undone.
 *
 * Reaching the end twice is easy. The stock update page sends a trailing
 * zero-length POST at offset == size after the final chunk, and offset + 0 >=
 * size satisfies the same end-of-file test. A browser retry, a refresh, or a
 * duplicated request does the same thing.
 *
 * This module holds the little state needed to answer "is this the end, and is
 * it the first time we have reached it" so the handler can be safely
 * re-entered. It is pure C with no platform dependency so the sequencing can
 * be tested natively; the actual flashing stays in the glue layer.
 */

#ifndef OTA_UPLOAD_H
#define OTA_UPLOAD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;      /* an upload is in progress (offset 0 seen) */
    bool finalized;   /* the end was reached and acted on once */
    long size;        /* total bytes announced by the client, signature included */
} ota_upload_t;

/*
 * Forget any previous upload. Call when a new upload starts (offset == 0), so
 * that a failed attempt cannot make the next one look already finished.
 */
void ota_upload_begin(ota_upload_t *st, long size);

/*
 * True once the bytes described by this chunk reach the announced size.
 * Mirrors the handler's original test, including the zero-length chunk at
 * offset == size, which is why the finalize guard below is needed.
 */
bool ota_upload_is_last_chunk(const ota_upload_t *st, long offset, long chunk_len);

/*
 * True exactly once per upload: for the first chunk that reaches the end.
 * Every later call returns false, including repeats of the same final chunk.
 *
 * Marks the upload finalized as a side effect, so the caller cannot forget to.
 * Returns false when no upload is active, so a stray POST after a reboot or
 * against a fresh handler does nothing.
 */
bool ota_upload_take_finalize(ota_upload_t *st, long offset, long chunk_len);

/*
 * True when the end has already been handled — the caller should acknowledge
 * the request and change nothing.
 */
bool ota_upload_is_finalized(const ota_upload_t *st);

#ifdef __cplusplus
}
#endif

#endif /* OTA_UPLOAD_H */
