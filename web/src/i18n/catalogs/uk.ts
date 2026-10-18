// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Ukrainian. Its cardinal rules use four plural categories -- one/few/many/other -- so the
// plural entry supplies all four (the parity test requires exactly the categories
// Intl.PluralRules("uk") reports, plus `other`).

import type { Message } from "../format.ts";
import type { MsgKey } from "./en.ts";
import { common } from "./uk/common.ts";
import { nav } from "./uk/nav.ts";
import { tokens } from "./uk/tokens.ts";
import { control } from "./uk/control.ts";
import { settings } from "./uk/settings.ts";
import { state } from "./uk/state.ts";
import { boiler } from "./uk/boiler.ts";
import { log } from "./uk/log.ts";
import { update } from "./uk/update.ts";

export const uk: Record<MsgKey, Message> = {
  ...common, ...nav, ...tokens, ...control, ...settings,
  ...state, ...boiler, ...log, ...update,
};
