/*
 * test_nvs_shadow.c — Native tests for NVS write scheduling.
 *
 * The cases that matter are the ones that lose user settings when wrong: a
 * change made just after boot, a forced flush before reboot, and the millis()
 * wraparound on a device that has been up for seven weeks.
 */

#include "test_framework.h"
#include "nvs_shadow.h"

#define INTERVAL_MS  (60UL * 1000UL)
#define SLOT         0

static nvs_shadow_t shadow;

/*
 * @feature Settings Persistence
 * @req REQ-PWR-048
 * @scenario A clean slot is never written
 * @given A freshly initialised scheduler with nothing marked
 * @when due is checked, with and without force
 * @then Neither reports work, because force must not invent writes
 */
void test_clean_slot_is_never_due(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, 1000, false));
    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, 1000, true));
    TEST_ASSERT_FALSE(nvs_shadow_any_due(&shadow, 1000, true));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-049
 * @scenario The first change after boot is written without waiting
 * @given A scheduler that has never written, marked dirty 5 seconds after boot
 * @when due is checked
 * @then It reports due — a setting changed right after power-on is not held
 *       for a full interval
 */
void test_first_write_after_boot_is_immediate(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, SLOT);
    TEST_ASSERT_TRUE(nvs_shadow_due(&shadow, SLOT, 5000, false));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-050
 * @scenario Changes inside the interval are deferred, not dropped
 * @given A slot written at t=5s, marked dirty again at t=10s
 * @when due is checked at t=10s and again at t=64s
 * @then It is not due at 10s, still dirty, and due once the interval elapses
 */
void test_second_write_waits_for_interval(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, SLOT);
    nvs_shadow_written(&shadow, SLOT, 5000);

    nvs_shadow_mark(&shadow, SLOT);
    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, 10000, false));
    TEST_ASSERT_TRUE(nvs_shadow_is_dirty(&shadow, SLOT));
    TEST_ASSERT_TRUE(nvs_shadow_due(&shadow, SLOT, 65000, false));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-051
 * @scenario The interval boundary is inclusive
 * @given A slot written at t=0 and marked dirty
 * @when due is checked one millisecond before the interval, and exactly on it
 * @then Only the second reports due
 */
void test_interval_boundary_is_inclusive(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, SLOT);
    nvs_shadow_written(&shadow, SLOT, 0);
    nvs_shadow_mark(&shadow, SLOT);

    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, INTERVAL_MS - 1, false));
    TEST_ASSERT_TRUE(nvs_shadow_due(&shadow, SLOT, INTERVAL_MS, false));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-052
 * @scenario A forced flush ignores the interval
 * @given A slot written moments ago and marked dirty again
 * @when a forced flush is requested, as on reboot or LCD menu exit
 * @then It reports due despite the interval not having elapsed
 */
void test_force_ignores_interval(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, SLOT);
    nvs_shadow_written(&shadow, SLOT, 10000);
    nvs_shadow_mark(&shadow, SLOT);

    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, 10500, false));
    TEST_ASSERT_TRUE(nvs_shadow_due(&shadow, SLOT, 10500, true));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-053
 * @scenario A completed write clears the pending change
 * @given A dirty slot that is then written
 * @when due and is_dirty are checked
 * @then Both report false — the change is persisted, not pending
 */
void test_write_clears_dirty(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, SLOT);
    nvs_shadow_written(&shadow, SLOT, 1000);

    TEST_ASSERT_FALSE(nvs_shadow_is_dirty(&shadow, SLOT));
    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, 100000, false));
    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, 100000, true));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-054
 * @scenario Marking is idempotent and does not restart the interval
 * @given A slot written at t=0, then marked repeatedly during the window
 * @when due is checked at the end of the original interval
 * @then It reports due — repeated edits must not postpone the write forever
 */
