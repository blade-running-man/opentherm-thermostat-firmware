// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The control card's failures in words that can be acted upon.
//
// The rule of every errors.ts in this SPA: the HEADLINE is the page's words, the DETAIL is the
// device's. The device's sentence is the whole point on this card -- "owned by Home Assistant:
// control_mode is ha", "heating_season is off: a boost would not heat" (ot_command_strerror(),
// ot_http_control_refusal()) -- so `detail` is always the device's own `err.message`, run through
// translateDetail() (i18n/detail.ts): a known firmware sentence comes back translated, an
// unknown one is shown in English rather than paraphrased, because paraphrasing here would have
// to guess which rule refused.
//
// Its own file rather than state/errors.ts: that one explains a write to ANY entity and keeps
// "the boiler has no such Data-ID" beside ownership inside one 409; here each headline names the
// control that was refused, and the device's sentence says which 409 it was.
//
// Relative imports carry the .ts extension so node can load this module (settings/tests/harness.ts).

import { ApiError } from "../../api/client.ts";
import { translateDetail } from "../../i18n/detail.ts";
import { t } from "../../i18n/index.ts";
import type { Explanation } from "../settings/errors.ts";

function browserSaid(err: unknown): string {
  const raw = err instanceof Error ? err.message : typeof err === "string" ? err : "";
  return raw !== "" ? raw : t("error.control.browserSilent");
}

/** A refused or failed write from the card. `what` names the control: "CH", "Boost", ... */
export function explainControlFailure(what: string, err: unknown): Explanation {
  if (err instanceof ApiError) {
    const status = err.status;
    const detail = translateDetail(err.message);
    switch (status) {
      case 409:
        // Ownership (the mode gives this command to another source), the season for a boost, or
        // a boiler without the Data-ID: which one is in the device's sentence, not guessed here.
        return { tone: "denied", status, headline: t("error.control.refused", { what }), detail };
      case 422:
        return { tone: "error", status, headline: t("error.control.valueRefused", { what }), detail };
      case 500:
        // NOT_SAVED: the executor accepted it and the store refused it (ot_http_control.c).
        return { tone: "error", status, headline: t("error.control.notKept", { what }), detail };
      case 503:
        return { tone: "error", status, headline: t("error.control.notCarriedOut", { what }), detail };
      case 401:
        return { tone: "denied", status, headline: t("error.control.passwordNeeded"), detail };
      case 403:
        return { tone: "denied", status, headline: t("error.control.writeRefused", { what }), detail };
      case 404:
        return { tone: "pending", status, headline: t("error.control.noRoute", { what }), detail };
      case 400:
      case 413:
        return { tone: "error", status, headline: t("error.control.badRequest", { what }), detail };
      default:
        return { tone: "error", status, headline: t("error.control.notAccepted", { what }), detail };
    }
  }
  return {
    tone: "offline",
    status: null,
    headline: t("error.control.offlineHeadline", { what }),
    detail: t("error.control.offlineDetail", { reason: browserSaid(err) }),
  };
}

/**
 * The document of an executor that has not started (executorStarted(), model.ts). Not a failed
 * request: the device answered, and answered the truth -- there is simply nothing behind the
 * document, and every control would come back 503 (ot_http_control.c, OT_THERMOSTAT_NO_TASK).
 */
export function explainNotStarted(): Explanation {
  return {
    tone: "error",
    status: null,
    headline: t("error.control.notStarted"),
    detail: t("error.control.notStartedDetail"),
  };
}

/** A failed GET /api/control. The card keeps its last document beside this. */
export function explainControlLoad(err: unknown): Explanation {
  if (err instanceof ApiError) {
    const status = err.status;
    if (status === 404 || status === 501)
      return {
        tone: "pending",
        status,
        headline: t("error.control.load.noRoute"),
        detail: t("error.control.load.noRouteDetail", { message: translateDetail(err.message) }),
      };
    if (status === 401 || status === 403)
      return {
        tone: "denied", status,
        headline: t("error.control.load.denied"),
        detail: translateDetail(err.message),
      };
    if (status < 400)
      return {
        tone: "error",
        status,
        headline: t("error.control.load.badDocument"),
        detail: translateDetail(err.message),
      };
    return {
      tone: "error", status,
      headline: t("error.control.load.fault"),
      detail: translateDetail(err.message),
    };
  }
  return {
    tone: "offline",
    status: null,
    headline: t("error.control.load.offlineHeadline"),
    detail: t("error.control.load.offlineDetail", { reason: browserSaid(err) }),
  };
}
