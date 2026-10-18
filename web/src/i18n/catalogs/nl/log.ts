// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Dutch translation of ../en/log.ts (page chrome + LOG_TEMPLATES entries).
import type { Message } from "../../format.ts";

export const log = {
  "log.title": "Logboek",
  "log.follow": "Volgen (elke {n} s)",
  "log.hint": "Het apparaat bewaart de laatste 80 regels. Tijden zijn sinds het opstarten.",
  "log.loading": "Logboek wordt bij het apparaat opgevraagd…",
  "log.empty": "Het logboek is leeg: het apparaat is net gestart.",

  "log.bus.answering": "ketel antwoordt",
  "log.bus.not_answering": "ketel antwoordt niet ({1}) na {2} pogingen; polling gaat door",
  "log.bus.id_unsupported": "ID {1} wordt niet ondersteund door de ketel; verwijderd uit de polling-ring",
  "log.bus.line_test_finished": "lijntest voltooid, polling hervat",

  "log.mqtt.connected": "broker verbonden",
  "log.mqtt.lost": "broker verbinding verbroken; nieuwe poging elke 10 s, verder verandert er niets",
  "log.mqtt.refused": "broker heeft de verbinding geweigerd: {1} (code {2})",
  "log.mqtt.unreachable": "broker onbereikbaar; {1} pogingen tot nu toe, nieuwe poging elke 10 s",
  "log.mqtt.not_configured": "geen broker geconfigureerd; MQTT blijft uit",
  "log.mqtt.command_refused": "opdracht geweigerd: {1} ({2} meer sinds de laatste regel)",
  "log.mqtt.acl_refused": "de broker heeft het opdrachtabonnement geweigerd: controleer de ACL",
  "log.mqtt.discovery_failed": "discovery voor {1} niet gepubliceerd: wordt niet weergegeven",

  "log.net.captive_up": "captive portal actief: elke naam wordt herleid naar {1}",
  "log.net.captive_dns_failed": "captive DNS is niet gestart ({1}); de installatiepagina is nog bereikbaar op {2}",
} satisfies Partial<Record<string, Message>>;
