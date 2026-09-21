# The bus's bookkeeping of its writes (include/ot_bus_track.h). Pure, and a component of its own
# for the reason ot_bus_sched is one (see its CMakeLists.txt): ot_bus.c is FreeRTOS, and a host
# suite that included a header of ot_bus would compile it. DO NOT fold this into ot_bus.
#
# ot_bus_sched is PUBLIC: the header's signatures carry its structs.
idf_component_register(SRCS "ot_bus_track.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_bus_sched)
