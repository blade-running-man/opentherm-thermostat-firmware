// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// How the control card asks the device again (refresh.ts): one request at a time, a trigger that
// lands during one kept rather than lost, and the CH command read by the poll while /ws is down.
//
// Run: node src/pages/control/tests/refresh.test.ts

import { ApiError } from "../../../api/client.ts";
import { readCard, singleFlight } from "../refresh.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

/** Lets every settled promise's callbacks run. */
const settle = () => new Promise<void>((resolve) => setTimeout(resolve, 0));

/** A request the test finishes by hand, and a count of how many were started. */
function gate() {
  const pending: Array<() => void> = [];
  const state = { started: 0 };
  const run = () => {
    state.started++;
    return new Promise<void>((resolve) => pending.push(resolve));
  };
  const finishOne = () => pending.shift()?.();
  return { state, run, finishOne };
}

// --- one request at a time --------------------------------------------------------------------

{
  const g = gate();
  const refresh = singleFlight(g.run, () => true);
  const first = refresh();
  void refresh();
  void refresh();
  eq(g.state.started, 1, "a trigger while a request is out starts no second request");
  g.finishOne();
  await settle();
  eq(g.state.started, 2,
     "but it is not lost: one more request runs when the first ends, so a /ws change that landed "
     + "mid-request is answered with a document from after it");
  g.finishOne();
  await first;
  await settle();
  eq(g.state.started, 2, "and only one more, however many triggers landed");
}

{
  const g = gate();
  let alive = true;
  const refresh = singleFlight(g.run, () => alive);
  void refresh();
  void refresh();
  alive = false;
  g.finishOne();
  await settle();
  eq(g.state.started, 1, "a page that has gone away asks nothing more");
}

{
  let started = 0;
  const refresh = singleFlight(async () => {
    started++;
    throw new Error("boom");
  }, () => true);
  await refresh();
  await refresh();
  eq(started, 2, "a request that failed does not wedge the next one");
}

// --- what one refresh reads -------------------------------------------------------------------

// A real document (test/test_api's pinned shape), as the renderer prints it for LOCAL control.
const DOC: unknown = JSON.parse(
  "{\"schema\":1,\"mode\":\"local\",\"state\":\"local\",\"reason\":\"none\",\"cause\":\"none\","
  + "\"heating_season\":true,\"status_high\":1,\"held_setpoint_dc\":450,"
  + "\"dhw\":{\"enable\":true,\"setpoint_dc\":null},"
  + "\"boost\":{\"active\":false,\"setpoint_dc\":null,\"remaining_s\":null},"
  + "\"failsafe\":{\"count\":0,\"last_duration_s\":0},\"watchdog_overdue_s\":0,\"stack_hwm\":1184}");
// GET /api/entities/ch_enable (ot_api_render_entity(), components/ot_api/ot_api.c): the synthetic
// row's value prints true/false, as in the /ws frame.
const CH_ON: unknown = { meta: { key: "ch_enable" }, value: { availability: "ok", value: true, age_ms: 40 } };

{
  let asked = 0;
  const r = await readCard(true, async () => DOC, async () => {
    asked++;
    return CH_ON;
  });
  eq([r.doc.state, r.ch, asked], ["local", null, 0],
     "with /ws open the command is the socket's: the entity is not asked for");
}

{
  const r = await readCard(false, async () => DOC, async () => CH_ON);
  eq(r.ch, true, "with /ws down the poll reads the command from GET /api/entities/ch_enable");
}

{
  const r = await readCard(false, async () => DOC, async () => {
    throw new TypeError("Failed to fetch");
  });
  eq([r.doc.state, r.ch], ["local", null],
     "a command that could not be read leaves the document, and the command unknown");
}

{
  let err: unknown = null;
  try {
    await readCard(true, async () => ({ schema: 2 }), async () => CH_ON);
  } catch (e) {
    err = e;
  }
  ok(err instanceof ApiError && err.status === 200,
     "an answer that is not the document is a fault of the answer, not silence");
}

report("control/refresh");
