// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The State page's failures in words that can be acted upon.
//
// Its own file, rather than explainError() from pages/settings: that one puts every 4xx under one
// headline, and this page must keep 405, 409 and 422 apart (below). The Explanation type and the
// Notice component are shared, so the tones, the colours and the markup are exactly the same.
//
// The rule is unchanged: the HEADLINE is the page's words, the DETAIL is the device's words run
// through translateDetail() (i18n/detail.ts) -- a known firmware sentence comes back translated,
// an unknown one is shown in English rather than paraphrased. Only the device knows what exactly
// it refused to do: the messages are written by ot_command_strerror(), and the endpoint hands
// them over in the `error` field.
//
// THE MAIN THING HERE IS THAT 409 AND 422 ARE KEPT APART. "The boiler does not support this
// Data-ID" and "the value is out of bounds" are different diagnoses and different actions: in
// the first case writing here is pointless for ever, in the second the number has to change.
// The firmware is built around not confusing the two (unsupported
// appears after two unknown-dataid answers); collapsing them into a single "it did not work"
// at the last step would throw that work away.
//
// Relative imports carry the .ts extension so node can load this module (settings/tests/harness.ts).

import { ApiError } from "../../api/client.ts";
import { translateDetail } from "../../i18n/detail.ts";
import { t } from "../../i18n/index.ts";
import type { Explanation } from "../settings/errors.ts";

function browserSaid(err: unknown): string {
  const raw = err instanceof Error ? err.message : typeof err === "string" ? err : "";
  return raw !== "" ? raw : t("error.state.browserSilent");
}

/** A read failure: `GET /api/entities` or `GET /api/state`. */
export function explainStateFailure(err: unknown): Explanation {
  if (err instanceof ApiError) {
    if (err.status === 404 || err.status === 501)
      return {
        tone: "pending",
        status: err.status,
        headline: t("error.state.noRoute"),
        detail: t("error.state.noRouteDetail", { message: translateDetail(err.message) }),
      };

    if (err.status === 401 || err.status === 403)
      return {
        tone: "denied",
        status: err.status,
        headline: t("error.state.denied"),
        detail: translateDetail(err.message),
      };

    return {
      tone: "error",
      status: err.status,
      headline: t("error.state.fault"),
      detail: translateDetail(err.message),
    };
  }

  return {
    tone: "offline",
    status: null,
    headline: t("error.state.offlineHeadline"),
    detail: t("error.state.offlineDetail", { reason: browserSaid(err) }),
  };
}

/**
 * A write failure: `POST /api/entities/<key>`.
 *
 * Every code is handled separately, because every one of them calls for its own action. The
 * catch-all branch at the end is needed, but it should not be reached regularly: the codes
 * are enumerated in components/ot_http/ot_http_registry.c and there are no others there.
 */
export function explainWriteFailure(err: unknown): Explanation {
  if (err instanceof ApiError) {
    const detail = translateDetail(err.message);
    switch (err.status) {
      case 405:
        // A property of THE ENTITY ITSELF, not of the caller's rights: nobody may ever write
        // here, which is why it has no input field. This code can only arrive in response to
        // a request from curl or from a stale page.
        return { tone: "denied", status: err.status, headline: t("error.state.write.readOnly"), detail };

      case 409:
        // TWO causes share this code (ot_http_registry.c): the boiler has no such Data-ID, or
        // the control mode gives this value to another source. The headline
        // names neither, because the page cannot tell them apart; the device's sentence in
        // the detail says which it was. What they share is what keeps them apart from 422:
        // no number typed here will be accepted. DO NOT headline this as "not supported by
        // the boiler" alone: in Home Assistant mode that sentence would be false.
        return {
          tone: "denied",
          status: err.status,
          headline: t("error.state.write.refused"),
          detail: t("error.state.write.refusedDetail", { message: detail }),
        };

      case 422:
        return {
          tone: "error",
          status: err.status,
          headline: t("error.state.write.outOfBounds"),
          detail: t("error.state.write.outOfBoundsDetail", { message: detail }),
        };

      case 404:
        return { tone: "pending", status: err.status, headline: t("error.state.write.noEntity"), detail };

      case 400:
      case 413:
        return { tone: "error", status: err.status, headline: t("error.state.write.badRequest"), detail };

      case 401:
      case 403:
        return { tone: "denied", status: err.status, headline: t("error.state.write.denied"), detail };

      default:
        return { tone: "error", status: err.status, headline: t("error.state.write.notAccepted"), detail };
    }
  }

  return {
    tone: "offline",
    status: null,
    headline: t("error.state.offlineHeadline"),
    detail: t("error.state.write.offlineDetail", { reason: browserSaid(err) }),
  };
}
