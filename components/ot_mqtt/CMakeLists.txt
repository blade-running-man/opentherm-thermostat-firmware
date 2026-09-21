# MQTT's pure half: the topic tree, what a message means, what an accepted
# command makes happen, what a state payload is. Host suites test_ot_mqtt, test_ot_mqtt_out and
# test_ot_mqtt_control. The client, its task and its buffers are ot_mqtt_link's.
#
# ot_command and ot_control are PUBLIC: ot_mqtt.h carries their types and ot_mqtt_handle() is the
# meeting of MQTT with ot_command_check(). ot_api renders a value in the one spelling /api/state
# and /ws use. NOT ot_ha: HA's birth topic is ot_mqtt_decide()'s argument, so the two pure halves
# build and test apart.
#
# NOT listed, and it must never be: mqtt, freertos, esp_timer, esp_log -- a suite includes
# ot_mqtt.h, and PlatformIO compiles every source of a library whose header a suite includes.
idf_component_register(SRCS "ot_mqtt_in.c" "ot_mqtt_out.c" "ot_mqtt_handle.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_command ot_control ot_registry
                       PRIV_REQUIRES ot_api)
