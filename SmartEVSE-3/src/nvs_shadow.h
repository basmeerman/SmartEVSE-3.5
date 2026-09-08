/*
 * nvs_shadow.h — Pure C write scheduling for NVS-persisted settings.
 *
 * Settings changes arrive far more often than flash should be written: a user
 * dragging a slider in the web UI, or a load-balancing master pushing values to
 * a node, can produce dozens of changes per minute. This module decides *when*
 * a pending change is written, without knowing anything about NVS, Preferences
 * or Arduino — so the policy is unit-testable while the actual write stays in
 * the platform layer.
 *
 * Policy per slot:
 *
 *   - marking a slot dirty never writes by itself;
 *   - the first write after boot happens as soon as something is dirty, so a
 *     change made seconds after power-on is not held hostage by the interval;
 *   - after that, a slot is due again only once the interval has elapsed since
 *     its last write;
 *   - a forced flush ignores the interval entirely. That is what a reboot or an
 *     LCD menu exit needs: the device may lose power before the window closes.
 *
 * All timestamps are millis()-style unsigned milliseconds. Comparisons use
 * unsigned subtraction, so the 49.7-day millis() wraparound is handled without
 * a special case — a device that has been up for weeks must not stop persisting
 * settings.
 */

#ifndef NVS_SHADOW_H
#define NVS_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Slots are fixed at compile time: no heap, no dynamic containers. Raise this
 * if settings are ever split into independently scheduled groups. */
#define NVS_SHADOW_MAX_SLOTS 4

typedef struct {
    uint32_t last_write_ms;   /* when this slot was last written */
    bool     dirty;           /* a change is pending */
    bool     ever_written;    /* false until the first successful write */
} nvs_shadow_slot_t;

typedef struct {
    nvs_shadow_slot_t slots[NVS_SHADOW_MAX_SLOTS];
    uint32_t          interval_ms;
} nvs_shadow_t;

/* Reset all slots and set the minimum interval between writes of one slot. */
void nvs_shadow_init(nvs_shadow_t *s, uint32_t interval_ms);

/* Record that a slot has pending changes. Idempotent: marking an already-dirty
 * slot does not extend or reset its interval. */
void nvs_shadow_mark(nvs_shadow_t *s, uint8_t slot);

/* True when `slot` should be written now.
 *
 * force=true reports every dirty slot as due regardless of the interval; it
 * never invents work for a slot with nothing pending. */
bool nvs_shadow_due(const nvs_shadow_t *s, uint8_t slot, uint32_t now_ms, bool force);

/* True when any slot is due — lets the caller skip opening NVS at all. */
bool nvs_shadow_any_due(const nvs_shadow_t *s, uint32_t now_ms, bool force);

/* Record a completed write: clears dirty and restarts the interval. Call this
 * only after the write actually succeeded, so a failed write is retried on the
 * next pass rather than silently dropped. */
void nvs_shadow_written(nvs_shadow_t *s, uint8_t slot, uint32_t now_ms);

/* True when a slot has pending changes, regardless of timing. Used to decide
 * whether a forced flush is needed before rebooting. */
bool nvs_shadow_is_dirty(const nvs_shadow_t *s, uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif /* NVS_SHADOW_H */
