/*
 * mqtt_publish.c — Change-only MQTT publish cache
 *
 * Pure C module that tracks previously published values per MQTT topic slot.
 * Before publishing, callers check mqtt_should_publish_int/str to determine
 * if the value changed, the heartbeat elapsed, or a forced re-publish is needed.
 *
 * No heap allocation — all state lives in the caller-provided mqtt_cache_t.
 */

#include "mqtt_publish.h"
#include "mqtt_parser.h"
#include <stdio.h>
#include <string.h>

void mqtt_cache_init(mqtt_cache_t *cache, uint16_t heartbeat_s) {
    memset(cache, 0, sizeof(*cache));
    cache->heartbeat_s = heartbeat_s;
}

uint16_t mqtt_crc16(const char *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint8_t)data[i]) << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

bool mqtt_should_publish_int(mqtt_cache_t *cache, mqtt_slot_t slot,
                             int32_t value, uint32_t now_s) {
    if (slot < 0 || slot >= MQTT_CACHE_MAX_SLOTS)
        return false;

    mqtt_cache_entry_t *e = &cache->entries[slot];

    /* Stale (forced) — always publish */
    if (e->flags & MQTT_ENTRY_STALE) {
        e->int_val = value;
        e->flags = MQTT_ENTRY_INT;
        e->last_pub_s = now_s;
        return true;
    }

    /* Empty (first time) — always publish */
    if ((e->flags & 0x0F) == MQTT_ENTRY_EMPTY) {
        e->int_val = value;
        e->flags = MQTT_ENTRY_INT;
        e->last_pub_s = now_s;
        return true;
    }

    /* Value changed */
    if (e->int_val != value) {
        e->int_val = value;
        e->flags = MQTT_ENTRY_INT;
        e->last_pub_s = now_s;
        return true;
    }

    /* Heartbeat elapsed */
    if (cache->heartbeat_s > 0 && (now_s - e->last_pub_s) >= cache->heartbeat_s) {
        e->last_pub_s = now_s;
        return true;
    }

    return false;
}

bool mqtt_should_publish_str(mqtt_cache_t *cache, mqtt_slot_t slot,
                             const char *value, uint32_t now_s) {
    if (slot < 0 || slot >= MQTT_CACHE_MAX_SLOTS)
        return false;

    mqtt_cache_entry_t *e = &cache->entries[slot];
    uint16_t hash = mqtt_crc16(value, strlen(value));

    /* Stale (forced) — always publish */
    if (e->flags & MQTT_ENTRY_STALE) {
        e->str_hash = hash;
        e->flags = MQTT_ENTRY_STR;
        e->last_pub_s = now_s;
        return true;
    }

    /* Empty (first time) — always publish */
    if ((e->flags & 0x0F) == MQTT_ENTRY_EMPTY) {
        e->str_hash = hash;
        e->flags = MQTT_ENTRY_STR;
        e->last_pub_s = now_s;
        return true;
    }

    /* Value changed (CRC16 mismatch) */
    if (e->str_hash != hash) {
        e->str_hash = hash;
        e->flags = MQTT_ENTRY_STR;
        e->last_pub_s = now_s;
        return true;
    }

    /* Heartbeat elapsed */
    if (cache->heartbeat_s > 0 && (now_s - e->last_pub_s) >= cache->heartbeat_s) {
        e->last_pub_s = now_s;
        return true;
    }

    return false;
}

void mqtt_cache_force_all(mqtt_cache_t *cache) {
    for (int i = 0; i < MQTT_CACHE_MAX_SLOTS; i++) {
        cache->entries[i].flags |= MQTT_ENTRY_STALE;
    }
}

/* ---- Home Assistant select entities (issue #193) ---- */

static const char *const ha_custom_button_options[] = { "On", "Off" };
static const char *const ha_mode_options[] = { "Off", "Normal", "Smart", "Solar", "Pause" };
static const char *const ha_cable_lock_options[] = { "0", "1" };  /* upstream #249: 0/1, like REST */

/* Charging mode names, indexed by MODE_NORMAL (0), MODE_SMART (1), MODE_SOLAR (2) */
static const char *const ha_mode_names[] = { "Normal", "Smart", "Solar" };

#define HA_COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

