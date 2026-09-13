/*
 * test_ota_upload.c — Native tests for chunked firmware upload sequencing.
 *
 * The bug these exist for: the /update handler reached its end-of-file branch
 * twice for one upload — once for the last real chunk, once for the trailing
 * zero-length POST the update page sends afterwards. The second pass pointed
 * the boot partition back at the running image and erased the freshly written,
 * signature-verified one, so a successful update silently reverted to the old
 * firmware with no error shown. Finalising exactly once is therefore a safety
 * property, not an optimisation.
 */

#include "test_framework.h"
#include "ota_upload.h"

#define IMAGE_SIZE 1546017L   /* a signed release image: firmware + 512b signature */
#define CHUNK      4096L

/* ---- Reaching the end ---- */

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-030
 * @scenario A chunk in the middle of the image is not the end
 * @given An upload of a full image, and a chunk well before the end
 * @when the chunk is tested
 * @then It is not treated as the last chunk
 */
void test_middle_chunk_is_not_the_end(void) {
    ota_upload_t st;
    ota_upload_begin(&st, IMAGE_SIZE);
    TEST_ASSERT_FALSE(ota_upload_is_last_chunk(&st, 0, CHUNK));
    TEST_ASSERT_FALSE(ota_upload_is_last_chunk(&st, IMAGE_SIZE / 2, CHUNK));
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-031
 * @scenario The chunk carrying the final byte is the end
 * @given A final partial chunk that completes the announced size
 * @when the chunk is tested
 * @then It is the last chunk
 */
void test_final_chunk_is_the_end(void) {
    ota_upload_t st;
    long tail = IMAGE_SIZE % CHUNK;
    ota_upload_begin(&st, IMAGE_SIZE);
    TEST_ASSERT_TRUE(ota_upload_is_last_chunk(&st, IMAGE_SIZE - tail, tail));
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-032
 * @scenario The trailing zero-length POST also reads as the end
 * @given The update page's extra POST at offset == size with an empty body
 * @when the chunk is tested
 * @then It is still the last chunk — which is exactly why finalising needs a
 *       guard rather than relying on this test alone
 */
void test_trailing_empty_chunk_reads_as_the_end(void) {
    ota_upload_t st;
    ota_upload_begin(&st, IMAGE_SIZE);
    TEST_ASSERT_TRUE(ota_upload_is_last_chunk(&st, IMAGE_SIZE, 0));
}

/* ---- Finalising exactly once ---- */

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-033
 * @scenario The full upload sequence finalises on the last chunk only
 * @given An upload walked chunk by chunk to completion
 * @when each chunk asks to finalize
 * @then Only the chunk carrying the final byte is granted it
 */
void test_finalize_granted_once_across_whole_upload(void) {
    ota_upload_t st;
    long offset;
    int grants = 0;

    ota_upload_begin(&st, IMAGE_SIZE);
    for (offset = 0; offset < IMAGE_SIZE; offset += CHUNK) {
        long len = (IMAGE_SIZE - offset < CHUNK) ? (IMAGE_SIZE - offset) : CHUNK;
        if (ota_upload_take_finalize(&st, offset, len)) {
            grants++;
            TEST_ASSERT_EQUAL_INT(1, (int)(offset + len == IMAGE_SIZE));
        }
    }
    TEST_ASSERT_EQUAL_INT(1, grants);
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-034
 * @scenario The trailing empty POST is refused finalisation
 * @given An upload already finalised by its last real chunk
 * @when the update page's zero-length POST at offset == size arrives
 * @then It is refused. This is the bug: granting it erased the verified image
 *       and reverted the boot partition to the old firmware.
 */
void test_trailing_empty_post_cannot_finalize_again(void) {
    ota_upload_t st;
    long tail = IMAGE_SIZE % CHUNK;

    ota_upload_begin(&st, IMAGE_SIZE);
    TEST_ASSERT_TRUE(ota_upload_take_finalize(&st, IMAGE_SIZE - tail, tail));
    TEST_ASSERT_FALSE(ota_upload_take_finalize(&st, IMAGE_SIZE, 0));
    TEST_ASSERT_TRUE(ota_upload_is_finalized(&st));
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-035
 * @scenario A replayed final chunk is refused
 * @given A browser retry or duplicated request repeating the same final chunk
 * @when it asks to finalize again
 * @then It is refused — retries must not be able to undo a completed update
 */
void test_replayed_final_chunk_is_refused(void) {
    ota_upload_t st;
    long tail = IMAGE_SIZE % CHUNK;

    ota_upload_begin(&st, IMAGE_SIZE);
    TEST_ASSERT_TRUE(ota_upload_take_finalize(&st, IMAGE_SIZE - tail, tail));
    TEST_ASSERT_FALSE(ota_upload_take_finalize(&st, IMAGE_SIZE - tail, tail));
    TEST_ASSERT_FALSE(ota_upload_take_finalize(&st, IMAGE_SIZE - tail, tail));
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-036
 * @scenario A new upload may finalise again
 * @given A completed upload, then a fresh one started at offset 0
 * @when the new upload reaches its end
 * @then It finalises normally — the guard is per upload, not permanent
 */
void test_new_upload_can_finalize_again(void) {
    ota_upload_t st;

    ota_upload_begin(&st, 100);
    TEST_ASSERT_TRUE(ota_upload_take_finalize(&st, 60, 40));
    TEST_ASSERT_FALSE(ota_upload_take_finalize(&st, 100, 0));

    ota_upload_begin(&st, 100);
    TEST_ASSERT_FALSE(ota_upload_is_finalized(&st));
    TEST_ASSERT_TRUE(ota_upload_take_finalize(&st, 60, 40));
}

/* ---- Stray input ---- */

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-037
 * @scenario A POST arriving with no upload in progress finalises nothing
 * @given A zeroed state, as after a reboot, with no offset-0 chunk seen
 * @when a chunk claims to reach the end
 * @then It is refused, so a stray or replayed request cannot touch a partition
 */
void test_no_active_upload_finalizes_nothing(void) {
    ota_upload_t st = {false, false, 0};
    TEST_ASSERT_FALSE(ota_upload_is_last_chunk(&st, 100, 0));
    TEST_ASSERT_FALSE(ota_upload_take_finalize(&st, 100, 0));
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-038
 * @scenario A zero or negative announced size never starts an upload
 * @given A size of 0 and a negative size
 * @when an upload is begun and a chunk offered
 * @then Nothing is active and nothing finalises
 */
void test_bad_size_never_activates(void) {
    ota_upload_t st;

    ota_upload_begin(&st, 0);
    TEST_ASSERT_FALSE(ota_upload_take_finalize(&st, 0, 0));

    ota_upload_begin(&st, -1);
    TEST_ASSERT_FALSE(ota_upload_take_finalize(&st, 0, 0));
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-039
 * @scenario Negative offsets and lengths are rejected
 * @given A malformed offset or chunk length from a hand-crafted request
 * @when the chunk is tested
 * @then It is not treated as the end
 */
void test_negative_values_are_rejected(void) {
    ota_upload_t st;
    ota_upload_begin(&st, IMAGE_SIZE);
    TEST_ASSERT_FALSE(ota_upload_is_last_chunk(&st, -1, IMAGE_SIZE));
    TEST_ASSERT_FALSE(ota_upload_is_last_chunk(&st, IMAGE_SIZE, -1));
}

/*
 * @feature OTA Upload Sequencing
 * @req REQ-API-040
 * @scenario NULL state is tolerated
 * @given A NULL pointer
 * @when each entry point is called
 * @then Nothing crashes and nothing reports success
 */
void test_null_state_is_safe(void) {
    ota_upload_begin(NULL, 100);
    TEST_ASSERT_FALSE(ota_upload_is_last_chunk(NULL, 0, 0));
    TEST_ASSERT_FALSE(ota_upload_take_finalize(NULL, 0, 0));
    TEST_ASSERT_FALSE(ota_upload_is_finalized(NULL));
}

int main(void) {
    TEST_SUITE_BEGIN("OTA Upload Sequencing");

    RUN_TEST(test_middle_chunk_is_not_the_end);
    RUN_TEST(test_final_chunk_is_the_end);
    RUN_TEST(test_trailing_empty_chunk_reads_as_the_end);
    RUN_TEST(test_finalize_granted_once_across_whole_upload);
    RUN_TEST(test_trailing_empty_post_cannot_finalize_again);
    RUN_TEST(test_replayed_final_chunk_is_refused);
    RUN_TEST(test_new_upload_can_finalize_again);
    RUN_TEST(test_no_active_upload_finalizes_nothing);
    RUN_TEST(test_bad_size_never_activates);
    RUN_TEST(test_negative_values_are_rejected);
    RUN_TEST(test_null_state_is_safe);

    TEST_SUITE_RESULTS();
}
