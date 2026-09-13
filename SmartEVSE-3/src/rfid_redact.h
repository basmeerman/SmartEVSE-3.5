/*
 * rfid_redact.h — Pure C fingerprinting of RFID UIDs for debug output.
 *
 * An RFID UID is a credential: it is the whole of what authorizes a charge on
 * this device. Printing one in full to the debug console hands anyone who can
 * reach the (unauthenticated, LAN-side) telnet port a card they can clone, and
 * the same applies to the serial console on a bench.
 *
 * Debugging card handling does still need to distinguish one card from another
 * — "which card did I just present", "which slot did that overwrite" — so this
 * module produces a short fingerprint instead of removing the value entirely:
 *
 *     04ac11bac01690   ->   "04ac..."
 *
 * Four hex characters is enough to tell apart the handful of cards on one
 * charger while leaving 24 of a 7-byte UID's 56 bits unstated, so a log capture
 * does not yield a usable card. This is the "first 4 chars + ellipsis" form
 * that CLAUDE.md permits for credential material.
 *
 * Fingerprints are for humans reading logs. Never parse one, never compare two
 * for authorization, and never widen the prefix to make the log more useful.
 */

#ifndef RFID_REDACT_H
#define RFID_REDACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hex characters of the UID that appear in a fingerprint. */
#define RFID_FINGERPRINT_PREFIX_CHARS 4

/* Buffer size a caller must provide: 4 hex + "..." + NUL. */
#define RFID_FINGERPRINT_MAX 8

/*
 * Fingerprint a UID held as raw bytes.
 *
 * Writes at most `outsize` bytes and always NUL-terminates, provided outsize
 * is non-zero and out is non-NULL. A NULL, zero-length, or all-zero UID yields
 * "?" — an absent card must not look like a short one.
 *
 * Note on the two reader formats: a 6-byte UID is stored from RFID[1] with the
 * 0x01 family code in RFID[0], so callers pass the pointer and length of the
 * significant bytes, not the raw 8-byte buffer. See the call sites in
 * OneWire.cpp.
 */
void rfid_fingerprint(const uint8_t *uid, size_t len, char *out, size_t outsize);

/*
 * Fingerprint a UID that is already a hex string, as received over MQTT or in
 * an uploaded rfid.txt line.
 *
 * Only leading hex digits are copied, so a malformed or hostile payload cannot
 * push arbitrary bytes — control characters, ANSI escapes, newlines forging a
 * second log line — into the debug stream through this path. Input with no
 * leading hex digit yields "?".
 */
void rfid_fingerprint_hex(const char *hex, char *out, size_t outsize);

#ifdef __cplusplus
}
#endif

#endif /* RFID_REDACT_H */
