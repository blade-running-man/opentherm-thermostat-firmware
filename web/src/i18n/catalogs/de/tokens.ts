// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// German translation of ../en/tokens.ts. Firmware identifiers (watchdog_s, failsafe_heat_days,
// control_mode ha/local) and every {placeholder} are kept verbatim, as en/tokens.ts requires.
// Draft wording; the owner reviews it later (non-blocking).
import type { Message } from "../../format.ts";

export const tokens = {
  "token.state.season_off.label": "Heizsaison aus",
  "token.state.season_off.note": "Es wird keine Wärme angefordert, unabhängig vom CH-Schalter. Warmwasser ist davon nicht betroffen.",
  "token.state.boost.label": "Boost",
  "token.state.boost.note": "Ein zeitlich begrenzter Boost hält seinen Vorlauf-Sollwert und fordert Wärme an, bis er endet.",
  "token.state.local.label": "Lokale Steuerung",
  "token.state.local.note": "Der eigene CH-Schalter und Vorlauf-Sollwert des Geräts steuern den Kessel.",
  "token.state.ha_waiting.label": "Warten auf Home Assistant",
  "token.state.ha_waiting.note": "Home Assistant besitzt den Kessel und hat seit der Übernahme keinen CH-Befehl gesendet. CH bleibt aus, bis das geschieht oder der Watchdog abläuft.",
  "token.state.failsafe.label": "Failsafe",
  "token.state.failsafe.note": "Home Assistant ist verstummt oder blind geworden, und das Gerät führt seinen eigenen Failsafe aus.",
  "token.state.ha.label": "Home Assistant hat die Kontrolle",
  "token.state.ha.note": "Die Befehle von Home Assistant steuern den Kessel.",
  "token.state.unknown.label": "Unbekannter Zustand \"{state}\"",
  "token.state.unknown.note": "Diese Seite kennt den vom Gerät gemeldeten Zustand nicht; der eigene Name des Geräts dafür wird angezeigt.",

  "token.reason.await_setpoint": "CH ist gewünscht; das Gerät wartet, bis der neue Vorlauf-Sollwert den Kessel erreicht, bevor es Wärme anfordert.",
  "token.reason.fs_disarmed": "CH ist gesperrt: Home Assistant hat innerhalb von failsafe_heat_days keine Wärme angefordert.",
  "token.reason.fs_blind": "Keine aktuelle Raumtemperatur: Heizen blind auf den Failsafe-Sollwert.",
  "token.reason.fs_room_cold": "Der Raum ist unter das Failsafe-Ziel gefallen: Heizen.",
  "token.reason.fs_room_warm": "Der Raum hat das Failsafe-Ziel erreicht: CH ist aus.",
  "token.reason.min_cycle": "Das CH-Bit wird für die minimale Failsafe-Zykluszeit gehalten.",
  "token.reason.other": "Grund: {reason}",

  "token.cause.watchdog": "Kein CH-Befehl von Home Assistant seit mehr als watchdog_s.",
  "token.cause.ha_blind": "Der eigene Raumsensor von Home Assistant ist veraltet, während dessen Befehle weiterhin eintreffen.",
  "token.cause.other": "Ursache: {cause}",

  "token.mode.ha": "Home Assistant besitzt die CH- und Warmwasser-Befehle (control_mode ha). Ein Schreibvorgang von dieser Seite wird trotzdem gesendet, und die Antwort des Geräts wird angezeigt.",
  "token.mode.local": "Die eigenen Bedienelemente dieses Geräts besitzen die CH- und Warmwasser-Befehle (control_mode local): diese Seite oder ein beliebiger REST-Client. Befehle von Home Assistant werden abgelehnt.",

  "token.avail.ok": "verfügbar",
  "token.avail.invalid": "ungültige Daten",
  "token.avail.unsupported": "vom Kessel nicht unterstützt",
  "token.avail.unknown": "noch keine Antwort",

  "error.detail.no_entity": "keine solche Entität",
  "error.detail.read_only": "Entität ist schreibgeschützt",
  "error.detail.out_of_range": "Wert außerhalb des zulässigen Bereichs",
  "error.detail.unsupported_id": "Kessel unterstützt diese Data-ID nicht",
  "error.detail.owned_ha": "im Besitz von Home Assistant: {1}",
  "error.detail.owned_local": "im Besitz des Thermostats: {1}",
  "error.detail.season_off_boost": "heating_season ist aus: ein Boost würde nicht heizen",
} satisfies Partial<Record<string, Message>>;
