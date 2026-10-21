---
layout: home

hero:
  name: OpenTherm Thermostat Firmware
  tagline: Own ESP-IDF firmware that makes an ESP32 the sole OpenTherm master of a gas boiler — Home Assistant decides, the device executes and guards
  actions:
    - theme: brand
      text: Guide
      link: /guide/introduction
    - theme: alt
      text: Architecture
      link: /architecture/overview
    - theme: alt
      text: Development
      link: /development/building

features:
  - title: Guide
    details: One binary per chip, configured identically at runtime. Wi-Fi is joined through the device's own setup access point; the MQTT broker, the controller mode and the room sources are set in the web UI. Nothing is set at compile time.
    link: /guide/introduction
    linkText: Get started
  - title: Architecture
    details: An executor, not a controller. Home Assistant holds the control law and the schedule; the firmware holds the commanded state, enforces the bounds, runs a watchdog and a bounded failsafe, and never reboots because a peer is gone.
    link: /architecture/overview
    linkText: Read the architecture
  - title: Protocol
    details: OpenTherm 2.2 as the only master on the bus — the 32-bit frame and its codecs, the Manchester decoder, and a conversation that never falls silent, because a boiler that hears no master demands heat.
    link: /protocol/frame
    linkText: Read the protocol
  - title: Interfaces
    details: MQTT with Home Assistant discovery, a REST API and a WebSocket push channel, all over one entity list generated from a single table. The web UI uses only the public API.
    link: /interfaces/rest-api
    linkText: Read the interfaces
  - title: Device
    details: The two boards and the OpenTherm shield, the status LED, networking and time, the room-temperature sources (MQTT and the shield's DS18B20), what lives in NVS, and the printed enclosure.
    link: /device/board
    linkText: Read about the device
  - title: Boiler
    details: The OpenTherm Data-IDs the firmware reads and writes, with three-valued availability discovered at runtime, and the reference boiler it runs on — an Intergas Kombi Kompakt HRE.
    link: /boiler/
    linkText: Read about the boiler
---
