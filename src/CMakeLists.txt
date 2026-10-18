# The project's "main" component. PlatformIO's ESP-IDF builder ignores build_src_filter
# (it selects sources here instead), so main.cpp is registered directly.
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES board web_assets ot_net ot_http ot_log nvs_flash esp_app_format
             ot_master ot_bus ot_frame ot_mqtt_link ot_onewire ot_state
)
