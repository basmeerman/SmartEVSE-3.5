/*
 * test_fw_version.c — Native tests for the bm- CalVer firmware version scheme
 * and the automatic-update decision built on it.
 *
 * The important cases here are the refusals: the updater must never move a
 * device between distributions on its own, and must never overwrite a
 * home-compiled build.
 */

#include "test_framework.h"
#include "fw_version.h"
#include <string.h>

/* ---- Parsing ---- */

/*
 * @feature Firmware Update
 * @req REQ-PWR-035
 * @scenario A well-formed distribution version parses into its fields
 * @given The version string "bm-2026.09.1"
 * @when fw_version_parse is called
 * @then It succeeds with year 2026, month 9 and sequence 1
 */
void test_parse_valid_version(void) {
    fw_version_t v;
    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.09.1", &v));
    TEST_ASSERT_EQUAL_INT(2026, v.year);
    TEST_ASSERT_EQUAL_INT(9, v.month);
    TEST_ASSERT_EQUAL_INT(1, v.seq);
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-036
 * @scenario A multi-digit sequence within one month parses
 * @given The version string "bm-2026.12.42"
 * @when fw_version_parse is called
 * @then It succeeds with sequence 42
 */
void test_parse_multi_digit_sequence(void) {
    fw_version_t v;
    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.12.42", &v));
    TEST_ASSERT_EQUAL_INT(12, v.month);
    TEST_ASSERT_EQUAL_INT(42, v.seq);
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-037
 * @scenario Reference-codebase version strings are not our scheme
 * @given Version strings in the reference codebase's vX.Y.Z and RC forms
 * @when fw_version_parse is called
 * @then Every one of them is refused
 */
void test_parse_rejects_upstream_versions(void) {
    fw_version_t v;
    TEST_ASSERT_FALSE(fw_version_parse("v3.13.0", &v));
    TEST_ASSERT_FALSE(fw_version_parse("v3.17.0-RC13", &v));
    TEST_ASSERT_FALSE(fw_version_parse("3.13.0", &v));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-038
 * @scenario A home-compiled build stamp is not a version
 * @given The timestamp string a local build produces
 * @when fw_version_parse is called
 * @then It is refused
 */
void test_parse_rejects_home_compiled(void) {
    fw_version_t v;
    TEST_ASSERT_FALSE(fw_version_parse("11:20:03@Jun 17 2024", &v));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-039
 * @scenario Malformed field widths and trailing characters are refused
 * @given Versions with an unpadded month, a five-digit year, a missing
 *        sequence, or trailing text after a valid version
 * @when fw_version_parse is called
 * @then All are refused rather than partially accepted
 */
void test_parse_rejects_malformed(void) {
    fw_version_t v;
    TEST_ASSERT_FALSE(fw_version_parse("bm-2026.9.1", &v));       /* month not padded */
    TEST_ASSERT_FALSE(fw_version_parse("bm-20260.09.1", &v));     /* 5-digit year */
    TEST_ASSERT_FALSE(fw_version_parse("bm-2026.09.", &v));       /* no sequence */
    TEST_ASSERT_FALSE(fw_version_parse("bm-2026.09.1x", &v));     /* trailing garbage */
    TEST_ASSERT_FALSE(fw_version_parse("bm-2026.09", &v));        /* truncated */
    TEST_ASSERT_FALSE(fw_version_parse("bm-", &v));
    TEST_ASSERT_FALSE(fw_version_parse("", &v));
    TEST_ASSERT_FALSE(fw_version_parse(NULL, &v));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-040
 * @scenario Out-of-range field values are refused
 * @given Versions with month 00, month 13 and sequence 0
 * @when fw_version_parse is called
 * @then All are refused
 */
void test_parse_rejects_out_of_range(void) {
    fw_version_t v;
    TEST_ASSERT_FALSE(fw_version_parse("bm-2026.00.1", &v));
    TEST_ASSERT_FALSE(fw_version_parse("bm-2026.13.1", &v));
    TEST_ASSERT_FALSE(fw_version_parse("bm-2026.09.0", &v));
}

/* ---- Ordering ---- */

/*
 * @feature Firmware Update
 * @req REQ-PWR-041
 * @scenario Versions order by year, then month, then sequence
 * @given Pairs differing in each field in turn
 * @when fw_version_compare is called
 * @then The more recent version compares greater, and equal versions compare 0
 */
void test_compare_orders_by_field_precedence(void) {
    fw_version_t a, b;

    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.09.1", &a));
    TEST_ASSERT_TRUE(fw_version_parse("bm-2027.01.1", &b));
    TEST_ASSERT_TRUE(fw_version_compare(&a, &b) < 0);   /* year wins over month */

    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.09.9", &a));
    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.10.1", &b));
    TEST_ASSERT_TRUE(fw_version_compare(&a, &b) < 0);   /* month wins over sequence */

    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.09.2", &a));
    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.09.10", &b));
    TEST_ASSERT_TRUE(fw_version_compare(&a, &b) < 0);   /* numeric, not lexical */

    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.09.1", &a));
    TEST_ASSERT_TRUE(fw_version_parse("bm-2026.09.1", &b));
    TEST_ASSERT_EQUAL_INT(0, fw_version_compare(&a, &b));
}

/* ---- Update decision ---- */

/*
 * @feature Firmware Update
 * @req REQ-PWR-042
 * @scenario A newer release of this distribution is installed
 * @given The device runs bm-2026.09.1 and the release feed offers bm-2026.10.1
 * @when fw_version_needs_update is called
 * @then It returns true
 */
void test_update_accepts_newer_same_distribution(void) {
    TEST_ASSERT_TRUE(fw_version_needs_update("bm-2026.09.1", "bm-2026.10.1"));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-043
 * @scenario An identical or older release is not installed
 * @given The device runs bm-2026.10.1
 * @when the feed offers the same version, or an older one
 * @then No update is performed in either case
 */
void test_update_refuses_same_or_older(void) {
    TEST_ASSERT_FALSE(fw_version_needs_update("bm-2026.10.1", "bm-2026.10.1"));
    TEST_ASSERT_FALSE(fw_version_needs_update("bm-2026.10.1", "bm-2026.09.1"));
    TEST_ASSERT_FALSE(fw_version_needs_update("bm-2026.10.2", "bm-2026.10.1"));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-044
 * @scenario The updater never moves a device between distributions
 * @given A device on this distribution seeing a reference-codebase release, and
 *        a device on a reference-codebase build seeing one of ours
 * @when fw_version_needs_update is called for both directions
 * @then Both are refused, so cross-distribution flashing stays a manual action
 */
void test_update_refuses_cross_distribution(void) {
    TEST_ASSERT_FALSE(fw_version_needs_update("bm-2026.09.1", "v3.14.0"));
    TEST_ASSERT_FALSE(fw_version_needs_update("v3.13.0", "bm-2026.09.1"));
    TEST_ASSERT_FALSE(fw_version_needs_update("v3.13.0", "v3.14.0"));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-045
 * @scenario A home-compiled build is never automatically overwritten
 * @given The device runs a local build stamped "11:20:03@Jun 17 2024"
 * @when the release feed offers a valid newer release
 * @then No update is performed
 */
void test_update_refuses_home_compiled(void) {
    TEST_ASSERT_FALSE(fw_version_needs_update("11:20:03@Jun 17 2024", "bm-2026.10.1"));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-047
 * @scenario A nightly build is never automatically replaced by a release
 * @given The device runs a nightly build, stamped "nightly-<short sha>" by the
 *        nightly workflow
 * @when the release feed offers a newer stable release
 * @then No update is performed — running nightly is a deliberate choice, and
 *       the updater must not silently undo it
 */
void test_update_refuses_nightly_build(void) {
    TEST_ASSERT_FALSE(fw_version_parse("nightly-a1b2c3d", NULL));
    TEST_ASSERT_FALSE(fw_version_needs_update("nightly-a1b2c3d", "bm-2026.10.1"));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-046
 * @scenario Missing or empty version data never triggers an update
 * @given NULL or empty strings on either side of the comparison
 * @when fw_version_needs_update is called
 * @then Every combination is refused
 */
void test_update_refuses_null_and_empty(void) {
    TEST_ASSERT_FALSE(fw_version_needs_update(NULL, "bm-2026.10.1"));
    TEST_ASSERT_FALSE(fw_version_needs_update("bm-2026.09.1", NULL));
    TEST_ASSERT_FALSE(fw_version_needs_update("", "bm-2026.10.1"));
    TEST_ASSERT_FALSE(fw_version_needs_update("bm-2026.09.1", ""));
}

int main(void) {
    TEST_SUITE_BEGIN("Firmware Version Scheme");

    RUN_TEST(test_parse_valid_version);
    RUN_TEST(test_parse_multi_digit_sequence);
    RUN_TEST(test_parse_rejects_upstream_versions);
    RUN_TEST(test_parse_rejects_home_compiled);
    RUN_TEST(test_parse_rejects_malformed);
    RUN_TEST(test_parse_rejects_out_of_range);
    RUN_TEST(test_compare_orders_by_field_precedence);
    RUN_TEST(test_update_accepts_newer_same_distribution);
    RUN_TEST(test_update_refuses_same_or_older);
    RUN_TEST(test_update_refuses_cross_distribution);
    RUN_TEST(test_update_refuses_home_compiled);
    RUN_TEST(test_update_refuses_nightly_build);
    RUN_TEST(test_update_refuses_null_and_empty);

    TEST_SUITE_RESULTS();
}
