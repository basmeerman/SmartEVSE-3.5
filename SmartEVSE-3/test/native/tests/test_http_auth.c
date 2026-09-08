/*
 * test_http_auth.c — pure C tests for the HTTP auth decision (Plan 16 Phase 1).
 * See src/http_auth.h and docs/security/plan-16-http-auth-layer.md.
 */

#include "test_framework.h"
#include "http_auth.h"

/* A non-zero PIN used by the happy-path tests. REQ-AUTH-006 adds dedicated
 * coverage for lcd_pin==0 (the "auth not provisioned" state). */
#define PIN_SET 1234

/* ---- AuthMode=OFF — everything passes through (backward compat) ---- */

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-001
 * @scenario AuthMode=OFF allows any request (no PIN, no Origin)
 */
void test_auth_off_allows_unauthenticated(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_OFF, PIN_SET,
            /*pw_ok*/ false, /*pw_ts*/ 0, /*now*/ 1000,
            /*origin*/ NULL, /*host*/ NULL));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-001
 * @scenario AuthMode=OFF allows request with foreign Origin (no CSRF check)
 */
void test_auth_off_allows_foreign_origin(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_OFF, PIN_SET, false, 0, 1000,
            "http://evil.example", "smartevse.local"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-001
 * @scenario AuthMode=OFF + lcd_pin=0 still allows (legacy upgrade path)
 * @given Legacy installation with AuthMode never enabled and no PIN set
 * @when Any request arrives
 * @then Allow — backward compat preserved regardless of PIN provisioning
 */
void test_auth_off_allows_when_pin_zero(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_OFF, /*lcd_pin*/ 0,
            false, 0, 1000, NULL, NULL));
}

/* ---- AuthMode=REQUIRED — denies unauthenticated ---- */

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-002
 * @scenario AuthMode=REQUIRED denies request without PIN verification
 */
void test_auth_required_denies_unauth(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_UNAUTH,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET,
            false, 0, 1000, NULL, "smartevse.local"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-002
 * @scenario AuthMode=REQUIRED allows PIN-verified request
 */
void test_auth_required_allows_authed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET,
            /*pw_ok*/ true, /*pw_ts*/ 900, /*now*/ 1000,
            NULL, "smartevse.local"));
}

/* ---- Session timeout ---- */

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-003
 * @scenario Authenticated session expires after HTTP_AUTH_SESSION_TIMEOUT_MS idle
 */
void test_auth_session_expires(void) {
    uint32_t now = 10UL * HTTP_AUTH_SESSION_TIMEOUT_MS;  /* safely past the window */
    uint32_t ts  = now - HTTP_AUTH_SESSION_TIMEOUT_MS - 1;
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_SESSION_EXPIRED,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, ts, now, NULL, NULL));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-003
 * @scenario Authenticated session still valid just before the timeout boundary
 */
void test_auth_session_just_before_timeout(void) {
    uint32_t now = 10UL * HTTP_AUTH_SESSION_TIMEOUT_MS;
    uint32_t ts  = now - HTTP_AUTH_SESSION_TIMEOUT_MS + 1;
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, ts, now, NULL, NULL));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-003
 * @scenario Session with zero timestamp is treated as "never set" (defensive)
 */
void test_auth_session_zero_ts_does_not_expire(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 0, 99999UL, NULL, NULL));
}

/* ---- CSRF Origin check ---- */

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-004
 * @scenario Missing Origin header allowed (non-browser integration)
 */
void test_auth_no_origin_allowed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            NULL, "192.168.1.50"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-004
 * @scenario Matching Origin allowed
 */
void test_auth_matching_origin_allowed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://192.168.1.50", "192.168.1.50"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-004
 * @scenario Matching hostname in origin allowed
 */
void test_auth_matching_hostname_origin_allowed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://smartevse-1234.local", "smartevse-1234.local"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-004
 * @scenario Foreign Origin blocked as CSRF
 */
