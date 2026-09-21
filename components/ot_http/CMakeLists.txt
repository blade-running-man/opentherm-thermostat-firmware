# Eleven source files, one component: the split is by ADDRESSEE, not by size. ot_http.c holds the
# route table and the server lifecycle, ot_http_gate.c the one access gate every request passes
# (the Host-header allowlist, the Content-Type gate, the credential check and the policy verdict --
# split off ot_http.c at the 350-line ceiling when the Host check was added),
# ot_http_config.c the six routes that work on an UNCONFIGURED
# device behind an open access point, ot_http_static.c the SPA and the captive-portal probes,
# ot_http_boiler.c the raw bus observation, ot_http_registry.c the registry, the state reads and
# the entity write, ot_http_ops.c the operation table behind POST /api/ops/<name>, ot_http_ws.c
# the /ws and ticket routes, ot_http_control.c the executor's live document and the HTTP answer
# to each of its verdicts.
# ot_http_ws_push.c is the second exception to "by addressee": it is ot_http_ws.c's own overflow,
# split at the 350-line ceiling along the seam that was already there -- routes and lifecycle on
# one side, the socket list and what goes into it on the other.
# ot_http_body.c is the exception to "by addressee": it holds the two helpers EVERY writing route
# uses, and it is a file of its own because ot_http.c is four lines short of the 350-line ceiling
# without them -- see its own header comment.
# ot_http_internal.h is the seam between them and is deliberately NOT in include/.
#
# ot_api is here for the registry routes; it pulls ot_state and ot_registry behind it -- but
# ot_http_ws.c includes both headers directly, and a component that names what it uses does not
# break when somebody trims ot_api's own requirements.
#
# esp_hw_support carries esp_random.h (the ticket's 128 bits). It is currently in the IDF's common
# requirements "for backward compatibility" (tools/cmake/build.cmake) -- named here for the release
# that finally drops it, exactly as ot_config does.
#
# ot_command arrived with POST /api/entities/<key>: the gluing of "parsed value -> write frame"
# happens in ot_http_registry.c, because ot_wire and ot_command deliberately know nothing about
# each other (ot_command.h explains why -- it was decided by linking, not by taste).
#
# lwip arrived with ot_http_ws_push.c: the non-blocking send override calls lwIP's send() with
# MSG_DONTWAIT directly. It comes in transitively today -- named anyway, exactly as esp_hw_support
# above. ot_lock left with the executor: the push frame is rendered by ot_api_render_frame(), which takes
# the lock itself as every ot_api renderer does, and no file here takes it any more.
#
# ot_thermostat, ot_control and ot_control_io arrived with the executor: the entity
# route and the boost hand their commands to the task, GET /api/control reads it, the verdicts are
# ot_control's, and the boost's two numbers are parsed by ot_control_io (pure, host-tested). There
# is no cycle: ot_thermostat requires neither ot_http nor anything that does.
#
# ot_mqtt_link is here for GET /api/status's mqtt block and nothing else: a saved broker
# setting reaches the client without a call from here (ot_http_config.c says why). No cycle: it
# requires ot_wire, never ot_http.
idf_component_register(
    SRCS "ot_http.c" "ot_http_gate.c" "ot_http_body.c" "ot_http_config.c" "ot_http_static.c"
         "ot_http_boiler.c" "ot_http_registry.c" "ot_http_ops.c" "ot_http_ws.c" "ot_http_ws_push.c"
         "ot_http_control.c"
    INCLUDE_DIRS "include"
    REQUIRES ot_wire esp_http_server esp_app_format ot_bus ot_observe esp_timer ot_policy ot_log ot_auth ot_captive
             ot_config ot_net web_assets ot_api ot_command ot_ticket ot_state ot_registry esp_hw_support
             lwip ot_thermostat ot_control ot_control_io ot_mqtt_link
)
