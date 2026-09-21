// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Dutch. See de.ts for what the type and the parity test enforce.

import type { Message } from "../format.ts";
import type { MsgKey } from "./en.ts";
import { common } from "./nl/common.ts";
import { nav } from "./nl/nav.ts";
import { tokens } from "./nl/tokens.ts";
import { control } from "./nl/control.ts";
import { settings } from "./nl/settings.ts";
import { state } from "./nl/state.ts";
import { boiler } from "./nl/boiler.ts";
import { log } from "./nl/log.ts";
import { update } from "./nl/update.ts";

export const nl: Record<MsgKey, Message> = {
  ...common, ...nav, ...tokens, ...control, ...settings,
  ...state, ...boiler, ...log, ...update,
};
