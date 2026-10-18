# The executor core: the ladder of states, the watchdog, the bounded
# failsafe, the boost, the held ID 1 and the ownership of every command.
#
# Pure: no ESP-IDF header, no timers, no tasks, no allocation. Time enters only as an argument,
# which is what lets the 15-minute watchdog, the three-day heat arm and the 49-day millisecond
# wrap be tested on the host in microseconds.
#
# DO NOT add a REQUIRES here to reach the clock, NVS or the bus. PlatformIO compiles ALL the
# sources of a library whose header a suite includes, so one esp_timer.h in this directory stops
# test_ot_control* -- and test_ot_command, which includes ot_control.h -- from
# linking at all. The rake is recorded for ot_bus_sched and ot_sensor. The impure half is
# ot_thermostat.
#
# DO NOT REQUIRES ot_sensor for its saturating add: ot_control copies the four-line helper
# (ot_control_failsafe.c) rather than pulling a component, its state type and its outlier filter
# into every suite that includes this header.
#
# ot_bus_sched is required for the OT_STATUS_* bit names alone, and it is pure. DO NOT reach for
# ot_bus instead: that one is the FreeRTOS half.
idf_component_register(SRCS "ot_control.c" "ot_control_apply.c" "ot_control_check.c"
                            "ot_control_failsafe.c" "ot_control_boost.c" "ot_control_names.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_bus_sched)
