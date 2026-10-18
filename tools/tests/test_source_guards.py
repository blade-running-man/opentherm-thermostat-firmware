# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Source guards: the glue calls no host suite reaches.

ot_bus.c and ot_thermostat's sources are FreeRTOS and have no suite; their pure halves do --
ot_bus_track (test_ot_bus_track) and ot_control_io (test_ot_control_io*). What ties each pair
together is a CALL, and a call replaced by its obvious neighbour compiles, links and passes every
suite while undoing the fix it carried:

* ot_bus.c reaching the scheduler's queue itself -- ot_bus_sched_write() or ot_bus_sched_done()
  instead of ot_bus_track_*() -- or handing ot_bus_track_done() a generation read at the wrong
  moment, or an answer it did not get: the write generation misses a write, and a hand write
  queued while another is on the wire is dropped after its 202 (G1);
* the thermostat queueing with ot_bus_write(), which evicts a pending hand write,
  instead of ot_bus_write_if_idle();
* the thermostat handing ot_control_io_send() an owed record made fresh each tick, or reset
  between ticks: every ID 56 the bus refused is forgotten, and a DHW setpoint accepted with a 202
  never reaches the boiler (G-F2; the re-review's mutation T8 passed every suite and both boards).

So these tests read the sources with their comments stripped -- a comment that NAMES a call is
not one -- and pin the calls. Each also asserts the call it expects IS there, so a rename cannot
make a guard pass by finding nothing.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUS = ROOT / "components" / "ot_bus" / "ot_bus.c"
THERMOSTAT = ROOT / "components" / "ot_thermostat"
TASK = THERMOSTAT / "ot_thermostat.c"
# The room mailbox/registry glue the two ot_room guards below pin moved out of
# TASK into its own file (file ceiling, CLAUDE.md) -- update THIS path, not the assertions, if it
# moves again.
ROOM = THERMOSTAT / "ot_thermostat_room.c"
HTTP_GATE = ROOT / "components" / "ot_http" / "ot_http_gate.c"
MAIN_CPP = ROOT / "src" / "main.cpp"


def code(path):
    """The C source without its comments. String literals in these files hold no comment
    markers, so the two patterns below are enough."""
    text = path.read_text()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def flat(text):
    """Whitespace collapsed to one space, so an alignment change is not a failure."""
    return " ".join(text.split())


def uses(src, name):
    """Every use of the identifier `name` -- a call, or a function passed by name."""
    return re.findall(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", src)


def test_ot_bus_reaches_the_write_queue_only_through_the_tracker():
    src = code(BUS)
    assert uses(src, "ot_bus_sched_write") == [], \
        "ot_bus.c queues past ot_bus_track: the write generation misses that write (G1)"
    assert uses(src, "ot_bus_sched_done") == [], \
        "ot_bus.c finishes an exchange past ot_bus_track: a write queued during it is dropped (G1)"
    assert len(uses(src, "ot_bus_track_write")) == 2, \
        "ot_bus_write() and the identity writes queue through the tracker"
    assert len(uses(src, "ot_bus_track_write_if_idle")) == 1, \
        "ot_bus_write_if_idle() queues through the tracker, once"
    assert len(uses(src, "ot_bus_track_done")) == 1, \
        "the bus task finishes each exchange through the tracker, once"


def test_the_bus_task_hands_the_tracker_the_step_generation_and_the_answer():
    src = flat(code(BUS))
    assert ("step = ot_bus_sched_step(&s_sched, now_ms()); gen = s_track.gen; "
            "taskEXIT_CRITICAL(&s_mux);") in src, \
        ("the generation is read in the critical section that builds the step -- read when the "
         "exchange is over, every write queued during it is invisible (G1)")
    assert ("ot_bus_track_done(&s_track, &s_sched, &step, gen, r == OT_EXCHANGE_OK, start, end);"
            in src), \
        "'answered' is the exchange's own result, never a constant (G1)"


def test_the_thermostat_never_queues_with_ot_bus_write():
    # EVERY source of the component, whatever its name: a file cut out later is covered unasked.
    files = sorted(THERMOSTAT.glob("*.c"))
    assert TASK in files, f"{TASK.name} is gone: a guard that reads nothing proves nothing"
    for f in files:
        assert uses(code(f), "ot_bus_write") == [], \
            f"{f.name}: ot_bus_write evicts a pending hand write"
    idle = [f.name for f in files for _ in uses(code(f), "ot_bus_write_if_idle")]
    assert idle == [TASK.name], \
        f"ot_bus_write_if_idle is handed to ot_control_io_send() once, by the tick; found: {idle}"


def test_the_tick_hands_ot_control_io_send_the_one_owed_record():
    raw = code(TASK)
    assert ("ot_control_io_send(&s_owed, &cfg, &out, ot_bus_write_if_idle);" in flat(raw)), \
        ("the tick calls ot_control_io_send() with the file-scope owed record and "
         "ot_bus_write_if_idle: a fresh record forgets every refused ID 56, ot_bus_write evicts")
    assert re.search(r"^static\s+ot_control_io_owed_t\s+s_owed\s*;", raw, flags=re.M), \
        ("'static ot_control_io_owed_t s_owed;' at file scope: the owed ID 56 must outlive the "
         "tick that the bus refused it in (G-F2)")
    assert len(uses(raw, "s_owed")) == 2, \
        ("s_owed is declared and handed to ot_control_io_send(), nothing else: a reset between "
         "ticks forgets a refused ID 56 as surely as a fresh record does")


def test_the_tick_saves_the_nvs_whole_hour_before_the_rtc_part_hour_mirror():
    # ot_thermostat.c's tick() is FreeRTOS glue with no host suite, and the ORDER of these
    # two calls carries load. NVS holds the whole-hour heat_hours; RTC_NOINIT mirrors the part-hour
    # (hh_ms) that survives a soft reset. Saving heat_hours FIRST makes a crash-window skew err
    # SAFE: the whole hour is on disk while RTC still holds the part-hour, so on restore the hour is
    # counted from BOTH -- heat_hours over-counts by an hour and the summer failsafe disarms
    # SOONER (less unwanted heat). Mirroring first errs UNSAFE: RTC's part-hour is reset while NVS
    # still holds the old count, the powered hour is lost from both, and the July burn is prolonged.
    # A neighbour swap compiles, links and passes every suite while reversing the fix, so pin it.
    raw = flat(code(TASK))
    save = raw.index("otth_save_heat_hours(out.heat_hours)")
    mirror = raw.index("otth_mirror(out.overdue_ms, out.hh_ms)")
    assert save < mirror, \
        ("otth_save_heat_hours() must precede otth_mirror(): saving the NVS whole-hour before the "
         "RTC part-hour mirror makes a crash-window skew over-count (failsafe disarms sooner = "
         "safe), not under-count (prolongs the summer burn)")


def test_the_tick_wires_ot_room_into_the_steer_input():
    # Moved from ot_thermostat.c into ot_thermostat_room.c (file
    # ceiling -- see ROOM's comment). ot_room is pure and host-tested (test_ot_room), but the
    # DRAIN, the TICK and the SELECTION that feed ot_control_in_t are FreeRTOS glue with no host
    # suite of their own. A neighbour swap -- ticking the registry but never reading the steer
    # selection back into `in`, or reading display into it instead -- compiles, links and passes
    # every suite while silently keeping the failsafe blind forever (not just with one ambient
    # slot, as before 2b, but even once a real room slot is configured). Pin all three.
    assert ROOM.exists(), f"{ROOM.name} is gone: a guard that reads nothing proves nothing"
    raw = flat(code(ROOM))
    assert "ot_room_tick(&s_room, now);" in raw, \
        "the tick must step ot_room's registry once per second, after draining the mailbox"
    assert "ot_room_select_steer(&s_room, &steer);" in raw, \
        "the tick must read the STEER selection (role-gated, ambient never steers) for `in`"
    assert "in->room_fresh =" in raw, \
        "ot_control_in_t.room_fresh must be set from ot_room's steer selection, every tick"


def test_ds18b20_task_submits_into_the_thermostat_mailbox():
    # main.cpp's ds18b20_task no longer filters or publishes for itself: it must
    # hand every good read to ot_thermostat_room_submit(), the mailbox ot_thermostat.c drains
    # into ot_room slot 0. Reverting to a task-local ot_sensor_update()/ot_state_set_virtual()
    # pair would compile and read fine but silently bypass the registry's steer selection.
    assert MAIN_CPP.exists(), f"{MAIN_CPP} is gone: a guard that reads nothing proves nothing"
    src = code(MAIN_CPP)
    assert "ot_thermostat_room_submit(" in src, \
        "ds18b20_task must submit its reading via ot_thermostat_room_submit(), not publish it itself"


def test_the_host_read_treats_a_truncated_header_as_foreign():
    # ot_http_gate.c is FreeRTOS/esp_http_server glue with no host suite. The Host-allowlist
    # decision (ot_http_host_ok) is pure and host-tested, but the mapping of the header READ into
    # that decision is not -- and that mapping held a rebinding bypass: mapping any non-OK
    # to host==NULL conflates ESP_ERR_HTTPD_RESULT_TRUNC (header >= the 64-byte buffer -> a Host
    # too long to be any address this device answers to) with ESP_ERR_NOT_FOUND (absent, which
    # fail-opens). A 64+ byte attacker domain then truncates -> "absent" -> fail-open -> the gate
    # is bypassed. Truncation MUST be refused, so pin that the file names the symbol.
    src = code(HTTP_GATE)
    assert "ESP_ERR_HTTPD_RESULT_TRUNC" in src, \
        ("ot_http_gate.c must handle a truncated Host header (ESP_ERR_HTTPD_RESULT_TRUNC) as a "
         "foreign host and refuse it -- mapping it to 'absent' fail-opens the rebinding gate")
    # And that the symbol is actually used to REFUSE (return false), not merely mentioned in prose.
    assert re.search(r"ESP_ERR_HTTPD_RESULT_TRUNC\s*\)\s*return\s+false", flat(src)), \
        ("the truncation branch must fail-CLOSED (return false), not just name the symbol")
