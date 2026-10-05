/*
 * net_watchdog.c - recovery when WiFi is associated but IP traffic is dead
 *
 * See net_watchdog.h.
 */

#include "net_watchdog.h"

#include <string.h>

void net_wd_init(net_watchdog_t *wd, unsigned long now_ms) {
    if (!wd) return;
    memset(wd, 0, sizeof(*wd));
    wd->last_ok_ms = now_ms;
    wd->last_action_ms = now_ms;
}

void net_wd_link_ok(net_watchdog_t *wd, unsigned long now_ms) {
    if (!wd) return;
    if (wd->reassociations > 0) {
        wd->recoveries++;
    }
    wd->armed = true;
    wd->last_ok_ms = now_ms;
    wd->reassociations = 0;
}

net_wd_action_t net_wd_decide(net_watchdog_t *wd, unsigned long now_ms,
                              bool associated, bool charging) {
    if (!wd) return NET_WD_NONE;

    /* Not associated: the STA_DISCONNECTED handler reconnects. Restart the
     * dead time so a fresh association gets a full window to prove itself. */
    if (!associated) {
        wd->last_ok_ms = now_ms;
        wd->reassociations = 0;
        return NET_WD_NONE;
    }

    /* Never answered: the gateway may simply not reply to ping. */
    if (!wd->armed) {
        return NET_WD_NONE;
    }

    if (now_ms - wd->last_ok_ms < NET_WD_DEAD_MS) {
        return NET_WD_NONE;
    }

    if (wd->reassociations == 0) {
        wd->reassociations = 1;
        wd->last_action_ms = now_ms;
        return NET_WD_REASSOCIATE;
    }

    if (now_ms - wd->last_action_ms < NET_WD_RETRY_MS) {
        return NET_WD_NONE;
    }

    wd->last_action_ms = now_ms;
    if (!charging) {
        return NET_WD_REBOOT;
    }
    if (wd->reassociations < UINT8_MAX) {
        wd->reassociations++;
    }
    return NET_WD_REASSOCIATE;
}
