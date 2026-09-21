# 04 — The live web interface

This scenario proves the browser side: the pages render, the card mirrors the device, the live
`/ws` channel carries changes without a reload and degrades cleanly when it cannot, the settings
form sends only what changed, and — the standing rule — **the web has no privileged handle**,
so everything a page does can be done with curl.

Have the curl harness from [`00-setup-and-safety.md`](00-setup-and-safety.md) ready, a laptop
browser with devtools on the Network tab, and a phone on the same network. The controls
themselves (what pressing them does at the boiler) are [`05-asking-for-heat.md`](05-asking-for-heat.md);
this file is about the interface.

## 1. The pages

- [ ] `http://$OT/` opens the Control page (the default route, "Control" highlighted).
      `/control`, `/boiler`, `/state`, `/log`, `/settings`, `/update` each open their page. On a
      phone held upright the nav wraps with no link cut off.
- [ ] The tab title reads "OpenTherm Thermostat". **No Russian in any page's own words** — Russian
      remains only in entity display names (the State and Boiler name columns) and the default
      device name. Ages carry English units ("ms", "s", "N min N s"), never "мс"/"с".
- [ ] The nav badge reads "Connected" and the Control page's last line reads that the device's
      changes arrive as they happen.

## 2. The card mirrors the device

- [ ] Compare `otget http://$OT/api/control` with the card: the state heading matches `state`
      (`season_off` → "Heating season off", `local` → "Local control", `boost` → "Boost",
      `ha_waiting` → "Waiting for Home Assistant", `failsafe` → "Failsafe", `ha` → "Home Assistant
      in control"); "Flow setpoint held" = `held_setpoint_dc` ÷ 10; "CH asked of the boiler" =
      bit 0 of `status_high`; "Hot water asked" = bit 1; "Hot water setpoint" = `dhw.setpoint_dc`
      ÷ 10, or "not set: the boiler keeps its own" when `null`.
- [ ] "CH command" matches `otget http://$OT/api/state | jq .state.ch_enable.value`.
- [ ] **Live through `/ws`:** with the page open and not reloaded, a curl change
      (`… heating_season -d '{"value":0}'`) shows on the card within ~2 s; switching back with
      `{"value":1}` follows.
- [ ] **Without `/ws`:** in devtools block `*/ws*` and reload. The badge leaves "Connected" and
      the card's last line says the live connection is down and it now asks every 5 s. A curl
      change shows within ~5 s. Unblock and reload → "Connected" returns.

## 3. The live channel is robust

- [ ] The connection badge goes **green** within a couple of seconds of opening the page.
- [ ] A value changes **without a reload** (watch the flow temperature, or write a setpoint from
      another terminal).
- [ ] Leaving the tab for two minutes keeps the badge green (10 s of silence before a keepalive,
      35 s before the client gives up).
- [ ] Opening a second and a third tab keeps every badge green — the connection LRU must not evict
      the oldest live one.
- [ ] Lock the phone / suspend the laptop for a minute, then wake: the page reconnects and
      repaints. **During the freeze the rest of the device stays responsive** — from a second
      machine, `otcode http://$OT/api/status` answers `200` within a second, not five.

> A grey badge over an otherwise-working page is the interesting failure: the socket carries
> exactly `state`, `delta` and `ping`, and anything else is dropped.

## 4. The WebSocket ticket is one-shot

```sh
T=$(otget -X POST http://$OT/api/ws-ticket | jq -r .ticket); echo "$T"
curl -sS -i --max-time 3 -N -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' \
  "http://$OT/ws?ticket=$T"
```

- [ ] The first attempt → **`101 Switching Protocols`** (then curl waits out `--max-time`).
- [ ] The same command with the **same** `$T` → **`401 Unauthorized`**, body
      `{"error":"ticket required"}`.
- [ ] A fresh ticket → `101` again; a ticket left unused for more than 30 s → `401`.
- [ ] `POST /api/ws-ticket` follows the read rule: without `-u` it is `401` on a device with a
      password, `200` on one without.
- [ ] After these attempts the Control page still connects.

> A second `101` on a reused ticket is the failure to look for — it means the `/ws` handler is
> never reached on the handshake.

## 5. The log page

- [ ] `/log` shows oldest first; each line carries an uptime stamp `h:mm:ss.mmm`, a tag and text;
      errors are red, warnings amber. The tail matches
      `otget http://$OT/api/log | jq -r '.[]' | tail -5`.
- [ ] With Follow on, a curl change shows its `control_state` line within ~3 s and the list stays
      at newest; with Follow off nothing moves until Refresh.
- [ ] Take the device off the network with the page open: a grey "No answer from the device"
      notice appears and the last lines stay on screen.

## 6. The settings form sends only what changed

- [ ] With nothing edited, the hint reads "Nothing changed." Change one field (say Failsafe heat
      days to 5) → "Sends failsafe_heat_days and nothing else." Save: the request body is exactly
      `{"failsafe_heat_days":5}`.
- [ ] **The stale write.** Open Settings in tab A and tab B. In A set heat days to 4 and save. In
      B (loaded before that save, not reloaded) change only the watchdog to 600 and save → the body
      is exactly `{"watchdog_s":600}`, with no `failsafe_heat_days`. `GET /api/config` then shows
      heat days 4 and watchdog 600; tab B shows heat days 4 and "Nothing changed."
- [ ] **The band carries the local setpoint.** In local mode, season on, flow setpoint 65 on the
      Control page. Set Highest flow setpoint to 60.0 and save → body `{"flow_max_dc":600}`, and
      "Flow setpoint held" becomes 60.0 °C. Set it back to 70.0.
- [ ] Validation refuses out-of-range values with the device's own sentence, not a paraphrase:
      Failsafe flow setpoint 75.0 (outside the band) → `422`, `the failsafe setpoint
      (failsafe_setpoint_dc) must lie between flow_min_dc and flow_max_dc`; Lowest flow setpoint
      70.0 with Highest at 70.0 → `422`, `the lowest flow temperature (flow_min_dc) must be below
      the highest (flow_max_dc)`.
- [ ] A value with too many decimals or a non-integer where a whole number is needed is caught
      before any request: `45.25` in Failsafe flow setpoint → the field hint, no request sent;
      `1.5` in Watchdog (s) → "a whole number is needed", no request.
- [ ] An unsaved edit in the Broker card survives the controller card's save (type a new topic
      prefix, don't save it, change the watchdog and save — the prefix box still holds what you
      typed). The Broker card's own save carries only the broker/device keys, none of the control
      keys.

