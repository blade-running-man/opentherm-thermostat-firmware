// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// German translation of ../en/log.ts (page chrome + LOG_TEMPLATES entries).
import type { Message } from "../../format.ts";

export const log = {
  "log.title": "Protokoll",
  "log.follow": "Verfolgen (alle {n} s)",
  "log.hint": "Das Gerät behält seine letzten 80 Zeilen. Zeiten sind seit dem Start.",
  "log.loading": "Protokoll wird vom Gerät angefordert…",
  "log.empty": "Das Protokoll ist leer: das Gerät ist gerade gestartet.",

  "log.bus.answering": "Kessel antwortet",
  "log.bus.not_answering": "Kessel antwortet nicht ({1}) nach {2} Versuchen; das Abfragen läuft weiter",
  "log.bus.id_unsupported": "ID {1} wird vom Kessel nicht unterstützt; aus dem Abfragering entfernt",
  "log.bus.line_test_finished": "Leitungstest beendet, Abfrage fortgesetzt",

  "log.mqtt.connected": "Broker verbunden",
  "log.mqtt.lost": "Broker-Verbindung verloren; erneuter Versuch alle 10 s, sonst ändert sich nichts",
  "log.mqtt.refused": "Broker hat die Verbindung abgelehnt: {1} (Code {2})",
  "log.mqtt.unreachable": "Broker nicht erreichbar; {1} Versuche bisher, erneuter Versuch alle 10 s",
  "log.mqtt.not_configured": "Kein Broker konfiguriert; MQTT bleibt aus",
  "log.mqtt.command_refused": "Befehl abgelehnt: {1} ({2} weitere seit der letzten Zeile)",
  "log.mqtt.acl_refused": "Der Broker hat das Befehls-Abonnement abgelehnt: ACL prüfen",
  "log.mqtt.discovery_failed": "Discovery für {1} nicht veröffentlicht: es wird nicht dargestellt",

  "log.net.captive_up": "Captive Portal aktiv: jeder Name löst auf {1}",
  "log.net.captive_dns_failed": "Captive-DNS ist nicht gestartet ({1}); die Einrichtungsseite ist weiterhin unter {2} erreichbar",
} satisfies Partial<Record<string, Message>>;
