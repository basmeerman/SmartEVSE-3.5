/*
 * net_iface.h — pure C network-interface selection logic (EtherLCD, issue #168)
 *
 * The EtherLCD add-on board (CH390D) gives a v3 SmartEVSE a wired Ethernet
 * interface alongside WiFi. Two decisions then become non-trivial and must
 * behave identically whether driven by the WiFi stack or the Ethernet driver:
 *
 *   1. "Is the unit network-connected?" — used to bring up OCPP/MQTT. True when
 *      EITHER interface has a usable IP. A WiFi association without a valid IP
 *      (0.0.0.0) does NOT count.
 *   2. "Should WiFi be shut down?" — Ethernet takes priority: when the cable is
 *      connected, WiFi is disabled so the wired link is used. But we must not
 *      redundantly tear down WiFi when it is already off.
 *
 * These are extracted here as pure functions (no Arduino / ESP-IDF deps) so they
 * compile and test natively with gcc. The firmware glue in network_common.cpp
 * supplies the live interface state.
 */
#ifndef NET_IFACE_H
#define NET_IFACE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns true if any interface currently has a usable IP address.
 *
 *   eth_has_ip        — Ethernet (CH390D) interface holds a DHCP lease
 *   wifi_connected    — WiFi station is associated to an AP
 *   wifi_has_valid_ip — WiFi station has a non-zero IPv4 address
 *
 * Ethernet wins immediately; otherwise WiFi counts only when associated AND
 * holding a valid (non 0.0.0.0) address.
 */
bool net_iface_has_ip(bool eth_has_ip, bool wifi_connected, bool wifi_has_valid_ip);

/*
 * Returns true if WiFi should be actively disabled right now.
 *
 *   eth_connected   — Ethernet link (cable) is up
 *   wifi_already_off — WiFi radio mode is already WIFI_OFF
 *
 * Ethernet takes priority over WiFi, but only issue the (disruptive) teardown
 * when WiFi is not already off — avoids repeated softAPdisconnect/disconnect.
 */
bool net_iface_should_disable_wifi(bool eth_connected, bool wifi_already_off);

#ifdef __cplusplus
}
#endif

#endif /* NET_IFACE_H */
