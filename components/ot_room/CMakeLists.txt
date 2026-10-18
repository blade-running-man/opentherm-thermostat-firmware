# The registry of room-temperature sources: composes ot_sensor
# per slot, picks a steer (room-role only) and a display (any role) reading.
#
# Pure: no ESP-IDF header, no timers, no tasks, no allocation. Time enters only as an
# argument, exactly like ot_sensor and ot_control -- see ot_room.h for why.
#
# DO NOT add a REQUIRES beyond ot_sensor here. PlatformIO compiles ALL the sources of a
# library whose header a suite includes, so pulling in esp_timer.h or a lock here would stop
# test_ot_room from linking on the host at all -- the rake already recorded for ot_bus_sched
# and ot_sensor.
idf_component_register(SRCS "ot_room.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_sensor)
