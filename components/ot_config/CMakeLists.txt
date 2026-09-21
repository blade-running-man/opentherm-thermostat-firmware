# Two halves, and the split between them is the design. The pure half -- ot_config.c and the
# files cut from it (_check, _strerror, _defaults, _record, _apply) -- has no ESP-IDF,
# no flash, no clock, and test_config_* covers it on the host. The ot_config_nvs*.c files are
# the thin layer that opens namespaces and moves bytes; each compiles to nothing off the device (it
# is wrapped in #ifdef ESP_PLATFORM) because the host test build compiles every .c in this
# directory: this component is a PlatformIO library as well as an IDF component (platformio.ini,
# lib_extra_dirs). DO NOT add a device-half file without that guard -- every host suite that
# includes ot_config.h stops compiling.
#
# ot_config_internal.h and ot_config_nvs_internal.h sit beside the sources and not in include/,
# so no other component can include them. Nothing is added to INCLUDE_DIRS for them: a quoted
# #include finds a header in the including file's own directory first.
#
# REQUIRES, not PRIV_REQUIRES: ot_config.h includes ot_secrets.h and
# ot_config_nvs.h includes esp_err.h, so anything including either needs those directories
# too.
#
# esp_hw_support is listed although it is currently in the IDF's common requirements
# (tools/cmake/build.cmake:282, where the comment says it is there "for backward compatibility").
# esp_random.h lives in it, and a component that names what it uses does not break on the release
# that finally removes it from that list.
idf_component_register(
    SRCS "ot_config.c" "ot_config_check.c" "ot_config_strerror.c" "ot_config_defaults.c"
         "ot_config_record.c" "ot_config_apply.c"
         "ot_config_nvs_load.c" "ot_config_nvs_save.c" "ot_config_nvs_io.c"
         "ot_config_nvs_recover.c" "ot_config_nvs_kdf.c"
    INCLUDE_DIRS "include"
    REQUIRES ot_secrets nvs_flash mbedtls esp_hw_support
)
