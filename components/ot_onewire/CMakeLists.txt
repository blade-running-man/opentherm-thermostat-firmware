# The RMT-backed DS18B20 read. NOT host-built: it reaches the driver and the managed
# onewire_bus, so it is compiled only for the board. The pure CRC/temperature decode is the
# sibling ot_onewire_decode component, which IS host-built.
#
# onewire_bus is a managed dependency (idf_component.yml); the component manager registers it
# under the name espressif__onewire_bus, which is what REQUIRES must name.
idf_component_register(SRCS "ot_onewire.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver ot_onewire_decode espressif__onewire_bus)
