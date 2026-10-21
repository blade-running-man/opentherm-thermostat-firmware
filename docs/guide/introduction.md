<script setup>
import { data } from '../reference/entities.data'
// Entity counts on this page come from the generated registry (see /reference/entities).
</script>

# Introduction

This firmware turns a small ESP32 board and an OpenTherm adapter shield into the room thermostat of
a modulating gas boiler. It speaks OpenTherm 2.2 as the **only master on the bus**, and it offers
three ways to use it:

- **MQTT with Home Assistant discovery** — the main control path. Home Assistant finds the device
  through its built-in MQTT integration; nothing is installed in Home Assistant and no YAML is
  written for the entities.
- **An HTTP REST API** that covers everything the web interface does — the interface has no
  private endpoints of its own.
- **A web interface of its own**, served by the device: what the executor is doing and why, the
  boiler's live readings, every entity, the settings and the device log.

All three are views of one model. The entity list behind them is generated from a single table
(`tools/opentherm_ids.py`), so a value has the same key in the REST path, the MQTT topic and the
Home Assistant entity.

## The device is the executor, not the brain

The control law does not live on the device. **Home Assistant is the controller; the firmware
carries out what it asks and guards against nonsense and against the controller disappearing.**

| Who | Owns |
| --- | --- |
| **Home Assistant** | The control law, schedules, scenarios, your sensors — every "when to heat and how hot" decision. |
| **The firmware** | The bus (sole master, never silent), the commanded state, the value bounds, the watchdog, the bounded failsafe, and its own view of the room through a registry of room-temperature sources. |
| **The web interface** | Diagnostics and minimal control: the CH switch, the flow setpoint and a boost in local mode. Everything it does is an ordinary REST call. |

It is **not** an OpenTherm Gateway: it does not sit between an existing thermostat and the boiler
and does not relay anyone else's frames — the shield has no slave side. The device *is* the
thermostat, and the old one comes off the boiler's OpenTherm terminals.

### Why not a thermostat with its own control loop

A PI loop, a weekly schedule and anti-cycling on the device would duplicate what Home Assistant
already does with the owner's sensors and the owner's calendar, and each would be a second place
where "how hot" is decided. The firmware therefore has **no PI/PID loop, no anti-windup, no
schedule, no modes engine, no anti-cycling and no room-temperature control loop.** Cycling is left
to the boiler's own anti-cycling and to Home Assistant. The only decision the firmware makes about
heat on its own is the bounded failsafe — what to do once the controller has gone quiet.

## The two modes

The device runs in one of two control modes, switched at runtime in
[Settings](/guide/configuration#controller):

- **Local** — the default on a fresh install. The device is driven from its own web interface (or
  REST): a stored CH switch, a stored flow setpoint and an optional boost. Home Assistant's commands
  are refused, and its control entities show as unavailable.
- **Home Assistant** — Home Assistant is the controller. The firmware carries out its commands
  within the configured bounds, runs the watchdog, and enters the failsafe if Home Assistant falls
  silent. This mode is refused while no MQTT broker is configured.

How each mode behaves, state by state, is in [Executor](/architecture/executor).

## What it guarantees

Three rules sit above everything else ([Failure model](/architecture/failure-model)):

1. **The bus never goes silent.** A boiler that hears no master for more than about five seconds
   reads it as a short-circuited thermostat and **demands heat**. Falling silent is not a safe state
   but the hottest one, so no failure — Wi-Fi, the broker, a stale sensor, a dead Home Assistant —
   is allowed to stop the conversation with the boiler.
2. **Central heating is never requested before the held flow setpoint has gone out.** A boiler asked
   for heat with no setpoint heats to whatever it last had.
3. **Nothing reboots because a peer is absent.** A missing router, broker or controller is retried
   for as long as it lasts; the device keeps driving the boiler and serving its web interface.

## What it does

- Polls the boiler's OpenTherm Data-IDs and keeps a three-valued availability for each (`ok`,
  `invalid`, `unsupported`), discovered at runtime: IDs the boiler does not know are dropped from
  the poll after two answers that say so. **{{ data.total }} entities** in all, **{{ data.writable }}**
  of them writable — temperatures, modulation, pressure, flow, status and fault flags, and the
  executor's own inputs and outputs (full list: [Entities](/reference/entities)).
- Holds the commanded state — CH on/off, the flow setpoint, hot water on/off and its setpoint, the
  heating season — and refuses any CH setpoint outside the configured flow band, from anyone.
- Runs a **watchdog** on Home Assistant's CH commands and a **bounded failsafe** that either holds
  the heat off (when Home Assistant has not asked for heat for days — the summer case), heats at a
  fixed failsafe setpoint, or holds a room target from a fresh room sensor.
- Reads the room temperature from two kinds of source — a reading Home Assistant publishes
  over MQTT, and the DS18B20 on the shield — with an outlier filter and freshness tracking
  ([Room sources](/device/room-sources)).
- Sets itself up from a browser: its own open access point with a DNS server that resolves every
  name to the device, answers to the phone's connectivity checks so it stays on that network, the
  setup page at the device's address, a bounded setup window, and a rollback to the last working
  Wi-Fi if a new password was mistyped
  ([First setup](/guide/first-setup)).
- Shows its health on the board's RGB LED ([Troubleshooting](/guide/troubleshooting#the-status-led)).
- Keeps its secrets write-only: no API ever returns a stored password.

## What it does not do

- **No over-the-air update.** The flash has two OTA slots and the web interface has an update
  page, but the firmware has no handler that accepts an image; the device is updated by USB — see
  [Updating](/guide/updating).
- **No weather compensation, no second heating circuit, no solar or ventilation Data-IDs, no TLS
  for MQTT, no gateway mode.**
- **No mDNS.** The device is reached by the address your router gives it.
- **No button functions.** The board's BOOT button is the chip's download strap and nothing else;
  no button gesture clears Wi-Fi or the password
  ([Troubleshooting](/guide/troubleshooting#forgotten-password)).
- **No room sources beyond MQTT and the shield DS18B20** — no Wi-Fi push/pull sensor, no BLE, no
  general 1-Wire bus.

## Hardware

| Part | Role |
| --- | --- |
| **LOLIN C3 mini** (ESP32-C3, 4 MB flash) | The main board. The shield plugs straight onto its D1-mini header. |
| **ESP32-C6 SuperMini** (ESP32-C6, 4 MB flash) | The second board. The shield does not fit it; it is wired with four wires. |
| **DIYLESS OpenTherm master shield** | The OpenTherm front end: a two-wire, polarity-free connection to the boiler, and a spot for a DS18B20. |

There is **one binary per chip**, configured identically at runtime; the build picks the board
descriptor from the chip it targets. Details: [Board](/device/board),
[LOLIN C3 mini](/reference/esp32-c3-mini), [ESP32-C6 SuperMini](/reference/esp32-c6-supermini),
[DIYLESS OpenTherm master shield](/reference/diyless-opentherm-master-shield).

The reference boiler is an Intergas Kombi Kompakt HRE
([its OpenTherm profile](/boiler/intergas-kombi-kompakt-hre)). Another boiler will support a
different set of Data-IDs; the firmware discovers which at runtime.

::: warning A green build is not readiness
The protocol, the executor and the room-source filter are tested on the host, but what the device
does on a real boiler is proven only by walking the scenarios in
[Hardware verification](/hardware-verification/). Treat a first install as a bring-up: keep a USB
cable at hand and the boiler in sight.
:::

## Next

[Installation](/guide/installation) — build the firmware, flash it and connect the boiler.
