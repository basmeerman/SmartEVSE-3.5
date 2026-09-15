/*
 * test_ha_select.c — Home Assistant select entity consistency (issue #193)
 *
 * Every select entity announced through MQTT discovery has a state topic, a
 * command topic and a fixed list of options. Home Assistant rejects any state
 * value that is not one of the options (and leaves the entity "unknown"), and
 * it sends the option text verbatim to the command topic. These tests pin the
 * three views together: the values the firmware publishes, the options it
 * announces, and the payloads the MQTT parser accepts.
 */

#include "test_framework.h"
#include "mqtt_publish.h"
#include "mqtt_parser.h"
#include <string.h>
#include <stdio.h>

#define PREFIX "SmartEVSE-7388-L"

static bool option_listed(const mqtt_ha_select_def_t *def, const char *value) {
    for (uint8_t i = 0; i < def->option_count; i++) {
        if (strcmp(def->options[i], value) == 0)
            return true;
    }
    return false;
}

/* Every state value the firmware can publish for a select, for all inputs.
 * Calls visit(value) for each non-NULL result. */
static void for_each_published_state(mqtt_ha_select_t id,
                                     void (*visit)(const mqtt_ha_select_def_t *, const char *, void *),
                                     void *arg) {
    const mqtt_ha_select_def_t *def = &mqtt_ha_selects[id];
    for (int v = 0; v <= 255; v++) {
        const char *s = NULL;
        switch (id) {
        case MQTT_HA_SELECT_CUSTOM_BUTTON: s = mqtt_ha_custom_button_state(v != 0); break;
        case MQTT_HA_SELECT_ENABLE_C2:     s = mqtt_ha_enable_c2_state((uint8_t)v); break;
        case MQTT_HA_SELECT_CABLE_LOCK:    s = mqtt_ha_cable_lock_state((uint8_t)v); break;
        case MQTT_HA_SELECT_PRIO_STRATEGY: s = mqtt_ha_prio_strategy_state((uint8_t)v); break;
        case MQTT_HA_SELECT_MODE:
            for (int flags = 0; flags < 4; flags++) {
                s = mqtt_ha_mode_state((flags & 1) != 0, (flags & 2) != 0, (uint8_t)v);
                if (s) visit(def, s, arg);
            }
            s = NULL;
            break;
        default: break;
        }
        if (s) visit(def, s, arg);
    }
}

static void count_unlisted(const mqtt_ha_select_def_t *def, const char *value, void *arg) {
    if (!option_listed(def, value)) {
        printf("    %s publishes '%s', which is not an announced option\n", def->name, value);
        (*(int *)arg)++;
    }
}

typedef struct { const char *option; bool seen; } option_hit_t;

static void mark_option(const mqtt_ha_select_def_t *def, const char *value, void *arg) {
    (void)def;
    option_hit_t *hit = (option_hit_t *)arg;
    if (strcmp(hit->option, value) == 0)
        hit->seen = true;
}

