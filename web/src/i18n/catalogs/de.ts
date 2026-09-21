// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// German. `Record<MsgKey, Message>` forces every key of en.ts to be present; the parity test
// (catalogs.test.ts) additionally forbids empty values, requires the same {placeholders} as
// English, and requires every plural category German's rules use plus `other`.

import type { Message } from "../format.ts";
import type { MsgKey } from "./en.ts";
import { common } from "./de/common.ts";
import { nav } from "./de/nav.ts";
import { tokens } from "./de/tokens.ts";
import { control } from "./de/control.ts";
import { settings } from "./de/settings.ts";
import { state } from "./de/state.ts";
import { boiler } from "./de/boiler.ts";
import { log } from "./de/log.ts";
import { update } from "./de/update.ts";

export const de: Record<MsgKey, Message> = {
  ...common, ...nav, ...tokens, ...control, ...settings,
  ...state, ...boiler, ...log, ...update,
};
