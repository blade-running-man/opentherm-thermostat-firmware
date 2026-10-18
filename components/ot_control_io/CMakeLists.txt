# The executor's translations (include/ot_control_io.h): the configuration snapshot, the patch a
# command persists through, the bus's report, the ID 56 readback, the reset reason, the synthetic
# entities and the GET /api/control document -- everything ot_thermostat would otherwise decide
# where no host suite reaches.
#
# Pure: no ESP-IDF header, no task, no lock. DO NOT add a REQUIRES for the clock, NVS or the bus:
# PlatformIO compiles every source of a library whose header a suite includes, and one esp_*.h
# here stops test_ot_control_io linking. The impure half is ot_thermostat.
#
# All four requirements are PUBLIC, because the header's signatures carry their types:
# ot_config_public_t and ot_config_patch_t, the ot_control_* structs, ot_api_control_t, and
# ot_frame's ot_msg_type_t (the readback). ot_frame is pure too.
idf_component_register(SRCS "ot_control_io.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_control ot_config ot_api ot_frame)
