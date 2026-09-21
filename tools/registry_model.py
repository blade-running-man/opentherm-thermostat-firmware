# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""The registry model: tools/opentherm_ids.py loaded, validated and flattened into Entity rows.

Every renderer (render_c.py, render_ts.py, and the Home Assistant discovery beside them)
walks the SAME build_entities() list, so no output can learn a table shape, a control number
or a validation rule the others do not know. What is specific to one output language -- its
literals, its banner, its enum encoding of a tuple -- lives in that renderer, not here.
"""
import importlib.util
import sys
from pathlib import Path

# The C enums are numbered by these tuples' order: APPEND ONLY, an insertion renumbers every
# value after it. Codec "none" is a synthetic row's: it has no wire encoding at all.
KINDS = ("sensor", "binary", "number", "switch", "enum")

CODECS = ("f88", "u16", "s16", "u8_hb", "u8_lb", "s8_hb", "s8_lb", "flag", "none")

# ot_control_cmd_t (ot_control.h); test_ot_command pins the two. 0 means "no command".
CONTROLS = {"CH_ENABLE": 1, "CH_SETPOINT": 2, "DHW_ENABLE": 3, "DHW_SETPOINT": 4, "SEASON": 5}


def control_number(name) -> int:
    if name is not None and name not in CONTROLS:
        raise SystemExit(f"unknown control command {name!r}; known: {sorted(CONTROLS)}")
    return CONTROLS.get(name, 0)


def _load_table():
    spec = importlib.util.spec_from_file_location(
        "opentherm_ids", Path(__file__).parent / "opentherm_ids.py")
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolve string annotations through sys.modules -- register it
    # BEFORE exec_module, otherwise `from __future__ import annotations` breaks the load.
    sys.modules["opentherm_ids"] = module
    spec.loader.exec_module(module)
    return module


table = _load_table()


class Entity:
    """One registry row, flattened out of whichever table shape produced it.

    The flattening happens HERE and only here: every renderer walks this list, so no two
    outputs cannot diverge because one renderer learned about a table shape the other one
    does not know about.
    """

    def __init__(self, key, name, kind, data_id, codec, *, unit=None,
                 device_class=None, state_class=None, icon=None, entity_category=None,
                 readable=True, writable=False, write_id=None, flag_high_byte=False,
                 flag_bit=0, min_value=None, max_value=None, default=None,
                 bounds_from=None, control=0, options=()):
        self.key = key
        self.name = name
        self.kind = kind
        self.data_id = data_id
        self.codec = codec
        self.unit = unit
        self.device_class = device_class
        self.state_class = state_class
        self.icon = icon
        self.entity_category = entity_category
        self.readable = readable
        self.writable = writable
        self.write_id = write_id
        self.flag_high_byte = flag_high_byte
        self.flag_bit = flag_bit
        self.min_value = min_value
        self.max_value = max_value
        self.default = default
        self.bounds_from = bounds_from
        self.control, self.options = control, options


def build_entities() -> list[Entity]:
    out: list[Entity] = []

    for r in table.VALUES:
        out.append(Entity(
            r.key, r.name, "number" if r.writable else "sensor", r.data_id, r.codec,
            unit=r.unit, device_class=r.device_class, state_class=r.state_class,
            icon=r.icon, entity_category=r.entity_category, readable=r.readable,
            writable=r.writable, write_id=r.write_id, min_value=r.min_value,
            max_value=r.max_value, default=r.default, bounds_from=r.bounds_from,
            control=control_number(r.control)))

    for r in table.FLAGS:
        out.append(Entity(
            r.key, r.name, "binary", r.data_id, "flag", device_class=r.device_class,
            icon=r.icon, entity_category=r.entity_category, readable=r.readable,
            flag_high_byte=r.high_byte, flag_bit=r.bit))

    for r in table.VIRTUALS:
        # No bounds, deliberately: a synthetic row is bounded by ot_control, not
        # by the table. DO NOT add 0..1 for a switch: that is a second place stating what
        # ot_control_check() decides, and the two would drift.
        out.append(Entity(
            r.key, r.name, r.kind, None, "none", unit=r.unit, device_class=r.device_class,
            state_class=r.state_class, icon=r.icon, entity_category=r.entity_category,
            readable=False, writable=r.writable, control=control_number(r.control),
            options=r.options))

    keys = [e.key for e in out]
    duplicates = sorted({k for k in keys if keys.count(k) > 1})
    if duplicates:
        raise SystemExit(f"duplicate keys in opentherm_ids.py: {duplicates}")
    return out