## 7. The web has no privileged handle

Run each of these with the Control page open; the page must follow within a second or two via
`/ws`:

- [ ] The five entity writes (`heating_season`, `ch_enable`, `ch_setpoint`, `dhw_enable`,
      `dhw_setpoint`, each `-d '{"value":…}'`) each answer `202 {"applied":{"command":N}}`; the two
      operations (`POST /api/ops/boost -d '{"setpoint":50,"minutes":2}'`, `POST /api/ops/boost_off`)
      each answer `202 {"running":true}`; a config save answers `{"saved":true}`. The card shows the
      same result each time.
- [ ] Every request the pages made during this walk went to a known path: `/api/control`,
      `/api/config`, `/api/entities…`, `/api/state`, `/api/ops/…`, `/api/log`, `/api/ot/raw`,
      `/api/ws-ticket`, `/ws`, `/api/wifi/scan`, `/api/provision`, and static files. No other path.

## 8. When the device is gone

- [ ] Power the device off with the Control page open: within ~15 s (a 5 s poll plus a 10 s
      request timeout) the card keeps its last values under a grey "No answer from the device";
      within ~35 s the badge reads "Disconnected". Pressing a control shows `<control>: no answer
      from the device` within those ten seconds. Power back on: the card recovers without a reload.
- [ ] Worth knowing by sight: an "The executor has not started" notice means the device answered
      `GET /api/control` with the zeroed document (the thermostat task did not start, and every
      control would answer `503`). Take the log before rebooting.

## Notes

- The failsafe banner on the web (which appears even when nothing reaches Home Assistant) is
  exercised in [`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md); the language of every
  page and its best-effort error translation are in [`09-language.md`](09-language.md).