void test_auth_foreign_origin_blocked(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_CSRF,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://evil.example", "192.168.1.50"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-004
 * @scenario Origin with unexpected scheme blocked
 */
void test_auth_origin_bad_scheme_blocked(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_CSRF,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "ws://192.168.1.50", "192.168.1.50"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-004
 * @scenario https:// Origin matching device IP allowed
 */
void test_auth_https_matching_origin_allowed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "https://192.168.1.50:8443", "192.168.1.50"));
}

/* ---- Precedence — CSRF check only applied when PIN ok ---- */

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-005
 * @scenario Unauth + foreign Origin reports UNAUTH first (PIN check precedes CSRF)
 */
void test_auth_unauth_precedes_csrf(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_UNAUTH,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, false, 0, 1000,
            "http://evil.example", "192.168.1.50"));
}

/* ---- REQ-AUTH-006 — LCDPin=0 must never produce ALLOW when AuthMode>=1 ----
 *
 * Without this guard, an owner who enables AuthMode=REQUIRED but forgets to
 * set a PIN (LCDPin defaults to 0) is exposed to a LAN attacker who can
 * unlock the session by POSTing an empty password to /lcd-verify-password,
 * because `atoi("") == 0 == LCDPin` sets LCDPasswordOK=true.
 */

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-006
 * @scenario AuthMode=REQUIRED with no PIN configured denies unauthenticated request
 * @given AuthMode=REQUIRED, lcd_pin=0, lcd_password_ok=false
 * @when A request arrives at a require_auth-gated endpoint
 * @then Return DENY_UNAUTH — auth is not reachable until a PIN is provisioned
 */
