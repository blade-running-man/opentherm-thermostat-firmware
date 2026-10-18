# 01 — First bring-up

This scenario takes a bare board from flash to a device on the owner's network serving its web
interface and answering its API — with the boiler **not yet connected**. It proves the board
boots, talks to the console, brings up its access point, joins WiFi, persists its settings, and
keeps its secrets. Nothing here asserts anything about the boiler; that is
[`02-boiler-conversation.md`](02-boiler-conversation.md).

Have the curl harness from [`00-setup-and-safety.md`](00-setup-and-safety.md) ready. This walk is
board-independent except where noted; for the ESP32-C6 SuperMini's flashing and wiring
differences see [00-setup §5](00-setup-and-safety.md).

## 1. Before power is applied

- [ ] **The boiler is NOT connected to the terminals.** This is the only hard requirement.
      GPIO8 is the OpenTherm input (header D2) and at the same time the strapping pin for entering
      flash mode; a transmitting boiler is the one thing that can block flash-mode entry. The
      shield alone is passive and does not pull the line down.
- [ ] With a meter, header **D0 (GPIO2) is not pulled to ground** — it must be HIGH at reset or
      the board will not start. Confirm the shield does not use D0 before fitting it.
- [ ] Write down the board's full station MAC (you need it for the MQTT topic prefix later). The
      **AP SSID, the device name and the topic prefix all carry the same four hex characters**
      taken from the station MAC — e.g. base `f0:f5:bd:2c:c3:8c` → network `opentherm-c38c`.

## 2. Flashing

- [ ] `~/.platformio/penv/bin/pio run -e lolin_c3_mini -t upload` — the board is detected
      (VID:PID `303a:1001`) and the upload runs to the end with no verification errors.

> If the board is not detected, hold BOOT (GPIO9), tap RESET, release BOOT, and retry.

## 3. The console

- [ ] `~/.platformio/penv/bin/pio device monitor` produces output at all. The C3 mini has no
      UART bridge, so a silent console with a live AP is a console-config defect, not a boot
      defect — check §4 first.
- [ ] The banner line `opentherm-thermostat <version>` appears.
- [ ] The descriptor line `board lolin_c3_mini: OpenTherm in=8 out=10, button=9 …` appears. Wrong
      numbers mean the wrong board descriptor was built.
- [ ] `web UI compiled in: <N> B` appears, N around 20 000.
- [ ] The line about serving its own open access point appears at WARN (the device counts as
      unclaimed).
- [ ] **No panics, no restarts.**

## 4. The access point and the web interface

- [ ] The open network `opentherm-XXXX` is visible from a phone — take the exact name from the
      console's `provisioning: … access point 'opentherm-XXXX'` line, do not compute it.
- [ ] The phone offers captive-portal sign-in by itself; if not, open `http://192.168.4.1`.
- [ ] The page loads with the header "OpenTherm Thermostat" and a working navigation.

## 5. Entering network settings

- [ ] The scan button lists surrounding networks.
- [ ] After entering SSID + password, the device joins the owner's network — the console reports
      it is on the owner's network — and it is reachable at the router-assigned address.
- [ ] `opentherm.local` does **not** resolve (no mDNS, deliberate).
- [ ] Given a knowingly wrong password, the device rolls back and **brings the AP back up** rather
      than staying unreachable.

## 6. Surviving a reboot

- [ ] After `RESET`, the device returns to the owner's network by itself, and `/api/config`
      returns the same settings as before.
- [ ] The same holds after a full power cut of a minute or so.

## 7. The API answers, with auth and secrets

- [ ] `otget http://$OT/api/status`, `/api/config` and `/api/log` each answer JSON.
- [ ] The auth rule holds: without `-u` the read is `401` when a password is set, `200` when none
      is — see [00-setup §1](00-setup-and-safety.md).
- [ ] **No password in the clear.** `GET /api/config` returns the sentinel (`__UNCHANGED__` for
      the interface password) — grep the raw response, not the form. A real password appearing
      here is a defect.

## 8. Defaults on a first, unclaimed boot

`GET /api/config` on a freshly provisioned device shows the safe defaults, so the device asks the
boiler for **no heat** until both the heating season and the CH switch are turned on:

- [ ] `control_mode` `0` (local control).
- [ ] `heating_season` `false`.
- [ ] `local_ch_enable` `false` (the `ch_enable` entity writes this in local mode).
- [ ] `ch_setpoint` (`local_ch_setpoint_dc`) `45.0`, inside the flow band
      `flow_min_dc`..`flow_max_dc` = `40.0`..`70.0`.
- [ ] `dhw_setpoint` unset — no ID 56 write goes out until one is written (the poll ring still
      reads it).

## 9. Flash mode with the boiler running (C3 only)

- [ ] After everything above passes, power down, connect the boiler with two wires (polarity does
      not matter for this test), power up — the board still starts, with no conversation expected
      yet (there is a master, but this scenario does not check the wire; that is
      [`02-boiler-conversation.md`](02-boiler-conversation.md)).
- [ ] **Enter flash mode ten times in a row with the boiler running.** If it fails even once,
      record it — a second descriptor with the OT input jumpered to D5 (GPIO1) would be added in
      `components/board/`.

> This item **does not apply** on the ESP32-C6, whose OT input is on GPIO18 and never reaches the
> strapping pins — see [00-setup §5](00-setup-and-safety.md).

## 10. What the device reports about itself

- [ ] `/api/control` carries the `ot_thermostat` task's `stack_hwm` (bytes; `null` until
      measured). After a few minutes of activity, above ~512 of 4096 is comfortable.
- [ ] Change the time zone (`tz`) and save: within a second the log reports the new zone, with
      **no power cycle** required. (The `I (…)` stamps are ms since boot and do not move.)
- [ ] Change the NTP server and save: the log does **not** restart SNTP immediately; after a
      reboot it names the new server. By design.
- [ ] `/api/state` spells values the way the pages and `/ws` do — booleans as `true`/`false`,
      modes as strings, never `1.00`/`4.00`.

## Notes

- The ESP32-C6 SuperMini is the second target and its flashing, wiring and antenna differences
  live in [00-setup §5](00-setup-and-safety.md). Its WiFi range is noticeably shorter than the
  C3's.
- **Known limitation:** when a stored network cannot be joined, the firmware surfaces no
  diagnosis — no disconnect reason, no SSID, nothing on the board — and the provisioning portal
  only returns after a long timeout. A device that silently fails to join is most likely this,
  not a flashing fault.
- The reset reason logged at boot and the watchdog-overdue carry-over are exercised in
  [`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md).
