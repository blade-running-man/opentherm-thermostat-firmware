// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// State page: table column headers, the write action, the page chrome, and its failures.
import type { Message } from "../../format.ts";

export const state = {
  "state.col.entity": "Entität",
  "state.col.value": "Wert",
  "state.col.availability": "Verfügbarkeit",
  "state.col.age": "Alter",
  "state.col.write": "Schreiben",
  "state.write": "Schreiben: {name}",

  "state.title": "Zustand",
  "state.loading": "Frage das Gerät nach seiner Entitätsliste…",
  "state.hint": "Die Werte werden alle {seconds} s aktualisiert. Die Eingabegrenzen sind die, "
    + "die der Kessel selbst gemeldet hat; solange er sie nicht gemeldet hat, gelten die "
    + "eigenen Grenzen der Tabelle. Ein Schreibvorgang antwortet mit „queued“: die Bus-Warteschlange "
    + "hält einen Schreibvorgang, und ob der Kessel ihn angenommen hat, zeigt sich im Wert der "
    + "Tabelle eine Abfragerunde später.",
  "state.value.yes": "ja",
  "state.value.no": "nein",
  "state.readOnly": "nur lesbar",
  "state.writeAction": "Schreiben",
  "state.notANumber": "keine Zahl",
  "state.queued": "{name}: eingereiht",

  "error.state.noRoute": "Diese Firmware-Version bietet die Entitätsliste nicht an",
  "error.state.noRouteDetail": "Die Endpunkte /api/entities und /api/state kamen mit dem "
    + "Zustandsmodell; die Firmware, die derzeit auf dem Gerät läuft, hat sie nicht. ({message})",
  "error.state.denied": "Das Gerät hat die Anzeige seines Zustands verweigert",
  "error.state.fault": "Das Gerät meldete einen Fehler",
  "error.state.offlineHeadline": "Keine Antwort vom Gerät",
  "error.state.offlineDetail": "Das Gerät wurde nicht erreicht, daher hat es nichts über "
    + "seinen Zustand gesagt. ({reason})",
  "error.state.browserSilent": "Der Browser hat keinen Grund angegeben.",

  "error.state.write.readOnly": "Diese Entität ist nur lesbar",
  "error.state.write.refused": "Dieser Schreibvorgang wird unabhängig vom Wert abgelehnt",
  "error.state.write.refusedDetail": "Es liegt nicht an der Zahl: entweder hat der Kessel "
    + "zweimal geantwortet, dass er diese Data-ID nicht kennt, und das Gerät hat aufgehört zu "
    + "fragen, oder der aktuelle Steuerungsmodus gibt diesen Wert an eine andere Quelle. "
    + "({message})",
  "error.state.write.outOfBounds": "Der Wert liegt außerhalb der Grenzen",
  "error.state.write.outOfBoundsDetail": "Die Anfrage wurde verstanden; es ist die Zahl, die "
    + "abgelehnt wurde. Die Grenzen neben dem Eingabefeld sind die, die der Kessel selbst "
    + "gemeldet hat, oder die eigenen der Tabelle, solange er es nicht getan hat. ({message})",
  "error.state.write.noEntity": "Diese Firmware-Version kennt diese Entität nicht",
  "error.state.write.badRequest": "Das Gerät hat die Anfrage nicht verstanden",
  "error.state.write.denied": "Das Gerät hat diesen Schreibvorgang abgelehnt",
  "error.state.write.notAccepted": "Das Gerät hat den Schreibvorgang nicht angenommen",
  "error.state.write.offlineDetail": "Das Gerät wurde nicht erreicht. Ob der Wert geschrieben "
    + "wurde, ist unbekannt; prüfen Sie es in der Tabelle, sobald die Verbindung wieder "
    + "besteht. ({reason})",
} satisfies Partial<Record<string, Message>>;
