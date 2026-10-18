// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// A failed /api/ot/raw request in words that can be read and acted upon.
//
// Its own, rather than explainError() from pages/settings, because its main message is about
// one endpoint: a 404 here means this build has no /api/ot/raw, and the page can show nothing
// without it. It used to be separate for a second reason as well -- this page spoke Russian --
// and the translation removed that one; the endpoint is reason enough. The Explanation type and
// the Notice component are shared, so the tones, the colours and the markup are the same.
//
// The rule is the same as in settings/errors.ts: the HEADLINE is the page's words, the
// DETAIL is the device's words unedited. Only the device knows what exactly it refused to do.

import { ApiError } from "../../api/client.ts";
import type { Explanation } from "../settings/errors.ts";

export function explainOtRawFailure(err: unknown): Explanation {
  if (err instanceof ApiError) {
    // 404 and 501 are the most likely answer, and it is NOT red. The endpoint appears in the
    // firmware separately from this page, and the SPA is compiled into the same binary, so a
    // 404 here means exactly one thing: the build on the device has not grown it yet. Red in
    // this spot would teach the owner that red means nothing.
    if (err.status === 404 || err.status === 501)
      return {
        tone: "pending",
        status: err.status,
        headline: "This firmware build does not serve /api/ot/raw",
        detail:
          "The route arrives with the OpenTherm bus; the firmware now on the device does not "
          + `have it, and this page can show nothing without it. (${err.message})`,
      };

    if (err.status === 401 || err.status === 403)
      return {
        tone: "denied",
        status: err.status,
        headline: "The device refused to show the state of the bus",
        detail: err.message,
      };

    return {
      tone: "error",
      status: err.status,
      headline: "The device reported a fault",
      detail: err.message,
    };
  }

  // There was no answer at all: fetch rejects the promise with a TypeError whose text was
  // written by the browser, not the device — and that difference shows in the absent status.
  const raw = err instanceof Error ? err.message : typeof err === "string" ? err : "";
  return {
    tone: "offline",
    status: null,
    headline: "No answer from the device",
    detail:
      "Nothing reached the device, so it has said nothing about the bus. "
      + `(${raw !== "" ? raw : "The browser gave no reason."})`,
  };
}
