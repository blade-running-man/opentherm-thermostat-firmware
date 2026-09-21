// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The web-side echo of "one list of entities" (CLAUDE.md): every firmware token union value the
// SPA declares (api/control.ts ControlState/ControlReason/mode, api/entities.ts Availability)
// must have a matching token.* catalog key, so the firmware's stable vocabulary and the
// catalog's translations cannot silently drift apart. Written before en/tokens.ts is filled in
// (TDD) -- every assertion here fails on the empty stub.
//
// Uses the project's own harness (eq/ok/report), not node:test/node:assert -- see
// pages/settings/tests/harness.ts: `types: ["vite/client"]` (tsconfig.app.json) leaves
// @types/node out of the program, so a bare `import ... from "node:test"` does not typecheck
// and would break `npm run build` (tsc -b), the real web gate.
//
// Run: node src/i18n/tests/tokens.test.ts

import { en } from "../catalogs/en.ts";
import { ok, report } from "../../pages/settings/tests/harness.ts";

// components/ot_control/ot_control_names.c (STATE_NAMES), api/control.ts ControlState.
const STATES = ["season_off", "boost", "local", "ha_waiting", "failsafe", "ha"];
// REASON_NAMES, api/control.ts ControlReason (minus "none"/"unknown").
const REASONS = ["await_setpoint", "fs_disarmed", "fs_blind", "fs_room_cold", "fs_room_warm", "min_cycle"];
// The two causes of failsafe (api/control.ts ControlDocument.cause, minus "none").
const CAUSES = ["watchdog", "ha_blind"];
// api/control.ts ControlDocument.mode.
const MODES = ["local", "ha"];
// api/entities.ts Availability.
const AVAIL = ["ok", "invalid", "unsupported", "unknown"];

for (const s of STATES) {
  ok(`token.state.${s}.label` in en, `token.state.${s}.label exists`);
  ok(`token.state.${s}.note` in en, `token.state.${s}.note exists`);
}
for (const r of REASONS) ok(`token.reason.${r}` in en, `token.reason.${r} exists`);
for (const c of CAUSES) ok(`token.cause.${c}` in en, `token.cause.${c} exists`);
for (const m of MODES) ok(`token.mode.${m}` in en, `token.mode.${m} exists`);
for (const a of AVAIL) ok(`token.avail.${a}` in en, `token.avail.${a} exists`);

report("i18n/tokens");