void test_repeated_marks_do_not_postpone(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, SLOT);
    nvs_shadow_written(&shadow, SLOT, 0);

    uint32_t t;
    for (t = 1000; t < INTERVAL_MS; t += 1000) {
        nvs_shadow_mark(&shadow, SLOT);
        TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, t, false));
    }
    TEST_ASSERT_TRUE(nvs_shadow_due(&shadow, SLOT, INTERVAL_MS, false));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-055
 * @scenario Scheduling survives the millis() wraparound
 * @given A slot written just before the 32-bit millisecond counter wraps, on a
 *        device that has been running for roughly 49.7 days
 * @when the counter wraps past zero and the interval elapses in real time
 * @then The slot is reported due, rather than waiting another 49.7 days
 */
void test_wraparound_does_not_stall_writes(void) {
    const uint32_t near_wrap = 0xFFFFFFFFUL - 30000UL;   /* 30s before wrap */

    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, SLOT);
    nvs_shadow_written(&shadow, SLOT, near_wrap);
    nvs_shadow_mark(&shadow, SLOT);

    /* 30s after the write, still before the wrap: not yet due. */
    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, SLOT, 0xFFFFFFFFUL, false));
    /* 30s past the wrap: 60s of real time have elapsed, so it is due. */
    TEST_ASSERT_TRUE(nvs_shadow_due(&shadow, SLOT, 30000UL, false));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-056
 * @scenario Slots are independent
 * @given Two slots, one dirty and one clean
 * @when due is checked for each and for the aggregate
 * @then Only the dirty slot is due, and the aggregate reflects it
 */
void test_slots_are_independent(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, 1);

    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, 0, 5000, false));
    TEST_ASSERT_TRUE(nvs_shadow_due(&shadow, 1, 5000, false));
    TEST_ASSERT_TRUE(nvs_shadow_any_due(&shadow, 5000, false));

    nvs_shadow_written(&shadow, 1, 5000);
    TEST_ASSERT_FALSE(nvs_shadow_any_due(&shadow, 5000, true));
}

/*
 * @feature Settings Persistence
 * @req REQ-PWR-057
 * @scenario Out-of-range slots and NULL state are handled without writing
 * @given A slot index beyond the table, and a NULL scheduler
 * @when the scheduling functions are called
 * @then Nothing is reported due and nothing crashes
 */
void test_invalid_input_is_safe(void) {
    nvs_shadow_init(&shadow, INTERVAL_MS);
    nvs_shadow_mark(&shadow, NVS_SHADOW_MAX_SLOTS);        /* out of range */
    TEST_ASSERT_FALSE(nvs_shadow_due(&shadow, NVS_SHADOW_MAX_SLOTS, 1000, true));
    TEST_ASSERT_FALSE(nvs_shadow_is_dirty(&shadow, NVS_SHADOW_MAX_SLOTS));
    TEST_ASSERT_FALSE(nvs_shadow_any_due(&shadow, 1000, true));

    nvs_shadow_init(NULL, INTERVAL_MS);
    nvs_shadow_mark(NULL, SLOT);
    nvs_shadow_written(NULL, SLOT, 1000);
    TEST_ASSERT_FALSE(nvs_shadow_due(NULL, SLOT, 1000, true));
    TEST_ASSERT_FALSE(nvs_shadow_any_due(NULL, 1000, true));
    TEST_ASSERT_FALSE(nvs_shadow_is_dirty(NULL, SLOT));
}

int main(void) {
    TEST_SUITE_BEGIN("NVS Write Scheduling");

    RUN_TEST(test_clean_slot_is_never_due);
    RUN_TEST(test_first_write_after_boot_is_immediate);
    RUN_TEST(test_second_write_waits_for_interval);
    RUN_TEST(test_interval_boundary_is_inclusive);
    RUN_TEST(test_force_ignores_interval);
    RUN_TEST(test_write_clears_dirty);
    RUN_TEST(test_repeated_marks_do_not_postpone);
    RUN_TEST(test_wraparound_does_not_stall_writes);
    RUN_TEST(test_slots_are_independent);
    RUN_TEST(test_invalid_input_is_safe);

    TEST_SUITE_RESULTS();
}
