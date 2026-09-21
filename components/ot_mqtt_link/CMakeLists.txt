# The broker connection: the esp-mqtt client, the one task that owns it, what it
# publishes and what it hears. IMPURE, and NOT host-built: no suite includes ot_mqtt_link.h. What it
# carries out is decided by ot_mqtt and ot_ha, both pure and host-tested; the calls that tie this
# glue to them are pinned by tools/tests/test_source_guards_mqtt.py.
#
# ot_wire is PUBLIC: ot_mqtt_link.h hands out ot_wire_mqtt_t. mqtt is esp-mqtt, the pinned
# framework's own component. ot_thermostat for the executor (the command's final answer, the
# snapshot the owner topic and the bounds are read from); ot_bus for a frame command; ot_net for
# the broker settings, the identity and the address. There is no cycle: none of them requires this.
idf_component_register(SRCS "ot_mqtt_link.c" "ot_mqtt_link_client.c" "ot_mqtt_link_publish.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_wire
                       PRIV_REQUIRES mqtt ot_mqtt ot_ha ot_net ot_thermostat ot_bus ot_state
                                     ot_registry ot_config esp_timer esp_app_format)
