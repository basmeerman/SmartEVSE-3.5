/*
 * test_ocpp_stop_sync.c — StopTransaction meter value synchronization
 *
 * Tests the pure C decision ocpp_stop_tx_ready() that gates MicroOcpp's
 * StopTxReadyInput. MicroOcpp reads meterStop the moment that input returns
 * true. After a reboot during charging, ocppInit() ends the restored
 * transaction with reason PowerLoss before the EV meter has delivered its
 * first Modbus reading, so meterStop was sampled as 0 and the backend stored
 * the session with no energy (issue #202).
 */

#include "test_framework.h"
#include "ocpp_logic.h"

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-123
 * @scenario StopTransaction waits for the settle window after charging stops
 * @given Charging stopped 4 seconds ago, EV meter reading is valid
 * @when ocpp_stop_tx_ready is called
 * @then Returns WAIT so the final Modbus readings can come through
 */
void test_stop_waits_during_settle_window(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(
        /*now_ms*/            100000UL,
        /*sync_ms*/           96000UL,
        /*ev_meter_present*/  true,
        /*energy_wh*/         7537105,
        /*meter_start_wh*/    7532754);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_WAIT, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-123
 * @scenario StopTransaction proceeds after the settle window with a valid reading
 * @given Charging stopped 5 seconds ago, energy 7537105 Wh >= meterStart 7532754 Wh
 * @when ocpp_stop_tx_ready is called
 * @then Returns READY and the live reading is used as meterStop
 */
void test_stop_ready_after_settle_with_valid_meter(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(100000UL, 95000UL, true, 7537105, 7532754);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_READY, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-124
 * @scenario PowerLoss stop at boot waits for the first EV meter reading
 * @given Boot 10 seconds ago, restored transaction with meterStart 7532754 Wh
 * @given The EV meter has not been read yet (energy 0 Wh)
 * @when ocpp_stop_tx_ready is called
 * @then Returns WAIT instead of letting MicroOcpp sample meterStop = 0
 */
void test_stop_waits_for_first_meter_reading_at_boot(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(20000UL, 10000UL, true, 0, 7532754);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_WAIT, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-124
 * @scenario Reading below meterStart is not accepted as meterStop
 * @given Settle window elapsed, energy 1000 Wh but meterStart 7532754 Wh
 * @when ocpp_stop_tx_ready is called
 * @then Returns WAIT because the reading cannot belong to this transaction
 */
void test_stop_waits_when_reading_below_meter_start(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(30000UL, 10000UL, true, 1000, 7532754);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_WAIT, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-124
 * @scenario First reading after boot releases the PowerLoss stop
 * @given Boot 12 seconds ago, energy now 7537105 Wh, meterStart 7532754 Wh
 * @when ocpp_stop_tx_ready is called
 * @then Returns READY
 */
void test_stop_ready_once_meter_reading_arrives(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(22000UL, 10000UL, true, 7537105, 7532754);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_READY, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-124
 * @scenario Unknown meterStart only requires a non-zero reading
 * @given Settle window elapsed, meterStart unknown (-1), energy 7537105 Wh
 * @when ocpp_stop_tx_ready is called
 * @then Returns READY
 */
void test_stop_ready_with_unknown_meter_start(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(22000UL, 10000UL, true, 7537105, -1);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_READY, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-125
 * @scenario Meter that never answers falls back to meterStart
 * @given The EV meter has delivered no valid reading for 60 seconds
 * @when ocpp_stop_tx_ready is called
 * @then Returns READY_FALLBACK so meterStop is set to meterStart (0 kWh, never negative)
 */
void test_stop_falls_back_after_max_wait(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(
        10000UL + OCPP_STOP_METER_WAIT_MAX_MS, 10000UL, true, 0, 7532754);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_READY_FALLBACK, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-125
 * @scenario Still waiting just before the maximum wait
 * @given No valid reading, 59.999 seconds since the stop
 * @when ocpp_stop_tx_ready is called
 * @then Returns WAIT
 */
void test_stop_still_waits_before_max_wait(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(
        10000UL + OCPP_STOP_METER_WAIT_MAX_MS - 1UL, 10000UL, true, 0, 7532754);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_WAIT, r);
}

/*
 * @feature OCPP Stop Value Sync
 * @req REQ-OCPP-125
 * @scenario No EV meter configured keeps the original 5 second behaviour
 * @given No EV meter, settle window elapsed, energy 0 Wh
 * @when ocpp_stop_tx_ready is called
 * @then Returns READY (there is no reading to wait for)
 */
void test_stop_ready_without_ev_meter(void) {
    ocpp_stop_ready_t r = ocpp_stop_tx_ready(20000UL, 10000UL, false, 0, 0);
    TEST_ASSERT_EQUAL_INT(OCPP_STOP_READY, r);
}

int main(void) {
    TEST_SUITE_BEGIN("OCPP Stop Value Sync");

    RUN_TEST(test_stop_waits_during_settle_window);
    RUN_TEST(test_stop_ready_after_settle_with_valid_meter);
    RUN_TEST(test_stop_waits_for_first_meter_reading_at_boot);
    RUN_TEST(test_stop_waits_when_reading_below_meter_start);
    RUN_TEST(test_stop_ready_once_meter_reading_arrives);
    RUN_TEST(test_stop_ready_with_unknown_meter_start);
    RUN_TEST(test_stop_falls_back_after_max_wait);
    RUN_TEST(test_stop_still_waits_before_max_wait);
    RUN_TEST(test_stop_ready_without_ev_meter);

    TEST_SUITE_RESULTS();
}
