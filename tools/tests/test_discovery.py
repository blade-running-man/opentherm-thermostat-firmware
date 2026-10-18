# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Home Assistant discovery: the documents render_discovery.py generates.

The five rules that kill an entity silently (carried over from the ComfoAir firmware) are tested here on
every document, expanded with a real context exactly as the firmware fills it; test_ha_schema.py
hands the same documents to Home Assistant's own schemas when HA is importable. An entity HA
refuses produces no error anywhere -- it simply never appears -- so these are the only witnesses
short of the owner's HA.
"""
import json
import re
import sys

import pytest

CTX = dict(prefix="opentherm/aabbccddeeff", device_id="aabbccddeeff", mac="aa:bb:cc:dd:ee:ff",
           name="Котёл", model="opentherm-thermostat", sw_version="1.0.0", ip="192.168.1.20")
# Everything the firmware may not know: no MAC string, no version, no address yet.
BARE = dict(CTX, mac="", sw_version="", ip="")
P = CTX["prefix"]

# The control entities, by object id: the only documents gated on the owner topic.
GATED = {"ch_enable", "ch_setpoint", "dhw_enable", "dhw_setpoint", "heating_season_off"}
# Frame rows HA never sees (16 and 24; 14, 124 and 126 are the firmware's own writes).
WRITE_ONLY = {"max_relative_modulation", "room_setpoint", "room_temperature",
              "master_ot_version", "master_product_version"}
COMMANDED = {"switch", "number", "button"}
SEGMENT = re.compile(r"[a-zA-Z0-9_-]+\Z")


@pytest.fixture(scope="module")
def disc(gen):
    return sys.modules["render_discovery"]


def expanded(disc, ctx=CTX, lo=400, hi=700):
    return [(d, json.loads(disc.expand(disc.template(d), ctx, lo, hi))) for d in disc.build_docs()]


def walk(value):
    yield value
    if isinstance(value, dict):
        for v in value.values():
            yield from walk(v)
    elif isinstance(value, list):
        for v in value:
            yield from walk(v)


# --- the five rules --------------------------------------------------------------------------

def test_rule1_options_travel_only_with_device_class_enum_and_never_with_a_unit(disc):
    enums = [d.object_id for d, doc in expanded(disc) if "ops" in doc]
    assert enums == ["control_mode", "control_state", "room_source"]
    for d, doc in expanded(disc):
        if "ops" in doc or doc.get("dev_cla") == "enum":
            assert d.component == "sensor", d.object_id
            assert doc.get("dev_cla") == "enum" and doc.get("ops"), d.object_id
            assert "unit_of_meas" not in doc and "stat_cla" not in doc, d.object_id


def test_rule2_a_pair_home_assistant_reads_together_is_never_published_half(disc):
    for d, doc in expanded(disc):
        assert ("cmd_t" in doc) == (d.component in COMMANDED), d.object_id
        if d.component == "number":
            assert {"min", "max", "step", "stat_t"} <= doc.keys(), d.object_id
        if d.component == "switch":
            assert {"pl_on", "pl_off", "stat_on", "stat_off", "stat_t"} <= doc.keys()
        if d.component == "button":
            assert "pl_prs" in doc
        if d.gated:
            assert [a["t"] for a in doc["avty"]] == [f"{P}/status", f"{P}/control/owner"]
            assert doc["avty_mode"] == "all" and "avty_t" not in doc, d.object_id
        else:
            assert doc["avty_t"] == f"{P}/status", d.object_id
            assert "avty" not in doc and "avty_mode" not in doc, d.object_id


def test_rule3_every_discovery_topic_segment_is_one_home_assistant_accepts(disc):
    seen = set()
    for d in disc.build_docs():
        assert SEGMENT.match(d.component) and SEGMENT.match(d.object_id), d.object_id
        assert (d.component, d.object_id) not in seen, d.object_id
        seen.add((d.component, d.object_id))
    assert SEGMENT.match(disc.DISCOVERY_PREFIX)
    ids = [doc["uniq_id"] for _, doc in expanded(disc)]
    assert len(ids) == len(set(ids))


def test_rule4_configuration_url_has_a_scheme_or_is_absent(disc):
    for _, doc in expanded(disc):
        assert doc["dev"]["cu"] == "http://192.168.1.20/"
    for _, doc in expanded(disc, BARE):
        assert "cu" not in doc["dev"] and "cns" not in doc["dev"] and "sw" not in doc["dev"]


def test_rule5_absent_metadata_is_omitted_never_empty(disc):
    for ctx in (CTX, BARE):
        for d, doc in expanded(disc, ctx):
            for value in walk(doc):
                assert value != "" and value is not None, d.object_id


# --- what is asked of the documents ---------------------------------------------------

def test_the_control_entities_and_only_they_are_gated(disc):
    assert {d.object_id for d in disc.build_docs() if d.gated} == GATED


def test_the_season_is_a_binary_sensor_and_an_off_button_never_a_switch(disc):
    season = {d.component: doc for d, doc in expanded(disc)
              if d.object_id in ("heating_season", "heating_season_off")}
    assert set(season) == {"binary_sensor", "button"}
    assert season["button"]["cmd_t"] == f"{P}/heating_season/set"
    assert season["button"]["pl_prs"] == "0"
    assert season["binary_sensor"]["stat_t"] == f"{P}/heating_season/state"


def test_payloads_are_ot_apis_spelling_out_and_ot_commands_numbers_in(disc):
    # A state is what ot_api renders (true/false); a command is a number ot_command_check() takes.
    for d, doc in expanded(disc):
        if d.component == "switch":
            assert (doc["pl_on"], doc["pl_off"], doc["stat_on"], doc["stat_off"]) == \
                ("1", "0", "true", "false"), d.object_id
        if d.component == "binary_sensor":
            assert (doc["pl_on"], doc["pl_off"]) == ("true", "false"), d.object_id


def test_no_document_asks_home_assistant_to_retain_a_command(disc):
    for d, doc in expanded(disc):
        assert "ret" not in doc and "retain" not in doc, d.object_id


def test_the_two_numbers_take_their_bounds_at_runtime(disc):
    nums = {d.object_id: (d, doc) for d, doc in expanded(disc, lo=405, hi=655)
            if d.component == "number"}
    assert set(nums) == {"ch_setpoint", "dhw_setpoint"}
    assert nums["ch_setpoint"][0].bounds == "flow" and nums["dhw_setpoint"][0].bounds == "state"
    for _, doc in nums.values():
        assert (doc["min"], doc["max"], doc["mode"]) == (40.5, 65.5, "box")
    assert nums["ch_setpoint"][1]["step"] == 0.5 and nums["dhw_setpoint"][1]["step"] == 1


def test_write_only_frame_rows_have_no_document(disc):
    assert WRITE_ONLY.isdisjoint(d.object_id for d in disc.build_docs())


def test_every_readable_virtual_or_control_row_has_a_document(gen, disc):
    want = {e.key for e in gen.build_entities() if e.data_id is None or e.readable or e.control}
    keys = [e.key for e in gen.build_entities()]
    assert {keys[d.entity_index] for d in disc.build_docs()} == want


def test_wire_marks_exactly_the_rows_read_from_the_boiler(gen, disc):
    ents = gen.build_entities()
    for d in disc.build_docs():
        e = ents[d.entity_index]
        assert d.wire == (e.data_id is not None and e.readable), d.object_id
    # ID 1's unsupported flag never clears; it must never hide the setpoint HA sends.
    assert not next(d for d in disc.build_docs() if d.object_id == "ch_setpoint").wire


def test_topics_follow_the_layout(gen, disc):
    keys = [e.key for e in gen.build_entities()]
    for d, doc in expanded(disc):
        key = keys[d.entity_index]
        assert d.object_id.startswith(key)
        if "stat_t" in doc:
            assert doc["stat_t"] == f"{P}/{key}/state"
        if "cmd_t" in doc:
            assert doc["cmd_t"] == f"{P}/{key}/set"
    attrs = [doc["json_attr_t"] for d, doc in expanded(disc) if "json_attr_t" in doc]
    assert attrs == [f"{P}/control_state/attributes"]


def test_ids_are_the_device_id_and_the_object_id_never_the_name(disc):
    for d, doc in expanded(disc):
        assert doc["uniq_id"] == f"aabbccddeeff_{d.object_id}"
        assert doc["def_ent_id"] == f"{d.component}.opentherm_{d.object_id}"


def test_a_brace_in_a_template_is_json_or_one_of_the_six_tokens(disc):
    for d in disc.build_docs():
        text = disc.template(d)
        assert text.isascii(), d.object_id
        for m in re.finditer(r"\{(.)", text):
            assert m.group(1) in '"}' or m.group(0) + text[m.end()] in \
                {"{P}", "{I}", "{D}", "{O}", "{L}", "{H}"}, (d.object_id, text[m.start():][:8])


def test_a_tenth_of_a_degree_renders_as_a_json_number(disc):
    assert [disc.number_text(v) for v in (400, 455, -5, 0, 705, -120)] == \
        ["40", "45.5", "-0.5", "0", "70.5", "-12"]


def test_the_file_on_disk_matches_what_the_generator_renders(gen, disc):
    assert gen.HA_OUT.read_text() == disc.render_discovery(), (
        "ha_generated.h is stale or was edited by hand; run python3 tools/generate_registry.py")
