# Pure: no ESP-IDF headers, no timers, no tasks. Time enters only as an argument, and that
# is what lets the 24-hour STALE limit and the 49-day uint32 wrap be tested on the host in
# milliseconds instead of in days.
#
# DO NOT add a REQUIRES here to reach the clock. The moment this component includes
# esp_timer.h, PlatformIO -- which compiles ALL the sources of a library -- stops being
# able to build test_ot_sensor at all. That is the rake already recorded for ot_bus_sched.
idf_component_register(SRCS "ot_sensor.c"
                       INCLUDE_DIRS "include")
