# Building

The firmware is an ESP-IDF project built with PlatformIO. Each board environment produces the
one universal binary for its chip; the web UI and the entity registry are built as part of it,
so there is no manual step to remember. This page covers the toolchain, the three environments,
what runs inside `pio run`, building several things at once, flashing, and building this
documentation site.

## Toolchain

| Tool | Version | Where it is decided |
| --- | --- | --- |
| Platform | `pioarduino/platform-espressif32`, pinned to `#55.03.311` | `platformio.ini`, both board environments |
| ESP-IDF | 5.5.5 — the framework the pinned platform installs | the platform pin |
| PlatformIO Core | not pinned by this repository | — |
| Node.js and npm | Node 22.6 or newer; `npm` must be on the `PATH` | `web/package.json` `engines`; `components/web_assets/CMakeLists.txt` |
| Python | 3 — the interpreter PlatformIO itself runs on executes the registry generator | `tools/regenerate.py` |

`pio` is often not on the `PATH`; PlatformIO installs it at `~/.platformio/penv/bin/pio`.

### Why the platform is pinned

`platform = https://github.com/pioarduino/platform-espressif32.git#55.03.311` is a deliberate
pin, and the pin carries load. A bare git URL floats: the first build clones whatever is on the
default branch at that moment, and that once moved the toolchain out from under a design that
had been verified against a different ESP-IDF tree, with no change in the repository at all.

Move the pin only on purpose: re-check the ESP-IDF behaviour the design relies on against the
new tree, and say so in the change.

The word `pioarduino` in the platform URL is the vendor's name and is irrelevant. What must
never appear is `framework = arduino`; both board environments say `framework = espidf` (see
[Conventions](/development/conventions)).

## The environments

`platformio.ini` has three environments.

| Environment | Board definition | Purpose |
| --- | --- | --- |
| `lolin_c3_mini` | `lolin_c3_mini` (ESP32-C3) | the main board; the DIYLESS shield plugs onto it |
| `supermini_c6` | `esp32-c6-devkitm-1` (ESP32-C6) | the second target, the ESP32-C6 SuperMini, wired to the shield |
| `native` | `platform = native` | the host test suites (see [Testing](/development/testing)) |

The two board environments are identical apart from the board: the same pinned platform, the
same `partitions.csv`, `monitor_speed = 115200` with the `esp32_exception_decoder` filter,
`test_ignore = *` (no host suite is ever built for a board), the `pre:tools/regenerate.py` hook
and `-DPROJECT_NAME_STR="opentherm-thermostat"`.

**The board descriptor is chosen by the chip, not by the environment name.**
`components/board/CMakeLists.txt` reads `IDF_TARGET` and compiles `board_lolin_c3_mini.c` for
`esp32c3` or `board_supermini_c6.c` for `esp32c6`; any other target stops the build with a
`FATAL_ERROR` naming the missing descriptor, rather than an empty component and a link failure on
`board_get()`. This allows exactly one board per chip. See
[Board](/device/board).

### Why `esp32-c6-devkitm-1` and not `devkitc-1`

The platform catalogue has no SuperMini definition. The two Espressif C6 definitions are
byte-for-byte identical except for the flash: `devkitm-1` is the 4 MB part, `devkitc-1` the
8 MB part. Choosing `devkitc-1` would silently overstate the flash and break the partition
table. The SuperMini is also sold with 8 MB under the same name, so check `esptool flash_id` on
your own unit before the first flash.

## Build the firmware

```sh
pio run -e lolin_c3_mini     # main board; the SPA build is part of it
pio run -e supermini_c6      # second target, ESP32-C6 SuperMini
```

::: warning Judge the build by its exit code
`pio run … | tail` returns the exit code of `tail`, not of the build, so a failed build looks
successful. Write the output to a file and check `$?`:

```sh
~/.platformio/penv/bin/pio run -e lolin_c3_mini > /tmp/build.log 2>&1; echo $?
```
:::

### What runs as part of `pio run`

Two steps run before anything compiles, so neither can be forgotten.

1. **`tools/regenerate.py`** — a PlatformIO `pre:` hook (`extra_scripts`). It runs
   `tools/generate_registry.py` with the interpreter that started SCons (`sys.executable`, not
   whatever `python3` is on the `PATH`) and fails the build if generation fails. A stale registry
   would not break the build — it would build and travel to the boiler as firmware whose entities
   diverge from the table — so it is regenerated every time, not checked and trusted. See
   [Entity registry](/development/entity-registry).
2. **The SPA build**, in `components/web_assets/CMakeLists.txt`, at CMake **configure** time.
   It stops with a `FATAL_ERROR` if `npm` is missing, runs `npm ci` in `web/` only when
   `web/node_modules` does not exist, then `npm run build`. `EMBED_FILES` is resolved by
   `idf_component_register` while CMake configures, so `web/dist/index.html.gz`, `app.js.gz`
   and `style.css.gz` have to exist by then; a custom target (`web_spa`, part of `ALL`) re-runs
   the build on every `pio run`, so an edit under `web/src` is picked up next time.

Embedding the three files, rather than shipping them in a LittleFS partition, makes the UI and
the firmware one image, flashed together: a device can never run a firmware with a UI built for
another. That is also why `partitions.csv` has no filesystem — two 1984 KiB application slots,
with `ota_0` held at `0x10000` (see [OTA and system](/device/ota-and-system)).

