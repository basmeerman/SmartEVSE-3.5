/*
 * test_ocpp_secrets.c — keep the OCPP AuthorizationKey out of replies and logs
 *
 * MicroOcppMongoose declares AuthorizationKey as a normal, readable
 * configuration key, so GetConfiguration returned its value to the backend,
 * and debug builds printed OCPP traffic, including that reply, on the
 * unauthenticated telnet console (issue #203). The firmware drops the value
 * from GetConfiguration replies and redacts it from console lines. These tests
 * cover the pure decisions behind both.
 */

#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "ocpp_logic.h"

/* ---- Which configuration keys are secret ---- */

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-130
 * @scenario AuthorizationKey is a secret configuration key
 * @given The configuration key name "AuthorizationKey"
 * @when ocpp_config_key_is_secret is called
 * @then Returns true, so GetConfiguration drops its value
 */
void test_secret_key_authorization_key(void) {
    TEST_ASSERT_TRUE(ocpp_config_key_is_secret("AuthorizationKey"));
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-130
 * @scenario Other configuration keys stay readable
 * @given The keys HeartbeatInterval, Cst_BackendUrl, an empty name, a near miss and NULL
 * @when ocpp_config_key_is_secret is called
 * @then Returns false for each
 */
void test_secret_key_others_readable(void) {
    TEST_ASSERT_FALSE(ocpp_config_key_is_secret("HeartbeatInterval"));
    TEST_ASSERT_FALSE(ocpp_config_key_is_secret("Cst_BackendUrl"));
    TEST_ASSERT_FALSE(ocpp_config_key_is_secret(""));
    TEST_ASSERT_FALSE(ocpp_config_key_is_secret("AuthorizationKeyX"));
    TEST_ASSERT_FALSE(ocpp_config_key_is_secret(NULL));
}

/* ---- Console redaction ---- */

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario GetConfiguration reply on the console is redacted
 * @given A "[MO] Send:" line with an AuthorizationKey entry and a readable key after it
 * @when ocpp_redact_auth_key is called
 * @then The AuthorizationKey value becomes *** and the other value is unchanged
 */
void test_redact_getconfiguration_reply(void) {
    char line[256];
    snprintf(line, sizeof(line), "%s",
        "[MO] Send: [3,\"1\",{\"configurationKey\":[{\"key\":\"AuthorizationKey\",\"readonly\":false,"
        "\"value\":\"0123456789ABCDEF\"},{\"key\":\"HeartbeatInterval\",\"readonly\":false,\"value\":\"300\"}]}]");
    TEST_ASSERT_EQUAL_INT(1, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING(
        "[MO] Send: [3,\"1\",{\"configurationKey\":[{\"key\":\"AuthorizationKey\",\"readonly\":false,"
        "\"value\":\"***\"},{\"key\":\"HeartbeatInterval\",\"readonly\":false,\"value\":\"300\"}]}]", line);
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario ChangeConfiguration with the value before the key is redacted
 * @given A "[MO] Recv:" ChangeConfiguration line whose value field comes before the key field
 * @when ocpp_redact_auth_key is called
 * @then The value becomes ***
 */
void test_redact_changeconfiguration_value_first(void) {
    char line[256];
    snprintf(line, sizeof(line), "%s",
        "[MO] Recv: [2,\"9\",\"ChangeConfiguration\",{\"value\":\"secret42\",\"key\":\"AuthorizationKey\"}]");
    TEST_ASSERT_EQUAL_INT(1, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING(
        "[MO] Recv: [2,\"9\",\"ChangeConfiguration\",{\"value\":\"***\",\"key\":\"AuthorizationKey\"}]", line);
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario Line truncated inside the value is redacted to the end
 * @given A console line cut off by the 256-byte console buffer in the middle of the key value
 * @when ocpp_redact_auth_key is called
 * @then Everything after the opening quote of the value becomes ***
 */
void test_redact_truncated_value(void) {
    char line[256];
    snprintf(line, sizeof(line), "%s",
        "[MO] Send: [3,\"1\",{\"configurationKey\":[{\"key\":\"AuthorizationKey\",\"readonly\":false,\"value\":\"0123");
    TEST_ASSERT_EQUAL_INT(1, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING(
        "[MO] Send: [3,\"1\",{\"configurationKey\":[{\"key\":\"AuthorizationKey\",\"readonly\":false,\"value\":\"***", line);
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario Short value is masked without growing the line
 * @given An AuthorizationKey value of two characters in a buffer with no spare room
 * @when ocpp_redact_auth_key is called
 * @then The value becomes ** and the line length is unchanged
 */
void test_redact_short_value_no_growth(void) {
    char line[] = "{\"key\":\"AuthorizationKey\",\"value\":\"ab\"}";
    size_t before = strlen(line);
    TEST_ASSERT_EQUAL_INT(1, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"AuthorizationKey\",\"value\":\"**\"}", line);
    TEST_ASSERT_EQUAL_INT((int) before, (int) strlen(line));
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario Escaped quote inside the value does not end the redaction early
 * @given An AuthorizationKey value containing an escaped quote
 * @when ocpp_redact_auth_key is called
 * @then The whole value becomes *** and the closing quote is kept
 */
void test_redact_escaped_quote(void) {
    char line[] = "{\"key\":\"AuthorizationKey\",\"value\":\"ab\\\"cdef\"}";
    TEST_ASSERT_EQUAL_INT(1, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"AuthorizationKey\",\"value\":\"***\"}", line);
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario Entry without a value is left alone
 * @given A GetConfiguration entry for AuthorizationKey whose value was already dropped
 * @when ocpp_redact_auth_key is called
 * @then Returns 0 and the line is unchanged, including the next entry's value
 */
void test_redact_entry_without_value(void) {
    const char *orig = "[{\"key\":\"AuthorizationKey\",\"readonly\":false},{\"key\":\"X\",\"value\":\"keep\"}]";
    char line[128];
    snprintf(line, sizeof(line), "%s", orig);
    TEST_ASSERT_EQUAL_INT(0, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING(orig, line);
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario MicroOcppMongoose auth token debug line is redacted
 * @given The MO_DL_DEBUG line "auth Token=<ChargeBoxId>:<key> (...)"
 * @when ocpp_redact_auth_key is called
 * @then The ChargeBoxId stays and the key becomes ***
 */
void test_redact_auth_token_line(void) {
    char line[128];
    snprintf(line, sizeof(line), "%s",
        "[MO] debug (MicroOcppMongooseClient.cpp:279): auth Token=EVSE7803:0011AABB (key will be converted to non-hex)");
    TEST_ASSERT_EQUAL_INT(1, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING(
        "[MO] debug (MicroOcppMongooseClient.cpp:279): auth Token=EVSE7803:*** (key will be converted to non-hex)", line);
}

/*
 * @feature OCPP Secret Handling
 * @req REQ-OCPP-131
 * @scenario Lines without the key are not touched
 * @given A StatusNotification line, an empty string and NULL
 * @when ocpp_redact_auth_key is called
 * @then Returns 0 and the line is unchanged; NULL does not crash
 */
void test_redact_no_key_untouched(void) {
    const char *orig = "[MO] Send: [2,\"5\",\"StatusNotification\",{\"connectorId\":1,\"value\":\"x\"}]";
    char line[128];
    snprintf(line, sizeof(line), "%s", orig);
    TEST_ASSERT_EQUAL_INT(0, (int) ocpp_redact_auth_key(line));
    TEST_ASSERT_EQUAL_STRING(orig, line);
    char empty[1] = "";
    TEST_ASSERT_EQUAL_INT(0, (int) ocpp_redact_auth_key(empty));
    TEST_ASSERT_EQUAL_INT(0, (int) ocpp_redact_auth_key(NULL));
}

int main(void) {
    TEST_SUITE_BEGIN("OCPP Secret Handling");

    RUN_TEST(test_secret_key_authorization_key);
    RUN_TEST(test_secret_key_others_readable);
    RUN_TEST(test_redact_getconfiguration_reply);
    RUN_TEST(test_redact_changeconfiguration_value_first);
    RUN_TEST(test_redact_truncated_value);
    RUN_TEST(test_redact_short_value_no_growth);
    RUN_TEST(test_redact_escaped_quote);
    RUN_TEST(test_redact_entry_without_value);
    RUN_TEST(test_redact_auth_token_line);
    RUN_TEST(test_redact_no_key_untouched);

    TEST_SUITE_RESULTS();
}
