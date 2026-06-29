/*
 * net_iface.c — pure C network-interface selection logic (EtherLCD, issue #168)
 * See net_iface.h for the rationale.
 */
#include "net_iface.h"

bool net_iface_has_ip(bool eth_has_ip, bool wifi_connected, bool wifi_has_valid_ip) {
    if (eth_has_ip) {
        return true;                    // wired Ethernet wins
    }
    if (!wifi_connected) {
        return false;                   // not associated → no connectivity
    }
    return wifi_has_valid_ip;           // associated, but needs a real IP
}

bool net_iface_should_disable_wifi(bool eth_connected, bool wifi_already_off) {
    return eth_connected && !wifi_already_off;
}
