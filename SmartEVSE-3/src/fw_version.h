/*
 * fw_version.h — Pure C firmware version parsing and update comparison.
 *
 * This firmware uses a fork-branded CalVer scheme:
 *
 *     bm-YYYY.MM.N        e.g. bm-2026.09.1
 *      |   |    |  |
 *      |   |    |  +-- sequence within the month, starting at 1
 *      |   |    +----- month, 01-12, zero-padded
 *      |   +---------- year, 4 digits
 *      +-------------- distribution prefix, see FW_DISTRIBUTION
 *
 * The prefix exists because devices can be flashed between distributions from
 * the update page, and a bare "v3.13.0" says nothing about which repository
 * built it. It is deliberately NOT semver-compatible with the reference
 * codebase's vX.Y.Z: the two projects diverged long ago and share no meaningful
 * version lineage, so a comparable number would imply a relationship that does
 * not exist.
 *
 * The automatic updater accepts an update ONLY when both the running and the
 * candidate version parse as this scheme. That single rule preserves three
 * properties:
 *
 *   - home-compiled builds (VERSION looks like "11:20:03@Jun 17 2024") are
 *     never automatically overwritten;
 *   - a device running this firmware never accepts a reference-codebase
 *     vX.Y.Z release as "newer", and vice versa, so automatic updates can
 *     never silently move a device between distributions;
 *   - moving between distributions stays possible, but only as a deliberate
 *     manual action from the update page.
 */

#ifndef FW_VERSION_H
#define FW_VERSION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Distribution prefix. Also reported as "distribution" in the /settings JSON
 * and on the MQTT /Distribution topic. */
#define FW_DISTRIBUTION      "basmeerman"
#define FW_VERSION_PREFIX    "bm-"

typedef struct {
    uint16_t year;   /* 2000-9999 */
    uint8_t  month;  /* 1-12 */
    uint16_t seq;    /* 1-9999, sequence within the month */
} fw_version_t;

/*
 * Parse a "bm-YYYY.MM.N" string.
 *
 * Returns true and fills *out on success. Returns false for anything else,
 * including reference-codebase "vX.Y.Z" strings, release candidates,
 * home-compiled timestamps, NULL and empty input. Rejects out-of-range fields
 * and trailing garbage rather than accepting a partial parse.
 */
bool fw_version_parse(const char *s, fw_version_t *out);

/*
 * Compare two parsed versions.
 *
 * Returns <0 if a is older than b, 0 if identical, >0 if a is newer.
 */
int fw_version_compare(const fw_version_t *a, const fw_version_t *b);

/*
 * Decide whether the automatic updater should install `latest` over `current`.
 *
 * True only when both strings parse as this distribution's scheme AND latest is
 * strictly newer. Any unparseable side yields false — see the header comment
 * for why that is the safety property, not a limitation.
 */
bool fw_version_needs_update(const char *current, const char *latest);

#ifdef __cplusplus
}
#endif

#endif /* FW_VERSION_H */
