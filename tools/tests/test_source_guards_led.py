# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Source guards for the status-LED glue: the calls no host suite can see.

ot_led_task is FreeRTOS and led_strip/RMT glue with no suite; what it decides (colour, motion, the
health ladder) is the pure, host-tested ot_led (test_ot_led). What ties the two together is a set
of CALLS, and a call replaced by its obvious neighbour compiles, links and passes every suite while
undoing the rule it carried -- the lesson of test_source_guards.py, whose helpers these tests reuse:

* one of the five sources (ot_mqtt_link_status, ot_net_get_state, ot_net_has_credentials,
  ot_state_get, ot_thermostat_control_get) quietly dropped from the sample -- the LED would then
  show a colour for a world it never actually looked at;
* "flame" read without first checking OT_AVAIL_OK -- a STALE or NEVER reading would show as
  "firing" instead of "idle";
* the ot_net_state_t -> ot_led_net_state_t conversion done with a bare cast instead of an explicit
  switch -- the two enums live in different headers and nothing else keeps them numbered alike;
* a bus write or the bus lock creeping into reader-only glue -- the OpenTherm master must never
  fall silent, and a cosmetic LED task is the last place that invariant should be re-litigated;
* the start guarded by something other than the board's rgb.gpio, so a board with no LED still
  spins up RMT on a GPIO that was never meant to carry one (CLAUDE.md, "not a single GPIO number
  outside components/board/").
"""
from pathlib import Path

from test_source_guards import code, flat, uses

ROOT = Path(__file__).resolve().parents[2]
LED_TASK = ROOT / "components" / "ot_led_task" / "ot_led_task.c"
MAIN = ROOT / "src" / "main.cpp"


def led_source():
    return code(LED_TASK)


def test_all_five_world_sources_are_sampled():
    src = led_source()
    for name in ("ot_mqtt_link_status", "ot_net_get_state", "ot_net_has_credentials",
                 "ot_state_get", "ot_thermostat_control_get"):
        assert uses(src, name), f"{name} not read: the LED would sample a world missing a source"


def test_flame_is_read_only_after_the_availability_check():
    # The two must sit close together and OT_AVAIL_OK first, so a rewrite that moves the read
    # before the check (or drops the check) still fails: "near" is enforced by a small window,
    # not just "both present somewhere in the file".
    src = flat(led_source())
    idx_read = src.find('ot_state_get("flame"')
    assert idx_read != -1, 'ot_state_get("flame" ...) not found'
    idx_avail = src.find("OT_AVAIL_OK")
    assert idx_avail != -1, "OT_AVAIL_OK not found"
    assert abs(idx_avail - idx_read) < 200, \
        "OT_AVAIL_OK must sit right beside the flame read, or a STALE/NEVER value can pass as ON"


def test_the_net_state_is_converted_by_an_explicit_switch_not_a_cast():
    src = led_source()
    assert "switch" in src, "ot_net_state_t must be converted with an explicit switch"
    assert uses(src, "OT_LED_NET_DOWN"), \
        "OT_LED_NET_DOWN not named: an unknown/added net state must fail toward showing a problem"
    flat_src = flat(src)
    # A bare cast between the two enums would defeat the whole point of the switch: it would still
    # compile and still assign w->net, but silently misread any value the two enums do not share.
    assert "(ot_led_net_state_t)" not in flat_src, \
        "a cast from ot_net_state_t to ot_led_net_state_t bypasses the explicit switch"


def test_the_glue_never_touches_the_bus():
    src = led_source()
    for name in ("ot_bus_write", "ot_bus_write_if_idle", "ot_lock", "ot_unlock",
                 "taskENTER_CRITICAL"):
        assert uses(src, name) == [], \
            f"{name} in ot_led_task.c: the status LED must be reader-only, never touching the bus"


def test_main_starts_the_led_task_only_when_the_board_has_one():
    src = code(MAIN)
    assert "if (b->rgb.gpio >= 0) {" in flat(src), \
        "the LED task must be started only when the board descriptor names a real GPIO"
    idx_guard = flat(src).find("if (b->rgb.gpio >= 0) {")
    idx_start = flat(src).find("ot_led_task_start(b)")
    assert idx_start != -1, "ot_led_task_start(b) not called from main.cpp"
    assert 0 <= idx_start - idx_guard < 200, \
        "ot_led_task_start() must be called inside the rgb.gpio >= 0 guard, not unconditionally"
