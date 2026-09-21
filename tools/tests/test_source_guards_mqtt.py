# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Source guards for the MQTT glue: the calls no host suite can see.

ot_mqtt_link is FreeRTOS and esp-mqtt glue with no suite; what it carries out is decided by the
pure ot_mqtt and ot_ha (test_ot_mqtt*, test_ot_ha*). What ties the two together is a CALL, and a
call replaced by its obvious neighbour compiles, links and passes every suite while undoing the rule
it carried -- the lesson of test_source_guards.py, whose helpers these tests reuse:

* a command reaching ot_command_check() or the executor past ot_mqtt_handle(), or with an origin
  named here instead of in pure code -- the command's one entry point;
* the retain flag not handed to ot_mqtt_decide() -- a retained command would feed the watchdog on
  every reconnect, a required behaviour;
* a reboot, the state model's lock, or a disabled reconnect in the glue;
* the event handler doing anything but copy bytes -- the heater firmware's ABBA deadlock;
* a secret in a log line (CLAUDE.md, "Secrets are write-only").
"""
import re
from pathlib import Path

from test_source_guards import code, flat, uses

ROOT = Path(__file__).resolve().parents[2]
LINK = ROOT / "components" / "ot_mqtt_link"
TASK = LINK / "ot_mqtt_link.c"
CLIENT = LINK / "ot_mqtt_link_client.c"
PUBLISH = LINK / "ot_mqtt_link_publish.c"
HTTP_CONFIG = ROOT / "components" / "ot_http" / "ot_http_config.c"
MAIN = ROOT / "src" / "main.cpp"


def link_sources():
    files = sorted(LINK.glob("*.c"))
    assert {f.name for f in files} >= {TASK.name, CLIENT.name, "ot_mqtt_link_publish.c"}
    return "\n".join(code(f) for f in files)


def function_body(src, name):
    m = re.search(rf"\b{name}\([^)]*\)\s*\{{(.*?)\n\}}\n", src, re.S)
    assert m, f"{name}() not found"
    return m.group(1)


def test_every_command_goes_through_ot_mqtt_handle_and_the_glue_names_no_origin():
    src = link_sources()
    assert len(uses(src, "ot_mqtt_handle")) == 1, "one call: the pure meeting with the executor"
    assert uses(src, "ot_command_check") == [], "a second early answer beside ot_mqtt_handle()"
    assert uses(src, "ot_command_encode") == [], "a write past the owner check (ot_command.h)"
    for origin in ("OT_ORIGIN_HA", "OT_ORIGIN_WEB"):
        assert uses(src, origin) == [], f"{origin} named in the glue: the origin is ot_mqtt's"
    assert "ot_thermostat_control_apply(origin, cmd, value, &store_err)" in flat(src), \
        "the effect forwards the origin ot_mqtt_handle() hands it, and nothing else"


def test_the_retain_flag_reaches_the_pure_decision_untouched():
    assert "in.retained = ev->retain;" in flat(code(CLIENT)), \
        "the handler must copy esp-mqtt's retain flag"
    assert "m->payload, m->payload_len, m->retained);" in flat(code(TASK)), \
        "ot_mqtt_decide() must be handed the retain flag the handler copied"
    # Both literals above still match with `m.retained = false;` written between them, and the
    # the required reconnect behaviour is then gone.
    assert len(re.findall(r"retained\s*=", link_sources())) == 1, \
        "`retained` is assigned exactly once in this component, from esp-mqtt's own flag"


def test_a_refused_subscription_is_seen_where_esp_mqtt_reports_it():
    # esp-mqtt sets MQTT_ERROR_TYPE_SUBSCRIBE_FAILED on a SUBACK byte >= 0x80 and then dispatches
    # MQTT_EVENT_SUBSCRIBED -- never MQTT_EVENT_ERROR (mqtt_client.c, the SUBACK branch). Read
    # under ERROR alone, a broker ACL denying <prefix>/+/set leaves the device silently deaf and
    # the hardware check fails against otherwise-correct code.
    body = flat(function_body(code(CLIENT), "on_event"))
    assert ("case MQTT_EVENT_SUBSCRIBED: if (ev->error_handle != NULL && "
            "ev->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED) "
            "bump(&g_sub_refused); break;") in body, \
        ("the subscription refusal must be counted inside MQTT_EVENT_SUBSCRIBED, and the case must "
         "END there: without the break it falls into MQTT_EVENT_ERROR, whose two tests are then "
         "either false or counted twice")


def test_the_two_mqtt_tasks_stay_below_the_bus_and_the_executor():
    # ot_bus is 10 and ot_thermostat 4: MQTT may never delay either, and both
    # numbers are literals no test can otherwise see.
    assert "#define PRIORITY 3" in flat(code(TASK)), "the link task's priority"
    assert "cfg.task.priority = 3;" in flat(code(CLIENT)), "esp-mqtt's own task's priority"


def test_the_owner_topic_and_the_discovery_flag_reach_the_pure_decisions():
    # Two call sites a mutation showed unguarded: hard-wiring
    # "online" makes the controls available in LOCAL mode, and ignoring g_discovery publishes
    # documents the owner switched off.
    text = flat(code(PUBLISH))
    assert "ot_mqtt_owner_payload(cfg.mode)" in text, \
        "the owner payload comes from the snapshot the executor stepped with"
    assert "ot_ha_wants(g_discovery, s_want);" in text, \
        "ha_discovery reaches ot_ha_wants(), or Q10's answer is not carried out"


def test_the_event_handler_only_sets_flags_and_copies_bytes():
    body = function_body(code(CLIENT), "on_event")
    for forbidden in ("esp_mqtt_client_", "ESP_LOG", "xSemaphoreTake", "ot_thermostat_",
                      "ot_state_", "vTaskDelay", "portMAX_DELAY"):
        assert forbidden not in body, f"{forbidden} in the esp-mqtt event handler"
    assert "xQueueSend(g_inbox, &in, 0)" in flat(body), "the inbox is fed without waiting"


def test_nothing_in_the_glue_reboots_pauses_the_bus_or_stops_reconnecting():
    src = link_sources()
    for name in ("esp_restart", "abort", "ot_lock", "ot_unlock", "taskENTER_CRITICAL",
                 "disable_auto_reconnect", "ot_bus_set_status"):
        assert uses(src, name) == [], f"{name} in ot_mqtt_link"
    text = flat(src)
    assert "cfg.outbox.limit = 16384;" in text, "the outbox is bounded"
    assert "cfg.network.reconnect_timeout_ms = 10000;" in text


def test_esp_mqtts_own_per_attempt_lines_are_silenced():
    text = flat(code(TASK))
    for tag in ('"mqtt_client"', '"transport_base"', '"transport"', '"esp-tls"'):
        assert tag in text, f"{tag} not silenced: a dead broker floods GET /api/log"
    assert "esp_log_level_set(noisy[i], ESP_LOG_NONE);" in text


def test_no_log_line_carries_a_credential():
    for line in re.findall(r"ESP_LOG[EWIDV]\(TAG,[^;]*;", link_sources(), re.S):
        for secret in ("password", "->user", "s_broker", "payload"):
            assert secret not in line, f"{secret} in a log line: {flat(line)}"


def test_the_broker_settings_are_wiped_after_every_read():
    body = flat(function_body(code(TASK), "follow_settings"))
    assert body.startswith("ot_net_broker(&s_broker);")
    assert body.endswith("explicit_bzero(&s_broker, sizeof s_broker);")


def test_status_carries_the_broker_block_and_provisioning_still_does_not():
    text = flat(code(HTTP_CONFIG))
    assert "ot_mqtt_link_status(&mqtt);" in text
    assert "ot_wire_render_provision(&st, &mqtt, &device," in text
    assert text.count("ot_wire_render_provision(&st, NULL, &device,") == 1, \
        "POST /api/provision answers before the radio moved: it has nothing to say about a broker"
    assert uses(text, "ot_mqtt_link_start") == [], "the httpd task must never enter esp-mqtt"


def test_main_starts_the_link_after_the_bus_and_the_executor_and_before_http():
    src = code(MAIN)
    order = [src.index(name) for name in
             ("ot_bus_start(", "ot_thermostat_start(", "ot_mqtt_link_start(", "ot_http_start(")]
    assert order == sorted(order), "bus, executor, MQTT, HTTP -- in that order (main.cpp)"


def test_a_fragmented_message_is_never_read_as_a_whole_command():
    # esp-mqtt dispatches one MQTT_EVENT_DATA per esp_transport_read() when a message needs more
    # than one read to arrive whole (mqtt_client.c, "Provide support for ... fragmented message"):
    # current_data_offset is 0 only on the first of those events, and CONFIG_MQTT_TOPIC_PRESENT_
    # ALL_DATA_EVENTS -- off everywhere in this tree's sdkconfig -- is the only reason a later
    # fragment's missing topic (topic_len 0) would otherwise reject it too. This line must not
    # depend on that Kconfig default holding: it is the one check that is correct on its own.
    body = flat(function_body(code(CLIENT), "on_event"))
    assert "if (ev->current_data_offset != 0) break;" in body, \
        "a later fragment of one message must never be read as a short, whole command"


def test_commands_already_queued_are_drained_before_settings_are_followed():
    # link_task() must drain() BEFORE follow_settings(): a command queued under the OLD prefix,
    # left to be judged after a restart has already rewritten g_ctx.prefix, is graded against a
    # prefix it was never published under and silently dropped as a mismatch -- no refusal, no
    # counter, no log line. The comment above drain() says why; the order itself was unpinned
    # until this guard, and a mutation swapping it passed all twelve.
    body = function_body(code(TASK), "link_task")
    assert body.index("drain();") < body.index("follow_settings();"), \
        "drain() must run before follow_settings() on every pass of the link task"


def test_both_command_subscriptions_are_taken_at_qos_1():
    # Commands are subscribed at QoS 1, "duplicates are harmless because every command
    # is idempotent". At QoS 0 a broker under load drops a ch_enable or a ch_setpoint and NOTHING
    # says so -- no refusal, no counter, no log line -- and the watchdog goes unfed, so a healthy
    # Home Assistant looks dead and the failsafe eventually lights the burner. The
    # QoS is a literal at the call site only (OT_MQTT_QOS_META is 1, OT_MQTT_QOS_STATE is 0), and
    # no checklist item models packet loss: downgrading it passed all twelve guards.
    subs = re.findall(r"esp_mqtt_client_subscribe_single\([^;]*;", flat(code(PUBLISH)))
    assert len(subs) == 3, ("three subscriptions: <prefix>/+/set, homeassistant/status and "
                             "<prefix>/room/state")
    meta = [s for s in subs if s.endswith("OT_MQTT_QOS_META);")]
    assert len(meta) == 2, "exactly the two command/discovery subscriptions are at QoS 1"


def test_the_room_state_subscription_is_taken_at_qos_0():
    # <prefix>/room/state is a reading, not a command -- a missed one is simply replaced by
    # the next (ot_sensor's own staleness rule), unlike a missed command, which would starve the
    # watchdog unnoticed (the guard above). Pinned separately so a QoS bump here (raising cost,
    # not safety) does not silently also weaken the command guard's count.
    text = flat(code(PUBLISH))
    assert 'ot_mqtt_topic(g_ctx.prefix, "room", "state", s_topic, sizeof s_topic) > 0' in text, \
        "the <prefix>/room/state subscription topic must be built with the shared topic helper"
    assert ("r3 = esp_mqtt_client_subscribe_single(g_client, s_topic, OT_MQTT_QOS_STATE);"
            in text), "the room/state subscription (r3) must be taken at QoS 0, a measurement"


def test_a_room_reading_is_dispatched_straight_to_ot_room_never_through_ot_mqtt_handle():
    # OT_MQTT_ROOM is a measurement (ot_mqtt.h), not a command -- it must reach
    # ot_thermostat_room_submit() directly and must NEVER be judged by ot_mqtt_handle(), which
    # runs a command through ot_command_check()/the executor and feeds the watchdog. Routed
    # there instead, a republished sensor value would count as HA activity (masking a dead
    # controller) or be misjudged as an unknown key. Slot 1 is the MQTT room slot (design).
    src = link_sources()
    assert "case OT_MQTT_ROOM:" in flat(src), "the switch must judge OT_MQTT_ROOM explicitly"
    body = function_body(code(TASK), "handle_inbound")
    m = re.search(r"case OT_MQTT_ROOM:(.*?)(?:case |\Z)", body, re.S)
    assert m, "OT_MQTT_ROOM must be its own case in handle_inbound()'s switch"
    case_body = flat(m.group(1))
    assert "ot_mqtt_handle" not in case_body, \
        "a room reading must never cross ot_mqtt_handle() (that is the command's one entry point)"
    assert "ot_thermostat_room_submit(1, in.value)" in flat(src) or \
        "ot_thermostat_room_submit(1, celsius)" in flat(src), \
        "the room reading must reach ot_thermostat_room_submit(), slot 1, with its value"


def test_one_publishing_pass_stays_bounded():
    # PER_PASS is how many messages one pass may publish before link_task() comes back round and
    # drains its inbox FIRST (ot_mqtt_link.c, the order the guard above pins). Raised, a burst --
    # a fresh connection owing every state, HA's `online` re-publishing all 66 documents, a prefix
    # rename -- holds the task for as long as it takes while commands queue behind it, and the
    # inbox of 8 drops them without waiting (ot_mqtt_link_client.c, xQueueSend with a zero delay).
    # The number is a literal no suite compiles, like PRIORITY and cfg.outbox.limit above; raising
    # it to 200 passed every other guard.
    text = flat(code(PUBLISH))
    assert "#define PER_PASS 8" in text, "the per-pass publish budget"
    assert "int budget = PER_PASS;" in text, "the budget comes from it, never written again"
