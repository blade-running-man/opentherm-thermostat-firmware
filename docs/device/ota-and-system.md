# OTA and system

**The firmware has no over-the-air update path.** The flash is laid out for one (two application
slots and an `otadata` partition), the web UI has an Update page, and the status LED has a tier
reserved for it, but no handler writes an image and nothing in the firmware selects a boot slot.
Firmware is installed over USB (see [Installation](/guide/installation) and
[Updating](/guide/updating)). This page states exactly what exists: the partitions, how the device
boots and restarts, the reset reason, the in-RAM log ring, and the system information the device
reports.

## What exists for OTA, and what does not

| Piece | State |
| --- | --- |
| `partitions.csv`: `otadata`, `ota_0` at `0x10000`, `ota_1` at `0x200000`, 1984 KiB each | present; see [Board](/device/board#partition-table) |
| A route that receives an image (`/update`, `/api/ota`) | **absent**. `ot_http` registers no such route; no component calls `esp_ota_*` |
| Boot-slot selection, image confirmation, rollback | **absent**. Nothing calls the OTA API, and `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is not set |
| Image signature verification | **absent**: secure boot and signed apps are off |
| The web UI's **Update** page (`web/src/pages/update/`) | present, but its upload cannot succeed (below) |
| `ot_led_note_ota()` and the LED's fast-blue `ota` tier | present and host-tested, **dormant**: nothing calls `ot_led_note_ota()`, so the flag is always false |

### What the Update page does

The page posts the chosen `.bin` as `multipart/form-data` to `POST /update`. On the device that
request meets the generic write handling every unregistered path gets:

1. The access gate runs first. A body-bearing write must declare `Content-Type: application/json`
   (the CSRF guard), so a multipart upload is answered **`415 Unsupported Media Type`** before any
   handler runs. The page reports "Upload failed: Unsupported Media Type".
2. Even a request that passed the gate would end in the catch-all write handler, which answers
   `501 Not Implemented` with `{"error":"not implemented yet"}`, or `403` from the access policy on a
   device without a password.

Nothing is written to flash, nothing restarts. The page's "Device is rebooting…" texts describe
behaviour the firmware does not have.

### Why an upload would need more than a handler

The access policy states the constraint an update path would have to meet: an update endpoint that
answers before anyone has claimed the device is unauthenticated code execution on the LAN, and this
firmware does not verify image signatures. That is why the policy keeps every write other than the
configuration, the commands and the WebSocket ticket **closed** on a device without a password,
and why an update route is not one more exempt path. See [Security](/architecture/security).

## Boot

`app_main` (`src/main.cpp`) only puts the components in order, and the order carries load:

1. `ot_log_install()`, **first**, so the boot lines land in the ring the web UI reads, not only on
   the USB console the owner may not have connected.
2. A line with the project name and the application version (`esp_app_get_description()`), then
   NVS (erased and re-initialised when it reports no free pages or a new version), the board line
   and the size of the compiled-in web UI.
3. The OpenTherm master and bus, **before the network**: a router that answers slowly must not
   delay the first conversation, because a slave that hears nothing from the master for more than
   5 s treats the thermostat as shorted and demands heat. The state model is zeroed before the bus
   starts, so the boiler's first answer is not erased.
4. The network (`ot_net_start()`).
5. The executor (`ot_thermostat_start()`), after the bus and the network, before HTTP.
6. The MQTT link, after the executor whose commands it carries, before HTTP, which reads its
   counters.
7. The HTTP server.
8. The DS18B20 task and the status LED task, last and at the lowest priority, each only when the
   board has the part.

**Nothing in that sequence reboots or stops because a step failed.** A master that did not
initialise, a network that did not start, an executor, MQTT link or HTTP server that did not start
are each logged with `ESP_LOGE` and stepped over: whatever still works, the owner is still owed.
The image boots from `ota_0`: with `otadata` empty, the bootloader takes the first application slot.

## Restarts

There is **no software restart in the firmware**: no endpoint, button gesture or timer calls
`esp_restart()`, and nothing restarts because a peer (Wi-Fi, broker, NTP server, Home Assistant) is
absent. The device restarts only on power, the board's reset button, or a fault the chip handles
itself. Those use ESP-IDF's defaults, as recorded in the checked-in `sdkconfig.supermini_c6`:

| Fault | Behaviour |
| --- | --- |
| panic | print and reboot, no delay (`CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT`) |
| interrupt watchdog | enabled; resets |
| task watchdog | enabled, 5 s, watches the idle task; **prints, does not panic** (`CONFIG_ESP_TASK_WDT_PANIC` off) |
| brown-out | detection enabled; resets |

### The reset reason

`esp_reset_reason()` is read once, by the executor at start (`otth_restore()` in
`ot_thermostat_persist.c`), and logged in one line, for example `reset reason panic: watchdog
overdue and part-hour restored (…)`. It is the one clue a reboot loop leaves. The names come from
`ot_control_io_reset_name()`: `unknown`, `poweron`, `ext`, `sw`, `panic`, `int_wdt`, `task_wdt`,
`wdt`, `deepsleep`, `brownout`, and `other` for anything else. The same reason decides whether the
executor's `RTC_NOINIT` record is believed (see
[Configuration storage](/device/config-storage#the-rtc-noinit-record)). The reset reason is not
reported by any API; it is in the log.

## The log ring

`ot_log` keeps the most recent device log lines in RAM and `GET /api/log` serves them as a JSON
array of strings, oldest first.

- **A fixed ring:** `OT_LOG_LINES` = 80 lines of `OT_LOG_LINE_LEN` = 160 bytes, about 13 KB,
  roughly the last minute of a busy boot. When it fills, the **oldest** line goes: during an
  incident the interesting lines are the most recent ones, and a log that stops recording when full
  is worse than none. The sizes are round numbers, to be raised only against a measured RAM
  figure.
- **`ot_log_install()`** hooks ESP-IDF's logging with `esp_log_set_vprintf()`: each line is
  formatted from a `va_copy` into the ring and the untouched argument list is forwarded to the
  previous handler, so the USB console keeps working. The console is the only channel when the
  network is what broke; the ring is an addition, never a replacement.
- **`ot_log_write()`** never allocates and never blocks, so any task may log, including at boot.
  Longer lines are truncated to a slot; trailing `\n`/`\r` are stripped; empty lines are dropped.
- **`ot_log_render()`** is all-or-nothing: it returns 0 when the array does not fit, and the handler
  answers `500 {"error":"log does not fit"}` rather than half a document behind `200`. It escapes
  `"`, `\`, `\n`, `\r`, `\t`, and **every byte outside printable ASCII as `\u00xx`**. That is the
  measured reason every log string in the firmware is English: a non-ASCII line comes back through
  `GET /api/log` unreadable and ungreppable. Its escaper is its own, not `ot_api`'s, so the log
  renders even when the entity model is in trouble.
- The ring has no lock of its own; it relies on being written by whichever task logs and read by
  the HTTP task, which it tolerates. RAM only: a reset clears it. Host suite `test/test_log`.

Two rules keep the ring useful. A recurring failure logs on the transition and then sparingly (the
DS18B20 task warns on the first failed read and then every 100th; MQTT refusals at most once a
minute with a count), so one stuck sensor cannot evict everything else. And nothing that holds a
secret is ever logged: the ring is served to whoever passes the access policy.

## System information

There is no separate system document. What the device reports about itself:

| Where | What |
| --- | --- |
| `GET /api/status` | the network and provisioning state, the device identity (`device_id`, `mac`, `sw_version` from `esp_app_get_description()`), and the MQTT block (`configured`, `connected`, `published`, `commands`, `rejected`, `reconnects`, `last_connack`) |
| `GET /api/ot/raw` | the bus's own counters: `uptime_ms`, `cycles`, `ok`, `failed`, `overdue`, `answering`, timeouts and frame errors, and the raw table of every Data-ID seen |
| `GET /api/control` | the executor's live document |
| `GET /api/log` | the log ring, including the boot lines (version, board, web UI size, reset reason) |

The fields are documented on [REST API](/interfaces/rest-api). The Home Assistant device carries
the same `sw_version` and MAC through discovery.
