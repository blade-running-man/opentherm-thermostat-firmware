# Two sources, and the split between them is the design. ot_auth.c is pure -- no ESP-IDF,
# no clock, no crypto -- and test/test_auth covers it on the host, which is the only way the
# cases it exists for can be written at all: an eight-kilobyte header, a NUL inside a password,
# a colon in three places. ot_auth_device.c binds the verifier to mbedtls and compiles to
# nothing off the device (it is wrapped in #ifdef ESP_PLATFORM) because the host test build
# compiles every .c in this directory: this component is a PlatformIO library as well as an IDF
# component (platformio.ini, lib_extra_dirs). Same arrangement, and same reason, as
# components/ot_config.
#
# REQUIRES ot_config, and it is the whole point rather than a convenience. The stored form
# of the web UI password is ot_config's record and there must not be a second one:
# ot_config_sanitize() erases a ui_pw_hash its own parser cannot read, so a password
# written in another shape would vanish at the next boot. This component parses a header and
# decides an outcome; what a stored password looks like is decided over there, once.
#
# REQUIRES, not PRIV_REQUIRES: ot_auth.c includes ot_config.h for the _Static_assert
# that ties the two password-length limits together, so the include directory has to travel.
#
# NOT listed, deliberately: ot_http. This component must never see esp_http_server. It is
# handed a header value and returns an answer; the moment it can send a response it can send a
# 401 of its own, and then the access policy in ot_policy has a second door. Same rule,
# and same wording, as components/ot_captive.
#
# mbedtls is not listed either, and does not need to be: ot_auth_device.c reaches it only
# through ot_config_device_kdf(), which is on the far side of that component's own
# interface. A REQUIRES line here would be this component claiming a dependency it does not have
# and would make "ot_auth hashes something" look true.
idf_component_register(
    SRCS "ot_auth.c" "ot_auth_device.c"
    INCLUDE_DIRS "include"
    REQUIRES ot_config
)
