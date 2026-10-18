# Hardware verification

**A green host build is not readiness.** Everything a build cannot prove — that the firmware
talks to a real boiler, asks it for heat safely, survives a broker and a browser and Home
Assistant — is proven only by walking it on a physical device. These are the records of that.

## What lives here

- **`scenarios/`** — the test scenarios the owner walks on real hardware, grouped by what the
  device *does*, not by any phase of how it was built. Each is self-contained; start with
  [`scenarios/00-setup-and-safety.md`](scenarios/00-setup-and-safety.md), which holds the curl
  harness, the log-over-USB rule, the second-board differences, and the one invariant every
  scenario leans on — **the bus must never fall silent** (silence > 5 s is read by the boiler
  as a demand for heat, so a dead peer must never stop the conversation).

They are **checklists to walk**, not a journal of past runs. Every checkbox reads *not yet
walked* (`[ ]`); tick them on your own copy as you go. Facts that are properties of a specific
boiler (which Data-IDs it supports, its low-flow floor) are recorded as **Notes**, not as
checkboxes, so a different appliance does not read them as the contract.

For what the firmware *does* rather than how to check it, see `docs/firmware-design.md`,
`docs/implementation-reference.md`, and the per-component `README.md`s.

## The scenarios

| File | The end-to-end path it walks |
| --- | --- |
| [`00-setup-and-safety.md`](scenarios/00-setup-and-safety.md) | **Read first.** Curl harness, the never-silent bus invariant, reading the log over USB, the reboot window, the ESP32-C6 second board, what this boiler cannot tell you |
| [`01-first-bring-up.md`](scenarios/01-first-bring-up.md) | Flash → boot → console banner → provisioning AP → joining WiFi → the web UI loads → the API answers with auth and secret sentinels → it survives a reboot |
| [`02-boiler-conversation.md`](scenarios/02-boiler-conversation.md) | Input polarity → the first frames decode → the registry and state fill with real boiler answers → DATA-INVALID is not taken for data → unsupported IDs are pruned → bounds come from the boiler → the Boiler page invents nothing |
| [`03-writes-and-refusals.md`](scenarios/03-writes-and-refusals.md) | A write reaches the boiler and comes back changed → every refusal refuses the right thing (422/405/409) → rights on a device with no password → the line test halts the bus for 20 s and nothing else does |
| [`04-live-web-and-ui.md`](scenarios/04-live-web-and-ui.md) | The live `/ws` channel (green badge, updates without reload, many tabs, suspend/resume, the one-shot ticket) → the log page → "no answer from the device" → the web has no privileged handle |
| [`05-asking-for-heat.md`](scenarios/05-asking-for-heat.md) | The CH bit raised for the first time → it never rises before the held ID 1 has gone out → season and enable gating → the boiler's own floor → the boost ladder → Home-Assistant-mode ownership refusals |
| [`06-home-assistant.md`](scenarios/06-home-assistant.md) | The broker and its status block → a broker that fails costs nothing else → discovery of every entity with no YAML → controls unavailable in local mode → commands and the watchdog → Home Assistant may write no OpenTherm frame |
| [`07-watchdog-and-failsafe.md`](scenarios/07-watchdog-and-failsafe.md) | The watchdog trips to the bounded failsafe → `fs_disarmed` vs `fs_blind` vs room-steered → an ambient source never steers → a retained command does not feed the watchdog → the failsafe banner on the web |
| [`08-room-sources.md`](scenarios/08-room-sources.md) | The registry of room-temperature sources → the MQTT source configured and persisted → a retained reading refused → the room/ambient role → display vs steer selection → the shield DS18B20 → web ↔ API parity |
| [`09-language.md`](scenarios/09-language.md) | The language switcher and its persistence → every page in every language → best-effort translation of log lines and errors → entity names translated in the web but English in Home Assistant → layout under longer words |
