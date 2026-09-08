/*
 * http_auth.c — Pure C HTTP auth decision. See http_auth.h.
 */

#include "http_auth.h"
#include <string.h>

/* ASCII-only lowercase for case-insensitive DNS/IP host comparison. Avoids
 * locale-dependent tolower() and works for the A–Z range that actually appears
 * in hostnames. Sufficient because RFC 1035 labels are LDH (letters/digits/
 * hyphen) and we only need case-insensitivity for the letter subset. */
static int http_auth_ascii_icmp(const char *a, size_t an, const char *b) {
    size_t i = 0;
    for (; i < an; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (cb == '\0') return 1;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return (int)ca - (int)cb;
    }
    return (b[i] == '\0') ? 0 : -1;
}

/* Return true if `origin` is this device's own origin.
 *
 * Parses `origin` as `scheme://HOST[:PORT][/path...]` and compares the HOST
 * portion case-insensitively to `host` (exact match, with optional port and
 * optional trailing slash). Unlike the earlier substring check, this rejects
 * attacker-controlled origins that merely contain `host` as a substring, e.g.
 *   host="smartevse.local"     → origin="http://smartevse.local.evil.com" → false
 *   host="192.168.1.50"        → origin="http://192.168.1.50.nip.io"      → false
 *   host="smartevse.local"     → origin="http://evil.com/smartevse.local" → false
 * while still accepting the legitimate same-origin cases:
 *   host="192.168.1.50"        → origin="http://192.168.1.50"             → true
 *   host="192.168.1.50"        → origin="http://192.168.1.50:8443"        → true
 *   host="smartevse-1234.local"→ origin="http://SmartEVSE-1234.LOCAL/"    → true
 */
static bool http_auth_origin_matches(const char *origin, const char *host) {
    if (!origin || !host) return false;
    if (origin[0] == '\0' || host[0] == '\0') return false;

    /* Locate scheme. Only http:// and https:// are accepted. */
    const char *host_start = NULL;
    if (strncmp(origin, "http://", 7) == 0) {
        host_start = origin + 7;
    } else if (strncmp(origin, "https://", 8) == 0) {
        host_start = origin + 8;
    } else {
        return false;
    }

    /* HOST ends at the first of ':' (port), '/' (path), or end-of-string.
     * A RFC 3986 authority may also contain user-info ("user@"), but browsers
     * do not include userinfo in Origin; if one does, the '@' will appear
     * before any ':' or '/' and we will refuse to match (desired — treat it
     * as suspicious). */
    size_t host_len = 0;
    while (host_start[host_len] != '\0' &&
           host_start[host_len] != ':'  &&
           host_start[host_len] != '/'  &&
           host_start[host_len] != '@') {
        host_len++;
    }

    /* If we hit '@', the authority carries userinfo — reject. */
    if (host_start[host_len] == '@') return false;

    /* Case-insensitive exact match against the configured host. */
    return http_auth_ascii_icmp(host_start, host_len, host) == 0;
}

http_auth_result_t http_auth_decide(uint8_t        auth_mode,
                                    uint16_t       lcd_pin,
                                    bool           lcd_password_ok,
                                    uint32_t       password_ok_ts_ms,
                                    uint32_t       now_ms,
                                    const char    *origin_header,
                                    const char    *allowed_origin_host) {
    /* AuthMode=0: legacy — let everything through. Preserves backward
     * compatibility on upgrade for every existing installation. */
    if (auth_mode == AUTH_MODE_OFF) {
        return HTTP_AUTH_ALLOW;
    }

    /* AuthMode>=1 with no PIN provisioned (LCDPin default 0): auth is not
     * reachable. Refuse regardless of the LCDPasswordOK flag — `atoi("")==0`
     * would otherwise let an empty-body POST to /lcd-verify-password mark the
     * session authenticated. This mirrors http_api_allow_unsigned_upload(),
     * which refuses the same lcd_pin==0 state. */
    if (lcd_pin == 0) {
        return HTTP_AUTH_DENY_UNAUTH;
    }

    /* AuthMode>=1: the PIN must have been verified recently. */
    if (!lcd_password_ok) {
        return HTTP_AUTH_DENY_UNAUTH;
    }

    /* Session-timeout: once idle past HTTP_AUTH_SESSION_TIMEOUT_MS, revoke.
     * Caller is responsible for resetting LCDPasswordOK based on this result.
     * Guard against a zero timestamp meaning "never set" to avoid spurious
     * expiration on cold boot with the flag somehow already true. */
    if (password_ok_ts_ms != 0 &&
        (now_ms - password_ok_ts_ms) >= HTTP_AUTH_SESSION_TIMEOUT_MS) {
        return HTTP_AUTH_DENY_SESSION_EXPIRED;
    }

    /* CSRF Origin check: only when the request carries an Origin header. A
     * missing Origin is normal for non-browser clients (curl, Home Assistant,
     * custom scripts) that deliberately integrate with the REST API. Block
     * only when Origin is PRESENT and does NOT match the device's own host. */
    if (origin_header != NULL && origin_header[0] != '\0') {
        if (!http_auth_origin_matches(origin_header, allowed_origin_host)) {
            return HTTP_AUTH_DENY_CSRF;
        }
    }

    return HTTP_AUTH_ALLOW;
}
