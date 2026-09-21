// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Nav + app-chrome strings (the nav links, theme toggle, language label).
import type { Message } from "../../format.ts";

export const nav = {
  "nav.control": "Steuerung",
  "nav.boiler": "Kessel",
  "nav.state": "Status",
  "nav.log": "Protokoll",
  "nav.settings": "Einstellungen",
  "nav.update": "Firmware-Update",
  "nav.language": "Sprache",
  "nav.theme.toggle": "Thema umschalten",
  "nav.theme.toLight": "Zum hellen Thema wechseln",
  "nav.theme.toDark": "Zum dunklen Thema wechseln",
  "conn.connected": "Verbunden",
  "conn.connecting": "Verbindung wird aufgebaut",
  "conn.disconnected": "Getrennt",
  "conn.connected.title": "Das Gerät sendet Änderungen, sobald sie eintreten",
  "conn.connecting.title": "Verbindung zum Gerät wird geöffnet",
  "conn.disconnected.title": "Keine Verbindung — die Messwerte können veraltet sein",
} satisfies Partial<Record<string, Message>>;
