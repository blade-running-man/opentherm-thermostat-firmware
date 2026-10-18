# Home Assistant discovery. PURE, host-tested (test_ot_ha, test_ot_ha_plan): the
# generated documents (include/ha_generated.h, tools/render_discovery.py), the six tokens filled at
# runtime, and when each document is owed. The esp-mqtt glue that publishes them is ot_mqtt_link.
#
# ot_state and ot_registry are PUBLIC: ot_ha.h names ot_availability_t, and the documents index
# OT_ENTITIES. ot_json for the one escaper (ot_json.h: DO NOT grow a second). NOT listed, and it
# must never be: mqtt, esp_timer, freertos -- a suite includes ot_ha.h, and PlatformIO compiles
# every source of a library whose header a suite includes.
idf_component_register(SRCS "ot_ha.c" "ot_ha_plan.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_state ot_registry
                       PRIV_REQUIRES ot_json)
