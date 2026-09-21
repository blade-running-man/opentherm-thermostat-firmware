# 06 — Home Assistant over MQTT

This scenario proves the MQTT path end to end: the broker is configured and reported, a broker
that fails costs the device nothing else, Home Assistant discovers every entity with no YAML, the
controls are unavailable while the device is in local control, commands feed the watchdog and
garbage does not, and — a hard rule — **Home Assistant may write no OpenTherm frame.**

You need: the device on the network; a broker (Mosquitto) you can stop and start; Home Assistant
with the MQTT integration pointed at the same broker; `mosquitto_sub`/`mosquitto_pub`. Below,
`<broker>` is the broker address, `<prefix>` the device's topic prefix
(`opentherm/<twelve hex of the MAC>` unless changed) and `<id>` those twelve hex digits. Keep one
terminal on the traffic for the whole walk:

```sh
mosquitto_sub -h <broker> -v -t '<prefix>/#' -t 'homeassistant/+/<id>/#'
```

Have the curl harness and the bus monitor from
[`00-setup-and-safety.md`](00-setup-and-safety.md) running — the bus must keep turning through
every broker failure below.

> No host suite compiles the MQTT client task or its glue; what they do on a network is proven
> only here.

## 1. Setting the broker, and the status block

- [ ] Before any broker, `GET /api/status`'s `mqtt` member reads `"configured":false`,
      `"connected":false`, every counter 0, and the log carries `mqtt: no broker configured; MQTT
      stays off`.
- [ ] `POST /api/config {"mqtt_host":"<broker>","mqtt_user":"<user>","mqtt_password":"<pw>"}` →
      `200` at once. Within ~2 s the log reads `mqtt: broker …, prefix <prefix>, discovery on` then
      `mqtt: broker connected`, with **no user or password in any log line**; `/api/status` reads
      `"configured":true,"connected":true,"reconnects":1`.
- [ ] `mosquitto_sub` shows `<prefix>/status online` (retained), `<prefix>/control/owner offline`
      (local, retained), `<prefix>/control_state/attributes` with `reason`/`cause`, and one
      `<prefix>/<key>/state` per entity — spelled as `GET /api/state` spells them. Never an empty
      payload.
- [ ] `GET /api/config` returns the sentinel for `mqtt_password`; grep the whole capture for the
      password — it is absent.

## 2. Home Assistant mode needs a broker

- [ ] `POST /api/config {"control_mode":1}` with no `mqtt_host` set → `422`, field
      `mode-needs-broker`. A device cannot be handed to Home Assistant with nowhere to hear it.

## 3. A broker that fails costs nothing else

Nothing reboots because a peer is absent, and `ot_bus` never pauses. Watch ID 0's `age_ms`:

```sh
while sleep 2; do
  curl -s http://$OT/api/ot/raw \
    | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["uptime_ms"], d["cycles"], [e["age_ms"] for e in d["ids"] if e["id"]==0])'
done
```

- [ ] **Stop the broker for 10 minutes.** The log reads `mqtt: broker lost; retrying every 10 s,
      nothing else changes` once, then `mqtt: broker unreachable; N attempts so far …` at most
      hourly — and no `mqtt_client`/`esp-tls` internals. ID 0's `age_ms` stays under ~2000,
      `uptime_ms` grows (no reboot), the boiler keeps doing what `/api/control` says, and the web
      answers throughout.
- [ ] **Start it:** within ~10 s `mqtt: broker connected`, `reconnects` +1, and `mosquitto_sub`
      sees `status online`, every state and every discovery document again.
- [ ] **Flap it** (stop 20 s / start 20 s for 5 minutes): no reboot, ID 0 `age_ms` under ~2000,
      `reconnects` counts the flaps, one "lost"/one "connected" per flap.
- [ ] **Wrong password:** `/api/status` shows `"last_connack":5` (or 4) with a reason, the log says
      the broker refused the connection once then hourly, no reboot; the right password back →
      `connected`, `last_connack` 0.
- [ ] **A black-hole host** keeps the web responsive; `POST /api/config {"mqtt_host":""}` answers
      at once and within ~15 s (one connect timeout) the log reads `no broker configured; MQTT
      stays off`. Put `mqtt_host` back before §4.
- [ ] **WiFi off 10 minutes:** on return MQTT reconnects by itself, no reboot.
- [ ] **The will, and the clean offline.** With the broker up, pull the device's power at the wall.
      Within ~90 s (60 s keepalive + grace) `mosquitto_sub` prints `<prefix>/status offline`
      **retained** (the broker published the will) and Home Assistant shows every entity
      unavailable. Power back on → `status online`, entities back.

## 4. Discovery — Home Assistant finds every entity, no YAML

- [ ] In HA → Settings → Devices there is one device named `device_name`, model
      `opentherm-thermostat`, with a firmware version, a **Visit** link to `http://<device>/`, and
      the MAC under connections — no YAML anywhere.
- [ ] Every boiler reading appears (sensors / binary sensors), plus `control_mode` and
      `control_state` (enum sensors), `ch_setpoint_effective`, `ch_enable_effective`,
      `failsafe_count`, `last_failsafe_duration_s`, a `heating_season` binary sensor, a
      `Room temperature` sensor and a `Room source` enum, and five controls: `ch_enable`/
      `dhw_enable` switches, `ch_setpoint`/`dhw_setpoint` numbers, and the season **off** button.
- [ ] The HA log (filter "mqtt") carries no "Invalid discovery" / "not a valid" line for this
      device.
