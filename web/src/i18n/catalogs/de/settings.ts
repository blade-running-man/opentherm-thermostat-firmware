// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// German translations for the Settings page. See en/settings.ts for provenance and the
// pinned-key notes. Every {placeholder} is kept identical to the English; firmware identifiers
// and config keys are not translated.
import type { Message } from "../../format.ts";

export const settings = {
  "settings.title": "Einstellungen",
  "settings.readOnly.headline": "Diese Einstellungen wurden von einer neueren Firmware geschrieben",
  "settings.readOnly.detail":
    "Diese Firmware-Version versteht das Layout der gespeicherten Konfiguration nicht und "
    + "verweigert daher das Überschreiben, um sie nicht zu beschädigen. Spielen Sie die "
    + "vorherige Firmware erneut auf, oder setzen Sie die Einstellungen über die Taste zurück, "
    + "bevor Sie hier etwas ändern.",
  "settings.load.hint":
    "Broker- und Geräteeinstellungen können erst angezeigt werden, wenn sie gelesen werden "
    + "können. Das WLAN-Formular oben hängt nicht davon ab.",
  "settings.tryAgain": "Erneut versuchen",
  "settings.loading": "Konfiguration des Geräts wird gelesen…",
  "settings.save": "Einstellungen speichern",
  "settings.saved": "Einstellungen gespeichert",
  "settings.saveBar.hint": "Speichert Broker- und Geräteeinstellungen zusammen. Das Netzwerk wird nicht berührt.",
  "settings.notSent": "Nicht gesendet — diese Seite hat nichts verlassen",
  "settings.refusedValue": "Das Gerät hat diesen Wert abgelehnt; der Grund steht im Hinweis unten.",
  "settings.nothingChanged": "Nichts geändert.",
  "settings.sends": "Sendet {keys} und sonst nichts.",

  "settings.broker.title": "MQTT-Broker",
  "settings.broker.subtitle": "Wohin das Gerät veröffentlicht, und wo Home Assistant es findet.",
  "settings.broker.host.label": "Broker-Host",
  "settings.broker.host.placeholder": "192.168.1.10 oder homeassistant.local",
  "settings.broker.port.label": "Broker-Port",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Broker-Benutzername",
  "settings.broker.password.label": "Broker-Passwort",
  "settings.broker.password.warning":
    "Diese Verbindung ist in dieser Version nicht verschlüsselt, daher ist dieses Passwort "
    + "für alles in Ihrem Netzwerk sichtbar. Verwenden Sie kein Passwort, das Sie auch "
    + "anderswo benutzen.",
  "settings.broker.topicPrefix.label": "Themen-Präfix",
  "settings.broker.discovery.label": "Home-Assistant-Discovery veröffentlichen",
  "settings.broker.discovery.hint":
    "Das Ändern des Präfixes verschiebt jedes Topic. Home Assistant behält die unter dem "
    + "alten Präfix bereits erkannten Entitäten, bis das Gerät sie zurückzieht — erwarten Sie "
    + "also eine Weile beide Varianten nach einer Änderung.",

  "settings.device.title": "Gerät",
  "settings.device.subtitle": "Wie es heißt, und wer es ändern darf.",
  "settings.device.name.label": "Gerätename",
  "settings.device.name.placeholder": "Lüftung",
  "settings.device.name.hint":
    "Nur ein Anzeigename. Die Identität des Geräts — seine Broker-Client-ID und seine "
    + "Home-Assistant-Geräte-ID — wird von seiner MAC-Adresse abgeleitet, sodass eine "
    + "Umbenennung hier kein zweites Gerät in Home Assistant erzeugen kann.",
  "settings.device.password.label": "Passwort für die Weboberfläche",
  "settings.device.password.warnTitle": "Für dieses Gerät gibt es kein „Passwort vergessen“.",
  "settings.device.password.warn1a":
    "Wenn Sie es verlieren, führt der einzige Weg zurück über die Taste am Gerät: fünf "
    + "Sekunden halten, ",
  "settings.device.password.warn1em": "nachdem es gestartet ist",
  "settings.device.password.warn1b":
    ". Das löscht die WLAN-Zugangsdaten und dieses Passwort zusammen und lässt die "
    + "Broker-Einstellungen unangetastet.",
  "settings.device.password.warn2a": "Wird die Taste ",
  "settings.device.password.warn2em": "während",
  "settings.device.password.warn2b":
    " das Gerät hochfährt gehalten, passiert etwas ganz anderes — der Chip wechselt in "
    + "seinen Firmware-Lader, was genauso aussieht wie ein defektes Gerät. Erst hochfahren "
    + "lassen, warten, dann halten.",
  "settings.device.password.noneSet":
    "Es ist kein Passwort gesetzt. Jeder, der dieses Gerät im Netzwerk erreichen kann, "
    + "kann diese Einstellungen ändern — für ein Gerät im Heimnetz eine vertretbare Wahl, "
    + "und bewusst die Voreinstellung.",

  "settings.executor.title": "Regler",
  "settings.executor.subtitle": "Modus, Watchdog, Failsafe und das Vorlaufband.",
  "settings.executor.mode.local": "Lokal — die eigenen Bedienelemente dieses Geräts",
  "settings.executor.mode.ha": "Home Assistant — benötigt einen Broker",
  "settings.executor.save": "Reglereinstellungen speichern",
  "settings.executor.saved": "Reglereinstellungen gespeichert",
  "settings.executor.label.control_mode": "Steuerungsmodus",
  "settings.executor.label.watchdog_s": "Watchdog (s)",
  "settings.executor.label.failsafe_setpoint_dc": "Failsafe-Vorlaufsollwert (°C)",
  "settings.executor.label.failsafe_room_target_dc": "Failsafe-Raumsollwert (°C)",
  "settings.executor.label.failsafe_heat_days": "Failsafe-Heiztage",
  "settings.executor.label.failsafe_min_cycle_s": "Failsafe-Mindestzykluszeit (s)",
  "settings.executor.label.flow_min_dc": "Niedrigster Vorlaufsollwert (°C)",
  "settings.executor.label.flow_max_dc": "Höchster Vorlaufsollwert (°C)",
  "settings.executor.note.watchdog_s":
    "Wie lange Home Assistant zu CH schweigen darf, bevor der Failsafe übernimmt.",
  "settings.executor.note.failsafe_setpoint_dc":
    "Der Vorlaufsollwert, mit dem der Failsafe heizt, und der ab dem Start gehaltene Wert.",
  "settings.executor.note.failsafe_room_target_dc":
    "Die Raumtemperatur, die der Failsafe hält, solange ein Raumsensor frisch ist.",
  "settings.executor.note.failsafe_heat_days":
    "Der Failsafe heizt nur, wenn Home Assistant innerhalb dieser Anzahl Tage Wärme "
    + "angefordert hat.",
  "settings.executor.note.failsafe_min_cycle_s":
    "Die kürzeste Ein- und die kürzeste Aus-Zeit des Failsafe.",
  "settings.executor.note.flow_min_dc":
    "Kein CH-Sollwert unter diesem Wert wird angenommen, von niemandem. Halten Sie ihn bei "
    + "oder über dem eigenen Parameter E des Kessels: eine Anforderung unter E wird nicht "
    + "ausgeführt.",
  "settings.executor.note.flow_max_dc": "Kein CH-Sollwert über diesem Wert wird angenommen, von niemandem.",
  "settings.executor.problem": "{label}: {kind} wird benötigt.",
  "settings.executor.kind.decimal": "eine Temperatur in Grad, mit höchstens einer Dezimalstelle",
  "settings.executor.kind.whole": "eine ganze Zahl",

  "settings.room.title": "Raumquelle (MQTT)",
  "settings.room.subtitle":
    "Eine Raumtemperatur, die Home Assistant an das Gerät veröffentlicht (docs/ha-room-source.md).",
  "settings.room.role.room": "Raum — steuert den Failsafe",
  "settings.room.role.ambient": "Umgebung — nur angezeigt, steuert nie",
  "settings.room.stale.hint":
    "Wie lange das Gerät ohne neue Veröffentlichung wartet, bevor diese Quelle als veraltet gilt.",
  "settings.room.forwarded.hint":
    "Wenn aktiviert, zwingt ein veralteter Wert den Failsafe, blind zu heizen, statt den "
    + "letzten Raumwert zu halten (der ha_blind-Fall).",
  "settings.room.save": "Raumquelleneinstellungen speichern",
  "settings.room.saved": "Raumquelleneinstellungen gespeichert",
  "settings.room.label.room_mqtt_enable": "MQTT-Raumquelle verwenden",
  "settings.room.label.room_mqtt_role": "Rolle",
  "settings.room.label.room_mqtt_stale_s": "Veraltet nach (s)",
  "settings.room.label.room_mqtt_ha_forwarded": "Home Assistant leitet einen veralteten Wert weiter",
  "settings.room.problem": "{label}: eine ganze Zahl wird benötigt.",

  "settings.wifi.title": "WLAN",
  "settings.wifi.subtitle": "Die Liste zeigt, was dieses Gerät hören kann — nicht, was Ihr Telefon zeigt.",
  "settings.wifi.network.label": "Netzwerk",
  "settings.wifi.network.choose": "— Netzwerk wählen —",
  "settings.wifi.network.currentlyConfigured": "derzeit konfiguriert",
  "settings.wifi.network.open": "offen",
  "settings.wifi.network.manualOption": "Anderes / verstecktes Netzwerk — Namen eingeben…",
  "settings.wifi.network.ssidLabel": "Netzwerkname (SSID)",
  "settings.wifi.network.ssidPlaceholder": "genau so, wie der Router ihn ausstrahlt",
  "settings.wifi.scan.again": "Erneut suchen",
  "settings.wifi.scan.start": "Nach Netzwerken suchen",
  "settings.wifi.chooseFromList": "Aus der Liste wählen",
  "settings.wifi.typeInstead": "Namen stattdessen eingeben",
  "settings.wifi.scan.hint":
    "Ein Name, der auf Ihrem Telefon erscheint, aber nicht in dieser Liste steht, ist der "
    + "übliche Grund, warum ein Gerät ein eindeutig vorhandenes Netzwerk „nicht findet“: die "
    + "meisten Router senden einen Namen auf beiden Bändern, und dieses Gerät hat kein "
    + "5-GHz-Funkmodul. Geben Sie ihn in diesem Fall ein — versteckte Netzwerke erscheinen in "
    + "keinem Scan, sie können nur eingegeben werden.",
  "settings.wifi.password.label": "WLAN-Passwort",
  "settings.wifi.password.fallbackNetworkName": "das gespeicherte Netzwerk",
  "settings.wifi.notice.newNetworkFallback": "das neue Netzwerk",
  "settings.wifi.password.hintUnencrypted":
    "Wenn Sie sich im eigenen Einrichtungsnetzwerk des Geräts befinden, ist dieses Netzwerk "
    + "offen: dieser Schlüssel überquert es einmal unverschlüsselt. Nichts in einer über "
    + "reines HTTP ausgelieferten Browserseite kann das verhindern.",
  "settings.wifi.network.openHint": "{ssid} ist offen — Passwortfeld leer lassen.",
  "settings.wifi.network.securedWarning":
    "{ssid} ist gesichert und das Passwortfeld ist leer. Das Gerät wird sich verbinden "
    + "wollen und abgewiesen werden.",
  "settings.wifi.beforeYouPress": "Bevor Sie das drücken",
  "settings.wifi.save": "WLAN speichern und neu verbinden",
  "settings.wifi.accepted": "Angenommen — das Gerät versucht es jetzt",

  "settings.wifi.signal.notSeen": "in diesem Scan nicht gesehen",
  "settings.wifi.signal.strong": "stark",
  "settings.wifi.signal.good": "gut",
  "settings.wifi.signal.weak": "schwach",
  "settings.wifi.signal.veryWeak": "sehr schwach",

  "settings.wifi.key.stored":
    "Der bereits für {ssid} gespeicherte Schlüssel wird erneut verwendet. Er wird nie an "
    + "diese Seite zurückgesendet, daher ist das Feld leer; tippen Sie, um ihn zu ersetzen.",
  "settings.wifi.key.empty":
    "Leer: Das Gerät versucht, ohne Schlüssel beizutreten. Richtig für ein offenes "
    + "Netzwerk, und für nichts sonst.",
  "settings.wifi.key.typed": "Dieser Schlüssel wird so, wie eingegeben, an das Gerät gesendet.",

  "settings.wifi.notice.line1":
    "Das Gerät antwortet auf dieses Formular, bevor es sich verbindet — eine Bestätigung "
    + "hier bedeutet also, dass die Zugangsdaten angenommen wurden, nicht dass {target} sie "
    + "übernommen hat.",
  "settings.wifi.notice.line2":
    "Während des Versuchs teilen sich das Einrichtungsnetzwerk, in dem Sie sich gerade "
    + "befinden, und {target} ein einziges Funkmodul. Diese Seite wird höchstwahrscheinlich "
    + "innerhalb weniger Sekunden nicht mehr reagieren — das ist hier der normale Ausgang, "
    + "kein Fehler.",
  "settings.wifi.notice.line3":
    "Danach befindet sich das Gerät in {target}, mit welcher Adresse auch immer Ihr Router "
    + "ihm zuweist. In der Liste der verbundenen Geräte Ihres Routers finden Sie es.",
  "settings.wifi.notice.fallback":
    "Ist der Schlüssel falsch, stellt das Gerät {previous} von selbst wieder her und kehrt "
    + "dorthin zurück — es ist das letzte Netzwerk, das diesem Gerät tatsächlich eine Adresse "
    + "gegeben hat, was es allein zu einem Rückkehrziel macht. Nichts muss zurückgesetzt "
    + "werden.",
  "settings.wifi.notice.noFallback":
    "Dieses Gerät hat noch nie ein Netzwerk erreicht, es gibt also nichts, wohin es "
    + "zurückkehren könnte. Ist der Schlüssel falsch, versucht es weiter, und sein eigenes "
    + "Einrichtungsnetzwerk bleibt etwa 15 Minuten ab dem Erscheinen aktiv. Danach ist das "
    + "Einrichtungsnetzwerk weg, bis das Gerät neu gestartet wird, wodurch es für ein paar "
    + "Minuten wiederkehrt.",

  "settings.secret.stored.text":
    "Auf dem Gerät gespeichert. Wird nie zurückgesendet, daher ist das Feld leer; tippen "
    + "Sie, um es zu ersetzen.",
  "settings.secret.stored.placeholder": "unverändert — tippen zum Ersetzen",
  "settings.secret.willClear.text": "Geleert. Speichern löscht den gespeicherten Wert.",
  "settings.secret.willClear.placeholder": "leer — Speichern löscht den gespeicherten Wert",
  "settings.secret.willReplace.text": "Speichern ersetzt den gespeicherten Wert.",
  "settings.secret.notSet.text": "Nicht gesetzt.",
  "settings.secret.notSet.placeholder": "nicht gesetzt",
  "settings.secret.willSet.text": "Speichern legt diesen Wert erstmals fest.",
  "settings.error.portRange": "Der Broker-Port muss eine ganze Zahl zwischen 1 und 65535 sein.",

  "settings.error.notImplemented.headline": "Das Gerät hat diese Route, aber noch keinen Code dahinter",
  "settings.error.noHandler.headline": "Diese Firmware-Version bedient diesen Endpunkt nicht",
  "settings.error.passwordNeeded.headline": "Das Gerät verlangt sein Passwort für die Weboberfläche",
  "settings.error.writeRefused.headline": "Das Gerät hat diesen Schreibzugriff abgelehnt",
  "settings.error.badRequest.headline": "Das Gerät wollte das nicht annehmen",
  "settings.error.fault.headline": "Das Gerät meldet einen Fehler",
  "settings.error.browserGaveNoReason": "Der Browser hat keinen Grund angegeben.",
  "settings.error.disconnectExpected.headline": "Keine Antwort — und genau das ist hier der Erfolg",
  "settings.error.disconnectExpected.detail":
    "Das Gerät antwortet, bevor es seinen Funk neu einstellt, daher bedeutet eine mitten "
    + "im Flug abbrechende Anfrage meist, dass es bereits versucht. ({browserSaid})",
  "settings.error.offline.headline": "Keine Antwort vom Gerät",
  "settings.error.offline.detail": "Nichts hat das Gerät erreicht, daher hat es dazu nichts gesagt. ({browserSaid})",
} satisfies Partial<Record<string, Message>>;
