// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Turning a failed request into something an owner can act on.
//
// One rule, and tests/errors.test.ts pins it for every status: THE DEVICE'S OWN SENTENCE IS
// WHAT IS SHOWN. ot_config_strerror() writes those sentences for exactly this purpose,
// and it is the only thing that knows which field was refused. This file adds a heading that
// says what KIND of answer arrived; it never rewrites the body -- it only passes the device's
// sentence through translateDetail() (i18n/detail.ts): a firmware sentence this SPA has a
// template for comes back translated, and one it does not is shown in English rather than
// paraphrased (the same best-effort, English-passthrough rule as pages/control/errors.ts).
//
// Relative imports below carry the .ts extension, unlike the .tsx files in this page.
// That is what lets node resolve this module when the suite runs it directly
// (tests/harness.ts explains why the suite is run that way); Vite is indifferent, and
// `allowImportingTsExtensions` is already on in tsconfig.app.json.

import { ApiError } from "../../api/client.ts";
import { translateDetail } from "../../i18n/detail.ts";
import { t } from "../../i18n/index.ts";

/**
 * How loudly to say it.
 *
 * "pending" exists because of 501. Half of this API answers 501 today -- write_gate returns it
 * for every write the access policy allows but that is not implemented
 * (components/ot_http/ot_http.c:188-198) -- and that is a true statement about the
 * firmware's progress, not a fault in the device or in what the owner typed. Painting it red
 * teaches the owner that red means nothing.
 */
export type ErrorTone = "pending" | "denied" | "error" | "offline";

export interface Explanation {
  tone: ErrorTone;
  /** The HTTP status, or null when nothing answered. */
  status: number | null;
  /** This page's one sentence about what kind of answer this is. */
  headline: string;
  /** What the device said, unedited -- or, when nothing answered, who is speaking instead. */
  detail: string;
}

export function explainError(
  err: unknown,
  opts: { disconnectExpected?: boolean } = {},
): Explanation {
  if (err instanceof ApiError) {
    // 501 is tested before the 5xx bucket it belongs to, deliberately: 500 and 501 differ by
    // one digit and mean opposite things here -- "this device is broken" against "this
    // firmware has not got there yet".
    if (err.status === 501)
      return {
        tone: "pending",
        status: err.status,
        headline: t("settings.error.notImplemented.headline"),
        detail: translateDetail(err.message),
      };

    // 404 is what this device answers for an unimplemented route: no handler is registered, and
    // asset_get refuses to return the SPA shell for an /api/ path so that "missing endpoint"
    // cannot masquerade as "the endpoint returned HTML" (ot_http.c:221-227). The UI is
    // compiled into the same binary as the firmware (web/README.md), so this page can never be
    // a version ahead of the device by accident -- a 404 here means this build has no handler,
    // which is news about the firmware's progress and not about the request.
    if (err.status === 404)
      return {
        tone: "pending",
        status: err.status,
        headline: t("settings.error.noHandler.headline"),
        detail: translateDetail(err.message),
      };

    if (err.status === 401)
      return {
        tone: "denied",
        status: err.status,
        headline: t("settings.error.passwordNeeded.headline"),
        detail: translateDetail(err.message),
      };

    // 403 is the access policy, and on an unclaimed device it is the EXPECTED answer to
    // everything except joining a network (ot_policy/ot_http_policy.c:31-46). It
    // is a refusal with a cause the owner can act on, not a malfunction.
    if (err.status === 403)
      return {
        tone: "denied",
        status: err.status,
        headline: t("settings.error.writeRefused.headline"),
        detail: translateDetail(err.message),
      };

    if (err.status >= 400 && err.status < 500)
      return {
        tone: "error",
        status: err.status,
        headline: t("settings.error.badRequest.headline"),
        detail: translateDetail(err.message),
      };

    return {
      tone: "error",
      status: err.status,
      headline: t("settings.error.fault.headline"),
      detail: translateDetail(err.message),
    };
  }

  // Nothing answered. fetch() rejects with a TypeError carrying a message from the browser,
  // not from the device, and the difference has to be visible: there is no status, and
  // whatever text there is was written by Chrome.
  const raw = err instanceof Error ? err.message : typeof err === "string" ? err : "";
  // translateDetail() too, so the composed offline/disconnect sentences carry the same
  // best-effort translation as the ApiError branches; a browser string it has no template for
  // (the usual "Failed to fetch") passes through unchanged, which errors.test.ts pins.
  const browserSaid = raw !== "" ? translateDetail(raw) : t("settings.error.browserGaveNoReason");

  if (opts.disconnectExpected)
    return {
      tone: "pending",
      status: null,
      headline: t("settings.error.disconnectExpected.headline"),
      detail: t("settings.error.disconnectExpected.detail", { browserSaid }),
    };

  return {
    tone: "offline",
    status: null,
    headline: t("settings.error.offline.headline"),
    detail: t("settings.error.offline.detail", { browserSaid }),
  };
}
