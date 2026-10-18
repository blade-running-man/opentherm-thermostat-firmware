# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Every discovery document, validated by Home Assistant's OWN schemas.

Skipped where Home Assistant is not importable -- the ordinary `python3 -m pytest tools/tests`
gate. Run it with an interpreter that has HA installed (the plan names one: the owner's HA
virtualenv, e.g. HA 2026.9):

    "$HA_PY" -m pytest tools/tests/test_ha_schema.py -q

Why it exists: an entity HA refuses produces no error anywhere, it simply never appears (the
five rules, render_discovery.py). test_discovery.py states each rule by hand; this asks HA.
"""
import importlib
import json
import sys

import pytest

abbreviations = pytest.importorskip("homeassistant.components.mqtt.abbreviations")

CTX = dict(prefix="opentherm/aabbccddeeff", device_id="aabbccddeeff", mac="aa:bb:cc:dd:ee:ff",
           name='Котёл "кухня"', model="opentherm-thermostat", sw_version="1.0.0",
           ip="192.168.1.20")
BARE = dict(CTX, mac="", sw_version="", ip="")


@pytest.fixture(scope="module")
def disc(gen):
    return sys.modules["render_discovery"]


def schema(component):
    return importlib.import_module(f"homeassistant.components.mqtt.{component}").DISCOVERY_SCHEMA


def unabbreviate(doc):
    """What homeassistant/components/mqtt/discovery.py does before it validates."""
    a = abbreviations
    out = {}
    for key, value in doc.items():
        key = a.ABBREVIATIONS.get(key, key)
        if key == "device":
            value = {a.DEVICE_ABBREVIATIONS.get(k, k): v for k, v in value.items()}
        elif key == "origin":
            value = {a.ORIGIN_ABBREVIATIONS.get(k, k): v for k, v in value.items()}
        elif key == "availability":
            value = [{a.ABBREVIATIONS.get(k, k): v for k, v in item.items()} for item in value]
        out[key] = value
    return out


@pytest.mark.parametrize("ctx", [CTX, BARE], ids=["full", "bare"])
def test_home_assistant_accepts_every_document(disc, ctx):
    for d in disc.build_docs():
        doc = json.loads(disc.expand(disc.template(d), ctx, 400, 700))
        schema(d.component)(unabbreviate(doc))   # raises vol.Invalid on refusal


def test_home_assistant_refuses_what_rule4_forbids(disc):
    d = disc.build_docs()[0]
    doc = unabbreviate(json.loads(disc.expand(disc.template(d), CTX, 400, 700)))
    doc["device"]["configuration_url"] = "192.168.1.20"
    with pytest.raises(Exception, match="url"):
        schema(d.component)(doc)


def test_home_assistant_refuses_what_rule1_forbids(disc):
    d = next(d for d in disc.build_docs() if d.object_id == "control_state")
    doc = unabbreviate(json.loads(disc.expand(disc.template(d), CTX, 400, 700)))
    doc["unit_of_measurement"] = "x"
    with pytest.raises(Exception, match="options"):
        schema("sensor")(doc)
