# The impure half of the executor. The decision is ot_control's and the
# translations are ot_control_io's, both pure and host-tested; this component is the task, the
# spinlock, the bus, NVS and RTC_NOINIT memory, and it stays thin because they are thick.
#
# PUBLIC (the header's signatures carry their types): ot_control (origin, command, snapshot,
# verdict), ot_config (ot_config_err_t), ot_api (ot_api_control_t, the document it fills),
# ot_frame (ot_msg_type_t, the reply ot_thermostat_heard() records).
#
# PRIVATE, and each named for what it is used for:
#   ot_control_io  every mapping the task carries out, the ID 56 readback included
#   ot_bus         ot_bus_set_status(), ot_bus_write_if_idle(), ot_bus_write_state()
#   ot_bus_sched   the OT_STATUS_* bit names of the log line -- pure, named because it is used
#   ot_net         ot_net_config_snapshot() and ot_net_config_apply(): the configuration's home
#   ot_state       ot_state_set_virtual() and ID 1's UNKNOWN-DATAID mark -- NOT the ID 56
#                  readback: ot_state keeps a WRITE-ACK like a READ-ACK (ot_control_io.h)
#   ot_time        ot_time_set_zone(), so a saved zone applies without a power cycle
#   ot_room        the pure room-source registry: owned by this task, drained from the
#                  mailbox once a tick (ot_thermostat_room.c, ot_thermostat_room_submit)
#   esp_timer      the monotonic clock; NOT one of the IDF's common requirements
#   nvs_flash      heat hours, in their own namespace (ot_thermostat_persist.c)
#   esp_system     esp_reset_reason(); a common requirement today, named for the release that
#                  stops pulling it in for free
#
# NOT ot_command: the executor's own writes are encoded by ot_control_io_dc_f88(), because
# ot_command_encode() refuses an ID the state model marked unsupported and never clears the mark
# (ot_control_io.h). ot_command stays the gate for HAND writes, in ot_http.
#
# ot_thermostat_room.c inits ot_room from ot_config_room_mqtt() -- DECLARED in ot_config.h but
# DEFINED in ot_net_config.c beside the live document it reads (ot_config.h says why) -- so linking
# it needs no new REQUIRES: ot_net is already PRIV_REQUIRES'd below and carries that symbol.
#
# ot_thermostat_room_cfg.c is the config->ot_room mapping, kept PURE and in its OWN
# translation unit (ot_thermostat_room_cfg.h says why) so it host-builds and host-tests
# (test/test_ot_thermostat_room/) without the rest of this component's FreeRTOS/bus/NVS sources.
# library.json's srcFilter narrows env:native's build of this directory to this one file; it has
# no effect here -- idf_component_register's SRCS below is what the board actually builds from.
idf_component_register(SRCS "ot_thermostat.c" "ot_thermostat_persist.c" "ot_thermostat_report.c"
                             "ot_thermostat_room.c" "ot_thermostat_room_cfg.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_control ot_config ot_api ot_frame
                       PRIV_REQUIRES ot_control_io ot_bus ot_bus_sched ot_net ot_state
                                     ot_time ot_room esp_timer nvs_flash esp_system)
