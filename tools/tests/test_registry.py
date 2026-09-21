# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""The tests are written AGAINST THE TABLE, not against what the generator printed.

A test that merely re-reads what the generator wrote proves nothing about whether a row
of the table reached the output.
"""


def all_entity_keys(table):
    """The expected set of keys, assembled INDEPENDENTLY of build_entities().

    The duplication of logic is deliberate: otherwise the test would not catch a bug in
    build_entities() itself, and that is exactly where it will be.
    """
    keys = [r.key for r in table.VALUES]
    keys += [r.key for r in table.FLAGS]
    keys += [r.key for r in table.VIRTUALS]
    return keys


def test_the_table_has_no_duplicate_keys(table):
    keys = all_entity_keys(table)
    duplicates = sorted({k for k in keys if keys.count(k) > 1})
    assert duplicates == [], f"duplicate keys: {duplicates}"


def test_every_data_id_is_inside_the_specification_range(table):
    for row in list(table.VALUES) + list(table.FLAGS):
        assert 0 <= row.data_id <= 127, f"{row.key}: Data-ID {row.data_id} outside 0..127"


def test_the_poll_ring_excludes_status_and_write_only_ids(table):
    # ID 0 is added by the scheduler on every second step; its presence in the ring would
    # mean the status goes out more often than the §4.3.1 rule allows.
    assert 0 not in table.POLL_IDS
    for data_id in table.POLL_IDS:
        readable = [r for r in table.VALUES + table.FLAGS
                    if r.data_id == data_id and r.readable]
        assert readable, f"ID {data_id} is in the poll ring, but no entity reads it"


def test_the_poll_ring_fits_the_scheduler(table):
    # OT_BUS_MAX_POLL is in components/ot_bus_sched/include/ot_bus_sched.h.
    assert len(table.POLL_IDS) <= 32, (
        f"{len(table.POLL_IDS)} identifiers do not fit into OT_BUS_MAX_POLL=32"
    )


def test_ch2_is_absent(table):
    # The second circuit is out of scope, and the boiler does not support it.
    ids = {r.data_id for r in list(table.VALUES) + list(table.FLAGS)}
    assert 8 not in ids


def test_bounds_sources_are_themselves_in_the_table(table):
    ids = {r.data_id for r in list(table.VALUES) + list(table.FLAGS)}
    for row in table.VALUES:
        if row.bounds_from is not None:
            assert row.bounds_from in ids, (
                f"{row.key} takes its bounds from ID {row.bounds_from}, which is not in the table"
            )


def test_writable_entities_declare_a_range(table):
    # A write without bounds is a write the boiler will reject, and it will look like
    # a firmware defect.
    for row in table.VALUES:
        if row.writable:
            assert row.min_value is not None and row.max_value is not None, (
                f"{row.key} is writable but declares no range"
            )


def test_no_secret_ever_reaches_the_registry(table):
    forbidden = ("password", "psk", "secret")
    for row in list(table.VALUES) + list(table.FLAGS) + list(table.VIRTUALS):
        for word in forbidden:
            assert word not in row.key, f"{row.key} looks like a secret"


def test_no_registry_string_needs_json_escaping(table):
    """No row of the table contains anything that would have to be escaped in JSON.

    The check stands HERE and not in the renderer, and that is a deliberate trade-off. The
    generator escapes these strings for the C literal, so at runtime `"` and `\\` reach the
    renderer as REAL characters, and ot_api prints them raw through %s -- the document
    would break. There is nothing to escape with at runtime: ot_json_escape() writes into
    someone else's buffer and, when there is not enough room, silently writes nothing, that
    is, it cannot report the required size, on which the whole snprintf semantics in ot_api
    rests. Besides, the names here are Cyrillic, and \\uXXXX would inflate every letter
    sixfold.

    So the ban is placed where the string is still a single one -- at the entrance to the
    table. Control characters are banned for the same reason: raw, they are illegal in JSON
    too.
    """
    for row in list(table.VALUES) + list(table.FLAGS) + list(table.VIRTUALS):
        for field, value in vars(row).items():
            # An enum's options are a tuple of strings, printed just as raw (ot_api.c).
            for text in (value if isinstance(value, tuple) else (value,)):
                if not isinstance(text, str):
                    continue
                for bad, shown in (('"', 'a quote'), ("\\", "a backslash")):
                    assert bad not in text, (
                        f"{row.key}.{field}: {shown} in a registry string; ot_api prints "
                        f"it into JSON without escaping"
                    )
                assert all(ord(c) >= 0x20 for c in text), (
                    f"{row.key}.{field}: a control character in a registry string"
                )


import re


def test_every_entity_reaches_the_c_registry_exactly_once(table, gen):
    header = gen.render_c_header()
    for key in all_entity_keys(table):
        # We count in the key's POSITION, not bare strings: an entity's name may coincide
        # with its key, and counting quotes would count that as a duplicate. The invariant
        # is one DESCRIPTOR per key.
        found = len(re.findall(rf'\.key\s*=\s*"{re.escape(key)}"', header))
        assert found == 1, f"{key}: descriptors in the C registry: {found}"


def test_every_entity_reaches_the_typescript_types_exactly_once(table, gen):
    types = gen.render_ts_types()
    for key in all_entity_keys(table):
        found = len(re.findall(rf'key:\s*"{re.escape(key)}"', types))
        assert found == 1, f"{key}: entries in entities.ts: {found}"


def test_the_registry_declares_the_count_it_actually_emits(table, gen):
    header = gen.render_c_header()
    declared = int(re.search(r"OT_ENTITY_COUNT\s*=\s*(\d+)", header).group(1))
    assert declared == len(all_entity_keys(table))


def test_the_generated_poll_ring_matches_the_table(table, gen):
    header = gen.render_c_header()
    block = re.search(r"OT_POLL_IDS\[\]\s*=\s*\{(.*?)\};", header, re.S).group(1)
    emitted = tuple(int(n) for n in re.findall(r"\d+", block))
    assert emitted == table.POLL_IDS


def test_absent_metadata_renders_as_null_not_empty_string(gen):
    # "no unit of measurement" and "an empty unit of measurement" are different things,
    # and the consumer is obliged to tell them apart.
    header = gen.render_c_header()
    assert '.unit = ""' not in header
    assert '.unit = NULL,' in header


def test_entities_without_a_range_render_as_nan_not_zero(gen):
    # Zero here would mean "min 0 max 0" for every entity without a range, and the
    # TypeScript projection of the same registry would write null there -- two projections
    # of one table would diverge.
    header = gen.render_c_header()
    assert ".min_value = NAN," in header


def test_bounds_source_is_minus_one_when_absent(gen):
    header = gen.render_c_header()
    assert ".bounds_from = -1," in header


def test_the_files_on_disk_match_what_the_generator_renders(gen):
    # A hole of the original, closed here: without this a hand edit of a generated file
    # lives until the next firmware build and passes every test.
    assert gen.C_OUT.read_text() == gen.render_c_header(), (
        "registry_generated.h is stale or was edited by hand; "
        "run python3 tools/generate_registry.py"
    )
    assert gen.TS_OUT.read_text() == gen.render_ts_types(), (
        "entities.ts is stale or was edited by hand; "
        "run python3 tools/generate_registry.py"
    )
