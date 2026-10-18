# Pure: no ESP-IDF headers, no driver, no RMT. The DS18B20 CRC-8 and temperature decode,
# split off from ot_onewire.c so the host suite (test_ot_onewire) links exactly these bytes
# and never drags in the RMT transport.
#
# DO NOT add a REQUIRES here. The moment this component reaches driver/ or esp_timer.h,
# PlatformIO -- which compiles ALL the sources of a library it pulls in -- can no longer
# build test_ot_onewire at all (the rake already recorded for ot_sensor and ot_bus_sched).
idf_component_register(SRCS "ot_onewire_decode.c"
                       INCLUDE_DIRS "include")
