# 00 — Setup and safety (read before any scenario)

This directory holds **test scenarios the owner walks on real hardware** — the boiler, a
broker, Home Assistant, a browser against the device. They are grouped by what the device
*does* (bring-up, conversation with the boiler, asking for heat, Home Assistant, …), not by
any phase of how the firmware was built. Each file is self-contained; this one holds the
setup and the safety rules **every** scenario leans on, so the others do not repeat them.

> **Flashing is the owner's business. Not one item in any scenario is done without him.**
> The build happens on the machine, the check happens at the boiler, and those are different
> events. **A green build is not readiness** — nothing here is proven until it is walked on a
> physical device.

Every checkbox is written as *not yet walked* (`[ ]`). Tick them as you go on your own copy;
they record what has actually been proven on the device.

---

## 1. The curl harness (used by every API scenario)

Every API handle — reads included — sits behind the interface password (HTTP Basic, no user
name, so the colon in `-u ":$PASS"` is mandatory). Set these once per session:

```sh
export OT=192.168.1.42                 # the device's address on your network
export OTPASS='interface-password'     # the web/interface password
alias otget='curl -sS -u ":$OTPASS"'
alias otcode='curl -sS -o /dev/null -w "%{http_code}\n" -u ":$OTPASS"'
```

- [ ] `otcode http://$OT/api/status` → `200`.
- [ ] The same request **without `-u`** → `401`. A `200` here means no password is set and the
      device is open to anyone on the network — set one before walking anything that writes.

> **DO NOT** drop the colon in `-u ":$OTPASS"`. Without it curl treats the whole string as a
> user name with an empty password, and every item fails `401` for a reason that has nothing
> to do with what you are testing.

Secrets are write-only: `GET /api/config` never returns a stored password — it hands back a
sentinel (`__UNCHANGED__` for the interface password, a placeholder for `mqtt_password`). If a
real password ever appears in that document, that is a defect, not a passing setup.

---

## 2. The bus must never fall silent — the one invariant behind every scenario

Silence from the master longer than **5 s** is read by the OpenTherm slave as a *shorted
thermostat*, and the boiler **goes into a demand for heat** (spec §3.5). Falling silent is not
the safe state — it is the hottest one. **No** failure has the right to stop the bus: not a
WiFi drop, not a dead broker, not a stale sensor, not an absent Home Assistant.

So **keep one terminal running the bus monitor for the whole of every scenario** and glance at
it after each item:

```sh
while sleep 15; do
  otget http://$OT/api/ot/raw \
    | jq -r '"\(.cycles) ok=\(.ok) failed=\(.failed) overdue=\(.overdue) answering=\(.answering)"'
done
```

Across every item, on every scenario:

- [ ] `cycles` grows monotonically (~15–16 per 15 s) — it never stops or resets.
- [ ] `answering` stays `true` while the boiler is connected.
- [ ] `overdue` does **not** grow. Growth means the bus task was held longer than the 1150 ms
      deadline — usually a blocked HTTP request or a flash write mid-conversation.
- [ ] the `failed`:`cycles` ratio does not grow (the odd lost frame is normal; a rising share
      is not).
- [ ] **the boiler does not heat on its own** — watch its display and `ch_active` / `flame` in
      `/api/state` (both `false` with heating off). A boiler heating with no command is a boiler
      that saw the bus go silent.

**The single legitimate exception** is the line test (scenario `03`): for its ~20 s the
`cycles` count stops dead *on purpose*, and the boiler will fire. It must resume by itself
afterwards. A pause anywhere else, in any other scenario, is a defect.

### Reading `/api/ot/raw`

Each entry in `.ids[]` is the *latest* answer to one Data-ID — `type`
(`read-ack`/`write-ack`/`data-invalid`/`unknown-dataid`), `raw` (decimal), `age_ms`, and a
`count` of answers since boot. There is no per-ID history. IDs that are only ever written
(1, 16, 24) show a `count` that moves solely on a write; polled IDs move on every pass.

---

## 3. Reading the log — over USB, not `/api/log`

