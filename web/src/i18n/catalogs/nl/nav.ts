// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Nav + app-chrome strings (the nav links, theme toggle, language label).
import type { Message } from "../../format.ts";

export const nav = {
  "nav.control": "Bediening",
  "nav.boiler": "Ketel",
  "nav.state": "Status",
  "nav.log": "Logboek",
  "nav.settings": "Instellingen",
  "nav.update": "Firmware-update",
  "nav.language": "Taal",
  "nav.theme.toggle": "Thema wisselen",
  "nav.theme.toLight": "Naar licht thema wisselen",
  "nav.theme.toDark": "Naar donker thema wisselen",
  "conn.connected": "Verbonden",
  "conn.connecting": "Verbinden",
  "conn.disconnected": "Verbinding verbroken",
  "conn.connected.title": "Het apparaat stuurt wijzigingen zodra ze gebeuren",
  "conn.connecting.title": "Verbinding met het apparaat wordt geopend",
  "conn.disconnected.title": "Geen verbinding — de waarden kunnen verouderd zijn",
} satisfies Partial<Record<string, Message>>;
