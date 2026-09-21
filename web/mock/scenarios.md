# Mock boiler simulator

Local development without hardware. `npm run dev:mock` starts the SPA (with HMR) and an
in-process boiler simulator that serves the firmware's `/api` and `/ws` contract from a
dynamic model, so the whole UI is usable with no device on the LAN.

## Run

```sh
npm run dev:mock
```

Open the printed URL (default http://localhost:5173). No `.env.local` and no proxy are
needed. Plain `npm run dev` still targets a real device via `VITE_DEVICE_IP`.

## What the model does

The simulator holds one `BoilerState` and evolves it once a second (`mock/evolve.ts`, time
constants accelerated for dev comfort):

- Writing the CH setpoint (`POST /api/entities/ch_setpoint`, or a boost) raises the held
  setpoint; while heating is enabled and the burner is lit, the flow temperature climbs
  toward it over ~30 s, the return tracks below it, modulation decays near the target, and
  the ID 0 flame/CH-active flags follow. Turn heating off and the flow cools toward the room.
- The room temperature drifts up while heating is active and down toward an outdoor baseline
  while idle. DHW behaves the same around its own setpoint.
- The control ladder picks the state the UI shows: `season_off` → `failsafe` → `boost` →
  (HA mode: watchdog-timeout → `failsafe`, no setpoint → `ha_waiting`, else `ha`) → `local`.

## Scenarios — forcing edge states

Everything the UI cannot easily reach on a healthy boiler is driven through the dev-only
endpoint `POST /__mock/scenario` (this route is **not** part of the firmware contract; it
exists only in the mock). `GET /__mock/scenario` returns the current knobs. Knobs:

| Knob | Effect in the UI |
| --- | --- |
| `mode` (`"local"`\|`"ha"`) | HA mode makes the CH control entities HA-owned (writes 409) and shows the HA/failsafe control states |
| `passwordSet` + `password` | Locks the API: GETs and writes return 401 with `WWW-Authenticate`, so the browser shows its Basic-auth dialog |
| `provisioned` (`false`) | Unprovisioned: only `POST /api/provision` is accepted; other writes 403 |
| `heatingSeason` (`false`) | Control state `season_off` |
| `failsafe` (`true`) | Forces the failsafe branch of the ladder |
| `boilerFault` (`true`) | Raises the ID 0 fault flag and a non-zero OEM fault code |
| `answering` (`false`) | Silent bus: `GET /api/ot/raw` reports `answering:false` and state reads go stale/unknown |
| `brokerUp` (`false`) | Reserved: a broker-down knob for a future `/api/status` mqtt block; no visible UI effect today |

Examples (default dev port 5173):

```sh
# Home Assistant mode — control entities become HA-owned
curl -X POST -H 'Content-Type: application/json' -d '{"mode":"ha"}' \
  http://localhost:5173/__mock/scenario

# Require a login (browser Basic dialog; user "admin", password below)
curl -X POST -H 'Content-Type: application/json' -d '{"passwordSet":true,"password":"admin"}' \
  http://localhost:5173/__mock/scenario

# Boiler fault + silent bus
curl -X POST -H 'Content-Type: application/json' -d '{"boilerFault":true,"answering":false}' \
  http://localhost:5173/__mock/scenario

# Failsafe branch
curl -X POST -H 'Content-Type: application/json' -d '{"mode":"ha","failsafe":true}' \
  http://localhost:5173/__mock/scenario

# Read the current knobs
curl http://localhost:5173/__mock/scenario
```

The simulator starts healthy and unlocked (provisioned, no password, `local` mode, heating
season on, boiler answering), so the full UI works out of the box; apply a scenario only to
exercise an edge state, and restart the dev server to reset.

## Tests

The pure core (model, evolve, projections, policy, ws framing, router) is host-tested:

```sh
npm run test:mock       # the mock's own suites
npm run typecheck:mock  # type-check the mock/ tree
```
