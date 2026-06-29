/*
 * test_net_iface.c — Native tests for pure-C network-interface selection logic
 * (EtherLCD add-on board, issue #168). Covers NetworkConnected() and the
 * Ethernet-priority WiFi-disable decision used by handleWIFImode().
 */

#include "test_framework.h"
#include "net_iface.h"

/* ================================================================
 * net_iface_has_ip() — "is the unit network-connected?"
 * ================================================================ */

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario Ethernet with a DHCP lease counts as connected regardless of WiFi
 * @given eth_has_ip is true while WiFi is down
 * @when net_iface_has_ip is evaluated
 * @then it returns true (wired link wins)
 */
void test_eth_ip_wins_when_wifi_down(void) {
    TEST_ASSERT_TRUE(net_iface_has_ip(true, false, false));
}

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario Ethernet IP wins even if WiFi is also fully up
 * @given eth_has_ip true and WiFi associated with a valid IP
 * @when net_iface_has_ip is evaluated
 * @then it returns true
 */
void test_eth_ip_wins_when_wifi_also_up(void) {
    TEST_ASSERT_TRUE(net_iface_has_ip(true, true, true));
}

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario WiFi associated with a valid IP counts as connected when no Ethernet
 * @given eth_has_ip false, WiFi associated, WiFi has a non-zero IP
 * @when net_iface_has_ip is evaluated
 * @then it returns true
 */
void test_wifi_with_valid_ip_connected(void) {
    TEST_ASSERT_TRUE(net_iface_has_ip(false, true, true));
}

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario WiFi associated but holding 0.0.0.0 is NOT connected
 * @given eth_has_ip false, WiFi associated, WiFi has no valid IP
 * @when net_iface_has_ip is evaluated
 * @then it returns false (association without an IP is not connectivity)
 */
void test_wifi_associated_without_ip_not_connected(void) {
    TEST_ASSERT_FALSE(net_iface_has_ip(false, true, false));
}

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario No interface up means not connected
 * @given eth_has_ip false and WiFi not associated
 * @when net_iface_has_ip is evaluated
 * @then it returns false
 */
void test_no_interface_not_connected(void) {
    TEST_ASSERT_FALSE(net_iface_has_ip(false, false, false));
}

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario A stale wifi_has_valid_ip flag cannot fake connectivity while disassociated
 * @given eth_has_ip false, WiFi NOT associated, but wifi_has_valid_ip true
 * @when net_iface_has_ip is evaluated
 * @then it returns false — association is required first
 */
void test_wifi_valid_ip_but_disassociated_not_connected(void) {
    TEST_ASSERT_FALSE(net_iface_has_ip(false, false, true));
}

/* ================================================================
 * net_iface_should_disable_wifi() — Ethernet-priority teardown
 * ================================================================ */

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario Ethernet cable connected while WiFi is on triggers a WiFi shutdown
 * @given eth_connected true and WiFi not already off
 * @when net_iface_should_disable_wifi is evaluated
 * @then it returns true
 */
void test_disable_wifi_when_eth_up_and_wifi_on(void) {
    TEST_ASSERT_TRUE(net_iface_should_disable_wifi(true, false));
}

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario Do not re-tear-down WiFi that is already off
 * @given eth_connected true and WiFi already off
 * @when net_iface_should_disable_wifi is evaluated
 * @then it returns false (avoid redundant disruptive teardown)
 */
void test_no_disable_when_wifi_already_off(void) {
    TEST_ASSERT_FALSE(net_iface_should_disable_wifi(true, true));
}

/*
 * @feature EtherLCD Network Selection
 * @req REQ-API-168
 * @scenario No Ethernet means WiFi is left alone
 * @given eth_connected false (WiFi on)
 * @when net_iface_should_disable_wifi is evaluated
 * @then it returns false — WiFi stays the active interface
 */
void test_no_disable_when_no_ethernet(void) {
    TEST_ASSERT_FALSE(net_iface_should_disable_wifi(false, false));
    TEST_ASSERT_FALSE(net_iface_should_disable_wifi(false, true));
}

int main(void) {
    TEST_SUITE_BEGIN("EtherLCD Network Interface Selection");

    /* net_iface_has_ip */
    RUN_TEST(test_eth_ip_wins_when_wifi_down);
    RUN_TEST(test_eth_ip_wins_when_wifi_also_up);
    RUN_TEST(test_wifi_with_valid_ip_connected);
    RUN_TEST(test_wifi_associated_without_ip_not_connected);
    RUN_TEST(test_no_interface_not_connected);
    RUN_TEST(test_wifi_valid_ip_but_disassociated_not_connected);

    /* net_iface_should_disable_wifi */
    RUN_TEST(test_disable_wifi_when_eth_up_and_wifi_on);
    RUN_TEST(test_no_disable_when_wifi_already_off);
    RUN_TEST(test_no_disable_when_no_ethernet);

    TEST_SUITE_RESULTS();
}
