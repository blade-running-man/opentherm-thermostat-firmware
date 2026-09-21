# Its own component, and for the same reason ot_policy is: this is the one part of
# provisioning that can be decided without a radio, and keeping it out of the component that
# owns esp_wifi is what lets every case in test/test_provision be a line rather than a
# fifteen-minute wait in front of a device.
idf_component_register(
    SRCS "ot_provision.c"
    INCLUDE_DIRS "include"
)
