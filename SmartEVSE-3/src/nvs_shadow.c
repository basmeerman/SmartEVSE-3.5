/*
 * nvs_shadow.c — Pure C write scheduling for NVS-persisted settings.
 * See nvs_shadow.h for the policy this implements.
 */

#include "nvs_shadow.h"
#include <string.h>

void nvs_shadow_init(nvs_shadow_t *s, uint32_t interval_ms) {
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->interval_ms = interval_ms;
}

void nvs_shadow_mark(nvs_shadow_t *s, uint8_t slot) {
    if (s == NULL || slot >= NVS_SHADOW_MAX_SLOTS) {
        return;
    }
    s->slots[slot].dirty = true;
}

bool nvs_shadow_due(const nvs_shadow_t *s, uint8_t slot, uint32_t now_ms, bool force) {
    const nvs_shadow_slot_t *e;

    if (s == NULL || slot >= NVS_SHADOW_MAX_SLOTS) {
        return false;
    }
    e = &s->slots[slot];

    if (!e->dirty) {
        return false;                  /* nothing pending: force does not invent work */
    }
    if (force || !e->ever_written) {
        return true;                   /* forced flush, or the first write after boot */
    }
    /* Unsigned subtraction: correct across the millis() wraparound. */
    return (uint32_t)(now_ms - e->last_write_ms) >= s->interval_ms;
}

bool nvs_shadow_any_due(const nvs_shadow_t *s, uint32_t now_ms, bool force) {
    uint8_t i;

    if (s == NULL) {
        return false;
    }
    for (i = 0; i < NVS_SHADOW_MAX_SLOTS; i++) {
        if (nvs_shadow_due(s, i, now_ms, force)) {
            return true;
        }
    }
    return false;
}

void nvs_shadow_written(nvs_shadow_t *s, uint8_t slot, uint32_t now_ms) {
    if (s == NULL || slot >= NVS_SHADOW_MAX_SLOTS) {
        return;
    }
    s->slots[slot].dirty         = false;
    s->slots[slot].last_write_ms = now_ms;
    s->slots[slot].ever_written  = true;
}

bool nvs_shadow_is_dirty(const nvs_shadow_t *s, uint8_t slot) {
    if (s == NULL || slot >= NVS_SHADOW_MAX_SLOTS) {
        return false;
    }
    return s->slots[slot].dirty;
}
