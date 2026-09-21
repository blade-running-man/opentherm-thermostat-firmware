idf_component_register(
    SRCS "ot_master.c"
    INCLUDE_DIRS "include"
    REQUIRES board ot_frame ot_decode ot_encode esp_driver_gptimer esp_driver_gpio
)
