# Web UI

The device serves its own single-page web UI from flash. It is a client of the public API and
nothing more: everything it shows comes from the [REST API](/interfaces/rest-api) and the
[WebSocket](/interfaces/websocket), and everything it does is a request any other client can make
with `curl`.

## No privileged handle

The rule is the project's, not the UI's: **if a screen needs a path of its own, the API is wrong.**
There is no header, cookie or route the browser gets that `curl` does not. The socket ticket is an
ordinary REST route under the ordinary policy; `/ws` carries exactly the values `GET /api/state`
carries and not one field more; and every page's write goes through the same
`POST /api/entities/{key}` or `POST /api/ops/{name}` that any client uses. All API calls go through
`request()` in `web/src/api/client.ts` — `api/registry.ts`, `api/control.ts`, `api/config.ts` and
`api/otRaw.ts` are seams of that one client, not second ones — so the error shape (`ApiError` with
the device's own `error` sentence and `field`) and the 10 s request timeout are the same everywhere.
The one exception is the firmware-update page, which calls `fetch("/update")` itself because an
upload is one long POST the timeout would cut off; it has no device route behind it (see
[Pages](#pages)).

## Stack and build

| | |
| --- | --- |
| Framework | Preact 10, `preact-router` 4, `@preact/signals` 2 |
| Language | TypeScript |
| Bundler | Vite, output as one IIFE script (`app.js`) and one stylesheet (`style.css`) |
| Source | `web/src/` |
| Output | `web/dist/index.html.gz`, `app.js.gz`, `style.css.gz` — gzipped at build time |
| Tests | `cd web && npm test` (`web/scripts/test.mjs`, Node 22.6 or newer) |

```sh
cd web
npm run dev        # dev server; proxies /api and /ws to VITE_DEVICE_IP from .env.local
npm run dev:mock   # dev server against an in-process boiler simulator (web/mock) -- no hardware
npm run build      # the real web gate -- tsc -b, then the three .gz files into dist/
```

The dev server proxies exactly `/api` and `/ws` — every path the firmware serves, and no other. The
mock mode serves the same contract from a simulator in `web/mock/`, scenarios in
`web/mock/scenarios.md`.

### Embedding

The UI is not uploaded to the device separately; it is compiled into the firmware image, so the UI
and the firmware are one artefact and an update can never leave a new binary serving an old UI.
`components/web_assets/CMakeLists.txt` runs `npm ci` when `web/node_modules` is missing and
`npm run build` at CMake **configure** time — `EMBED_FILES` is resolved while CMake configures, so
the `.gz` files must already exist then — and a custom target re-runs the build on every firmware
build, so an edit under `web/src` is picked up by the next `pio run`. A firmware build therefore
needs Node and npm (see [Building](/development/building)).

`web_assets()` hands the HTTP layer a table of three records:

| Path | `Content-Type` | Blob |
| --- | --- | --- |
| `/` | `text/html` | `index.html.gz` |
| `/app.js` | `application/javascript` | `app.js.gz` |
| `/style.css` | `text/css` | `style.css.gz` |

They are served verbatim with `Content-Encoding: gzip`; nothing is compressed at runtime. Any other
non-API GET returns the shell, so a reload on a client-side route works; an unknown `/api/…` path is
a JSON `404` instead (see [REST API](/interfaces/rest-api#the-write-gate-and-the-fallbacks)).

## Pages

Routes are declared in `web/src/main.tsx`; the navigation bar is `web/src/components/Nav`, which
also holds the dark/light theme toggle and the language switcher.

| Route | Nav label | Page | What it uses |
| --- | --- | --- | --- |
| `/`, `/control` | Control | `pages/control` — the default screen: what the executor is doing and why, the heating and hot-water controls, the boost, and a pixel-art scene of the boiler, radiator and pipes driven by the live values | `/ws`; `GET /api/control` every 5 s (the reason and countdowns no entity carries); `GET /api/config` (the flow band) and `GET /api/entities/dhw_setpoint` (the hot-water bounds), once per visit; `GET /api/entities/ch_enable` while the socket is down; `POST /api/entities/{key}`; `POST /api/ops/boost`, `boost_off` |
| `/boiler` | Boiler | `pages/boiler` — the raw bus table over all heard Data-IDs, decoded, plus the line test and the Data-ID sweep | `GET /api/ot/raw` every 2 s; `POST /api/ops/linetest`, `POST /api/ops/scan` |
| `/state` | State | `pages/state` — every entity the registry knows, its value and availability, with writable ones editable | `GET /api/state` every 2 s; `GET /api/entities` every 30 s (the live bounds); `POST /api/entities/{key}` |
| `/log` | Log | `pages/log` — the device's 80-line log ring | `GET /api/log` every 3 s |
| `/settings` | Settings | `pages/settings` — Wi-Fi, broker, device (name, UI password), executor settings, the MQTT room source. The time zone and the NTP server have no field: the form carries their loaded values and posts them back unchanged ([Settings with no card](/guide/configuration#settings-with-no-card)) | `GET`/`POST /api/config`, `POST /api/provision`, `GET /api/wifi/scan` |
| `/update` | Firmware Update | `pages/update` — pick a `.bin` and upload it | `POST /update` — see below |

Any unknown route falls back to the Control page. Each route sets its own document title.

The pages that poll keep **one request in flight at a time**: a device that dropped off would
otherwise collect a queue of hung requests. The Control page's refresh is single-flight with a
re-run, so a `/ws` change arriving mid-request is followed by one more read instead of being
answered with the document from before it.

::: warning The firmware-update page has nothing behind it
The device implements no upload route. The page posts `multipart/form-data` to `/update`. The
access gate checks the `Host` header first (a foreign one is `403`), then refuses the upload with
`415` before the password or the policy is asked — only `application/json` may carry a write
body — and the write gate would answer `501` in any case. Updating is done over the
serial port; see [Updating](/guide/updating) and [OTA and system](/device/ota-and-system).
:::

### Secrets in the settings form

`GET /api/config` returns the sentinel `__UNCHANGED__` in place of every stored secret, and
`SecretField.tsx` never renders it into an input — a user typing on the end of it would store
`__UNCHANGED__hunter2`. The box is shown empty and the sentinel stays in the form's state: an
untouched field posts the sentinel back and the device keeps the stored secret, a typed value
replaces it, and an emptied box posts `""` and clears it. The placeholder says which of those
empty-looking states the box is in. The executor settings and the room source are saved with
their own `POST /api/config` patches; the five executor-state values (heating season, CH and DHW
enable and setpoints) are not settings at all and are written through `POST /api/entities/{key}`
from the Control page.

## The live channel

The UI opens **one** WebSocket for the life of the page — `connectDevice()` in
`web/src/api/device.ts`, called once from `main.tsx` before the first render — not one per screen,
because the device has seven sockets in total. `DeviceSocket` (`web/src/api/ws.ts`) fetches a new
ticket from `POST /api/ws-ticket` for every connect, reconnects with jittered back-off from 1 s to
30 s, and treats 35 s without a frame as a dead socket. A `state` frame replaces the `deviceState`
signal and a `delta` merges into it; the connection badge in the navigation shows `connecting`,
`open` or `closed`.

Not every page lives on the socket. The Control page and its boiler scene read `deviceState`, and
fall back to polling the CH command while the socket is down. The State and Boiler pages poll on
purpose: they show availability, age and the raw bus, which no frame carries. On the setup access
point the ticket route is refused (only provisioning is accepted there), so the socket stays closed
and the pages fall back to polling.

## Languages

The UI speaks English, German, Dutch and Ukrainian. The firmware itself is English everywhere, and
so is everything Home Assistant sees.

| Part | Where | How |
| --- | --- | --- |
| UI strings | `web/src/i18n/catalogs/<lang>/*.ts` — `boiler`, `common`, `control`, `log`, `nav`, `settings`, `state`, `tokens`, `update` | One catalog per language, English the reference; a key missing from a catalog falls back to English |
| Entity names | `web/src/i18n/entityNames.generated.ts`, read by `entityName()` | **Generated** by `tools/render_entity_names_ts.py` from `tools/opentherm_ids.py` (English) and `tools/entity_names_i18n.py` (DE/NL/UK) |
| Firmware text | `web/src/i18n/firmwareText.ts`, `detail.ts` | Best-effort translation of log lines and error sentences by pattern; an unmatched line stays English |

- **The choice is the user's, stored in `localStorage` under `ot.lang`.** There is no
  auto-detection from the browser; an unset or unknown value is English. The choice is mirrored onto
  `<html lang>`.
- **Entity names are an overlay, keyed by the entity key.** The English name in
  `tools/opentherm_ids.py` is canonical — it is what discovery sends to Home Assistant — and the
  three translations are a web-only overlay that never touches discovery. The generator refuses a
  missing translation, and `tools/tests/test_entity_names_i18n.py` pins that the overlay's keys
  match the registry exactly and every row has all three languages.
- **Firmware text is not a contract.** The device sends English prose, not machine codes, and
  ESP-IDF's own log lines are not the project's to restructure. A firmware sentence that changes
  silently reverts that one line to English until its pattern is updated.

## Why the UI is embedded rather than uploaded

A UI stored on a filesystem partition can drift from the firmware: an update that rewrites one and
not the other leaves a page calling routes that no longer exist, or no longer calling ones that do.
Built into the binary, the two are versioned, flashed and rolled back together, and the partition
table needs no filesystem at all. The cost is a firmware build that needs Node, and the UI's share of
the image — the three files are about 80 KB gzipped, nearly all of it `app.js`.

Source: [`web/README.md`](../../web/README.md),
[`components/web_assets/README.md`](../../components/web_assets/README.md).
