# DIYLess OpenTherm Master Shield — hardware reference

A hardware note for the **DIYLess "OpenTherm Master Shield"**
(<https://diyless.com/product/esp8266-thermostat-shield>), the adapter this firmware talks to
the boiler through. Written from the two board photos, the vendor page, and this project's own
board descriptor (`components/board/board_lolin_c3_mini.c`), which is the authority for the pin
mapping actually used.

**Scope.** This documents the shield as a piece of hardware: its form factor, its silkscreen,
its two isolated OpenTherm channels, and how it plugs onto the boards this firmware runs on. It
is not a schematic — component values below are read off the SMD markings in the photo and are
approximate where noted.

## What it is

A Wemos **D1 mini form-factor** shield that turns a microcontroller into an **OpenTherm
master**: it drives the low-voltage OpenTherm current loop to a boiler and receives the
boiler's reply, with **galvanic isolation** in both directions (two optocouplers). It is a
master (thermostat) adapter — it commands a boiler; it is not a boiler-side (slave) interface.

- **Silkscreen:** `OPENTHERM MASTER SHIELD · DIYLess.com`.
- **Designed to stack** on a WeMos D1 mini (ESP8266) or WeMos D1 mini ESP32; the vendor notes
  it works with "all types of microcontrollers" given the right two GPIOs. In this project it
  stacks directly on the **LOLIN C3 mini** (same D1-mini header) and is wired to the **ESP32-C6
  SuperMini** with flying leads (that board has a different header).
- **Boiler connection:** a green 2-pin **screw terminal** (`KF128-2P`, 3.81 mm), labelled
  **B1 / B2 · "Boiler"**. **Polarity does not matter** — connect with any 2-wire cable or a
  twisted pair.
- **Temperature sensor:** a footprint and pads marked **`T` (+ / −)** for a **DS18B20** 1-Wire
  sensor. The DS18B20 ships with the shield but is **not soldered** — it comes loose with the
  pin headers.
- **Power:** the host board is powered as usual (micro-USB / its own regulator). The shield
  takes 3V3 and GND from the header; the OpenTherm loop itself is powered by the boiler.

## Board layout

### Top / silkscreen side (photo 1)

Standard D1-mini header labels down both edges, plus the shield's own markings:

```
   left edge            right edge
   ---------            ---------
   TX                   RST
   RX                   A0
   D1  >  ---.          D0
   D2  <  ---'  (jumpers) D5   <- "T" pad group near here
   D3                   D6
   D4                   D7
   GND                  D8
   5V                   3V3
   -                    T (+)

        B2   [ Boiler ]   B1
```

- **`D1 (>)` and `D2 (<)`** carry the two solder-jumper arrows on the silkscreen. On the D1-mini
  header these two positions are the OpenTherm **OUT** (`>`, master → boiler) and **IN** (`<`,
  boiler → master) lines. The dashed marks are selection jumpers.
- **`B1 / B2 / Boiler`** at the bottom = the boiler screw terminal.
- **`T` with `+`** on the right edge = the DS18B20 temperature-sensor pads.

### Component side (photo 2)

The active circuitry and the boiler terminal:

- **Two EL817 optocouplers** (Everlight EL817 / PC817-class) — the isolation barrier. One
  couples the outbound drive (master → boiler), the other the inbound reply (boiler → master),
  so no galvanic path exists between the boiler's OpenTherm loop and the microcontroller.
- **`OUT (<)` and `IN (>)` test pads** beside the optocouplers, matching the D1/D2 lines.
- **`Slave / Boiler` LED** — activity indicator on the boiler side of the link.
- **Green 2-pin screw terminal** — B1 / B2, the boiler loop.
- **`T` pad with `+ / −` (and a `K` mark)** — the DS18B20 connection.
- **Passives / semiconductors** — see the verified BOM below.

## Bill of materials (from the reference design)

The DIYLess shield is the SMD productization of **Ihor Melnyk's OpenTherm Adapter**
(<https://ihormelnyk.com/opentherm_adapter>) — the open reference design this whole family of
shields (DIYLess master/slave, and many ESPHome/gateway builds) is based on. Its published BOM
is the authority for the circuit, and it matches the shield photo part-for-part:

| Part | Value / type | Qty | Confirms in photo |
| --- | --- | --- | --- |
| Optocoupler | **PC817** | 2 | the two `EL817` (Everlight EL817 = PC817-compatible) |
| PNP transistor | **BC858A** | 1 | the SOT-23 marked **`3K`** (BC858A marking code) |
| Signal diode | **1N4148** | 4 | the four small diodes on the left |
| Zener diode | **4V7** | 1 | one of `D5/D6/D7` |
| Zener diode | **4V3** | 1 | one of `D5/D6/D7` |
| Zener diode | **15V** | 1 | one of `D5/D6/D7` |
| Resistor | **100 Ω** | 1 | SMD `101` |
| Resistor | **220 Ω** | 1 | SMD `221` |
| Resistor | **330 Ω** | 2 | SMD `331` ×2 |
| Resistor | **1.5 kΩ** | 1 | SMD `152` |

Notes on the mapping:

- The reference design is **two independent isolated channels**, one per optocoupler: the
  transmit channel (microcontroller `OUT` → boiler loop) and the receive channel (boiler loop →
  microcontroller `IN`). This is why there are two PC817/EL817.
- The **BC858A** (PNP) sits in the master's loop-current source; the **1N4148 ×4** and the three
  **Zeners** clamp and set the switching thresholds on the loop side of the isolation barrier.
- The SMD resistor codes read off the photo (`101/221/331/331/152`, plus a couple more around
  the `Slave/Boiler` LED and pull-ups) line up with the reference values above. Melnyk's
  through-hole BOM lists exactly: 100 Ω, 220 Ω, 330 Ω ×2, 1.5 kΩ. The shield may carry one or two
  extra small resistors (LED series / pull-ups) not in the minimal through-hole list.
- **Operating principle** (from the reference design): the master modulates the **current** in
  the boiler loop (nominal ~5–9 mA for a "0", ~17–23 mA for a "1"), and reads the boiler's reply
  by sensing the **voltage** the boiler modulates on the same loop. The optocouplers keep both
  the microcontroller and the boiler galvanically isolated from each other.

The full schematic and PCB layout images are on the reference-design page above; DIYLess does
not publish a separate shield schematic — the shield is electrically the same circuit in SMD.

## Pin mapping (as used by this firmware)

The shield hard-wires OpenTherm **OUT to header D1** and **IN to header D2**. On a D1-mini
header those positions land on specific GPIOs, which is what the board descriptor encodes.

### LOLIN C3 mini (`esp32c3`) — shield stacked directly

Derived from `sch_c3_mini_v2.1.0.pdf` and the D1 mini v4 header order, and recorded in
`components/board/board_lolin_c3_mini.c`:

| Shield line | D1-mini label | GPIO | Notes |
| --- | --- | --- | --- |
| OpenTherm **OUT** (master → boiler) | `D1` | **GPIO10** | `ot_out_inverted = true` |
| OpenTherm **IN** (boiler → master) | `D2` | **GPIO8** | `ot_in_inverted = false` |
| DS18B20 (`T`) 1-Wire | `D5` | **GPIO1** | confirmed by bench probe |

- **Only the OUT line is inverted.** The IN line is read as-is; its active level is HIGH. This
  is the single field taken from a reference OpenTherm library rather than derived from the
  spec — if the receive path decodes not a single frame, flip `ot_in_inverted` **first**.
- **Strapping-pin cautions.** `GPIO8` (IN) is a strapping pin but does not block ordinary boot
  from flash. The OpenTherm line **idles LOW**, so a connected-but-silent boiler holds `GPIO8`
  down permanently — booting is unaffected, but **entering flash mode with the boiler connected
  usually needs BOOT held**. The hazard pin to watch is `GPIO2` (header `D0`), which must be
  HIGH at reset; the shield does not occupy `D0` — verify with a multimeter.

### ESP32-C6 SuperMini (`esp32c6`) — flying-wire adaptation

The DIYLess shield does **not** plug into the SuperMini (different header). It is wired to the
shield's **D1 (OUT)**, **D2 (IN)**, **3V3** and **GND** pads with four flying leads, and the
GPIOs are chosen freely:

| Shield line | GPIO | Notes |
| --- | --- | --- |
| OpenTherm **OUT** | **GPIO19** | `ot_out_inverted = true` |
| OpenTherm **IN** | **GPIO18** | `ot_in_inverted = false` |
| DS18B20 | — | none wired yet (`onewire_gpio = -1`) |

`GPIO18/19` are adjacent right-row pads; on the C6 neither is a strapping pin, nor USB
(`GPIO12/13`), nor UART0, nor an LED, nor the button. **Do not** copy the C3's `GPIO8/10` here,
and **do not** copy the C6's `18/19` back to the C3 (there they are native USB — copying them
kills the console and flashing).

> All GPIO numbers live only in `components/board/`. Every other component reaches these pins
> through `board_get()`. If a future board changes the wiring, edit the descriptor — nothing
> else.

## Wiring checklist

1. **Boiler:** two wires from the boiler's OpenTherm terminals to the shield's **B1 / B2** screw
   terminal. Polarity is irrelevant. Twisted pair or any 2-core cable is fine.
2. **DS18B20 (optional):** solder the supplied sensor to the `T` pads (`+` / `−` / data). On the
   C3 the data line is **D5 / GPIO1**; set `onewire_gpio` if you wire it on another board.
3. **Host board:** stack the shield on a LOLIN C3 mini (or D1-mini-compatible board), or run the
   four flying leads (OUT/IN/3V3/GND) to a SuperMini C6.
4. **Power** the host board over USB. The OpenTherm loop is powered by the boiler.
5. **First flash with the boiler attached:** hold **BOOT** (GPIO9) — the idle-LOW OpenTherm line
   otherwise pins the strapping input down and can block entry to flash mode.

## Notes and references

- Vendor page: <https://diyless.com/product/esp8266-thermostat-shield> (includes the DIYLess
  Arduino OpenTherm library and integration blog posts). This firmware uses its **own** ESP-IDF
  OpenTherm master, not the Arduino library — the shield is just the analog front-end.
- **Reference design (schematic + BOM):** Ihor Melnyk's OpenTherm Adapter,
  <https://ihormelnyk.com/opentherm_adapter> — the open design the shield productizes; the BOM
  above is taken from it. The OpenTherm library it pairs with: `github.com/ihormelnyk/opentherm_library`.
- Authoritative pin mapping in-repo: `components/board/board_lolin_c3_mini.c`,
  `components/board/board_supermini_c6.c`, and `components/board/README.md`.
- OpenTherm line behaviour and inversion rationale: `docs/esp32-c3-mini.md` and
  `docs/firmware-design.md`; note that **silence on the bus for > 5 s is read by the boiler as a
  shorted thermostat and it demands heat** — the bus must never be stopped by a peer failure.
- The BOM is taken from the reference design and cross-checked against the shield photo
  part-for-part; the SMD marking codes on the shield match the reference values. The only
  uncertainty left is one or two small resistors (LED series / pull-ups) the reference's minimal
  through-hole BOM does not list.
