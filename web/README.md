# Web UI

Preact + Vite + TypeScript single-page application for the OpenTherm thermostat
firmware.

It is not uploaded to the device. Vite gzips the bundle to three files and the
firmware build compiles them in with `EMBED_FILES`, so the UI and the firmware
are one artefact and an OTA can never leave a new binary serving an old UI. The
mechanism is `../components/web_assets/CMakeLists.txt`, which runs this build at
CMake's configure stage.

```sh
npm run dev      # dev server; proxies /api and /ws to the device
npm run dev:mock # dev server against an in-process boiler simulator -- no hardware. See mock/scenarios.md
npm run build    # three .gz files into dist/ -- the firmware build runs this itself
```

Point the dev server at a device with `VITE_DEVICE_IP` in `.env.local`. To develop
without a device, `npm run dev:mock` serves the firmware's `/api` and `/ws` contract
from a dynamic simulator (`mock/`) and needs no `.env.local`.

## Rules

- **No privileged endpoints.** Everything this UI calls must be reachable by any
  other client through the same API (see `../docs/firmware-design.md`). A path that exists
  only for a screen here is a bug in the API, not a shortcut.
- **`api/entities.ts` is generated**, not hand-edited: the single source of truth for
  entities is `tools/opentherm_ids.py` (CLAUDE.md, "One entity list, ever").
- **Secrets are never rendered into an input.** See `SecretField.tsx` -- the device
  returns a sentinel in place of a stored password and the field must stay empty.
