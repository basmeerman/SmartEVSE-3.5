/*
 * rfid_redact.c — Pure C fingerprinting of RFID UIDs for debug output.
 * See rfid_redact.h for why the value is shortened rather than removed.
 */

#include "rfid_redact.h"

static const char HEX_DIGITS[] = "0123456789abcdef";

/* Write "?" for the cases where there is no card to fingerprint. Kept in one
 * place so an absent UID can never be mistaken for a very short one. */
static void write_unknown(char *out, size_t outsize) {
    if (outsize >= 2) {
        out[0] = '?';
        out[1] = '\0';
    } else if (outsize == 1) {
        out[0] = '\0';
    }
}

/* Append as much of "..." as fits, then terminate. The ellipsis is what tells
 * a reader the value was shortened deliberately, so it is worth the bytes. */
static void finish(char *out, size_t outsize, size_t pos) {
    size_t i;
    for (i = 0; i < 3 && pos + 1 < outsize; i++) {
        out[pos++] = '.';
    }
    out[pos] = '\0';
}

static int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static char to_lower_hex(char c) {
    return (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
}

void rfid_fingerprint(const uint8_t *uid, size_t len, char *out, size_t outsize) {
    size_t pos = 0;
    size_t i;
    int all_zero = 1;

    if (out == NULL || outsize == 0) {
        return;
    }
    if (uid == NULL || len == 0) {
        write_unknown(out, outsize);
        return;
    }

    /* RFID[] is memset to zero when no card is held. Printing "0000..." for
     * that would be a fingerprint of nothing, and reads as a real card. */
    for (i = 0; i < len; i++) {
        if (uid[i] != 0x00) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        write_unknown(out, outsize);
        return;
    }

    for (i = 0; i < len && pos + 1 < outsize && pos < RFID_FINGERPRINT_PREFIX_CHARS; i++) {
        out[pos++] = HEX_DIGITS[(uid[i] >> 4) & 0x0f];
        if (pos + 1 < outsize && pos < RFID_FINGERPRINT_PREFIX_CHARS) {
            out[pos++] = HEX_DIGITS[uid[i] & 0x0f];
        }
    }

    finish(out, outsize, pos);
}

void rfid_fingerprint_hex(const char *hex, char *out, size_t outsize) {
    size_t pos = 0;

    if (out == NULL || outsize == 0) {
        return;
    }
    if (hex == NULL || !is_hex_digit(hex[0])) {
        write_unknown(out, outsize);
        return;
    }

    while (pos + 1 < outsize && pos < RFID_FINGERPRINT_PREFIX_CHARS && is_hex_digit(hex[pos])) {
        out[pos] = to_lower_hex(hex[pos]);
        pos++;
    }

    finish(out, outsize, pos);
}
