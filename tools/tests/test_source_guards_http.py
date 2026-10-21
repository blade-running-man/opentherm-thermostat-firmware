# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""Source guards for ot_http: the checks no host suite can see.

ot_http is esp_http_server glue and is not host-built, so a check in it that regresses compiles,
links and passes every suite. The one pinned here is op_linetest()'s parameter filter in
ot_http_ops.c. Its job is to answer 422 for EVERY value that the later `(uint32_t)` cast would
turn into something ot_bus_line_test() refuses, so that the 409 "line test already running" means
exactly one thing. The obvious-looking `duration <= 0` reopens the hole: 0.5 passes it, casts to
0, the bus refuses duration 0, and the client is told a test is running when none is.

The tests read the source with its comments stripped (test_source_guards.code) -- a comment that
QUOTES the old check is not the check -- and each asserts the form it expects IS there, so a
rename cannot make a guard pass by finding nothing.
"""
import re
from pathlib import Path

from test_source_guards import code, flat, uses

ROOT = Path(__file__).resolve().parents[2]
OPS = ROOT / "components" / "ot_http" / "ot_http_ops.c"


def linetest_body():
    src = flat(code(OPS))
    start = src.find("static op_refusal_t op_linetest(")
    assert start != -1, "op_linetest() not found in ot_http_ops.c"
    end = src.find(" } ", start)
    assert end != -1, "end of op_linetest() not found"
    return src[start:end]


def test_duration_is_compared_against_the_post_cast_floor_not_zero():
    body = linetest_body()
    assert re.search(r"duration\s*(<=|<|>|>=)\s*0(\.0f?)?(?![0-9.])", body) is None, \
        "duration compared against 0: 0 < d < 1 passes, casts to 0, and the bus answers 409"
    assert "!(duration >= 1.0f && duration <= (float)OT_BUS_TEST_MAX_MS)" in body, \
        "the duration check must accept exactly 1.0f..OT_BUS_TEST_MAX_MS, in the NaN-refusing form"


def test_half_period_is_checked_by_the_bus_limits_in_the_same_form():
    body = linetest_body()
    assert re.search(r"half\s*(<=|<|>|>=)\s*0(\.0f?)?(?![0-9.])", body) is None, \
        "half_period_ms compared against 0: the bus floor is OT_BUS_TEST_HALF_MIN_MS"
    assert ("!(half >= (float)OT_BUS_TEST_HALF_MIN_MS && "
            "half <= (float)OT_BUS_TEST_HALF_MAX_MS)") in body, \
        "the half-period check must accept exactly OT_BUS_TEST_HALF_MIN_MS..HALF_MAX_MS"


def test_the_checks_run_before_the_cast_into_the_bus():
    body = linetest_body()
    call = body.find("ot_bus_line_test((uint32_t)duration, (uint32_t)half)")
    assert call != -1, "ot_bus_line_test((uint32_t)duration, (uint32_t)half) not found"
    for check in ("!(duration >= 1.0f", "!(half >= (float)OT_BUS_TEST_HALF_MIN_MS"):
        idx = body.find(check)
        assert idx != -1 and idx < call, f"{check} must come before the cast into ot_bus_line_test"
    assert len(uses(body, "ot_bus_line_test")) == 1