const mqtt_ha_select_def_t mqtt_ha_selects[MQTT_HA_SELECT_COUNT] = {
    [MQTT_HA_SELECT_CUSTOM_BUTTON] = { "Custom Button", "/CustomButton", "/Set/CustomButton",
                                       ha_custom_button_options, HA_COUNT(ha_custom_button_options), MQTT_HA_GATE_ALWAYS },
    [MQTT_HA_SELECT_MODE]          = { "Mode", "/Mode", "/Set/Mode",
                                       ha_mode_options, HA_COUNT(ha_mode_options), MQTT_HA_GATE_ALWAYS },
    [MQTT_HA_SELECT_ENABLE_C2]     = { "EnableC2", "/EnableC2", "/Set/EnableC2",
                                       mqtt_enable_c2_strings, MQTT_ENABLE_C2_COUNT, MQTT_HA_GATE_ALWAYS },
    [MQTT_HA_SELECT_CABLE_LOCK]    = { "Cable Lock", "/CableLock", "/Set/CableLock",
                                       ha_cable_lock_options, HA_COUNT(ha_cable_lock_options), MQTT_HA_GATE_CABLE_LOCK },
    [MQTT_HA_SELECT_PRIO_STRATEGY] = { "Priority Strategy", "/PrioStrategy", "/Set/PrioStrategy",
                                       mqtt_prio_strategy_strings, MQTT_PRIO_STRATEGY_COUNT, MQTT_HA_GATE_MASTER },
};

bool mqtt_ha_gate_open(mqtt_ha_gate_t gate, uint8_t load_bl, uint8_t lock) {
    switch (gate) {
    case MQTT_HA_GATE_MASTER:     return load_bl == 1;
    case MQTT_HA_GATE_CABLE_LOCK: return lock != 0;
    case MQTT_HA_GATE_ALWAYS:
    default:                      return true;
    }
}

uint8_t mqtt_ha_gate_mask(uint8_t load_bl, uint8_t lock) {
    uint8_t mask = 0;
    for (int g = 0; g < MQTT_HA_GATE_COUNT; g++) {
        if (mqtt_ha_gate_open((mqtt_ha_gate_t)g, load_bl, lock))
            mask |= (uint8_t)(1u << g);
    }
    return mask;
}

int mqtt_ha_select_options_json(const mqtt_ha_select_def_t *def, char *buf, size_t size) {
    if (size == 0)
        return -1;
    int n = snprintf(buf, size, ", \"options\" : [");
    if (n < 0 || (size_t)n >= size) {
        buf[0] = '\0';
        return -1;
    }
    size_t len = (size_t)n;
    for (uint8_t i = 0; i < def->option_count; i++) {
        n = snprintf(buf + len, size - len, "%s\"%s\"", i ? ", " : "", def->options[i]);
        if (n < 0 || (size_t)n >= size - len) {
            buf[0] = '\0';
            return -1;
        }
        len += (size_t)n;
    }
    n = snprintf(buf + len, size - len, "]");
    if (n < 0 || (size_t)n >= size - len) {
        buf[0] = '\0';
        return -1;
    }
    return (int)(len + (size_t)n);
}

const char *mqtt_ha_mode_state(bool access_off, bool access_pause, uint8_t mode) {
    if (access_off)
        return "Off";
    if (access_pause)
        return "Pause";
    return mode < HA_COUNT(ha_mode_names) ? ha_mode_names[mode] : NULL;
}

const char *mqtt_ha_custom_button_state(bool on) {
    return on ? "On" : "Off";
}

const char *mqtt_ha_enable_c2_state(uint8_t enable_c2) {
    return enable_c2 < MQTT_ENABLE_C2_COUNT ? mqtt_enable_c2_strings[enable_c2] : NULL;
}

const char *mqtt_ha_cable_lock_state(uint8_t cable_lock) {
    return cable_lock < HA_COUNT(ha_cable_lock_options) ? ha_cable_lock_options[cable_lock] : NULL;
}

const char *mqtt_ha_prio_strategy_state(uint8_t prio_strategy) {
    return prio_strategy < MQTT_PRIO_STRATEGY_COUNT ? mqtt_prio_strategy_strings[prio_strategy] : NULL;
}