void test_auth_required_no_pin_configured_denies(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_UNAUTH,
        http_auth_decide(AUTH_MODE_REQUIRED, /*lcd_pin*/ 0,
            false, 0, 1000, NULL, "192.168.1.50"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-006
 * @scenario AuthMode=REQUIRED with no PIN configured ignores LCDPasswordOK=true
 * @given Somehow lcd_password_ok=true (bug, stale state, or bypass attempt) but lcd_pin=0
 * @when A request arrives at a require_auth-gated endpoint
 * @then Return DENY_UNAUTH — a cleared PIN must invalidate any cached auth
 */
void test_auth_required_no_pin_denies_even_if_flag_set(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_UNAUTH,
        http_auth_decide(AUTH_MODE_REQUIRED, /*lcd_pin*/ 0,
            /*pw_ok*/ true, /*pw_ts*/ 900, /*now*/ 1000,
            /*origin*/ "http://192.168.1.50", /*host*/ "192.168.1.50"));
}

/* ---- REQ-AUTH-007 — Origin matcher must be host-exact, not substring ----
 *
 * The previous `strstr(origin, host)` implementation accepted any origin whose
 * hostname contained the device host as a substring. An attacker-controlled
 * domain such as `smartevse-1234.local.evil.com` would pass, enabling CSRF
 * against any admin with a live session. These tests fix the check to a
 * scheme-aware host-exact comparison with optional :port suffix.
 */

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-007
 * @scenario Attacker subdomain that suffixes the device mDNS host is rejected
 * @given Device host is "smartevse-1234.local", admin has a live session
 * @when Origin is "http://smartevse-1234.local.evil.com"
 * @then DENY_CSRF — the host must match exactly, not just be a substring
 */
void test_auth_csrf_substring_suffix_rejected(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_CSRF,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://smartevse-1234.local.evil.com",
            "smartevse-1234.local"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-007
 * @scenario Attacker IP-suffix domain (nip.io-style) is rejected
 * @given Device IP is 192.168.1.50
 * @when Origin is "http://192.168.1.50.nip.io"
 * @then DENY_CSRF
 */
void test_auth_csrf_ip_suffix_rejected(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_CSRF,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://192.168.1.50.nip.io",
            "192.168.1.50"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-007
 * @scenario Attacker domain that prefixes the device host is rejected
 * @given Device host is "smartevse.local"
 * @when Origin is "http://evil.smartevse.local" (subdomain of attacker-owned TLD)
 * @then DENY_CSRF
 */
void test_auth_csrf_substring_prefix_rejected(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_CSRF,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://evilsmartevse.local",
            "smartevse.local"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-007
 * @scenario Origin that embeds device host in userinfo/path is rejected
 * @given Device host is "smartevse.local"
 * @when Origin is "http://evil.com/smartevse.local" (host portion is "evil.com")
 * @then DENY_CSRF — only the hostname portion of the Origin is compared
 */
void test_auth_csrf_host_in_path_rejected(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_DENY_CSRF,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://evil.com/smartevse.local",
            "smartevse.local"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-007
 * @scenario Case-insensitive host match accepted (DNS labels are case-insensitive)
 * @given Device host is "smartevse-1234.local"
 * @when Origin is "http://SmartEVSE-1234.LOCAL"
 * @then ALLOW
 */
void test_auth_csrf_case_insensitive_match_allowed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://SmartEVSE-1234.LOCAL",
            "smartevse-1234.local"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-007
 * @scenario Matching host with explicit port accepted
 * @given Device host is "192.168.1.50"
 * @when Origin is "http://192.168.1.50:80"
 * @then ALLOW
 */
void test_auth_csrf_matching_host_with_port_allowed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://192.168.1.50:80",
            "192.168.1.50"));
}

/*
 * @feature HTTP Auth
 * @req REQ-AUTH-007
 * @scenario Matching host with trailing slash accepted
 * @given Device host is "smartevse.local"
 * @when Origin is "http://smartevse.local/" (browsers don't send this, but be safe)
 * @then ALLOW
 */
void test_auth_csrf_matching_host_with_trailing_slash_allowed(void) {
    TEST_ASSERT_EQUAL_INT(HTTP_AUTH_ALLOW,
        http_auth_decide(AUTH_MODE_REQUIRED, PIN_SET, true, 900, 1000,
            "http://smartevse.local/",
            "smartevse.local"));
}

int main(void) {
    TEST_SUITE_BEGIN("HTTP Auth");

    RUN_TEST(test_auth_off_allows_unauthenticated);
    RUN_TEST(test_auth_off_allows_foreign_origin);
    RUN_TEST(test_auth_off_allows_when_pin_zero);
    RUN_TEST(test_auth_required_denies_unauth);
    RUN_TEST(test_auth_required_allows_authed);
    RUN_TEST(test_auth_session_expires);
    RUN_TEST(test_auth_session_just_before_timeout);
    RUN_TEST(test_auth_session_zero_ts_does_not_expire);
    RUN_TEST(test_auth_no_origin_allowed);
    RUN_TEST(test_auth_matching_origin_allowed);
    RUN_TEST(test_auth_matching_hostname_origin_allowed);
    RUN_TEST(test_auth_foreign_origin_blocked);
    RUN_TEST(test_auth_origin_bad_scheme_blocked);
    RUN_TEST(test_auth_https_matching_origin_allowed);
    RUN_TEST(test_auth_unauth_precedes_csrf);
    RUN_TEST(test_auth_required_no_pin_configured_denies);
    RUN_TEST(test_auth_required_no_pin_denies_even_if_flag_set);
    RUN_TEST(test_auth_csrf_substring_suffix_rejected);
    RUN_TEST(test_auth_csrf_ip_suffix_rejected);
    RUN_TEST(test_auth_csrf_substring_prefix_rejected);
    RUN_TEST(test_auth_csrf_host_in_path_rejected);
    RUN_TEST(test_auth_csrf_case_insensitive_match_allowed);
    RUN_TEST(test_auth_csrf_matching_host_with_port_allowed);
    RUN_TEST(test_auth_csrf_matching_host_with_trailing_slash_allowed);

    TEST_SUITE_RESULTS();
}
