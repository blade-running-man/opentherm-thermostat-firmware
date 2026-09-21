# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Generates the entity registry from tools/opentherm_ids.py.

Four outputs, all forbidden to edit by hand. A bug in an output is fixed HERE, not in the
output: editing a generated file is a second list of entities, which CLAUDE.md forbids,
and it will silently disappear on the next build.

This file is the driver: it owns the command line and where each output is written. The
model is registry_model.py, and each output has its own renderer beside it (render_c.py,
render_ts.py), fed the same build_entities(). A further output -- the Home Assistant discovery
-- is one more render_*.py module and one more entry in `outputs` in main().
"""
import argparse
import sys
from pathlib import Path

# The siblings are imported by name. Run as a script (regenerate.py, a shell) tools/ is
# already sys.path[0]; tools/tests/conftest.py loads this file by path from the repo root,
# where it is not, and without this line the imports below fail only under pytest.
if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

# build_entities, CONTROLS, KINDS, CODECS and table are re-exported for tools/tests, which
# reach the whole generator through this one module (conftest's `gen`); `table` must be the
# model's own object, since a test monkeypatches it and then calls build_entities().
from registry_model import CODECS, CONTROLS, KINDS, build_entities, table  # noqa: E402,F401
from render_c import render_c_header  # noqa: E402
from render_discovery import render_discovery  # noqa: E402
from render_entity_names_ts import render_entity_names_ts  # noqa: E402
from render_ts import render_ts_types  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
C_OUT = ROOT / "components" / "ot_registry" / "include" / "registry_generated.h"
# ONLY entities.ts is generated. The neighbouring web/src/api/types.ts is hand-written:
# what is there is the device configuration -- WiFi, broker, password -- about which the
# table knows nothing, and generating into it would erase those types on the very first run.
TS_OUT = ROOT / "web" / "src" / "api" / "entities.ts"
# The Home Assistant discovery documents, beside the component that fills them in.
HA_OUT = ROOT / "components" / "ot_ha" / "include" / "ha_generated.h"
# The entity display names in every UI language -- a web-only overlay (HA sees English only).
NAMES_OUT = ROOT / "web" / "src" / "i18n" / "entityNames.generated.ts"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="non-zero exit code if the outputs are stale")
    args = parser.parse_args()

    outputs = {C_OUT: render_c_header(), TS_OUT: render_ts_types(),
               HA_OUT: render_discovery(), NAMES_OUT: render_entity_names_ts()}

    if args.check:
        stale = [p for p, content in outputs.items()
                 if not p.exists() or p.read_text() != content]
        for path in stale:
            print(f"stale: {path.relative_to(ROOT)}", file=sys.stderr)
        if stale:
            print("run: python3 tools/generate_registry.py", file=sys.stderr)
            return 1
        return 0

    for path, content in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        print(f"wrote {path.relative_to(ROOT)} ({len(content.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
