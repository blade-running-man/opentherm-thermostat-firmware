# ot_json is NOT required here, and that is not an omission. The only strings the
# renderer prints come from the generated registry, and tools/tests/test_registry.py
# forbids in them the quote, the backslash and control characters -- that is,
# everything escaping would change. See test_no_registry_string_needs_json_escaping.
# ot_api_control.c (GET /api/control) prints ot_control's own names for the state and the reason,
# so ot_control is required -- publicly, because ot_api_control.h carries its enum types. It is
# pure (no ESP-IDF header), so test_api links it without dragging a framework in.
idf_component_register(SRCS "ot_api.c" "ot_api_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ot_state ot_registry ot_lock ot_control)
