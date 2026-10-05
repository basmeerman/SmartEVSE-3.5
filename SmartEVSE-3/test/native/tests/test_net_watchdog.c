/*
 * test_net_watchdog.c — recovery when WiFi is associated but IP traffic is dead
 *
 * Issue #199: after a network scan both units stayed associated to the access
 * point (so ARDUINO_EVENT_WIFI_STA_DISCONNECTED never fired) while no IP
 * traffic got through for ~38 hours. The pure C watchdog decides, from the
 * time of the last proven-good IP exchange (gateway ping reply), when to force
 * a WiFi reassociation and when to escalate to a reboot.
 */

#include "test_framework.h"
#include "net_watchdog.h"

static net_watchdog_t wd;

#define T0 1000000UL

static void setup_armed(void) {
    net_wd_init(&wd, T0);
    net_wd_link_ok(&wd, T0);
}

/*
 * @feature Network Watchdog
 * @req REQ-API-041
 * @scenario Healthy link does nothing
 * @given The gateway answered 60 seconds ago
 * @when net_wd_decide is called while associated
 * @then Returns NONE
 */
void test_wd_healthy_link_no_action(void) {
    setup_armed();
    TEST_ASSERT_EQUAL_INT(NET_WD_NONE, net_wd_decide(&wd, T0 + 60000UL, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-041
 * @scenario Watchdog stays disarmed until the gateway has answered once
 * @given The gateway never answered a ping since boot (it may block ICMP)
 * @when net_wd_decide is called 1 hour later while associated
 * @then Returns NONE, so a network without ping replies is never disturbed
 */
void test_wd_not_armed_without_first_reply(void) {
    net_wd_init(&wd, T0);
    TEST_ASSERT_EQUAL_INT(NET_WD_NONE, net_wd_decide(&wd, T0 + 3600000UL, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-042
 * @scenario Associated but silent for the dead time forces a reassociation
 * @given The gateway last answered NET_WD_DEAD_MS ago and WiFi is still associated
 * @when net_wd_decide is called
 * @then Returns REASSOCIATE
 */
void test_wd_reassociates_after_dead_time(void) {
    setup_armed();
    TEST_ASSERT_EQUAL_INT(NET_WD_REASSOCIATE, net_wd_decide(&wd, T0 + NET_WD_DEAD_MS, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-042
 * @scenario No action just before the dead time
 * @given The gateway last answered NET_WD_DEAD_MS - 1 ms ago
 * @when net_wd_decide is called
 * @then Returns NONE
 */
void test_wd_no_action_before_dead_time(void) {
    setup_armed();
    TEST_ASSERT_EQUAL_INT(NET_WD_NONE, net_wd_decide(&wd, T0 + NET_WD_DEAD_MS - 1UL, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-042
 * @scenario Reassociation is not repeated every call
 * @given A reassociation was just requested
 * @when net_wd_decide is called again one second later, link still dead
 * @then Returns NONE
 */
void test_wd_reassociate_once_per_window(void) {
    setup_armed();
    net_wd_decide(&wd, T0 + NET_WD_DEAD_MS, true, false);
    TEST_ASSERT_EQUAL_INT(NET_WD_NONE, net_wd_decide(&wd, T0 + NET_WD_DEAD_MS + 1000UL, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-043
 * @scenario Still dead after a reassociation escalates to a reboot when idle
 * @given A reassociation was requested and the link is still dead NET_WD_RETRY_MS later
 * @given The EVSE is not charging
 * @when net_wd_decide is called
 * @then Returns REBOOT
 */
void test_wd_reboots_when_reassociation_did_not_help(void) {
    setup_armed();
    unsigned long t = T0 + NET_WD_DEAD_MS;
    net_wd_decide(&wd, t, true, false);
    TEST_ASSERT_EQUAL_INT(NET_WD_REBOOT, net_wd_decide(&wd, t + NET_WD_RETRY_MS, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-044
 * @scenario Never reboot while charging, reassociate again instead
 * @given A reassociation did not help and the EVSE is charging
 * @when net_wd_decide is called NET_WD_RETRY_MS after the reassociation
 * @then Returns REASSOCIATE, never REBOOT
 */
void test_wd_never_reboots_while_charging(void) {
    setup_armed();
    unsigned long t = T0 + NET_WD_DEAD_MS;
    net_wd_decide(&wd, t, true, true);
    TEST_ASSERT_EQUAL_INT(NET_WD_REASSOCIATE, net_wd_decide(&wd, t + NET_WD_RETRY_MS, true, true));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-044
 * @scenario Reboot follows once charging has ended
 * @given The link stayed dead through two reassociations during charging
 * @when charging stops and the next retry window elapses
 * @then Returns REBOOT
 */
void test_wd_reboots_after_charging_ends(void) {
    setup_armed();
    unsigned long t = T0 + NET_WD_DEAD_MS;
    net_wd_decide(&wd, t, true, true);
    t += NET_WD_RETRY_MS;
    net_wd_decide(&wd, t, true, true);
    t += NET_WD_RETRY_MS;
    TEST_ASSERT_EQUAL_INT(NET_WD_REBOOT, net_wd_decide(&wd, t, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-045
 * @scenario A reply after a reassociation resets the escalation
 * @given A reassociation was requested and the gateway then answered
 * @when the link is dead again NET_WD_DEAD_MS after that reply
 * @then Returns REASSOCIATE again (not REBOOT) and the recovery is counted
 */
void test_wd_reply_resets_escalation(void) {
    setup_armed();
    unsigned long t = T0 + NET_WD_DEAD_MS;
    net_wd_decide(&wd, t, true, false);
    t += 20000UL;
    net_wd_link_ok(&wd, t);
    TEST_ASSERT_EQUAL_INT(1, wd.recoveries);
    TEST_ASSERT_EQUAL_INT(NET_WD_REASSOCIATE, net_wd_decide(&wd, t + NET_WD_DEAD_MS, true, false));
}

/*
 * @feature Network Watchdog
 * @req REQ-API-045
 * @scenario A normal reply is not counted as a recovery
 * @given No reassociation was requested
 * @when the gateway answers
 * @then recoveries stays 0
 */
void test_wd_plain_reply_not_a_recovery(void) {
    setup_armed();
    net_wd_link_ok(&wd, T0 + 30000UL);
    TEST_ASSERT_EQUAL_INT(0, wd.recoveries);
}

/*
 * @feature Network Watchdog
 * @req REQ-API-045
 * @scenario Not associated: the existing disconnect handler is in charge
 * @given WiFi is not associated and the gateway last answered long ago
 * @when net_wd_decide is called, and again right after the link comes back
 * @then Returns NONE both times; the dead time restarts at reassociation
 */
void test_wd_not_associated_defers_to_wifi_events(void) {
    setup_armed();
    unsigned long t = T0 + 3600000UL;
    TEST_ASSERT_EQUAL_INT(NET_WD_NONE, net_wd_decide(&wd, t, false, false));
    TEST_ASSERT_EQUAL_INT(NET_WD_NONE, net_wd_decide(&wd, t + 1000UL, true, false));
}

int main(void) {
    TEST_SUITE_BEGIN("Network Watchdog");

    RUN_TEST(test_wd_healthy_link_no_action);
    RUN_TEST(test_wd_not_armed_without_first_reply);
    RUN_TEST(test_wd_reassociates_after_dead_time);
    RUN_TEST(test_wd_no_action_before_dead_time);
    RUN_TEST(test_wd_reassociate_once_per_window);
    RUN_TEST(test_wd_reboots_when_reassociation_did_not_help);
    RUN_TEST(test_wd_never_reboots_while_charging);
    RUN_TEST(test_wd_reboots_after_charging_ends);
    RUN_TEST(test_wd_reply_resets_escalation);
    RUN_TEST(test_wd_plain_reply_not_a_recovery);
    RUN_TEST(test_wd_not_associated_defers_to_wifi_events);

    TEST_SUITE_RESULTS();
}
