// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Nav + app-chrome strings (the nav links, theme toggle, language label).
import type { Message } from "../../format.ts";

export const nav = {
  "nav.control": "Control",
  "nav.boiler": "Boiler",
  "nav.state": "State",
  "nav.log": "Log",
  "nav.settings": "Settings",
  // The page's own heading (Update.tsx), so link and page share one name.
  "nav.update": "Firmware Update",
  "nav.language": "Language",
  "nav.theme.toggle": "Toggle theme",
  "nav.theme.toLight": "Switch to light theme",
  "nav.theme.toDark": "Switch to dark theme",
  "conn.connected": "Connected",
  "conn.connecting": "Connecting",
  "conn.disconnected": "Disconnected",
  "conn.connected.title": "The device sends changes as they happen",
  "conn.connecting.title": "Opening the connection to the device",
  "conn.disconnected.title": "No connection — the readings may be out of date",
} satisfies Record<string, Message>;
