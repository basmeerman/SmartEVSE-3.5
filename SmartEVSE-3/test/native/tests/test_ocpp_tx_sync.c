/*
 * test_ocpp_tx_sync.c — detection of OCPP transactions MicroOcpp gives up on
 *
 * MicroOcpp retries StartTransaction/StopTransaction TransactionMessageAttempts
 * times, then marks the transaction silent and deletes its meter data without
 * telling anyone. On a half-open WebSocket the attempts are spent quickly, and
 * real sessions never reached the backend (issue #200). These tests cover the
 * pure decisions the firmware uses to notice and count that, and to raise the
 * library's default retry limit.
 */

#include "test_framework.h"
#include "ocpp_logic.h"

/*
 * @feature OCPP Transaction Sync
 * @req REQ-OCPP-126
 * @scenario Transaction still syncing is kept under watch
 * @given A watched transaction that is not silent and whose StopTransaction is not confirmed
 * @when ocpp_tx_watch_decide is called
 * @then Returns KEEP
 */
void test_tx_watch_keeps_pending(void) {
    TEST_ASSERT_EQUAL_INT(OCPP_TXWATCH_KEEP, ocpp_tx_watch_decide(false, false));
}

/*
 * @feature OCPP Transaction Sync
 * @req REQ-OCPP-126
 * @scenario Confirmed StopTransaction releases the watch as synced
 * @given A watched transaction whose StopTransaction was confirmed by the backend
 * @when ocpp_tx_watch_decide is called
 * @then Returns SYNCED
 */
void test_tx_watch_synced_on_stop_confirmed(void) {
    TEST_ASSERT_EQUAL_INT(OCPP_TXWATCH_SYNCED, ocpp_tx_watch_decide(false, true));
}

/*
 * @feature OCPP Transaction Sync
 * @req REQ-OCPP-126
 * @scenario Silent transaction is reported as discarded
 * @given MicroOcpp exceeded TransactionMessageAttempts and marked the transaction silent
 * @when ocpp_tx_watch_decide is called
 * @then Returns DISCARDED
 */
void test_tx_watch_discarded_when_silent(void) {
    TEST_ASSERT_EQUAL_INT(OCPP_TXWATCH_DISCARDED, ocpp_tx_watch_decide(true, false));
}

/*
 * @feature OCPP Transaction Sync
 * @req REQ-OCPP-126
 * @scenario Silent wins over the local stop confirmation
 * @given A silent transaction whose StopTransaction MicroOcpp confirmed locally without sending it
 * @when ocpp_tx_watch_decide is called
 * @then Returns DISCARDED, not SYNCED
 */
void test_tx_watch_silent_overrides_stop_confirmed(void) {
    TEST_ASSERT_EQUAL_INT(OCPP_TXWATCH_DISCARDED, ocpp_tx_watch_decide(true, true));
}

/*
 * @feature OCPP Transaction Sync
 * @req REQ-OCPP-127
 * @scenario Library default retry limit is raised
 * @given TransactionMessageAttempts holds the MicroOcpp default of 3
 * @when ocpp_tx_attempts_upgrade is called
 * @then Returns OCPP_TX_ATTEMPTS_DEFAULT (10)
 */
void test_tx_attempts_upgrade_from_library_default(void) {
    TEST_ASSERT_EQUAL_INT(OCPP_TX_ATTEMPTS_DEFAULT, ocpp_tx_attempts_upgrade(3));
    TEST_ASSERT_EQUAL_INT(10, OCPP_TX_ATTEMPTS_DEFAULT);
}

/*
 * @feature OCPP Transaction Sync
 * @req REQ-OCPP-127
 * @scenario A value set by the backend is left alone
 * @given TransactionMessageAttempts is 5 (set by the backend via ChangeConfiguration)
 * @when ocpp_tx_attempts_upgrade is called
 * @then Returns 5 unchanged
 */
void test_tx_attempts_keeps_backend_value(void) {
    TEST_ASSERT_EQUAL_INT(5, ocpp_tx_attempts_upgrade(5));
    TEST_ASSERT_EQUAL_INT(1, ocpp_tx_attempts_upgrade(1));
}

/*
 * @feature OCPP Transaction Sync
 * @req REQ-OCPP-127
 * @scenario Upgraded value is stable
 * @given TransactionMessageAttempts already holds 10
 * @when ocpp_tx_attempts_upgrade is called
 * @then Returns 10, so a reboot does not change it again
 */
void test_tx_attempts_upgrade_idempotent(void) {
    TEST_ASSERT_EQUAL_INT(10, ocpp_tx_attempts_upgrade(10));
}

int main(void) {
    TEST_SUITE_BEGIN("OCPP Transaction Sync");

    RUN_TEST(test_tx_watch_keeps_pending);
    RUN_TEST(test_tx_watch_synced_on_stop_confirmed);
    RUN_TEST(test_tx_watch_discarded_when_silent);
    RUN_TEST(test_tx_watch_silent_overrides_stop_confirmed);
    RUN_TEST(test_tx_attempts_upgrade_from_library_default);
    RUN_TEST(test_tx_attempts_keeps_backend_value);
    RUN_TEST(test_tx_attempts_upgrade_idempotent);

    TEST_SUITE_RESULTS();
}
