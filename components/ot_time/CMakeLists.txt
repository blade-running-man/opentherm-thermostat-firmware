# esp_netif carries esp_netif_sntp.h; lwip carries the SNTP implementation behind it. Both are
# named although the build would find them by accident: a component that names what it uses does
# not break on the release that stops pulling it in for free.
#
# ot_config is NOT required. This component takes the zone and the server as arguments and knows
# nothing about where they were stored -- which is what lets ot_time_start() be called from a
# test harness, or from a provisioning path, without a configuration document existing.
idf_component_register(SRCS "ot_time.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_netif lwip)