`ot_log_render()` escapes every non-ASCII byte as `\u00xx`, so a log line with any non-ASCII
character (the default device name is Russian) comes back through `GET /api/log` unreadable and
ungreppable. **Read the journal over the USB console**, where it is plain text:

```sh
~/.platformio/penv/bin/pio device monitor -e lolin_c3_mini | grep ot_bus
```

The journal ring is 80 lines; boot lines roll out after roughly an hour, so read the ones you
need soon after the event. `I (12345) …` timestamps are milliseconds since boot — they do not
track wall-clock and do not move on their own.

---

## 4. The reboot window is named, not hidden

Any reboot takes longer than the 5 s silence window, so by spec (§3.5, §8.1.1) the boiler
**briefly demands heat** during a restart. The wording "the boiler does not go into heat
demand on reboot" is **unachievable and is not what to check.** The item passes if the boiler
**returns to normal after the first conversation** once the device is back — not if the demand
never happens. Where a scenario measures the window, that number is what an OTA update will
cost the house.

---

## 5. The second board — ESP32-C6 SuperMini (a separate pass)

The C3 is the main board; the C6 SuperMini is the second target. Where a scenario is
board-independent, walk it on whichever board is on the bench. Where it is not, the C6 differs
in ways that will kill the board or its console if copied from the C3:

- [ ] **Check flash size before the first flash:** `~/.platformio/penv/bin/esptool flash_id`.
      Expect **4 MB**. An 8 MB board needs `esp32-c6-devkitc-1` instead — a mismatch breaks the
      partition table.
- [ ] Flash with its own env: `~/.platformio/penv/bin/pio run -e supermini_c6 -t upload`.
- [ ] Console descriptor line reads `board supermini_c6: OpenTherm in=18 out=19, button=9 …`.
      Seeing `in=8 out=10` means the C3 env was built by mistake.
- [ ] **Four wires to the shield, the only way** — the D1-mini shield does not plug into the
      SuperMini: `D1 (OT OUT) → IO19`, `D2 (OT IN) → IO18`, `3V3 → 3V3`, `GND → GND`.

> **DO NOT** take GPIO numbers from the C3 descriptor. On the C3, OT is on GPIO8/10 and USB
> owns 18/19; on the C6 it is the reverse. **DO NOT touch IO12/IO13 with a stray wire** — the
> C6 brings its USB Serial/JTAG out there, and a short there costs the board its console and
> its flashability.

The C6's ceramic-antenna WiFi range is noticeably worse than the C3's: a link that breaks at
the boiler but holds at the desk is an antenna problem, not a firmware one. The "enter
flash-mode ten times with the boiler running" item (scenario `01`) **does not apply** on the
C6 — its OT input is on GPIO18, and the boiler never reaches the strapping pins.

---

## 6. What this boiler cannot tell you (facts, not failures)

Some scenarios expect an answer this particular boiler (an Intergas Kombi Kompakt HRE) cannot
give. These are properties of the appliance, recorded here once so no scenario reads them as
defects:

- **ID 27 (outside temperature)** and **ID 5 (fault flags)** answer `DATA-INVALID` — no outdoor
  sensor on X4 8-9, no faults. They must show as `invalid`/`null`, never `ok`+`0`.
- **ID 57 (`MaxTSet`)** answers `UNKNOWN-DATAID` — the boiler branch of the CH ceiling is
  unreachable here; only the config branch runs. A boiler that implements ID 57 would answer
  differently, and that is not a bug to "fix".
- The unsupported Data-IDs on this unit are **15, 28, 33, 49, 57, 117, 118, 119, 121, 122,
  125, 127** — pruned from the poll ring after two `UNKNOWN-DATAID` answers.
- The boiler's own low-flow floor is its parameter **`E`** (≈ 40 °C) with behaviour parameter
  **`E.`** (`0` ignore / `1` heat at minimum / `2` heat at maximum). The firmware obeys a low
  request; the boiler may not — indistinguishable from outside.

A different boiler will differ on all of the above. Re-derive them for your unit; do not treat
these numbers as the contract.
