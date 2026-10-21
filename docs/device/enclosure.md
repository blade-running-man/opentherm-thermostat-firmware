# Enclosure

A free-standing, 3D-printable case for the **LOLIN C3 mini** stacked over the **DIYLESS OpenTherm
Master Shield**: a lid window over the RGB status LED, poke-holes over the two buttons, ventilation,
and a single front window for USB-C power and the boiler loop. The model is parametric OpenSCAD in
`hardware/enclosure/`; it is render-validated, and no print of it has been fitted. This page records
what the case is and why each dimension and feature was chosen, so the numbers can be tuned on the
first print without re-deriving them.

## What the case must not get in the way of

The device is a two-board stack joined by 2.54 mm stackable headers. The goal is a printable case
comparable to the commercial DIYLESS "OpenTherm Thermostat Case"
(<https://diyless.com/product/opentherm-thermostat-case>, made for exactly this "D1 mini ESP32 +
Master OpenTherm Shield" pair; the vendor page gives no dimensions). Four things it must leave
usable:

1. **The two buttons**, RST and BOOT. BOOT (GPIO9) enters download mode, which may be required to
   flash with the boiler connected, because a connected, silent boiler holds the OpenTherm input
   low (see [Board](/device/board#strapping-on-the-c3)). A sealed case that buries BOOT makes
   reflashing impossible.
2. **Air**: modest ventilation.
3. **The boiler connection.** The shield's green `KF128-2P` (3.81 mm) screw terminal (`B1 / B2`)
   must be reachable and its two wires must leave the case.
4. **The RGB status LED**, the WS2812B on GPIO7 that shows the health ladder (see
   [Indication](/device/indication)).

### The stack orientation drives the layout

The owner's photos of the real assembly settle it: **the C3 is the TOP board, the shield is the
BOTTOM board.** Consequences, all load-bearing:

- The C3's **USB-C, RST, BOOT and RGB LED face up** toward the lid, so the LED window and the button
  poke-holes are in the **lid**, and USB-C leaves through a **front-edge** opening.
- The **tall parts, the KF128 terminal (~9.5 mm) and the optocouplers, sit between the boards**,
  inside the inter-board gap. Their height is contained in the gap, not stacked on top of the
  shield; adding the terminal's height on top of the gap would overstate the box by about 8 mm
  (~32 mm instead of ~24 mm).
- USB-C (on the C3's edge) and the KF128 (in the gap) are on the **same short edge**, so both cables
  leave through **one front window**.
- Below the shield the header down-pins are **trimmed nearly flush** (the owner's choice), so only a
  small clearance is needed under the bottom board.

::: info The DS18B20 belongs outside
Mount the shield's DS18B20 **remotely on leads** (DIYLESS's own recommendation): inside a closed box
it measures the box's self-heating, not the room. Its lead leaves with the boiler cable through the
front window. The firmware treats the shield sensor as an `ambient` source in any case (see
[Room sources](/device/room-sources)).
:::

## Dimensions {#dimensions}

The values below are the defaults in `hardware/enclosure/params.scad`, taken from datasheets and
from photos of the real assembly; no fresh measurement is required to print a first article. The
height-setting ones (marked ⌗) are worth a caliper check on the assembled stack, because they depend
on the build. All of them are parameters, tunable without redesign.

| Feature | Value (mm) | Parameter | Source |
| --- | --- | --- | --- |
| C3 mini PCB outline | 34.3 × 25.4 | `board_l` takes the larger of the two boards | LOLIN/WEMOS spec, `dim_c3_mini_v1.0.0.pdf` |
| Shield / D1-mini outline | 34.2 × 25.6 | `board_w` takes the larger | WEMOS D1 mini, "Size 34.2 × 25.6" |
| PCB thickness (both) | 1.6 | `c3_thick`, `shield_thick` | standard 2-layer FR-4 |
| PCB corner radius | 2.0 | `board_r` | D1-mini family |
| **Board-to-board gap** (holds the KF128 and optocouplers) | **10.0** ⌗ | `board_gap` | photo; at least the ~9.5 mm KF128 body |
| Clearance under the shield (trimmed down-pins) | 3.0 ⌗ | `below_h` | photo; the owner's choice |
| Headroom over the C3 top (USB-C shell) | 3.5 | `top_clear` | USB-C shell ~3.2 mm |
| Front window for USB-C and the KF128 | 15 wide | `front_win_w` | ~9 mm USB-C, plus the KF128 and wires |
| WS2812B (GPIO7) | 3535 package (3.5 × 3.5 mm), C3 top face ⌗ | `led_x`, `led_y` (0, 6.0) | `WS2812B-3535` in the LOLIN schematic; position from photos |
| RST / BOOT | SMD tactile, 2 × 4 mm, 3.5 mm tall, C3 top, near the front edge ⌗ | `btn_hole_x`, `btn_hole_y` (±8.8, 13.15) | `SW_2x4x3.5` in the LOLIN schematic; positions estimated (LOLIN publishes no component placement) |

The "3 mm length" quoted for the stackable header is the male-pin tail, not the board gap; the gap
is set by the parts sandwiched between the boards (~10 mm).

**Height budget** (external ≈ **39 × 30 × 24 mm**): floor 2 + under-shield 3 + shield 1.6 + gap 10
+ C3 1.6 + top clearance 3.5 + lid 2. The gap is the single dominant term; the KF128 lives inside
it. In plan, the cavity is the larger board outline plus 0.3 mm `fit` per side, inside 2 mm walls.

## Chosen approach: an edge-retained two-part box

| Approach | Verdict |
| --- | --- |
| **A. Two-part box, boards held by edge geometry** | **chosen**. A base tray whose cavity matches the footprint plus clearance; the shield rests on four floor posts, the C3 rides above it on the headers, the cavity walls hold the stack laterally; the lid is a vented cap with a friction rim. No fastener holds the boards: geometry does |
| B. Standoffs through the mounting holes | rejected. The C3 has only two Ø2.0 holes, both on one edge, so the board cantilevers and the stack rocks; and it needs M2 hardware |
| C. Board mounted to the lid, faceplate-style | rejected: harder to print and align, and the case stands on a shelf |

The case is free-standing; there is no wall mount.

## Geometry and features

Origin at the footprint centre, Z = 0 at the outer bottom, **+Y = front** (the USB-C / KF128 edge).
The shield sits on the floor posts, the C3 above it, the tall parts in the gap. Base and lid print
open face up with no supports.

- **Front (+Y) window.** One opening (`front_win_w`) runs from the gap up through the rim: the KF128
  shows in the lower part, the USB-C on the C3's edge in the upper part, and both cables (plus the
  remote DS18B20 lead) leave here. It reaches the rim, so the lid caps it and no bridge is needed.
- **Lid: LED window.** A Ø5 mm pocket over the WS2812B (`led_win_d`, `led_x`, `led_y`), cut from
  the inside of the lid plate. By default it stops 0.8 mm short of the outer face
  (`led_win_membrane`), leaving a thin skin of the plate that diffuses the LED; `led_through = true`
  cuts it through for a light pipe or a resin drop.
- **Lid: button poke-holes.** A 4 mm square hole (`btn_hole_w`) centred on each button position,
  mirror-symmetric about the long axis: `(±btn_hole_x, btn_hole_y)`, by default 4 mm in from the
  side edges and 4 mm in from the front edge of the board. Both holes lie inside the lid's friction
  rim, so nothing below the plate blocks the pin. The C3 faces up, so a pin through a hole presses
  the top-actuated button: download mode works with the case closed.
- **Ventilation.** Five 1.6 mm slots in the lid's back half (`vent_slots`, `vent_slot_w`), clear of
  the LED and button holes.
- **Board retention.** Four floor posts lift the shield by `below_h`, the cavity walls hold the
  stack laterally (with `fit` clearance), and header friction holds the C3 to the shield.
- **Lid join: friction rim.** A rim (`rim_h` 3 mm deep, `rim_w` 1.6 mm thick) drops into the cavity
  and grips by friction; `snap_fit` (0.2 mm per side) tunes the tightness. Snap beads and M2 screws
  are deliberately left out: a tuned friction fit is reliable and needs no hardware.

## Printing

- **Material: PETG.** The case may live near a warm boiler, and PLA softens.
- **Orientation:** the base open face up; the **lid upside down** (top plate on the bed, rim up). No
  supports: the front window runs to the rim, and the vents, LED and button holes are in a flat
  plate.
- **Walls:** 2 mm (`wall`, five 0.4 mm perimeters), 2 mm floor and lid plate, 0.2 mm layers, at
  least 3 perimeters.
- **Clearances:** 0.3 mm around the PCB (`fit`), 0.2 mm at the lid rim (`snap_fit`); tune per
  printer.

## Files

```
hardware/enclosure/
  params.scad    every dimension and tolerance; the only file normally edited
  case.scad      geometry (includes params.scad); renders base | lid | assembly, `cut` for a cutaway
  README.md      render and export commands, print and assembly steps, tuning notes
  preview*.png   rendered exterior and cutaway
```

`part` selects what is rendered (`"base"`, `"lid"`, or `"assembly"` with a translucent board mock);
`cut = true` slices away the +X half to inspect the internal fit. Export is one command per part,
for example:

```sh
openscad -o base.stl -D 'part="base"' case.scad
openscad -o lid.stl  -D 'part="lid"'  case.scad
```

Only the `.scad` sources, the README and the previews are committed; **no STL**, which is a build
output. Both `.scad` files are hand-written source under the 350-line ceiling; the parameters /
geometry split is the seam, and nothing in `case.scad` is hard-coded.

## Verifying the fit

A successful render is not readiness: fit is physical. After the first print, check:

1. The stack drops in: the shield seats on the posts, the C3 rides above without rocking.
2. USB-C plugs in through the front window and the plug's overmould clears it.
3. The KF128 is reachable and the boiler wires leave through the front window.
4. Both buttons actuate through their poke-holes, and **download mode can be entered with the case
   closed** (hold BOOT, tap RST): the flashing path must survive the case.
5. The RGB LED is visible through its window across the health ladder's colours.
6. The lid rim grips firmly; if not, adjust `snap_fit` and reprint the lid.

This list is the enclosure's fit check; [Hardware verification](/hardware-verification/) has no
enclosure scenario.

### Parameters worth a caliper check

The likeliest first-print corrections, each a one-line override, which is why the model is
parametric:

- `board_gap`: the assembled shield-top to C3-bottom distance, the biggest height driver;
- `below_h`: the clearance under the shield for the trimmed down-pins;
- `led_x` / `led_y` and `btn_hole_x` / `btn_hole_y`: the LED and button positions on the C3's top
  face.

Everything else is a printer-tolerance tweak, not a measurement, and none of it blocks printing.
