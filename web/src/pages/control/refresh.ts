// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// How the control card asks the device again (Control.tsx).
//
// Pure: no DOM, no signals, and no network of its own -- every request is handed in -- so
// tests/refresh.test.ts reaches all of it. The page wiring that was here before had no test, and
// a card that asks the device once too few is a card showing yesterday's state.
//
// Relative imports carry the .ts extension so node can load this module (settings/tests/harness.ts).

import { ApiError } from "../../api/client.ts";
import type { ControlDocument } from "../../api/control.ts";
import { chFromEntity, isControlDocument } from "./model.ts";

/**
 * One request at a time, and a trigger that lands during one is NOT lost: it runs once more when
 * the first ends. Without that, a /ws change arriving mid-request would be answered with the
 * document from before it, and the card would sit on it until the next poll.
 *
 * `alive` is the page's own "still mounted": once it is false nothing further is asked.
 *
 * `run` reports its own failures -- the card turns them into a notice -- and one that escapes is
 * dropped here rather than left to wedge the card behind `running` or to surface as an unhandled
 * rejection in the console.
 */
export function singleFlight(run: () => Promise<void>, alive: () => boolean): () => Promise<void> {
  let running = false;
  let again = false;
  const trigger = async (): Promise<void> => {
    if (running) {
      again = true;
      return;
    }
    running = true;
    try {
      await run();
    } catch {
      // See above: run() owns its failures.
    } finally {
      running = false;
    }
    if (again && alive()) {
      again = false;
      await trigger();
    }
  };
  return trigger;
}

export interface CardReading {
  doc: ControlDocument;
  /** The CH command read beside the document while /ws is down; null when the socket carries it. */
  ch: boolean | null;
}

/**
 * One refresh: GET /api/control, and -- only while the socket is down -- GET
 * /api/entities/ch_enable beside it, because the CH command is an entity and reaches the page
 * over /ws (model.ts, shownChCommand()). Both are routes curl uses on the same terms.
 *
 * An answer that is not the document is a fault OF THE ANSWER and is thrown as an ApiError
 * carrying the status the device gave, so the card says exactly that instead of drawing
 * `undefined`. A command that could not be read is not a fault: the document still stands, and
 * the command shows as a dash.
 */
export async function readCard(live: boolean, getControl: () => Promise<unknown>,
                               getCh: () => Promise<unknown>): Promise<CardReading> {
  const answer: unknown = await getControl();
  if (!isControlDocument(answer))
    throw new ApiError(200, "the answer is not a control document (schema 1)");
  if (live) return { doc: answer, ch: null };
  return { doc: answer, ch: await getCh().then(chFromEntity, () => null) };
}
