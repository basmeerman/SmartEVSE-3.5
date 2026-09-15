/*
 * test_ca_roots.c — Trust anchors for TLS connections made by the firmware
 *
 * Firmware downloads start at github.com / api.github.com (Sectigo, USERTrust
 * ECC root) but a release asset is served after a redirect from
 * release-assets.githubusercontent.com (Let's Encrypt, ISRG Root X1). The HTTP
 * client keeps the CA of the first request for the redirect, so the GitHub
 * bundle must contain both roots or the download can never verify.
 */

#include "test_framework.h"
#include "ca_roots.h"
#include <string.h>

/* Distinctive base64 lines of each root certificate */
#define USERTRUST_ECC_FRAGMENT "ZXR3b3JrMS4wLAYDVQQDEyVVU0VSVHJ1c3QgRUNDIENlcnRpZmljYXRpb24gQXV0"
#define ISRG_ROOT_X1_FRAGMENT  "ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY"

static int count_occurrences(const char *haystack, const char *needle) {
    int n = 0;
    size_t len = strlen(needle);
    for (const char *p = strstr(haystack, needle); p; p = strstr(p + len, needle))
        n++;
    return n;
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-059
 * @scenario The GitHub CA bundle trusts the release asset download host
 * @given GitHub redirects release downloads to a host chained to ISRG Root X1
 * @when The CA bundle used for GitHub requests is inspected
 * @then It contains ISRG Root X1
 */
void test_github_bundle_contains_isrg_root(void) {
    TEST_ASSERT_TRUE(strstr(CA_BUNDLE_GITHUB, ISRG_ROOT_X1_FRAGMENT) != NULL);
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-059
 * @scenario The GitHub CA bundle still trusts github.com and api.github.com
 * @given github.com and api.github.com chain to USERTrust ECC
 * @when The CA bundle used for GitHub requests is inspected
 * @then It contains USERTrust ECC
 */
void test_github_bundle_contains_usertrust_root(void) {
    TEST_ASSERT_TRUE(strstr(CA_BUNDLE_GITHUB, USERTRUST_ECC_FRAGMENT) != NULL);
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-059
 * @scenario The GitHub CA bundle is a well-formed sequence of PEM certificates
 * @given A PEM bundle made by concatenating root certificates
 * @when BEGIN and END markers are counted and the separation is checked
 * @then There are exactly two certificates, each ending in a newline before the next begins
 */
void test_github_bundle_is_well_formed_pem(void) {
    TEST_ASSERT_EQUAL_INT(2, count_occurrences(CA_BUNDLE_GITHUB, "-----BEGIN CERTIFICATE-----"));
    TEST_ASSERT_EQUAL_INT(2, count_occurrences(CA_BUNDLE_GITHUB, "-----END CERTIFICATE-----"));
    TEST_ASSERT_EQUAL_INT(0, count_occurrences(CA_BUNDLE_GITHUB, "-----END CERTIFICATE----------BEGIN"));
    TEST_ASSERT_EQUAL_INT(2, count_occurrences(CA_BUNDLE_GITHUB, "-----END CERTIFICATE-----\n"));
}

/*
 * @feature Firmware Update
 * @req REQ-PWR-059
 * @scenario The Let's Encrypt root used for MQTT and OCPP TLS is unchanged
 * @given MQTT and OCPP TLS connections trust ISRG Root X1 only
 * @when The Let's Encrypt root is inspected
 * @then It is exactly one certificate, ISRG Root X1
 */
void test_letsencrypt_root_is_isrg_only(void) {
    TEST_ASSERT_EQUAL_INT(1, count_occurrences(CA_ROOT_ISRG_X1_PEM, "-----BEGIN CERTIFICATE-----"));
    TEST_ASSERT_TRUE(strstr(CA_ROOT_ISRG_X1_PEM, ISRG_ROOT_X1_FRAGMENT) != NULL);
    TEST_ASSERT_TRUE(strstr(CA_ROOT_ISRG_X1_PEM, USERTRUST_ECC_FRAGMENT) == NULL);
}

int main(void) {
    TEST_SUITE_BEGIN("TLS Trust Anchors");

    RUN_TEST(test_github_bundle_contains_isrg_root);
    RUN_TEST(test_github_bundle_contains_usertrust_root);
    RUN_TEST(test_github_bundle_is_well_formed_pem);
    RUN_TEST(test_letsencrypt_root_is_isrg_only);

    TEST_SUITE_RESULTS();
}
