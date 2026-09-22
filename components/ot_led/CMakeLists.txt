# Pure: the health ladder and the animation curves are pure functions of a sampled world and a
# monotonic time (ot_led_world_t.now_ms). No ESP-IDF header, no timer, no task, no pin -- that is
# what lets it be host-tested. DO NOT add a REQUIRES that reaches a clock/driver: PlatformIO
# compiles ALL of a library's sources for a suite that includes its header, and test_ot_led would
# stop linking -- the rake already recorded for ot_sensor/ot_room.
#
# REQUIRES ot_control ONLY -- not ot_net, on purpose. ot_led_world_t.net is ot_led_net_state_t
# (this component's own copy of ot_net_state_t's three values, see ot_led.h), not ot_net_state_t
# itself: ot_net.h drags "esp_err.h" and, transitively, all of ot_net's impure sources into any
# host suite that reaches it (verified by running the host build, not by reasoning -- see the
# header). ot_control.h has no such problem: it is genuinely pure, already reached by
# test_ot_command's suite today.
idf_component_register(SRCS "ot_led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_control)
