// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What the control card says when the device says no.
//
// Run: node src/pages/control/tests/errors.test.ts
//
// The sentences below are the firmware's own: ot_command_strerror()
// (components/ot_command/ot_command.c) and ot_http_control_refusal()
// (components/ot_http/ot_http_control.c). The rule pinned is that the card shows them VERBATIM:
// in Home Assistant mode the device's "owned by Home Assistant: control_mode is ha" is the whole
// explanation, and the card does not pre-empt it by guessing.

import { setLocale } from "../../../i18n/index.ts";
setLocale("en"); // errors.ts now reads t(); pin the English wording

import { ApiError } from "../../../api/client.ts";
import { explainControlFailure, explainControlLoad, explainNotStarted } from "../errors.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

const OWNED_BY_HA = "owned by Home Assistant: control_mode is ha";
const SEASON_OFF = "heating_season is off: a boost would not heat";
// ot_command_strerror()'s, which is what POST /api/entities/<key> answers -- the short sentence.
// ot_http_control_refusal() borrows the same words for ownership, so the early refusal and the
// final one read alike.
const OUT_OF_RANGE = "value out of range";
// ot_http_control_refusal()'s own, which only POST /api/ops/<name> reaches.
const BAD_MINUTES = "minutes must be a whole number 1..480";
const NO_TASK = "the thermostat task is not running; nothing would carry it out";

{
  const e = explainControlFailure("CH", new ApiError(409, OWNED_BY_HA));
  eq(e.detail, OWNED_BY_HA, "an ownership refusal is shown in the device's words, unedited");
  eq(e.tone, "denied", "and as a refusal, not a fault");
  eq(e.status, 409, "with its status");
  ok(e.headline.startsWith("CH"), "the headline names the control that was refused");
}

{
  const e = explainControlFailure("Boost", new ApiError(409, SEASON_OFF));
  eq(e.detail, SEASON_OFF, "the boost's own 409 is the device's sentence too");
}

{
  const refused = explainControlFailure("Flow setpoint", new ApiError(409, OWNED_BY_HA));
  const bounds = explainControlFailure("Flow setpoint", new ApiError(422, OUT_OF_RANGE));
  ok(refused.headline !== bounds.headline,
     "409 and 422 read differently: 'not yours to send' is not 'another number would do'");
  eq(bounds.detail, OUT_OF_RANGE, "and the bounds sentence is the device's, whatever its length");
  eq(bounds.tone, "error", "a refused value is an error the owner can fix");
  eq(explainControlFailure("Boost", new ApiError(422, BAD_MINUTES)).detail, BAD_MINUTES,
     "the operations route's own 422 travels the same way");
}

{
  // The SAME control for both: with two controls the headlines differ by name alone, whatever
  // each status says.
  const e = explainControlFailure("Boost", new ApiError(503, NO_TASK));
  eq([e.status, e.detail], [503, NO_TASK], "the missing task is said as the device says it");
  const s = explainControlFailure("Boost", new ApiError(500, "the settings were refused"));
  eq(s.detail, "the settings were refused", "a store that refused to keep it, too (NOT_SAVED)");
  ok(s.headline !== e.headline,
     "and the two do not share a headline: 'could not keep it' is not 'nothing would carry it out'");
  eq(e.headline, "Boost: nothing on the device would carry it out", "503 is the executor not running");
}

for (const status of [400, 401, 403, 404, 413, 418]) {
  const e = explainControlFailure("CH", new ApiError(status, "from the device"));
  eq([e.status, e.detail], [status, "from the device"], `${status}: the status and the words survive`);
}

// The tone of each answer: a refusal is not a fault, and a missing route is news about the build.
eq([400, 401, 403, 404, 409, 413, 418, 422, 500, 503]
     .map((status) => explainControlFailure("CH", new ApiError(status, "x")).tone),
   ["error", "denied", "denied", "pending", "denied", "error", "error", "error", "error", "error"],
   "each status in its tone");
eq(explainControlFailure("CH", new ApiError(401, "password required")).headline,
   "The device wants its web-interface password", "a write without the password asks for it");

{
  const e = explainNotStarted();
  ok(e.headline === "The executor has not started" && e.detail.includes("503"),
     "the document of an executor that is not running says so, and why every control would fail");
  eq(e.tone, "error", "past a boot's first seconds it is a fault, not news about the build");
}

{
  const e = explainControlFailure("DHW", new TypeError("Failed to fetch"));
  eq([e.tone, e.status], ["offline", null], "nothing answered: no status, and grey");
  ok(e.detail.includes("Failed to fetch"), "the browser's words, marked as the browser's");
}

// --- reading the document -----------------------------------------------------------------------

eq(explainControlLoad(new ApiError(404, "not found")).tone, "pending",
   "a firmware without /api/control is news about the build, not a fault");
eq(explainControlLoad(new ApiError(401, "password required")).tone, "denied", "401 is the password");
{
  const e = explainControlLoad(new ApiError(200, "the answer is not a control document (schema 1)"));
  ok(e.tone === "error" && e.headline.includes("not a control document"),
     "a 200 whose body is not the document is said to be exactly that");
}
{
  const e = explainControlLoad(new TypeError("NetworkError"));
  ok(e.tone === "offline" && e.detail.includes("NetworkError"), "and silence is silence");
}

report("control/errors");
