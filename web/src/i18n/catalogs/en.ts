// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The English catalog -- the source of truth for the KEY SET, composed from the per-namespace
// files under en/. `MsgKey` is `keyof typeof en`. The spread of `satisfies`-typed namespace
// objects keeps the literal keys, so a missing key elsewhere is still a tsc error.

import type { Message } from "../format.ts";
import { common } from "./en/common.ts";
import { nav } from "./en/nav.ts";
import { tokens } from "./en/tokens.ts";
import { control } from "./en/control.ts";
import { settings } from "./en/settings.ts";
import { state } from "./en/state.ts";
import { boiler } from "./en/boiler.ts";
import { log } from "./en/log.ts";
import { update } from "./en/update.ts";

export const en = {
  ...common, ...nav, ...tokens, ...control, ...settings,
  ...state, ...boiler, ...log, ...update,
} satisfies Record<string, Message>;

/** The key set every catalog must cover. */
export type MsgKey = keyof typeof en;
