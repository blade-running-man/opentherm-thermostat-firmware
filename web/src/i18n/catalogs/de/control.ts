// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// German translation of ../en/control.ts. Firmware identifiers and every {placeholder} are kept
// verbatim, as en/control.ts requires. Draft wording; the owner reviews it later (non-blocking).
import type { Message } from "../../format.ts";

export const control = {
  "failsafe.entries": { one: "Eintrag", other: "Einträge" },

  "control.title": "Steuerung",
  "control.loading": "Das Gerät wird gefragt, was es gerade tut…",
  "control.live": "Live: die Änderungen des Geräts treffen ein, sobald sie geschehen.",
  "control.poll": "Die Live-Verbindung ist unterbrochen; die Karte fragt alle {n} s nach, "
    + "einschließlich des CH-Befehls.",
  "control.stack": "Stack-Reserve der Thermostat-Task: {n} Bytes.",
  "control.accepted": "{what}: angenommen",

  "control.onoff.on": "ein",
  "control.onoff.off": "aus",

  "control.failsafe.count": "{count} Failsafe-{word} seit diesem Start",
  "control.failsafe.active": "Failsafe ist aktiv",
  "control.failsafe.ran": "Der Failsafe ist seit diesem Start gelaufen",
  "control.failsafe.last": "; der letzte dauerte {duration}",

  "control.sinceHa.ha": "Letzter angenommener CH-Befehl von Home Assistant: vor {counted}.",
  "control.sinceHa.waiting": "Seit dem Start dieses Geräts oder dem Wechsel in den Home-"
    + "Assistant-Modus wurde kein CH-Befehl von Home Assistant angenommen; der Watchdog zählt "
    + "{counted} (er bleibt über einen Neustart hinweg erhalten).",
  "control.sinceHa.other": "Der Watchdog zählt {counted} ohne einen angenommenen CH-Befehl von "
    + "Home Assistant.",

  "control.season.label": "Heizsaison",

  "control.heating.title": "Zentralheizung",
  "control.ch.label": "CH",
  "control.ch.commandRow": "CH-Befehl",
  "control.ch.askedRow": "CH beim Kessel angefordert",
  "control.flow.label": "Vorlauf-Sollwert",
  "control.flow.heldRow": "Gehaltener Vorlauf-Sollwert",
  "control.flow.newRow": "Neuer Vorlauf-Sollwert",
  "control.ch.explain": "Der Befehl ist das, was der Besitzer des Kessels angefordert hat; "
    + "\"beim Kessel angefordert\" ist das CH-Bit, das das Gerät zuletzt gesendet hat. Sie "
    + "unterscheiden sich kurz nach einem neuen Sollwert — CH wartet, bis der Sollwert "
    + "gesendet wurde — und immer, wenn die Saison oder der Failsafe den Befehl überschreibt.",

  "control.dhw.label": "Warmwasser",
  "control.dhw.commandRow": "Warmwasser-Befehl",
  "control.dhw.askedRow": "Warmwasser beim Kessel angefordert",
  "control.dhw.setpointLabel": "Warmwasser-Sollwert",
  "control.dhw.newSetpointRow": "Neuer Warmwasser-Sollwert",
  "control.dhw.unset": "nicht gesetzt: der Kessel behält seinen eigenen",

  "control.boost.label": "Boost",
  "control.boost.stopLabel": "Boost stoppen",
  "control.boost.runningRow": "Läuft",
  "control.boost.running": "{sp}, {time} verbleibend",
  "control.boost.none": "Es läuft kein Boost.",
  "control.boost.bothNeeded": "beide Felder brauchen eine Zahl",
  "control.boost.setpointAria": "Boost-Vorlauf-Sollwert",
  "control.boost.lengthAria": "Boost-Dauer",
  "control.boost.lengthAriaMinutes": "Boost-Dauer, Minuten",
  "control.boost.explain": "Ein Boost heizt für die angegebenen Minuten auf dem Vorlauf-"
    + "Sollwert und gibt danach an den CH-Befehl zurück. Das Gerät entscheidet, wann er "
    + "laufen darf, und sagt, warum er es nicht darf, wenn das der Fall ist.",

  "control.button.on": "Ein",
  "control.button.off": "Aus",
  "control.button.set": "Setzen",
  "control.button.stop": "Stopp",
  "control.button.start": "Start",
  "control.validation.notANumber": "keine Zahl",

  "control.hero.ariaLabel": "Kesselstatus",
  "control.hero.heatingOn": "▲ Heizt",
  "control.hero.heatingOff": "Heizung aus",
  "control.hero.dhwOn": "▲ Warmwasser",
  "control.hero.dhwOff": "Warmwasser aus",
  "control.hero.flow": "Vorlauf",
  "control.hero.return": "Rücklauf",
  "control.hero.modulation": "Modulation",
  "control.hero.bar": "Bar",
  "control.hero.tag.flow": "VORLAUF",
  "control.hero.tag.return": "RÜCKLAUF",
  "control.hero.tag.rad": "HEIZK.",
  "control.hero.tag.sink": "BECKEN",
  "control.hero.tag.cold": "KALT",
  "control.hero.tag.boiler": "KESSEL",
  "control.hero.tag.standby": "Standby",

  "error.control.refused": "{what}: vom Gerät abgelehnt",
  "error.control.valueRefused": "{what}: der Wert wurde abgelehnt",
  "error.control.notKept": "{what}: das Gerät konnte ihn nicht übernehmen",
  "error.control.notCarriedOut": "{what}: nichts auf dem Gerät würde ihn ausführen",
  "error.control.passwordNeeded": "Das Gerät verlangt sein Weboberflächen-Passwort",
  "error.control.writeRefused": "{what}: das Gerät hat diesen Schreibvorgang abgelehnt",
  "error.control.noRoute": "{what}: diese Firmware-Version kennt diese Route nicht",
  "error.control.badRequest": "{what}: das Gerät hat die Anfrage nicht verstanden",
  "error.control.notAccepted": "{what}: das Gerät hat es nicht angenommen",
  "error.control.offlineHeadline": "{what}: keine Antwort vom Gerät",
  "error.control.offlineDetail": "Es kam nichts zurück, daher ist unklar, ob es wirksam wurde; "
    + "die Karte zeigt es, sobald das Gerät wieder antwortet. ({reason})",
  "error.control.notStarted": "Der Executor ist nicht gestartet",
  "error.control.notStartedDetail": "Das Gerät antwortet, aber seine Thermostat-Task läuft "
    + "nicht: es gibt keinen Zustand anzuzeigen, und jede Steuerung hier würde mit 503 "
    + "abgelehnt. Nach den ersten Sekunden nach einem Start bedeutet das, dass die Task gar "
    + "nicht gestartet ist, und die Log-Seite sagt, warum.",
  "error.control.load.noRoute": "Diese Firmware-Version bietet GET /api/control nicht an",
  "error.control.load.noRouteDetail": "Das Dokument des Executors ist nicht verfügbar. ({message})",
  "error.control.load.denied": "Das Gerät hat die Anzeige seiner Steuerungen verweigert",
  "error.control.load.badDocument": "Die Antwort des Geräts ist kein Steuerungsdokument, das "
    + "diese Seite lesen kann",
  "error.control.load.fault": "Das Gerät meldete einen Fehler",
  "error.control.load.offlineHeadline": "Keine Antwort vom Gerät",
  "error.control.load.offlineDetail": "Das Gerät wurde nicht erreicht, daher hat es nichts über "
    + "seine Steuerungen gesagt; die Karte behält die letzte Antwort, die sie hatte. ({reason})",
  "error.control.browserSilent": "Der Browser hat keinen Grund angegeben.",
} satisfies Partial<Record<string, Message>>;
