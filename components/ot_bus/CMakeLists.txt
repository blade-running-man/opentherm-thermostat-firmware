# The task that owns the bus. The pure scheduler lives in a separate component
# ot_bus_sched -- why exactly that way is written in its CMakeLists.txt.
idf_component_register(
    SRCS "ot_bus.c"
    INCLUDE_DIRS "include"
    # ot_state -- for the sake of ot_state_is_unsupported(): a flagged identifier drops
    # out of the polling ring. That direction only: ot_state knows nothing about the bus
    # and must not, otherwise there would be a cycle ot_bus -> ot_state -> ot_bus.
    # ot_bus_track -- the bookkeeping of the writes (the ID 1 count, the write generation):
    # pure, so that a host suite reaches it; this component is FreeRTOS and has none.
    REQUIRES ot_bus_sched ot_bus_track ot_master ot_frame ot_observe ot_state esp_timer
)
