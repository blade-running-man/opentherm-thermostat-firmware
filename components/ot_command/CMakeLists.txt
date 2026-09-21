# ot_control is a PUBLIC requirement: ot_command.h includes ot_control.h for ot_origin_t,
# ot_control_cfg_t and ot_control_cmd_t, so every component that includes ot_command.h
# (ot_http and ot_mqtt) needs its include path. It is pure -- no ESP-IDF header -- so
# the host suites that link ot_command stay framework-free (the rake in ot_command.h).
idf_component_register(SRCS "ot_command.c" "ot_command_check.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_registry ot_state ot_frame ot_control)
