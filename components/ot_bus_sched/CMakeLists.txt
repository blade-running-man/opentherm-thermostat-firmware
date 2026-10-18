# The pure half of the bus is a SEPARATE COMPONENT, not a file inside ot_bus.
#
# The reason was found by running it, not by reasoning. In a host build PlatformIO
# compiles ALL the sources of a library, not only those whose headers the test
# includes: as soon as ot_bus.c with FreeRTOS appeared next to the scheduler,
# test_ot_bus_sched stopped building with `fatal error: esp_err.h not found`.
# Dependency resolution at include granularity is a property of ESP-IDF, not of
# PlatformIO, and it must not be relied on in host suites.
#
# DO NOT move this file back into ot_bus and DO NOT fix this via srcFilter:
# srcFilter makes a single component half host-buildable and breaks the claim in
# platformio.ini that the host tests compile exactly the same sources the device
# executes.
idf_component_register(SRCS "ot_bus_sched.c"
                       INCLUDE_DIRS "include")
