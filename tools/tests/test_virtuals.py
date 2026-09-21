# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""The synthetic entities: the third table, VIRTUALS.

A file of its own, cut from test_registry.py along the table seam: these rows have no
Data-ID, and every test here is about what that absence must and must not do. The rule of
test_registry.py holds here too: the tests are written against the TABLE, not against what
the generator printed.
"""
import re

import pytest

# ot_control_cmd_t, components/ot_control/include/ot_control.h. Written out HERE a second time
# on purpose: the generator's mapping is what emits the numbers, and a test that read them
# back from the generator would pin nothing. The C side is pinned against ot_control.h by a
# host test in test_ot_command.
CONTROL_NUMBERS = {"CH_ENABLE": 1, "CH_SETPOINT": 2, "DHW_ENABLE": 3,
                   "DHW_SETPOINT": 4, "SEASON": 5}

# The executor's rows, exactly:
# key -> (kind, writable, control, unit, device_class, state_class, entity_category).
META = ("unit", "device_class", "state_class", "entity_category")
EXECUTOR_ROWS = {
    "ch_enable": ("switch", True, "CH_ENABLE", None, None, None, None),
    "dhw_enable": ("switch", True, "DHW_ENABLE", None, None, None, None),
    "heating_season": ("switch", True, "SEASON", None, None, None, None),
    # HA's enum sensor: device_class "enum", no unit, no state_class.
    "control_mode": ("enum", False, None, None, "enum", None, None),
    "control_state": ("enum", False, None, None, "enum", None, None),
    "ch_setpoint_effective": ("sensor", False, None, "°C", "temperature", "measurement",
                              None),
    "ch_enable_effective": ("binary", False, None, None, None, None, None),
    "failsafe_count": ("sensor", False, None, None, None, "total_increasing", "diagnostic"),
    "last_failsafe_duration_s": ("sensor", False, None, "s", "duration", "measurement",
                                 "diagnostic"),
}

# Non-executor sensor virtuals: rows that carry no executor decision but still live in the
# VIRTUALS table because they have no Data-ID (a device-local reading, not on the wire).
# room_temperature_effective is the on-shield DS18B20.
# room_source is which room-source registry slot is behind that reading --
# also not an ot_control_io executor output.
NON_EXECUTOR_ROWS = {
    "room_temperature_effective": ("sensor", False, None, "°C", "temperature", "measurement",
                                   None),
    "room_source": ("enum", False, None, None, None, None, None),
}


def descriptor(header, key):
    """The C initialiser of one entity: from its `.key` line to its closing brace."""
    m = re.search(rf'\.key = "{re.escape(key)}",(.*?)\n    \}},', header, re.S)
    assert m, f"{key}: no descriptor in the C registry"
    return m.group(1)


def ts_row(types, key):
    m = re.search(rf'^  \{{ key: "{re.escape(key)}",.*$', types, re.M)
    assert m, f"{key}: no row in entities.ts"
    return m.group(0)


def test_the_executor_rows_are_exactly_the_contracted_ones(table):
    got = {r.key: (r.kind, r.writable, r.control) + tuple(getattr(r, f) for f in META)
           for r in table.VIRTUALS}
    # The nine executor rows stay pinned exactly; the non-executor sensor virtuals are pinned
    # separately, and together they must exhaust VIRTUALS -- so a new synthetic row cannot be
    # added without deciding, here, which of the two it is.
    assert {k: got[k] for k in EXECUTOR_ROWS} == EXECUTOR_ROWS
    assert {k: got[k] for k in NON_EXECUTOR_ROWS} == NON_EXECUTOR_ROWS
    assert set(got) == set(EXECUTOR_ROWS) | set(NON_EXECUTOR_ROWS)


def test_a_virtual_row_has_no_data_id_to_get_wrong(table):
    # Not a sentinel in a field: the field does not exist, so no row can be given an ID 0
    # by a slip -- and a synthetic row at ID 0 would be decoded from every other reply.
    for row in table.VIRTUALS:
        assert not hasattr(row, "data_id"), row.key


def test_virtual_rows_emit_minus_one_none_codec_and_their_ha_metadata(table, gen):
    header = gen.render_c_header()
    for row in table.VIRTUALS:
        d = descriptor(header, row.key)
        assert ".data_id = -1," in d, row.key
        assert ".codec = OT_CODEC_NONE," in d, row.key
        assert ".readable = false," in d, row.key
        # The table pinned above is worth nothing if the generator drops a field on the way.
        for f in META:
            value = getattr(row, f)
            literal = "NULL" if value is None else f'"{value}"'
            assert f".{f} = {literal}," in d, f"{row.key}.{f}"


def test_virtual_rows_never_reach_the_poll_ring(table, gen):
    # The ring is what the scheduler asks the boiler; a synthetic row there would be a
    # Data-ID asked for an entity the boiler has never heard of.
    real = {r.data_id for r in table.VALUES + table.FLAGS if r.readable} - {0}
    assert set(table.POLL_IDS) == real
    assert all(1 <= i <= 127 for i in table.POLL_IDS)
    for e in gen.build_entities():
        if e.data_id is None:
            assert not e.readable, f"{e.key}: a synthetic row marked readable"


def test_control_names_are_known_and_only_on_writable_rows(table, gen):
    assert gen.CONTROLS == CONTROL_NUMBERS
    rows = [r for r in list(table.VALUES) + list(table.VIRTUALS) if r.control is not None]
    for r in rows:
        assert r.control in CONTROL_NUMBERS, f"{r.key}: unknown command {r.control}"
        assert r.writable, f"{r.key}: a command on a read-only row"
    # One entity per command: two would be two doors into the same executor input.
    assert sorted(r.control for r in rows) == sorted(CONTROL_NUMBERS)
    by_key = {r.key: r for r in table.VALUES}
    assert by_key["ch_setpoint"].control == "CH_SETPOINT"
    assert by_key["dhw_setpoint"].control == "DHW_SETPOINT"


def test_the_c_registry_emits_every_control_number(table, gen):
    header = gen.render_c_header()
    for r in list(table.VALUES) + list(table.VIRTUALS):
        expected = CONTROL_NUMBERS[r.control] if r.control else 0
        assert f".control = {expected}," in descriptor(header, r.key), r.key
    for r in table.FLAGS:
        assert ".control = 0," in descriptor(header, r.key), r.key


def test_enum_rows_have_options_and_no_other_row_does(table):
    for r in table.VIRTUALS:
        if r.kind == "enum":
            assert len(r.options) >= 2, r.key
            assert len(set(r.options)) == len(r.options), f"{r.key}: a repeated option"
            for o in r.options:
                # Also keeps the C separator "|" out of an option.
                assert re.fullmatch(r"[a-z][a-z0-9_]*", o), f"{r.key}: option {o!r}"
        else:
            assert r.options == (), f"{r.key}: options on a {r.kind}"
    for r in list(table.VALUES) + list(table.FLAGS):
        assert not hasattr(r, "options"), r.key


def test_the_option_order_is_the_executors_enum_order(table):
    # The stored value is the option INDEX (ot_state_set_virtual), and the executor hands in
    # its enum ordinal: this order IS ot_control_mode_t and ot_control_state_t.
    by_key = {r.key: r for r in table.VIRTUALS}
    assert by_key["control_mode"].options == ("local", "ha")
    assert by_key["control_state"].options == (
        "season_off", "boost", "local", "ha_waiting", "failsafe", "ha")


def test_options_reach_both_outputs(table, gen):
    header, types = gen.render_c_header(), gen.render_ts_types()
    for r in list(table.VALUES) + list(table.FLAGS) + list(table.VIRTUALS):
        options = getattr(r, "options", ())
        if options:
            assert f'.options = "{"|".join(options)}",' in descriptor(header, r.key)
            listed = ", ".join(f'"{o}"' for o in options)
            assert f"options: [{listed}]" in ts_row(types, r.key)
        else:
            assert ".options = NULL," in descriptor(header, r.key), r.key
            assert "options: null" in ts_row(types, r.key), r.key


def test_typescript_data_id_is_null_for_synthetic_rows_only(table, gen):
    types = gen.render_ts_types()
    for r in table.VIRTUALS:
        assert "dataId: null," in ts_row(types, r.key), r.key
    for r in list(table.VALUES) + list(table.FLAGS):
        assert f"dataId: {r.data_id}," in ts_row(types, r.key), r.key


def test_kind_and_codec_ordinals_only_ever_grow(gen):
    # The C enums are numbered by tuple order: an insertion renumbers every entity after it.
    assert gen.KINDS == ("sensor", "binary", "number", "switch", "enum")
    assert gen.CODECS == ("f88", "u16", "s16", "u8_hb", "u8_lb", "s8_hb", "s8_lb", "flag",
                          "none")


def test_only_data_id_rows_carry_table_bounds(gen):
    # A frame write is bounded by the table (and by the boiler, bounds_from); a synthetic row
    # by ot_control. Bounds on a synthetic row would be a second place stating
    # what ot_control_check() decides. test_ot_command holds the C side to the same split.
    for e in gen.build_entities():
        if e.data_id is None:
            assert e.min_value is None and e.max_value is None, e.key
        elif e.writable:
            assert e.min_value is not None and e.max_value is not None, e.key


def test_the_generator_refuses_a_key_shared_across_tables(gen, monkeypatch):
    clash = gen.table.Virtual("flow_temperature", "Дубль", "sensor")
    monkeypatch.setattr(gen.table, "VIRTUALS", gen.table.VIRTUALS + (clash,))
    with pytest.raises(SystemExit, match="flow_temperature"):
        gen.build_entities()


def test_the_generator_refuses_an_unknown_command(gen, monkeypatch):
    typo = gen.table.Virtual("x_enable", "Икс", "switch", writable=True, control="CH_ENABEL")
    monkeypatch.setattr(gen.table, "VIRTUALS", gen.table.VIRTUALS + (typo,))
    with pytest.raises(SystemExit, match="CH_ENABEL"):
        gen.build_entities()
