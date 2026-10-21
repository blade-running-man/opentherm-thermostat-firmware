# Updating

**There is no over-the-air update; the device is updated by USB.** The flash is laid out for
over-the-air updates and the web interface has an update page, but nothing on the device accepts an
image. A new version is installed the same way as the first one: built from source and flashed. This
page says exactly what exists, and how to update without losing your settings.

## What exists, and what does not

| Piece | State |
| --- | --- |
| Two application slots in the partition table (`ota_0`, `ota_1`, 1984 KiB each) and the `otadata` partition that selects between them | **Present** (`partitions.csv`) |
| An HTTP endpoint that receives an image and writes it to the other slot | **Absent.** No route accepts an upload; there is no `esp_ota_*` call anywhere in the firmware |
| The web interface's **Update** page (`/update`), with a file picker for a `.bin` | **Present, but nothing behind it** — see below |
| Rollback of an image that does not come up | **Absent** — there is no second image to roll back to |
| The status LED's "update in progress — do not power off" pattern (fast blue blink) | Drawn by the LED code, but nothing ever raises it |
| Any restart command | **Absent.** No API reboots the device |

The application always runs from `ota_0` at `0x10000`, which is where a USB flash writes it.

### What the Update page does

The page sends the chosen file as a multipart form to `POST /update`. Every request first passes
the access gate: it checks the `Host` header (a foreign one is `403`), then requires every write
carrying a body to declare `Content-Type: application/json`, and only then asks for the password
and the access rules. A write the firmware does not implement falls through to one catch-all
handler. A multipart upload from the page therefore fails at once:

- with `415 Unsupported Media Type` — the page shows **Upload failed: Unsupported Media Type**; or,
  for a large file, the device answers and closes while the browser is still sending, and the page
  shows **Upload failed — connection lost (device may be rebooting)**. The device is **not**
  rebooting; nothing was written.

A JSON `POST` to `/api/ota` answers `403` on a device without a password, `401` when a password is
set and the request does not carry it, and `501 {"error":"not implemented yet"}` only when it is
authenticated — the route has no code behind it.

::: info Why the partitions exist anyway
The partition table carries two application slots although only `ota_0` is used, because changing
the table is a full erase, settings included: with the slots in place an update handler needs no
repartition. `ota_0` must stay at `0x10000` and `nvs` is 16 KiB (not the stock 24 KiB) for the same
reason: a larger `nvs` pushes the first slot off `0x10000`. The layout is in
[OTA and system](/device/ota-and-system).
:::

## Updating over USB

```sh
git pull
pio run -e lolin_c3_mini -t upload     # or: -e supermini_c6
pio device monitor -e lolin_c3_mini    # watch it come back
```

The upload writes the bootloader, the partition table and the application. It does **not** touch
the NVS partition, so the Wi-Fi, the web interface password, the broker and every executor setting
survive. The first lines on the console after the restart name the new version.

- **The board must be reachable over USB.** In an installed enclosure that means opening it or
  routing a cable. If the upload cannot find the board, use the download mode described in
  [Installation](/guide/installation#flashing); on the C3, a connected boiler can block it.
- **Do not use `-t erase`** for an update: it wipes the settings with everything else.

::: warning A restart is a brief demand for heat
Any restart takes longer than the five seconds an OpenTherm boiler waits before it reads a silent
master as a short-circuited thermostat, so **the boiler briefly demands heat** while the device is
being flashed and while it boots. This is unavoidable and harmless when short; what to check is
that the boiler returns to normal after the first conversation once the device is back. Update with
the boiler in sight, and not in the middle of a long flash or a cold night if you can help it.
:::

After a restart in Home Assistant mode the device waits in `ha_waiting` (CH off) for Home
Assistant's next CH command, and enters the failsafe if none arrives within `watchdog_s`. How the
watchdog's count survives soft resets, and why a power-on reset starts it from zero, is in
[Failure model](/architecture/failure-model).

## Going back to an older version

Check out the older version and flash it the same way. If the newer firmware had moved the stored
settings to a layout the older one does not understand, the older one does not guess: it runs on
what it can read, marks the configuration **read-only**, and refuses every settings write until the
newer firmware is back or the settings are erased. The Settings page says so in a banner at the top
([Configuration](/guide/configuration#when-the-stored-settings-cannot-be-used)).

## Starting from scratch

To clear every setting and install fresh:

```sh
pio run -e lolin_c3_mini -t erase
pio run -e lolin_c3_mini -t upload
```

The device then comes up as on its first day, with its setup network on the air
([First setup](/guide/first-setup)).