#### Why the hook is not in `[env:native]`

The host tests must see the tree exactly as it is. `tools/tests/test_registry.py` compares the
committed outputs with what the generator renders now
(`test_the_files_on_disk_match_what_the_generator_renders`); regenerating before the tests run
would make that divergence check pass by construction.

### `sdkconfig.defaults` and the generated files

`sdkconfig.defaults` is the committed source of every Kconfig choice: the custom partition table,
4 MB flash, the USB Serial/JTAG console (the LOLIN C3 mini has no UART bridge, so without it the
console is silent), WebSocket support and the WebSocket pre-handshake callback, among others —
each with its reason beside it. A build writes the full configuration to `sdkconfig.<env>` in the
project root. `sdkconfig.lolin_c3_mini` is `.gitignore`d; `sdkconfig.supermini_c6` is
tracked. A value set by hand (through `menuconfig`) in a generated file overrides the defaults;
deleting the generated file regenerates it from `sdkconfig.defaults`.

## Parallel work

Several builds can run side by side — two agents, or a firmware build beside a web task — but
three things are shared, and they are lifted differently:

| Shared thing | Liftable? | How |
| --- | --- | --- |
| The PlatformIO build directory (`.pio`) | yes | `PLATFORMIO_BUILD_DIR=/tmp/pio-<task>` moves the build away wholesale; the main `.pio` is untouched. Each parallel build gets its own. |
| `web/dist` | yes, by a worktree | every firmware build runs `npm run build` into the repository's `web/dist`, and `PLATFORMIO_BUILD_DIR` does not move it. A firmware build running beside a web task goes into its own git worktree. |
| A header both tasks edit | no | an edit by one breaks the build of the other. Non-overlapping files are a condition of parallel work, not a wish. |

```sh
PLATFORMIO_BUILD_DIR=/tmp/pio-c6 ~/.platformio/penv/bin/pio run -e supermini_c6 > /tmp/pio-c6.log 2>&1; echo $?
```

## The web UI on its own

```sh
cd web
npm run dev        # dev server; proxies /api and /ws to a real device
npm run dev:mock   # dev server against the built-in mock device, no hardware needed
npm run build      # tsc -b, then the production build into web/dist
```

The dev server proxies exactly `/api` and `/ws` (the latter with the WebSocket upgrade) to
`VITE_DEVICE_IP`, read through Vite's `loadEnv` — put it in `web/.env.local`; unset, it falls
back to `192.168.50.92` (`web/vite.config.ts`). The UI talks to the device only through its
public API, so nothing else needs proxying. In `dev:mock` mode there is no proxy: the plugin in
`web/mock/` answers those paths in-process.

`npm run build` emits one IIFE `app.js`, one `style.css` and `index.html`, gzips them and deletes
the originals — the three `.gz` files the firmware embeds. It is also the real type-check of the
SPA; `tsc --noEmit` is not (see [Testing](/development/testing) and [Web UI](/interfaces/web-ui)).

## Flashing

```sh
pio run -e lolin_c3_mini -t upload
```

Flashing a device is the owner's business — **ask first**. Before the first flash of a C6
SuperMini, check its flash size (above). Installing a release and later updates are described in
[Installation](/guide/installation) and [Updating](/guide/updating).

## This documentation site

The site is VitePress, rooted at `docs/` (config: `docs/.vitepress/config.mts`; the `vitepress`
version is pinned exactly in `docs/package.json`).

```sh
cd docs
npm install
npm run docs:dev      # local preview with hot reload
npm run docs:build    # static build into docs/.vitepress/dist
npm run docs:preview  # serve the static build
```

Three environment variables change where the build goes; all are optional:

| Variable | Effect |
| --- | --- |
| `DOCS_BASE` | the site's base path, for serving under a sub-path such as `/<repo>/`; default `/` |
| `DOCS_OUT_DIR` | the output directory instead of `docs/.vitepress/dist` |
| `DOCS_CACHE_DIR` | the Vite cache directory instead of the default under `docs/.vitepress/` |

The last two exist so that several builds can run at once without sharing state:

```sh
cd docs && DOCS_CACHE_DIR=/tmp/vp-x/cache DOCS_OUT_DIR=/tmp/vp-x/dist npm run docs:build > /tmp/vp-x.log 2>&1; echo $?
```

What the config enforces:

- **Dead links fail the build.** `ignoreDeadLinks` lets through only links that genuinely leave
  the built site: relative links with two or more leading `../`, which from a section page always
  leave `docs/` (to a component README or a source file). The vendor PDFs live in
  `docs/public/manuals/` and are served with the site, so links to them are ordinary links.
  VitePress checks the page a link names, not its `#anchor`: a heading renamed under a live link
  does not fail the build. A root page such as `index.md` must therefore not
  link `../` — the check sees the link, not the page it is on.
- **Links between site pages** are absolute site paths without `.md`:
  `[Executor](/architecture/executor)`.
- **The entity tables are generated at build time.** [Entities](/reference/entities) and the
  [Boiler](/boiler/) page read `web/src/api/entities.ts` — the generator's committed TypeScript
  output — through the data loader `docs/reference/entities.data.ts`. The docs build does not run
  the Python generator; `python3 tools/generate_registry.py --check` says whether the committed
  output is stale.
