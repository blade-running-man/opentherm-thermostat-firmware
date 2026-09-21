# REQUIRES, not PRIV_REQUIRES, for the first three: ot_net.h exposes board_t,
# ot_wifi_t and ot_prov_state_t in its own interface, so anything including it needs
# those directories too.
#
# ot_captive is here because the DNS responder lives and dies with the access point, and the
# only code that knows when the access point is on the air is this one. Note which half is used:
# ot_captive_dns_server.h, the socket. The TABLE of probe paths is ot_http's business
# and is deliberately not touched here -- this component has no business answering HTTP.
#
# esp_timer is named although it is easy to get away without: it is NOT one of the IDF's common
# requirements (tools/cmake/build.cmake:282 lists exactly `cxx newlib freertos esp_hw_support heap
# log soc hal esp_rom esp_common esp_system`), and esp_timer_get_time() is the clock every deadline
# in ot_provision is measured against. A component that names what it uses does not break on
# the release that stops pulling it in by accident.
idf_component_register(
    SRCS "ot_net.c" "ot_net_config.c" "ot_net_prov.c" "ot_net_radio.c"
    INCLUDE_DIRS "include"
    REQUIRES board ot_config ot_provision ot_captive ot_wire
             esp_wifi esp_netif esp_event esp_timer nvs_flash
)
