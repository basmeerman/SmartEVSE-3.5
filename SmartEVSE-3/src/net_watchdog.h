/*
 * net_watchdog.h - recovery when WiFi is associated but IP traffic is dead
 *
 * The only WiFi recovery path in the firmware is WiFi.reconnect() on
 * ARDUINO_EVENT_WIFI_STA_DISCONNECTED. When the station stays associated to
 * the access point while the IP layer has stopped working, that event never
 * fires and the charger stays unreachable (issue #199: ~38 hours after a
 * network scan, until the AP forced a reassociation).
 *
 * The glue layer pings the gateway periodically and reports each reply with
 * net_wd_link_ok(). This pure C module decides, from the time of the last
 * reply, when to force a reassociation and when to escalate to a reboot.
 * It never reboots while the EVSE is charging.
 */

#ifndef NET_WATCHDOG_H
#define NET_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WD_DEAD_MS    300000UL   /* No gateway reply for 5 min: link is dead        */
#define NET_WD_RETRY_MS   600000UL   /* Wait 10 min after a reassociation to escalate   */

typedef enum {
    NET_WD_NONE       = 0,
    NET_WD_REASSOCIATE = 1,          /* Force a WiFi disconnect + reconnect              */
    NET_WD_REBOOT      = 2           /* Reassociation did not help and EVSE is idle     */
} net_wd_action_t;

typedef struct {
    bool          armed;             /* Gateway has answered at least once              */
    unsigned long last_ok_ms;        /* Last proven-good IP exchange                    */
    unsigned long last_action_ms;    /* Last REASSOCIATE / REBOOT decision              */
    uint8_t       reassociations;    /* Reassociations since the last reply             */
    uint32_t      recoveries;        /* Reassociations that brought the link back       */
} net_watchdog_t;

void net_wd_init(net_watchdog_t *wd, unsigned long now_ms);

/* A gateway reply (or other proof of IP traffic) was received. Arms the watchdog. */
void net_wd_link_ok(net_watchdog_t *wd, unsigned long now_ms);

/*
 *   associated — WiFi.status() == WL_CONNECTED
 *   charging   — State == STATE_C (never reboot then)
 */
net_wd_action_t net_wd_decide(net_watchdog_t *wd, unsigned long now_ms,
                              bool associated, bool charging);

#ifdef __cplusplus
}
#endif

#endif /* NET_WATCHDOG_H */