/* The state string the firmware would publish after applying a parsed command. */
static const char *state_after_command(mqtt_ha_select_t id, const mqtt_command_t *c) {
    switch (id) {
    case MQTT_HA_SELECT_CUSTOM_BUTTON: return mqtt_ha_custom_button_state(c->custom_button);
    case MQTT_HA_SELECT_ENABLE_C2:     return mqtt_ha_enable_c2_state(c->enable_c2);
    case MQTT_HA_SELECT_CABLE_LOCK:    return mqtt_ha_cable_lock_state(c->cable_lock);
    case MQTT_HA_SELECT_PRIO_STRATEGY: return mqtt_ha_prio_strategy_state(c->prio_strategy);
    case MQTT_HA_SELECT_MODE:
        if (c->mode == MQTT_MODE_OFF)   return mqtt_ha_mode_state(true, false, 0);
        if (c->mode == MQTT_MODE_PAUSE) return mqtt_ha_mode_state(false, true, 0);
        return mqtt_ha_mode_state(false, false, c->mode);
    default: return NULL;
    }
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-037
 * @scenario Every value published on a select state topic is an announced option
 * @given The select entities announced through Home Assistant MQTT discovery
 * @when The state value is formatted for every possible setting value
 * @then Each published value appears in that select's announced options
 */
void test_published_states_are_announced_options(void) {
    int unlisted = 0;
    for (int id = 0; id < MQTT_HA_SELECT_COUNT; id++)
        for_each_published_state((mqtt_ha_select_t)id, count_unlisted, &unlisted);
    TEST_ASSERT_EQUAL_INT(0, unlisted);
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-037
 * @scenario Every announced option can actually be published
 * @given The select entities announced through Home Assistant MQTT discovery
 * @when All possible setting values are formatted as state values
 * @then Each announced option is produced by at least one setting value
 */
void test_announced_options_are_reachable(void) {
    int missing = 0;
    for (int id = 0; id < MQTT_HA_SELECT_COUNT; id++) {
        const mqtt_ha_select_def_t *def = &mqtt_ha_selects[id];
        for (uint8_t i = 0; i < def->option_count; i++) {
            option_hit_t hit = { def->options[i], false };
            for_each_published_state((mqtt_ha_select_t)id, mark_option, &hit);
            if (!hit.seen) {
                printf("    %s announces '%s', which is never published\n", def->name, def->options[i]);
                missing++;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(0, missing);
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-038
 * @scenario Every announced option is accepted on the command topic and round-trips
 * @given A select entity and one of its announced options
 * @when Home Assistant sends that option text to the select's command topic
 * @then The parser accepts it and the resulting state publishes the same option
 */
void test_announced_options_round_trip_through_parser(void) {
    static const mqtt_cmd_type_t expected_cmd[MQTT_HA_SELECT_COUNT] = {
        [MQTT_HA_SELECT_CUSTOM_BUTTON] = MQTT_CMD_CUSTOM_BUTTON,
        [MQTT_HA_SELECT_MODE]          = MQTT_CMD_MODE,
        [MQTT_HA_SELECT_ENABLE_C2]     = MQTT_CMD_ENABLE_C2,
        [MQTT_HA_SELECT_CABLE_LOCK]    = MQTT_CMD_CABLE_LOCK,
        [MQTT_HA_SELECT_PRIO_STRATEGY] = MQTT_CMD_PRIO_STRATEGY,
    };
    int failures = 0;
    for (int id = 0; id < MQTT_HA_SELECT_COUNT; id++) {
        const mqtt_ha_select_def_t *def = &mqtt_ha_selects[id];
        char topic[96];
        snprintf(topic, sizeof(topic), "%s%s", PREFIX, def->command_topic);
        for (uint8_t i = 0; i < def->option_count; i++) {
            mqtt_command_t c;
            bool ok = mqtt_parse_command(PREFIX, topic, def->options[i], &c);
            const char *state = ok ? state_after_command((mqtt_ha_select_t)id, &c) : NULL;
            if (!ok || c.cmd != expected_cmd[id] || !state || strcmp(state, def->options[i]) != 0) {
                printf("    %s option '%s' does not round-trip (parsed=%d, state='%s')\n",
                       def->name, def->options[i], ok, state ? state : "(null)");
                failures++;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(0, failures);
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-038
 * @scenario Select command topics are the state topics under /Set
 * @given The select entity table
 * @when The command topic of each select is compared with its state topic
 * @then The command topic is "/Set" followed by the state topic
 */
void test_command_topic_matches_state_topic(void) {
    for (int id = 0; id < MQTT_HA_SELECT_COUNT; id++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "/Set%s", mqtt_ha_selects[id].topic);
        TEST_ASSERT_EQUAL_STRING(expected, mqtt_ha_selects[id].command_topic);
    }
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-037
 * @scenario Priority Strategy announces the strategy names it publishes
 * @given The Priority Strategy select, whose state topic carries the strategy name
 * @when Its options and published values are read
 * @then Options are ModbusAddr, FirstConn, LastConn and strategy 2 publishes LastConn
 */
void test_prio_strategy_options_are_names(void) {
    const mqtt_ha_select_def_t *def = &mqtt_ha_selects[MQTT_HA_SELECT_PRIO_STRATEGY];
    TEST_ASSERT_EQUAL_INT(3, def->option_count);
    TEST_ASSERT_EQUAL_STRING("ModbusAddr", def->options[0]);
    TEST_ASSERT_EQUAL_STRING("FirstConn", def->options[1]);
    TEST_ASSERT_EQUAL_STRING("LastConn", def->options[2]);
    TEST_ASSERT_EQUAL_STRING("LastConn", mqtt_ha_prio_strategy_state(2));
    TEST_ASSERT_TRUE(mqtt_ha_prio_strategy_state(3) == NULL);
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-037
 * @scenario Cable Lock keeps its 0/1 contract
 * @given The Cable Lock select (values settled as 0/1 in upstream issue #249)
 * @when Its options and published values are read
 * @then Options are "0" and "1", and the state topic is /CableLock
 */
void test_cable_lock_keeps_numeric_contract(void) {
    const mqtt_ha_select_def_t *def = &mqtt_ha_selects[MQTT_HA_SELECT_CABLE_LOCK];
    TEST_ASSERT_EQUAL_STRING("/CableLock", def->topic);
    TEST_ASSERT_EQUAL_INT(2, def->option_count);
    TEST_ASSERT_EQUAL_STRING("0", mqtt_ha_cable_lock_state(0));
    TEST_ASSERT_EQUAL_STRING("1", mqtt_ha_cable_lock_state(1));
    TEST_ASSERT_TRUE(mqtt_ha_cable_lock_state(2) == NULL);
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-037
 * @scenario Mode state never indexes past the mode names
 * @given Access is neither Off nor Pause
 * @when Mode is 3, one past the last mode name
 * @then No state value is returned instead of reading beyond the name table
 */
void test_mode_state_out_of_range_is_null(void) {
    TEST_ASSERT_EQUAL_STRING("Solar", mqtt_ha_mode_state(false, false, 2));
    TEST_ASSERT_TRUE(mqtt_ha_mode_state(false, false, 3) == NULL);
    TEST_ASSERT_EQUAL_STRING("Off", mqtt_ha_mode_state(true, false, 3));
    TEST_ASSERT_EQUAL_STRING("Pause", mqtt_ha_mode_state(false, true, 3));
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-039
 * @scenario Master-only selects are announced only on the load balancing Master
 * @given Priority Strategy is only published when LoadBl is 1
 * @when The announce gate is evaluated for standalone, Master and Node roles
 * @then It is open only for LoadBl 1
 */
void test_master_gate_follows_load_balancing_role(void) {
    TEST_ASSERT_EQUAL_INT(MQTT_HA_GATE_MASTER, mqtt_ha_selects[MQTT_HA_SELECT_PRIO_STRATEGY].gate);
    TEST_ASSERT_FALSE(mqtt_ha_gate_open(MQTT_HA_GATE_MASTER, 0, 0));
    TEST_ASSERT_TRUE(mqtt_ha_gate_open(MQTT_HA_GATE_MASTER, 1, 0));
    TEST_ASSERT_FALSE(mqtt_ha_gate_open(MQTT_HA_GATE_MASTER, 2, 0));
    TEST_ASSERT_FALSE(mqtt_ha_gate_open(MQTT_HA_GATE_MASTER, 8, 0));
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-039
 * @scenario Cable Lock is announced only when a lock actuator is configured
 * @given Cable Lock state is only published when Lock is not 0
 * @when The announce gate is evaluated for Lock 0 (disabled), 1 (solenoid) and 2 (motor)
 * @then It is closed for 0 and open for 1 and 2, independent of the role
 */
void test_cable_lock_gate_follows_lock_setting(void) {
    TEST_ASSERT_EQUAL_INT(MQTT_HA_GATE_CABLE_LOCK, mqtt_ha_selects[MQTT_HA_SELECT_CABLE_LOCK].gate);
    TEST_ASSERT_FALSE(mqtt_ha_gate_open(MQTT_HA_GATE_CABLE_LOCK, 1, 0));
    TEST_ASSERT_TRUE(mqtt_ha_gate_open(MQTT_HA_GATE_CABLE_LOCK, 2, 1));
    TEST_ASSERT_TRUE(mqtt_ha_gate_open(MQTT_HA_GATE_CABLE_LOCK, 0, 2));
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-039
 * @scenario Selects without a publish condition are always announced
 * @given Mode, EnableC2 and Custom Button are published on every node
 * @when Their announce gate is evaluated on a Node without a lock
 * @then The gate is open
 */
void test_ungated_selects_always_announced(void) {
    TEST_ASSERT_EQUAL_INT(MQTT_HA_GATE_ALWAYS, mqtt_ha_selects[MQTT_HA_SELECT_MODE].gate);
    TEST_ASSERT_EQUAL_INT(MQTT_HA_GATE_ALWAYS, mqtt_ha_selects[MQTT_HA_SELECT_ENABLE_C2].gate);
    TEST_ASSERT_EQUAL_INT(MQTT_HA_GATE_ALWAYS, mqtt_ha_selects[MQTT_HA_SELECT_CUSTOM_BUTTON].gate);
    TEST_ASSERT_TRUE(mqtt_ha_gate_open(MQTT_HA_GATE_ALWAYS, 2, 0));
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-039
 * @scenario A role or lock change is detectable so discovery can be refreshed
 * @given The announce gate mask was taken when MQTT connected
 * @when LoadBl or Lock changes at runtime (web, LCD menu or Modbus)
 * @then The gate mask changes, and it does not change for Node address changes
 */
void test_gate_mask_changes_with_role_and_lock(void) {
    uint8_t node = mqtt_ha_gate_mask(2, 0);
    TEST_ASSERT_EQUAL_INT(node, mqtt_ha_gate_mask(5, 0));
    TEST_ASSERT_NOT_EQUAL(node, mqtt_ha_gate_mask(1, 0));
    TEST_ASSERT_NOT_EQUAL(node, mqtt_ha_gate_mask(2, 1));
    TEST_ASSERT_EQUAL_INT(mqtt_ha_gate_mask(2, 1), mqtt_ha_gate_mask(2, 2));
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-040
 * @scenario Options are rendered as a discovery JSON fragment
 * @given The Priority Strategy select
 * @when Its options are written into a large enough buffer
 * @then The fragment lists the names in order and the length is returned
 */
void test_options_json_fragment(void) {
    char buf[96];
    int n = mqtt_ha_select_options_json(&mqtt_ha_selects[MQTT_HA_SELECT_PRIO_STRATEGY], buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(", \"options\" : [\"ModbusAddr\", \"FirstConn\", \"LastConn\"]", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-040
 * @scenario A too small buffer never yields a truncated options fragment
 * @given A buffer shorter than the Priority Strategy options fragment
 * @when The options are written into it
 * @then -1 is returned and the buffer holds an empty, terminated string
 */
void test_options_json_truncation_rejected(void) {
    char buf[20];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_EQUAL_INT(-1, mqtt_ha_select_options_json(&mqtt_ha_selects[MQTT_HA_SELECT_PRIO_STRATEGY], buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/*
 * @feature Home Assistant Select Discovery
 * @req REQ-MQTT-040
 * @scenario Every select's options fit the firmware's discovery buffer
 * @given The firmware renders options into a MQTT_HA_OPTIONS_JSON_MAX byte buffer
 * @when The options fragment of each select in the table is rendered
 * @then None is rejected, so no select is silently left unannounced
 */
void test_all_options_fit_firmware_buffer(void) {
    for (int id = 0; id < MQTT_HA_SELECT_COUNT; id++) {
        char buf[MQTT_HA_OPTIONS_JSON_MAX];
        TEST_ASSERT_GREATER_THAN(0, mqtt_ha_select_options_json(&mqtt_ha_selects[id], buf, sizeof(buf)));
    }
}

int main(void) {
    TEST_SUITE_BEGIN("Home Assistant Select Discovery");

    RUN_TEST(test_all_options_fit_firmware_buffer);

    RUN_TEST(test_published_states_are_announced_options);
    RUN_TEST(test_announced_options_are_reachable);
    RUN_TEST(test_announced_options_round_trip_through_parser);
    RUN_TEST(test_command_topic_matches_state_topic);
    RUN_TEST(test_prio_strategy_options_are_names);
    RUN_TEST(test_cable_lock_keeps_numeric_contract);
    RUN_TEST(test_mode_state_out_of_range_is_null);
    RUN_TEST(test_master_gate_follows_load_balancing_role);
    RUN_TEST(test_cable_lock_gate_follows_lock_setting);
    RUN_TEST(test_ungated_selects_always_announced);
    RUN_TEST(test_gate_mask_changes_with_role_and_lock);
    RUN_TEST(test_options_json_fragment);
    RUN_TEST(test_options_json_truncation_rejected);

    TEST_SUITE_RESULTS();
}
