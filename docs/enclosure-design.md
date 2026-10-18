# 3D-printed enclosure — design

**Date:** 2026-09-22
**Status:** implemented (v2, orientation corrected from the owner's photos) — model in `hardware/enclosure/`
**Scope:** a free-standing, 3D-printable case for the **LOLIN C3 mini** stacked over the **DIYLess
OpenTherm Master Shield**: a lid window over the RGB status LED, poke-holes over the two buttons,
ventilation, and a single front window for USB-C power and the boiler loop.

This document is self-contained: it states not only *what* the case is but *why* each dimension
and feature was chosen. Dimensions come from datasheets and from photos of the real assembly (§2);
the model is parametric so the height-setting numbers can be tuned on the first print.

---

## 1. Motivation

The device is a two-board stack: a LOLIN C3 mini carrying the firmware and the DIYLess OpenTherm
Master Shield, joined by 2.54 mm stackable headers. The owner wants a printable case comparable to
the commercial DIYLess "OpenTherm Thermostat Case"
(<https://diyless.com/product/opentherm-thermostat-case>, an enclosure for exactly this
"D1 mini ESP32 + Master OpenTherm Shield" pair — the vendor page gives no dimensions).

Four things the case must not get in the way of, all named by the owner:

1. **The two buttons** (RST and BOOT). BOOT (GPIO9) enters download mode, which — per
   `docs/esp32-c3-mini.md` §5 and the boiler-idle-LOW caveat — is required to flash with the boiler
   connected. A sealed case that buries BOOT makes reflashing impossible.
2. **Air.** Modest ventilation (see the DS18B20 note below).
3. **The boiler connection.** The shield's green `KF128-2P` (3.81 mm) screw terminal (`B1 / B2`)
   must be reachable, and its two wires must exit the case to the boiler.
4. **The RGB status LED.** The `feat/led-status` work drives the WS2812B on **GPIO7** as a health
   ladder; the case must show it.

**The stack orientation drives the whole layout — and it is the opposite of a first guess.** The
owner's photos settle it: **the C3 is the TOP board, the shield is the BOTTOM board.** Consequences,
all load-bearing:

- The C3's **USB-C, RST, BOOT and RGB LED face UP** toward the lid — so the LED window and the
  button poke-holes go in the **lid**, and USB-C exits a **front-edge** opening. (An earlier draft
  assumed the shield on top covering the C3, forcing side access — the photos disproved it.)
- The **tall parts — the KF128 terminal (~9.5 mm) and the optocouplers — sit BETWEEN the boards**,
  inside the inter-board gap. Their height is therefore **contained in the gap, not stacked on top
  of the shield.** The earlier draft double-counted this (an 8.5 mm gap *plus* 9.5 mm above),
  making the box ~8 mm too tall. Correcting it drops the box from ~32 mm to **~24 mm**.
- USB-C (C3 top edge) and the KF128 (in the gap) are on the **same short edge** (per the photos),
  so both cables leave through **one front window**.
- Below the shield, the header **down-pins are trimmed nearly flush** (owner's choice), so only a
  small clearance is needed under the bottom board.

**DS18B20 note.** The shield's DS18B20 is best **mounted remotely on leads** (DIYLess's own
recommendation): inside a sealed box it measures the box's self-heating, not the room. Its lead
exits with the boiler cable through the front window.

---

## 2. Dimensions — datasheets + the owner's photos

No fresh measurement is required to print a first article; the values below are the defaults in
`hardware/enclosure/params.scad`. The height-setting ones (marked ⌗) are worth a caliper check on
the assembled stack, because they depend on the build — all are parameters, tunable without redesign.

| Feature | Value (mm) | Source |
| --- | --- | --- |
| C3 mini PCB outline | 34.3 × 25.4 | LOLIN/WeMos spec, `dim_c3_mini_v1.0.0.pdf` |
| Shield / D1-mini outline | 34.2 × 25.6 | WeMos D1 mini "Size 34.2 × 25.6" |
| PCB thickness (both) | 1.6 | standard 2-layer FR-4 |
| PCB corner radius | 2.0 | D1-mini family |
| **Board-to-board gap** (holds the KF128 + optos) | **10.0 ⌗** | photo; ≥ KF128 body ~9.5 |
| Under-shield clearance (trimmed down-pins) | 3.0 ⌗ | photo / owner's choice |
| Headroom over the C3 top (USB-C shell) | 3.5 | USB-C ~3.2 mm |
| USB-C + KF128 front window | 15 wide | ~9 mm USB-C + KF128 + wires |
| WS2812B (GPIO7) | 5050 pkg, C3 top face | LOLIN v2.1 |
| RST / BOOT | SMD tactile, C3 top, front edge ⌗ | LOLIN v2.1 (no published component XY) |

Note: the "3 mm length" the owner cited for the stackable header is the male-pin tail, not the
board gap; the gap is set by the parts sandwiched between the boards (~10 mm).

**Height budget** (external ≈ **39 × 30 × 24 mm**): floor 2 + under-shield 3 + shield 1.6 +
gap 10 + C3 1.6 + top clear 3.5 + lid 2. The gap (10) is now the single dominant term; the KF128
lives inside it.

---

## 3. Chosen approach — edge-retained two-part box (Approach A)

Three approaches were weighed:

- **A. Two-part box, boards held by edge geometry (chosen).** A base tray whose inner cavity
  matches the footprint + clearance; the **shield** rests on four floor posts, the **C3** rides
  above it on the headers, and the cavity walls hold the stack laterally. The lid is a vented cap
  with a friction rim. **No fasteners hold the boards** — geometry does.
- **B. Standoffs through the mounting holes.** The C3 has only two Ø2.0 holes, both on one edge, so
  the board cantilevers and the stack rocks. Needs M2 hardware. Rejected.
- **C. Board mounted to the lid, faceplate-style.** Harder to print and align. Rejected for a
  shelf-standing box.

**Decision: A**, confirmed by the owner. Free-standing (no wall mount).

---

## 4. Geometry and features

Origin at the footprint centre, Z=0 at the outer bottom. **+Y = front** (the USB-C / KF128 edge).
Shield on the floor posts, C3 above it, tall parts in the gap. Base and lid print open-face-up with
no supports.

- **Front (+Y) window.** One opening (`front_win_w`) runs from the gap up through the rim: the
  **KF128** shows in the lower (gap) part, the **USB-C** on the C3's top edge in the upper part, and
  both cables (plus the remote DS18B20 lead) leave here. It reaches the rim, so the lid caps it —
  no bridge.
- **Lid — LED window.** A hole over the WS2812B (`led_x/led_y`); by default a 0.8 mm translucent
  membrane diffuses it (`led_through = true` cuts a clean hole for a light pipe).
- **Lid — button poke-holes.** One over RST and one over BOOT (`btn_hole_*`); the C3 faces up, so a
  pin through the hole presses the top-actuated button. Download mode works with the case closed.
- **Ventilation.** Slots in the lid's back half (`vent_slots`, `vent_slot_w`), clear of the
  LED/button holes.
- **Board retention.** Four floor posts lift the shield by `below_h`; cavity walls hold the stack
  laterally; header friction holds the C3 to the shield.
- **Lid join — friction rim.** A rim drops into the cavity and grips by friction (`snap_fit` tunes
  tightness). Snap beads and M2 screws are deliberately deferred: a tuned friction fit is reliable
  and needs no hardware.

---

## 5. Print and material notes

- **Material: PETG** — the case may live near a warm boiler; PLA softens.
- **Orientation:** base open-face up; **lid upside-down** (top plate on the bed, rim up). No
  supports — the front window runs to the rim (no bridge), and vents/LED/button holes are in a
  flat plate.
- **Walls:** 2 mm (`wall`), 0.2 mm layers, ≥3 perimeters.
- **Clearances:** 0.3 mm around the PCB (`fit`), 0.2 mm lid rim (`snap_fit`) — tune per printer.

---

## 6. File layout

New top-level `hardware/` directory (no CAD existed in the repo before):

```
hardware/enclosure/
  params.scad    every dimension/tolerance — the only file normally edited
  case.scad      geometry (includes params.scad); renders base | lid | assembly, `cut` debug
  README.md      render/export commands, print + assembly steps, tuning notes
  preview*.png   rendered exterior + cutaway
```

Export is one command per part, e.g. `openscad -o base.stl -D 'part="base"' case.scad`. Only the
`.scad` sources, README and previews are committed — **no STL** (a build output). Both `.scad`
files are hand-written source and stay under the 350-line ceiling; the parameters/geometry split
is the seam.

---

## 7. Verification (owner, on real hardware)

A successful render is not readiness — fit is physical. After the first print the owner checks, and
the result becomes a scenario under `docs/hardware-verification/scenarios/` once confirmed:

1. The stack drops in — the shield seats on the posts, the C3 rides above without rocking.
2. USB-C plugs in through the front window; the plug overmold clears it.
3. The KF128 is reachable and the boiler wires exit the front window.
4. Both buttons actuate through their lid poke-holes; **download mode can be entered with the case
   closed** (hold BOOT, tap RST) — the flashing path must survive the case.
5. The RGB LED is visible through its lid window across the health ladder's colours.
6. The lid rim grips firmly; adjust `snap_fit` and reprint the lid if not.

The most likely first-print corrections are `board_gap`, `below_h`, and the LED/button positions —
all one-line parameter overrides, which is why the model is parametric.

---

## 8. Parameters worth a caliper check (not blockers)

- `board_gap` — the assembled shield-top-to-C3-bottom distance (biggest height driver).
- `below_h` — clearance under the shield for the trimmed down-pins.
- `led_x/led_y`, `btn_hole_x/btn_hole_y` — LED and button positions on the C3 top face.

Everything else is a printer-tolerance tweak, not a measurement. None block printing.
