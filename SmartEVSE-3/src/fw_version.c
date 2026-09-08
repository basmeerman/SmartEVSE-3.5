/*
 * fw_version.c — Pure C firmware version parsing and update comparison.
 * See fw_version.h for the scheme and the safety properties it encodes.
 */

#include "fw_version.h"
#include <string.h>

/* Parse exactly `digits` decimal characters into *out. Returns the position
 * after the field, or NULL if the field is not exactly that many digits. Using
 * a fixed width keeps "bm-2026.9.1" and "bm-20260.09.1" out rather than
 * silently accepting them and producing a version that sorts wrongly. */
static const char *parse_fixed(const char *s, int digits, uint16_t *out) {
    uint16_t value = 0;
    int i;
    for (i = 0; i < digits; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return NULL;
        }
        value = (uint16_t)(value * 10 + (uint16_t)(s[i] - '0'));
    }
    *out = value;
    return s + digits;
}

/* Parse 1-4 digits, terminated by end of string. Used for the sequence field,
 * which has no fixed width. */
static bool parse_seq(const char *s, uint16_t *out) {
    uint16_t value = 0;
    int digits = 0;
    if (*s == '\0') {
        return false;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return false;                      /* trailing garbage */
        }
        if (++digits > 4) {
            return false;
        }
        value = (uint16_t)(value * 10 + (uint16_t)(*s - '0'));
        s++;
    }
    *out = value;
    return true;
}

bool fw_version_parse(const char *s, fw_version_t *out) {
    uint16_t year = 0, month = 0, seq = 0;

    if (s == NULL || out == NULL) {
        return false;
    }
    if (strncmp(s, FW_VERSION_PREFIX, strlen(FW_VERSION_PREFIX)) != 0) {
        return false;                          /* not our distribution */
    }
    s += strlen(FW_VERSION_PREFIX);

    s = parse_fixed(s, 4, &year);
    if (s == NULL || *s != '.') {
        return false;
    }
    s++;

    s = parse_fixed(s, 2, &month);
    if (s == NULL || *s != '.') {
        return false;
    }
    s++;

    if (!parse_seq(s, &seq)) {
        return false;
    }

    if (year < 2000 || month < 1 || month > 12 || seq < 1) {
        return false;
    }

    out->year  = year;
    out->month = (uint8_t)month;
    out->seq   = seq;
    return true;
}

int fw_version_compare(const fw_version_t *a, const fw_version_t *b) {
    if (a->year != b->year) {
        return a->year < b->year ? -1 : 1;
    }
    if (a->month != b->month) {
        return a->month < b->month ? -1 : 1;
    }
    if (a->seq != b->seq) {
        return a->seq < b->seq ? -1 : 1;
    }
    return 0;
}

bool fw_version_needs_update(const char *current, const char *latest) {
    fw_version_t cur, lat;

    /* Both sides must be this distribution's scheme. An unparseable current
     * version means a home-compiled or foreign build, which must never be
     * automatically replaced; an unparseable latest means the release feed is
     * not ours. Either way: no automatic update. */
    if (!fw_version_parse(current, &cur)) {
        return false;
    }
    if (!fw_version_parse(latest, &lat)) {
        return false;
    }
    return fw_version_compare(&cur, &lat) < 0;
}
