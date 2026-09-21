# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""The DE/NL/UK entity-name overlay covers exactly the registry, and the generator refuses
a mismatch. This is the Python side of the same guard web/src/i18n/tests/entityNames.test.ts
keeps on the emitted file."""
import importlib.util
import sys
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent.parent


def _load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def test_overlay_keys_match_registry_exactly(gen):
    from entity_names_i18n import ENTITY_NAMES_I18N

    registry = {e.key for e in gen.build_entities()}
    overlay = set(ENTITY_NAMES_I18N)
    assert overlay == registry, (
        f"missing: {sorted(registry - overlay)}; orphan: {sorted(overlay - registry)}"
    )


def test_every_overlay_row_has_all_three_languages(gen):
    from entity_names_i18n import ENTITY_NAMES_I18N, LOCALES

    for key, row in ENTITY_NAMES_I18N.items():
        for loc in LOCALES:
            assert row.get(loc, "").strip(), f'{key} has no "{loc}" name'


def test_render_raises_on_a_missing_translation(gen, monkeypatch):
    _load("opentherm_ids")
    ren = _load("render_entity_names_ts")
    monkeypatch.delitem(ren.ENTITY_NAMES_I18N, "fault")
    with pytest.raises(ValueError, match="missing translations"):
        ren.render_entity_names_ts()
