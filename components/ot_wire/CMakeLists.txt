# Pure, and the whole reason it is its own component rather than three files inside
# ot_http: what a body MEANS can be decided without a socket, and a decision that needs a
# socket can only be exercised by flashing a board. The bodies this reads are written by whoever
# is in radio range of an OPEN access point, so "only testable on hardware" was not an
# option. Same argument, and same shape, as components/ot_policy.
#
# REQUIRES, not PRIV_REQUIRES, for all four: ot_wire.h exposes ot_wifi_t,
# ot_config_patch_t, ot_config_err_t and the ot_prov_* enums in its own
# interface, and OT_SECRET_SENTINEL travels with them.
#
# NOT listed, deliberately: esp_http_server, ot_http, ot_net. This component turns
# bytes into a decision and a decision into bytes; the moment it can send a response it can answer
# a request, and then the access policy in ot_policy has a second door. Same rule, and same
# wording, as components/ot_captive and components/ot_auth.
#
# ot_wire_command.c -- the same rules and the same public header, a separate file only
# because ot_wire.c has long been over the 350-line ceiling and the seam runs along
# responsibility: the command layer separate from configuration and provisioning.
#
# ot_command is NOT in REQUIRES, and that is measured, not chosen by taste. While it was
# there, the test_wire_command suite -- a test of TEXT parsing -- linked ot_state,
# ot_registry and the whole boiler model, and failed on an unresolved ot_lock. That is why
# the parsed value and the parsed parameters are declared here, in ot_wire.h: request
# bodies are what parsing produces, not what encoding consumes. DO NOT bring the
# dependency back "so that the command layer is in one place": splitting the types across
# files of the SAME component would not have helped -- PlatformIO compiles all of a
# library's sources.
# ot_wire_config.c and ot_wire_provision.c -- cut out of ot_wire.c along the same kind
# of seam, one document per file. ot_wire_writer.h sits beside them and not in include/, so no
# other component can include it; nothing is added to INCLUDE_DIRS for it, because a quoted
# #include finds a header in the including file's own directory first.
idf_component_register(
    SRCS "ot_wire.c" "ot_wire_config.c" "ot_wire_provision.c" "ot_wire_command.c"
    INCLUDE_DIRS "include"
    REQUIRES ot_json ot_config ot_provision ot_secrets
)
