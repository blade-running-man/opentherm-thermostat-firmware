# Impure glue: the ONE place WS2812/RMT and the source sampling live. All decisions belong to
# the pure ot_led; this file only samples the world, ticks the animation, and writes the strip.
# READER ONLY -- it must never write an OpenTherm frame, take the bus lock, or block on the net;
# the bus must never fall silent (a silent master reads to the boiler as a heat demand).
#
# ot_thermostat is required (beyond the plan's first sketch) for ot_thermostat_control_get() --
# the ONLY clean, task-safe, read-only getter of the current ot_control_state_t. There is no
# lighter getter in the task layer, and the LED must NOT recompute control or call
# ot_control_step()/apply(); it only observes. No cycle: ot_thermostat does not depend on this.
idf_component_register(SRCS "ot_led_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_led ot_net ot_mqtt_link ot_control ot_state ot_thermostat board driver led_strip)
