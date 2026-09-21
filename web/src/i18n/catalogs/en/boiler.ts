// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Boiler page: the raw Data-ID view (Boiler.tsx) and its table (RawTable.tsx). Everything
// here is the SPA's own chrome and diagnostic prose. NOT here: row.type (the OpenTherm
// answer-type token, api/otRaw.ts), the hex/f8.8/u16/s16/high-low/flags decode columns
// (decode.ts), formatAge's output, and entity names (those go through entityName(), never a
// catalog key -- RawTable.tsx's nameForId()).
import type { Message } from "../../format.ts";

export const boiler = {
  "boiler.title": "Boiler",
  "boiler.loading": "Asking the device about the bus…",

  "boiler.counter.cycles": "cycles",
  "boiler.counter.answers": "answers",
  "boiler.counter.failures": "failures",
  "boiler.counter.overdue": "overdue",
  "boiler.uptimeHint": "The device has been up for {uptime}. Refreshes every {seconds} s.",

  "boiler.answering": "The boiler answers",
  "boiler.silent": "The boiler does not answer",
  "boiler.silentHint": "The master is sending requests{cycles} and no answer comes. Check, "
    + "in this order: the interface board's power, the pair's polarity, the input inversion.",
  "boiler.silentHint.cyclesSuffix": " ({count} cycles)",

  "boiler.line": "Line: {text}.",
  "boiler.line.idle": "input idle (0 %) — as it should be while the boiler is silent",
  "boiler.line.inverted": "input constantly active ({duty} %) — the input polarity looks inverted",
  "boiler.line.toggling": "input toggling ({duty} %) — frames are on the line",

  "boiler.scan.run": "Ask every Data-ID (~4 min)",
  "boiler.scan.running": "Sweep running: {done} of {total}",
  "boiler.scan.hint": "Asks the boiler about every identifier from 0 to 127, once each, and "
    + "puts the answers in the table below. Read-only — the sweep writes nothing. An "
    + "identifier missing from the table after the sweep did not answer at all; a row "
    + "reading \"unknown-dataid\" means the boiler said outright that it has no such thing. "
    + "Those are different things, and both are useful.",
  "boiler.scan.forbidden": "The device is not on a network yet: during first-time setup only "
    + "the setup itself is allowed. Connect Wi-Fi on the Settings page.",
  "boiler.scan.failed": "The request did not go through.",

  "boiler.lineTest.run": "Test the line with a multimeter",
  "boiler.lineTest.running": "Test running, 20 s…",
  "boiler.lineTest.hint": "For twenty seconds the line toggles slowly, two seconds in each "
    + "state. Measure the voltage at the boiler's terminals: it should drop from 15–24 V to "
    + "seven or below, and come back. If it does not change, the adapter's output is not "
    + "working or the bus has no power.",
  "boiler.lineTest.warning": "No frames go out meanwhile, so the boiler will most likely fire up",
  "boiler.lineTest.warningNote": " — that is what the boiler does when the thermostat falls "
    + "silent (a silent thermostat is read as a demand for heat). It is expected.",
  "boiler.lineTest.failed": "The request did not go through: a test may already be running.",

  "boiler.raw.empty": "No Data-ID has answered yet. While the boiler is silent the table is "
    + "empty — that is a state, not a fault of the page.",
  "boiler.raw.answers": "{count} answers",
  "boiler.raw.col.id": "ID",
  "boiler.raw.col.name": "name",
  "boiler.raw.col.type": "answer type",
  "boiler.raw.col.raw": "raw",
  "boiler.raw.col.f88": "f8.8",
  "boiler.raw.col.u16": "u16",
  "boiler.raw.col.s16": "s16",
  "boiler.raw.col.hiLo": "high / low byte",
  "boiler.raw.col.flags": "flags",
  "boiler.raw.col.age": "age",
} satisfies Record<string, Message>;
