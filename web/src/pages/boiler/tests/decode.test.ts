// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The readings of a raw 16-bit word, and the age of an answer.
//
// Run: node src/pages/boiler/tests/decode.test.ts
//
// The suite exists because f8.8 is a SIGNED fixed-point value, and the one mistake that can
// really be made here is reading it unsigned: the boiler hands back 0xFFB0 for -0.31 °C
// (the outside temperature in winter), and an unsigned reading will show 65456/256 = 255.69
// and the page will lie about overheating where it is freezing outdoors. The other cases are
// pinned along with it so that editing one function does not shift its neighbours.

import {
  FRESH_MS,
  STALE_MS,
  ageLevel,
  asF88,
  asS16,
  asU16,
  flagBits,
  formatAge,
  hiByte,
  hex16,
  loByte,
} from "../decode.ts";
import { eq, report } from "../../settings/tests/harness.ts";

// --- f8.8 ---------------------------------------------------------------------------------
eq(asF88(0x1440), "20.25", "0x1440 -- 20.25 °C, an ordinary setpoint");
eq(asF88(0x0000), "0.00", "zero");
eq(asF88(0xffb0), "-0.31", "0xFFB0 -- a negative outside temperature, not 255.69");
eq(asF88(0x8000), "-128.00", "the bottom of the range");
// 32767/256 = 127.996..., and two decimal places round that to 128.00. That is as it should
// be: the column shows a reading, and the exact word is always next to it in the raw column.
eq(asF88(0x7fff), "128.00", "the top of the range rounds upwards");

// --- integers -------------------------------------------------------------------------------
eq(asU16(0xffff), 65535, "u16 -- as it is");
eq(asS16(0xffff), -1, "s16 -- signed");
eq(asS16(0x7fff), 32767, "s16 -- the last positive");
eq(asS16(0x8000), -32768, "s16 -- the first negative");

// --- bytes and hex --------------------------------------------------------------------------
eq(hiByte(0x0301), 3, "high byte");
eq(loByte(0x0301), 1, "low byte");
eq(hex16(0x0301), "0x0301", "hex is always four digits");
eq(hex16(0), "0x0000", "zero gets four digits too");

// --- flags --------------------------------------------------------------------------------
eq(flagBits(0x0281), "0000 0010 . 1000 0001", "the format from the brief: nibbles, a dot between the bytes");
eq(flagBits(0), "0000 0000 . 0000 0000", "zero");
eq(flagBits(0xffff), "1111 1111 . 1111 1111", "all sixteen");

// --- rubbish off the wire -------------------------------------------------------------------
// The value comes from JSON and nobody has checked it. None of the functions has the right to
// return NaN or a thirty-character string: the page is a debug page, and a broken answer on it
// must be SEEN, not turned into an empty cell.
eq(asU16(70000), 4464, "going past 16 bits is normalised");
eq(flagBits(-1), "1111 1111 . 1111 1111", "a negative is normalised");

// --- age ------------------------------------------------------------------------------------
eq(ageLevel(0), "fresh", "just now");
eq(ageLevel(FRESH_MS - 1), "fresh", "the freshness boundary is exclusive");
eq(ageLevel(FRESH_MS), "normal", "exactly 5 s -- no longer fresh");
eq(ageLevel(STALE_MS), "normal", "exactly 30 s -- not stale yet");
eq(ageLevel(STALE_MS + 1), "stale", "past 30 s -- stale");

eq(formatAge(940), "940 ms", "under a second -- in milliseconds");
eq(formatAge(5700), "5.7 s", "seconds with a tenth");
eq(formatAge(72000), "1 min 12 s", "over a minute -- minutes and seconds");

report("boiler/decode");