- [ ] Unsupported IDs never appear: no `max_ch_setpoint` (ID 57), and after five minutes
      `mosquitto_sub -t 'homeassistant/+/<id>/max_ch_setpoint/config'` shows nothing retained.
- [ ] `ch_setpoint`'s range in HA equals `flow_min_dc`..`flow_max_dc` / 10; change `flow_max_dc`
      via `POST /api/config` and HA's max follows within ~2 s. `dhw_setpoint`'s range is the
      boiler's ID 48 (or 30..80).
- [ ] Restart Home Assistant: its birth message makes the device re-publish discovery, and
      `published` in `/api/status` jumps by ~the entity count within ~10 s.
- [ ] `POST /api/config {"ha_discovery":false}` clears every entity (empty retained configs);
      `true` brings them back under the same ids.
- [ ] Rename the device (`device_name`): the HA device name changes with no duplicate entities.
      Change `topic_prefix`: within ~2 s the old prefix's `status` reads `offline` and everything
      reappears under the new prefix. The old prefix's other retained topics are left behind
      (clear them with `mosquitto_sub --remove-retained`). Put the prefix back.

## 5. In local mode the controls are unavailable

- [ ] In local mode `<prefix>/control/owner` is `offline`; in HA the five controls are
      **unavailable** (not a stale writable value), while `heating_season`, `control_state`, the
      `*_effective` mirrors and every reading stay available.
- [ ] `mosquitto_pub -h <broker> -t '<prefix>/ch_enable/set' -m 1` → the log reads `mqtt: command
      refused: owned by the thermostat: control_mode is local …`, `rejected` +1, `/api/control`
      unchanged.
- [ ] `POST /api/config {"control_mode":1}` → within ~2 s the owner topic reads `online` and the
      controls become available — never before the executor `state` leaves `local`. Back to local →
      owner `offline` within ~2 s and the controls grey out.

## 6. Commands, and what feeds the watchdog

In HA mode, season on, `watchdog_s` 120. A refusal is logged at most once a minute — read
`rejected`, not the log-line count.

- [ ] HA `ch_enable` on → `state` `ha_waiting` → `ha`; the CH bit rises once the held ID 1 has gone
      out (the invariant, [`05-asking-for-heat.md`](05-asking-for-heat.md)); `commands` +1. The
      burner lights and the flow climbs — the whole heat path proven through Home Assistant.
- [ ] HA `ch_setpoint` to 55 → `held_setpoint_dc` 550 within ~1 s, `ch_setpoint_effective` 55
      within ~10 s.
- [ ] **A retained command does not feed the watchdog.** Stop every HA automation writing this
      device, then: `POST /api/config {"mqtt_port":1884}` (nothing listens; the subscription is
      gone); `mosquitto_pub -h <broker> -r -t '<prefix>/ch_enable/set' -m 1` (the broker only
      stores it); `POST /api/config {"mqtt_port":1883}` (reconnect, the broker replays with
      RETAIN=1). The log reads `mqtt: command refused: a retained command is ignored`; `rejected`
      +1, `commands` does not move, and `/api/control` reaches `failsafe` ~120 s after the last
      fresh command. Clean up: `mosquitto_pub -h <broker> -r -n -t '<prefix>/ch_enable/set'`.
- [ ] A fresh `ch_enable` every 60 s keeps `state` `ha` for 10 minutes. A fresh `dhw_enable` every
      30 s **alone** does not: `failsafe` after 120 s — DHW does not feed the watchdog.
- [ ] The season button in HA turns `heating_season` off (`state` `season_off`, CH down);
      `mosquitto_pub -t '<prefix>/heating_season/set' -m 1` → refused `only the thermostat may turn
      the heating season on`.
- [ ] `mosquitto_pub -t '<prefix>/ch_setpoint/set' -m hello` → refused `the payload is not a
      number` (the log line does not contain `hello`); `-m 80` (above `flow_max`) → refused `value
      out of range`; neither feeds the watchdog. 100 garbage messages in 10 s → `rejected` +100
      with at most one log line per minute.
- [ ] HA `dhw_setpoint`/`dhw_enable` work in HA mode. If the boiler answers `UNKNOWN-DATAID` to
      ID 56 twice, the `dhw_setpoint` entity disappears from HA until a reboot and every DHW write
      is refused — note it if it happens.

## 7. Home Assistant may write no OpenTherm frame

- [ ] `mosquitto_pub` to `max_ch_setpoint/set -m 60`, `room_setpoint -m 21.5`, `room_temperature
      -m 20`, `max_relative_modulation -m 50` — each refused `mqtt: command refused: this entity is
      not Home Assistant's to write`, `rejected` +4, and `/api/ot/raw` shows **no** write of
      ID 57/16/24/14 (their `count`/`age_ms` do not move). The refusal is made at the one command
      entry point, not on the HTTP surface.
- [ ] `POST /api/entities/max_ch_setpoint {"value":60}` over REST still works — the rule is about
      *who* is writing, not the entity.

## 8. What the owner reads

- [ ] After a day with the broker off (or §3 extended), `GET /api/log` holds at most ~24 `mqtt`
      lines beyond the transitions.
- [ ] A broker ACL that lets the device publish but denies it the command subscription →
      `mqtt: the broker refused the command subscription: check its ACL`, while states still
      publish.

## Notes

- The failsafe seen from Home Assistant — `fs_disarmed` vs `fs_blind`, the reboot-blind case, and
  the room-steered failsafe — is [`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md).
- The room-temperature source that Home Assistant can feed is
  [`08-room-sources.md`](08-room-sources.md).
