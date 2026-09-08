/*
 * test_ocpp_meter_identity.c — Native tests for the meter identity reported in
 * the OCPP BootNotification.
 *
 * This is what an inboekdienstverlener checks at onboarding against a MID
 * conformity declaration, so the cases that matter are: the default is sent
 * when nothing is configured, an operator override actually replaces it, and a
 * serial is never invented when none was entered.
 */

#include "test_framework.h"
#include "ocpp_logic.h"
#include <string.h>

#define DEFAULT_NAME "Eastron3P"

/* ---- What gets published ---- */

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-060
 * @scenario Without manual identity the built-in meter name is published
 * @given Manual meter identity is off and the EV meter is an Eastron 3-phase
 * @when the BootNotification identity is resolved
 * @then meterType is the EMConfig name and no meterSerialNumber is reported
 */
void test_default_publishes_emconfig_name(void) {
    const char *type = NULL, *serial = "sentinel";
    ocpp_resolve_meter_identity(false, NULL, NULL, DEFAULT_NAME, &type, &serial);
    TEST_ASSERT_EQUAL_STRING(DEFAULT_NAME, type);
    TEST_ASSERT_TRUE(serial == NULL);
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-061
 * @scenario A manual override replaces the built-in name
 * @given Manual identity is on, with the type-approved model designation and a
 *        serial taken from the meter's type plate
 * @when the identity is resolved
 * @then Both operator values are published, not the EMConfig name
 */
void test_manual_override_is_published(void) {
    const char *type = NULL, *serial = NULL;
    ocpp_resolve_meter_identity(true, "Eastron SDM72D-M-MID", "21051234", DEFAULT_NAME,
                                &type, &serial);
    TEST_ASSERT_EQUAL_STRING("Eastron SDM72D-M-MID", type);
    TEST_ASSERT_EQUAL_STRING("21051234", serial);
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-062
 * @scenario Overrides are ignored while manual identity is switched off
 * @given Previously entered override values, with the manual switch off
 * @when the identity is resolved
 * @then The built-in name is published and the serial is omitted — turning the
 *       switch off must fully revert to the automatic identity
 */
void test_overrides_ignored_when_manual_off(void) {
    const char *type = NULL, *serial = NULL;
    ocpp_resolve_meter_identity(false, "Eastron SDM72D-M-MID", "21051234", DEFAULT_NAME,
                                &type, &serial);
    TEST_ASSERT_EQUAL_STRING(DEFAULT_NAME, type);
    TEST_ASSERT_TRUE(serial == NULL);
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-063
 * @scenario An empty type override falls back rather than publishing nothing
 * @given Manual identity is on but only the serial was filled in
 * @when the identity is resolved
 * @then meterType is the built-in name and the serial is still published
 */
void test_empty_type_falls_back_serial_kept(void) {
    const char *type = NULL, *serial = NULL;
    ocpp_resolve_meter_identity(true, "", "21051234", DEFAULT_NAME, &type, &serial);
    TEST_ASSERT_EQUAL_STRING(DEFAULT_NAME, type);
    TEST_ASSERT_EQUAL_STRING("21051234", serial);
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-064
 * @scenario A serial is never invented
 * @given Manual identity is on with an empty, whitespace-only, or NULL serial
 * @when the identity is resolved
 * @then meterSerialNumber is omitted in every case. During a fraud-prevention
 *       check an empty or blank serial looks like an answer; absence does not.
 */
void test_blank_serial_is_omitted(void) {
    const char *type = NULL, *serial = NULL;

    ocpp_resolve_meter_identity(true, "Eastron SDM72D-M-MID", "", DEFAULT_NAME, &type, &serial);
    TEST_ASSERT_TRUE(serial == NULL);

    ocpp_resolve_meter_identity(true, "Eastron SDM72D-M-MID", "   ", DEFAULT_NAME, &type, &serial);
    TEST_ASSERT_TRUE(serial == NULL);

    ocpp_resolve_meter_identity(true, "Eastron SDM72D-M-MID", NULL, DEFAULT_NAME, &type, &serial);
    TEST_ASSERT_TRUE(serial == NULL);
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-065
 * @scenario A whitespace-only type override falls back to the built-in name
 * @given Manual identity is on and the type field holds only spaces
 * @when the identity is resolved
 * @then The built-in name is published — a blank field is a typo, not a value
 */
void test_whitespace_type_falls_back(void) {
    const char *type = NULL, *serial = NULL;
    ocpp_resolve_meter_identity(true, "   ", NULL, DEFAULT_NAME, &type, &serial);
    TEST_ASSERT_EQUAL_STRING(DEFAULT_NAME, type);
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-066
 * @scenario Callers may ask for only one of the two values
 * @given A NULL out-pointer for the serial
 * @when the identity is resolved
 * @then The type is still produced and nothing is written through the NULL
 */
void test_null_out_pointers_are_safe(void) {
    const char *type = NULL;
    ocpp_resolve_meter_identity(true, "Eastron SDM72D-M-MID", "21051234", DEFAULT_NAME,
                                &type, NULL);
    TEST_ASSERT_EQUAL_STRING("Eastron SDM72D-M-MID", type);

    ocpp_resolve_meter_identity(true, "X", "Y", DEFAULT_NAME, NULL, NULL);  /* must not crash */
}

/* ---- Validation of what the operator may enter ---- */

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-067
 * @scenario Values within the OCPP field limit are accepted
 * @given A realistic model designation, a serial, and a value of exactly 25
 *        characters
 * @when each is validated
 * @then All are accepted
 */
void test_validate_accepts_valid_values(void) {
    char exact[OCPP_METER_FIELD_MAX + 1];
    memset(exact, 'A', OCPP_METER_FIELD_MAX);
    exact[OCPP_METER_FIELD_MAX] = '\0';

    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_OK, ocpp_validate_meter_field("Eastron SDM72D-M-MID"));
    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_OK, ocpp_validate_meter_field("21051234"));
    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_OK, ocpp_validate_meter_field(exact));
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-068
 * @scenario Empty input is accepted as "not set"
 * @given An empty string and NULL
 * @when each is validated
 * @then Both are accepted, since clearing a field is how the operator reverts
 */
void test_validate_accepts_empty(void) {
    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_OK, ocpp_validate_meter_field(""));
    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_OK, ocpp_validate_meter_field(NULL));
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-069
 * @scenario Values beyond the OCPP CiString25 limit are rejected
 * @given A 26-character value
 * @when it is validated
 * @then It is rejected, rather than being silently truncated by the CSMS
 */
void test_validate_rejects_too_long(void) {
    char toolong[OCPP_METER_FIELD_MAX + 2];
    memset(toolong, 'A', OCPP_METER_FIELD_MAX + 1);
    toolong[OCPP_METER_FIELD_MAX + 1] = '\0';

    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_TOO_LONG, ocpp_validate_meter_field(toolong));
}

/*
 * @feature OCPP Meter Identity
 * @req REQ-OCPP-070
 * @scenario Non-printable characters are rejected
 * @given Values containing a newline and a tab
 * @when each is validated
 * @then Both are rejected before they can reach the BootNotification JSON
 */
void test_validate_rejects_control_characters(void) {
    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_BAD_CHARS, ocpp_validate_meter_field("SDM72D\nMID"));
    TEST_ASSERT_EQUAL_INT(OCPP_VALIDATE_BAD_CHARS, ocpp_validate_meter_field("SDM72D\tMID"));
}

int main(void) {
    TEST_SUITE_BEGIN("OCPP Meter Identity");

    RUN_TEST(test_default_publishes_emconfig_name);
    RUN_TEST(test_manual_override_is_published);
    RUN_TEST(test_overrides_ignored_when_manual_off);
    RUN_TEST(test_empty_type_falls_back_serial_kept);
    RUN_TEST(test_blank_serial_is_omitted);
    RUN_TEST(test_whitespace_type_falls_back);
    RUN_TEST(test_null_out_pointers_are_safe);
    RUN_TEST(test_validate_accepts_valid_values);
    RUN_TEST(test_validate_accepts_empty);
    RUN_TEST(test_validate_rejects_too_long);
    RUN_TEST(test_validate_rejects_control_characters);

    TEST_SUITE_RESULTS();
}
