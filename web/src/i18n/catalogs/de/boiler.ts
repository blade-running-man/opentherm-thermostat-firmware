// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Boiler page. See en/boiler.ts for what stays untranslated (row.type, decode columns,
// formatAge, entity names).
import type { Message } from "../../format.ts";

export const boiler = {
  "boiler.title": "Kessel",
  "boiler.loading": "Das Gerät wird nach dem Bus gefragt…",

  "boiler.counter.cycles": "Zyklen",
  "boiler.counter.answers": "Antworten",
  "boiler.counter.failures": "Fehler",
  "boiler.counter.overdue": "überfällig",
  "boiler.uptimeHint": "Das Gerät läuft seit {uptime}. Aktualisierung alle {seconds} s.",

  "boiler.answering": "Der Kessel antwortet",
  "boiler.silent": "Der Kessel antwortet nicht",
  "boiler.silentHint": "Der Master sendet Anfragen{cycles} und es kommt keine Antwort. Prüfen "
    + "Sie in dieser Reihenfolge: die Stromversorgung der Schnittstellenplatine, die Polarität "
    + "des Paares, die Eingangsinvertierung.",
  "boiler.silentHint.cyclesSuffix": " ({count} Zyklen)",

  "boiler.line": "Leitung: {text}.",
  "boiler.line.idle": "Eingang im Ruhezustand (0 %) — so, wie es sein sollte, während der "
    + "Kessel schweigt",
  "boiler.line.inverted": "Eingang dauerhaft aktiv ({duty} %) — die Eingangspolarität scheint "
    + "invertiert zu sein",
  "boiler.line.toggling": "Eingang wechselt ({duty} %) — Rahmen liegen auf der Leitung",

  "boiler.scan.run": "Jede Data-ID abfragen (~4 min)",
  "boiler.scan.running": "Durchlauf läuft: {done} von {total}",
  "boiler.scan.hint": "Fragt den Kessel nach jeder Kennung von 0 bis 127 einmal ab und trägt "
    + "die Antworten in die Tabelle unten ein. Nur Lesezugriff — der Durchlauf schreibt "
    + "nichts. Eine Kennung, die nach dem Durchlauf in der Tabelle fehlt, hat gar nicht "
    + "geantwortet; eine Zeile mit \"unknown-dataid\" bedeutet, dass der Kessel ausdrücklich "
    + "gesagt hat, dass es so etwas nicht gibt. Das sind unterschiedliche Dinge, und beide "
    + "sind nützlich.",
  "boiler.scan.forbidden": "Das Gerät ist noch nicht im Netzwerk: bei der Ersteinrichtung ist "
    + "nur die Einrichtung selbst erlaubt. Verbinden Sie WLAN auf der Einstellungsseite.",
  "boiler.scan.failed": "Die Anfrage ist nicht durchgekommen.",

  "boiler.lineTest.run": "Leitung mit einem Multimeter testen",
  "boiler.lineTest.running": "Test läuft, 20 s…",
  "boiler.lineTest.hint": "Zwanzig Sekunden lang wechselt die Leitung langsam, zwei Sekunden "
    + "je Zustand. Messen Sie die Spannung an den Klemmen des Kessels: sie sollte von 15–24 V "
    + "auf sieben oder darunter fallen und wieder zurückkehren. Ändert sie sich nicht, "
    + "funktioniert die Ausgangsstufe des Adapters nicht, oder der Bus hat keine Spannung.",
  "boiler.lineTest.warning": "In der Zwischenzeit gehen keine Rahmen hinaus, daher wird der "
    + "Kessel höchstwahrscheinlich anspringen",
  "boiler.lineTest.warningNote": " — das macht der Kessel, wenn der Thermostat verstummt "
    + "(ein verstummter Thermostat wird als Wärmeanforderung gelesen). Das ist erwartet.",
  "boiler.lineTest.failed": "Die Anfrage ist nicht durchgekommen: möglicherweise läuft "
    + "bereits ein Test.",

  "boiler.raw.empty": "Noch keine Data-ID hat geantwortet. Solange der Kessel schweigt, ist "
    + "die Tabelle leer — das ist ein Zustand, kein Fehler der Seite.",
  "boiler.raw.answers": "{count} Antworten",
  "boiler.raw.col.id": "ID",
  "boiler.raw.col.name": "Name",
  "boiler.raw.col.type": "Antworttyp",
  "boiler.raw.col.raw": "Rohwert",
  "boiler.raw.col.f88": "f8.8",
  "boiler.raw.col.u16": "u16",
  "boiler.raw.col.s16": "s16",
  "boiler.raw.col.hiLo": "High-/Low-Byte",
  "boiler.raw.col.flags": "Flags",
  "boiler.raw.col.age": "Alter",
} satisfies Partial<Record<string, Message>>;
