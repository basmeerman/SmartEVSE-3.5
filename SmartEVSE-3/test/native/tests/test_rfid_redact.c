/*
 * test_rfid_redact.c — Native tests for RFID UID fingerprinting in debug output.
 *
 * The property under test is a security one: a debug log must let an operator
 * tell two cards apart without letting a reader of that log reconstruct one.
 * So the cases that matter are how much is shown, that the rest is genuinely
 * gone, and that a hostile payload cannot use this path to write whatever it
 * likes into the log.
 */

#include "test_framework.h"
#include "rfid_redact.h"
#include <string.h>

/* The 7-byte UID observed on the bench charger, which is what prompted this. */
static const uint8_t UID7[7] = {0x04, 0xac, 0x11, 0xba, 0xc0, 0x16, 0x90};

/* ---- What a fingerprint shows ---- */

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-080
 * @scenario A 7-byte UID is reduced to four hex characters
 * @given The UID 04ac11bac01690 read from a card
 * @when it is fingerprinted
 * @then The output is "04ac..." — the operator can identify the card, a reader
 *       of the log gets 16 of its 56 bits
 */
void test_seven_byte_uid_is_shortened(void) {
    char out[RFID_FINGERPRINT_MAX];
    rfid_fingerprint(UID7, sizeof(UID7), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("04ac...", out);
}

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-081
 * @scenario The remaining bytes do not appear anywhere in the output
 * @given The same UID
 * @when it is fingerprinted
 * @then No later byte of the UID is present in the string. This is the actual
 *       security claim; the exact prefix format is secondary to it.
 */
void test_trailing_bytes_are_absent(void) {
    char out[RFID_FINGERPRINT_MAX];
    rfid_fingerprint(UID7, sizeof(UID7), out, sizeof(out));
    TEST_ASSERT_TRUE(strstr(out, "11") == NULL);
    TEST_ASSERT_TRUE(strstr(out, "ba") == NULL);
    TEST_ASSERT_TRUE(strstr(out, "c0") == NULL);
    TEST_ASSERT_TRUE(strstr(out, "16") == NULL);
    TEST_ASSERT_TRUE(strstr(out, "90") == NULL);
}

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-082
 * @scenario Two different cards produce different fingerprints
 * @given Two UIDs differing within the first two bytes
 * @when both are fingerprinted
 * @then The outputs differ, so the log still distinguishes the cards
 */
void test_different_cards_are_distinguishable(void) {
    static const uint8_t other[7] = {0x04, 0x9f, 0x11, 0xba, 0xc0, 0x16, 0x90};
    char a[RFID_FINGERPRINT_MAX], b[RFID_FINGERPRINT_MAX];
    rfid_fingerprint(UID7, sizeof(UID7), a, sizeof(a));
    rfid_fingerprint(other, sizeof(other), b, sizeof(b));
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
}

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-083
 * @scenario A 6-byte UID from the old reader format fingerprints the same way
 * @given The six significant bytes, the 0x01 family code having been skipped
 *        by the caller
 * @when they are fingerprinted
 * @then The first two bytes are shown, so both reader formats read alike in
 *       the log
 */
void test_six_byte_uid(void) {
    static const uint8_t uid6[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11};
    char out[RFID_FINGERPRINT_MAX];
    rfid_fingerprint(uid6, sizeof(uid6), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("dead...", out);
}

/* ---- Absent cards ---- */

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-084
 * @scenario No card reads as unknown rather than as a card
 * @given A NULL pointer, a zero length, and the all-zero buffer that RFID[]
 *        holds when no card is present
 * @when each is fingerprinted
 * @then All yield "?" — "0000..." would look like a real card in the log
 */
void test_absent_card_reads_as_unknown(void) {
    static const uint8_t zeros[7] = {0};
    char out[RFID_FINGERPRINT_MAX];

    rfid_fingerprint(NULL, 7, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("?", out);

    rfid_fingerprint(UID7, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("?", out);

    rfid_fingerprint(zeros, sizeof(zeros), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("?", out);
}

/* ---- Hex-string input, as received over MQTT or in rfid.txt ---- */

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-085
 * @scenario A hex payload is shortened the same way as raw bytes
 * @given The MQTT payload "04ac11bac01690"
 * @when it is fingerprinted
 * @then The result matches the byte-form fingerprint of the same card, so one
 *       card logs identically whichever path it arrived by
 */
void test_hex_payload_matches_byte_form(void) {
    char from_hex[RFID_FINGERPRINT_MAX], from_bytes[RFID_FINGERPRINT_MAX];
    rfid_fingerprint_hex("04ac11bac01690", from_hex, sizeof(from_hex));
    rfid_fingerprint(UID7, sizeof(UID7), from_bytes, sizeof(from_bytes));
    TEST_ASSERT_EQUAL_STRING("04ac...", from_hex);
    TEST_ASSERT_EQUAL_STRING(from_bytes, from_hex);
}

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-086
 * @scenario Uppercase hex normalises to lowercase
 * @given The same UID typed in uppercase in an uploaded rfid.txt
 * @when it is fingerprinted
 * @then The output is lowercase, so the same card cannot appear as two
 *       different entries in a log
 */
void test_hex_payload_is_normalised(void) {
    char out[RFID_FINGERPRINT_MAX];
    rfid_fingerprint_hex("04AC11BAC01690", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("04ac...", out);
}

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-087
 * @scenario A hostile payload cannot inject bytes into the log
 * @given Payloads carrying a newline, an ANSI escape, and a percent sign —
 *        an attacker publishing to Set/RFID controls this string completely
 * @when each is fingerprinted
 * @then Copying stops at the first non-hex character, so no control character
 *       reaches the debug stream and no second log line can be forged
 */
void test_hostile_payload_is_truncated_at_first_non_hex(void) {
    char out[RFID_FINGERPRINT_MAX];

    rfid_fingerprint_hex("04\nSTATE: A Error: 0", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("04...", out);

    rfid_fingerprint_hex("ab\x1b[31mred", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("ab...", out);

    rfid_fingerprint_hex("0%s%n", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0...", out);
}

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-088
 * @scenario Input with no leading hex digit reads as unknown
 * @given NULL, an empty payload, and a payload starting with a letter outside
 *        the hex range
 * @when each is fingerprinted
 * @then All yield "?" rather than a bare ellipsis that would look like a
 *       redacted card
 */
void test_non_hex_input_reads_as_unknown(void) {
    char out[RFID_FINGERPRINT_MAX];

    rfid_fingerprint_hex(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("?", out);

    rfid_fingerprint_hex("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("?", out);

    rfid_fingerprint_hex("zz11", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("?", out);
}

/* ---- Buffer safety ---- */

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-089
 * @scenario An undersized buffer is never overrun
 * @given Output buffers of 1 through RFID_FINGERPRINT_MAX bytes, each followed
 *        by a guard byte
 * @when a UID is fingerprinted into each
 * @then Every result is NUL-terminated within its buffer and the guard byte is
 *       untouched
 */
void test_short_buffers_are_safe(void) {
    static char scratch[RFID_FINGERPRINT_MAX + 1];
    size_t size;

    for (size = 1; size <= RFID_FINGERPRINT_MAX; size++) {
        memset(scratch, 0x7f, sizeof(scratch));
        rfid_fingerprint(UID7, sizeof(UID7), scratch, size);
        TEST_ASSERT_TRUE(memchr(scratch, '\0', size) != NULL);
        TEST_ASSERT_EQUAL_INT(0x7f, (unsigned char)scratch[size]);

        memset(scratch, 0x7f, sizeof(scratch));
        rfid_fingerprint_hex("04ac11bac01690", scratch, size);
        TEST_ASSERT_TRUE(memchr(scratch, '\0', size) != NULL);
        TEST_ASSERT_EQUAL_INT(0x7f, (unsigned char)scratch[size]);
    }
}

/*
 * @feature RFID Redaction
 * @req REQ-AUTH-090
 * @scenario A NULL output buffer is tolerated
 * @given out == NULL, and outsize == 0
 * @when a UID is fingerprinted
 * @then Neither call writes anything or crashes — a logging helper must never
 *       be the thing that takes the charger down
 */
void test_null_output_is_safe(void) {
    char out[RFID_FINGERPRINT_MAX];
    rfid_fingerprint(UID7, sizeof(UID7), NULL, sizeof(out));
    rfid_fingerprint(UID7, sizeof(UID7), out, 0);
    rfid_fingerprint_hex("04ac", NULL, sizeof(out));
    rfid_fingerprint_hex("04ac", out, 0);
}

int main(void) {
    TEST_SUITE_BEGIN("RFID Redaction");

    RUN_TEST(test_seven_byte_uid_is_shortened);
    RUN_TEST(test_trailing_bytes_are_absent);
    RUN_TEST(test_different_cards_are_distinguishable);
    RUN_TEST(test_six_byte_uid);
    RUN_TEST(test_absent_card_reads_as_unknown);
    RUN_TEST(test_hex_payload_matches_byte_form);
    RUN_TEST(test_hex_payload_is_normalised);
    RUN_TEST(test_hostile_payload_is_truncated_at_first_non_hex);
    RUN_TEST(test_non_hex_input_reads_as_unknown);
    RUN_TEST(test_short_buffers_are_safe);
    RUN_TEST(test_null_output_is_safe);

    TEST_SUITE_RESULTS();
}
