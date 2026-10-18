# web_assets — the SPA embedded via `EMBED_FILES`, built at the CMake configure stage

## Purpose

The single-page web UI compiled into the firmware image as three gzipped files
(`index.html.gz`, `app.js.gz`, `style.css.gz`), exposed to the firmware as a
NULL-terminated table of `web_asset_t` records that the HTTP layer serves
verbatim with `Content-Encoding: gzip`. Because the UI is part of the firmware
binary, the UI and the firmware are one artefact: an OTA update cannot leave a
new binary serving an old UI.

## Responsibility

Owns:

- The build-time embedding of the SPA into the firmware binary.
- The mapping from each embedded blob to its serving metadata: request path,
  `Content-Type`, and the `{start, len}` byte range of the gzipped bytes
  (`web_assets.c`).
- Triggering the SPA build (`npm run build`, and `npm ci` when `node_modules`
  is absent) so an edit under `web/src` is picked up on the next `pio run`.

Does NOT do:

- Serve HTTP. There is no socket code here; the HTTP handler calls
  `web_assets()` and does the serving, including setting `Content-Encoding: gzip`.
- Compress at runtime. Everything is pre-gzipped by Vite at build time; the
  handler never compresses anything.
- Contain the SPA source. The Preact/Vite/TypeScript source lives in `web/`
  (outside this component) and is built into `web/dist`; only the three built
  `.gz` files are embedded.

## Public API

Declared in `include/web_assets.h`, C linkage (wrapped in `extern "C"` so the
C++ entry point links against the unmangled C symbol):

```c
typedef struct {
    const char          *path;          // "/", "/app.js", "/style.css"
    const char          *content_type;
    const unsigned char *start;
    size_t               len;
} web_asset_t;

const web_asset_t *web_assets(void);
```

`web_assets()` returns a pointer to a NULL-terminated table (a sentinel record
with `.path == NULL`); the caller iterates until `.path` is `NULL`. The three
live entries map:

| path         | content_type             | blob              |
| ------------ | ------------------------ | ----------------- |
| `/`          | `text/html`              | `index.html.gz`   |
| `/app.js`    | `application/javascript` | `app.js.gz`       |
| `/style.css` | `text/css`               | `style.css.gz`    |

Each record's `start`/`len` point at the gzipped bytes; `len` is computed as
`_end - _start`. Callers must serve these bytes as-is and add
`Content-Encoding: gzip` (they are already compressed).

## Implementation

The embedded symbols come from ESP-IDF's `EMBED_FILES`, which turns each file
name into linker symbols by replacing dots and slashes with underscores and
adding a `_binary_` prefix and `_start`/`_end` suffixes. `web_assets.c` declares
them via `asm(...)` aliases (e.g. `_binary_index_html_gz_start`) and fills a
`static web_asset_t s_assets[4]` (three assets plus the NULL sentinel) on each
call to `web_assets()`.

Build/embed mechanism (`CMakeLists.txt`):

- `WEB_DIR = ../../web`, `WEB_DIST = ../../web/dist`.
- `find_program(npm)` — a `FATAL_ERROR` if npm is missing, because the UI is
  compiled into the firmware and building the project needs Node.
- If `web/node_modules` is absent, run `npm ci --no-fund --no-audit`; a non-zero
  result is fatal.
- Run `npm run build` at **configure** time (`execute_process`), so the three
  `.gz` files exist before `idf_component_register` resolves `EMBED_FILES`.
  A non-zero result is fatal.
- `idf_component_register` registers `web_assets.c`, the `include` dir, and the
  three `EMBED_FILES` from `web/dist`.
- An additional `add_custom_target(web_spa ALL ...)` re-runs `npm run build` and
  is made a dependency of the component lib, so an edit under `web/src` is
  rebuilt on the next `pio run`.

Invariants / DO NOT:

- The SPA is built at **configure** time, not as a plain build rule, because
  `EMBED_FILES` is resolved by `idf_component_register` while CMake configures —
  the `.gz` files must already exist at that point.
- The embedded blobs are gzipped; do not compress again at runtime and do not
  strip the `Content-Encoding: gzip` the handler adds.
- Do not rename an embedded file without updating `web_assets.c`: the
  `_binary_*` symbol names are derived mechanically from the file names, so a
  rename changes the symbols and requires matching the `asm(...)` aliases.

## Tests

No dedicated host suite for this C component. The web test suites live under
`web/` (`web/scripts/test.mjs`, run via `cd web && npm test`); the web build
gate is `cd web && npm run build`, which is exactly what this component invokes
to produce the embedded `.gz` files.

## Notes

- Building the SPA inside the firmware build removes any manual "don't forget to
  rebuild the front end" step — the front end cannot drift from the binary.
- Embedding the three files (~15 KB gzipped) as one artefact was chosen over a
  separate filesystem partition specifically so an OTA cannot leave a new binary
  serving a stale UI.
