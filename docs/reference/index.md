# Reference

The reference section holds two kinds of material: a table generated from the firmware itself,
and the hardware source documents the firmware's board support was built from.

## Entities

[Entities](/reference/entities) lists every entity the firmware exposes — key, name, OpenTherm
Data-ID, codec, unit, device class, category, options and bounds. It is rendered at site build
time from the generated `web/src/api/entities.ts`, so it is always the list the firmware was built
with. It is never edited by hand; see [Entity registry](/development/entity-registry).

## Hardware source documents

The two boards the firmware is built for (one binary per chip), and the OpenTherm adapter both of
them drive. Each records
the chip and board facts with their sources, and how this firmware uses the pins. Where one of
them and the pages of this site disagree about how this firmware behaves, the site and the code
win; the one place pin numbers live is `components/board/`.

| Document | What it is |
| --- | --- |
| [LOLIN C3 mini](/reference/esp32-c3-mini) | The main board (ESP32-C3, 4 MB flash): the D-label → GPIO mapping read from the LOLIN schematic, the strapping pins, the native-USB pins that must stay free, and how the shield stacks onto it. |
| [ESP32-C6 SuperMini](/reference/esp32-c6-supermini) | The second target (ESP32-C6, 4 MB flash): its pads from the unbranded board schematic, the strapping and USB pins that differ from the C3, and the four-wire connection to the shield. |
| [DIYLESS OpenTherm master shield](/reference/diyless-opentherm-master-shield) | The OpenTherm master front-end both boards drive: its electronics and BOM, the line polarity, and which GPIO each shield line lands on per board. |

The vendor PDFs behind the C3 document (schematic, datasheet, dimensions) are kept in
`docs/public/manuals/esp32-c3-mini/` and served with the site; the C3 document links each one.
