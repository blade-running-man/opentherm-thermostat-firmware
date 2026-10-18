// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Dutch translations for the Settings page. See en/settings.ts for provenance and the
// pinned-key notes. Every {placeholder} is kept identical to the English; firmware identifiers
// and config keys are not translated.
import type { Message } from "../../format.ts";

export const settings = {
  "settings.title": "Instellingen",
  "settings.readOnly.headline": "Deze instellingen zijn geschreven door een nieuwere firmware",
  "settings.readOnly.detail":
    "Deze build begrijpt de indeling van de opgeslagen configuratie niet en weigert daarom "
    + "die te overschrijven in plaats van te beschadigen. Installeer de firmware die dit "
    + "apparaat eerder had opnieuw, of wis de instellingen met de knop, voordat u hier iets "
    + "wijzigt.",
  "settings.load.hint":
    "De broker- en apparaatinstellingen kunnen pas worden getoond als ze gelezen kunnen "
    + "worden. Het wififormulier hierboven hangt daar niet van af.",
  "settings.tryAgain": "Opnieuw proberen",
  "settings.loading": "Configuratie van het apparaat wordt gelezen…",
  "settings.save": "Instellingen opslaan",
  "settings.saved": "Instellingen opgeslagen",
  "settings.saveBar.hint": "Slaat de broker- en apparaatinstellingen samen op. Het netwerk wordt niet aangeraakt.",
  "settings.notSent": "Niet verzonden — deze pagina heeft niets verlaten",
  "settings.refusedValue": "Het apparaat heeft deze waarde geweigerd; de reden staat in de melding hieronder.",
  "settings.nothingChanged": "Niets gewijzigd.",
  "settings.sends": "Verzendt {keys} en verder niets.",

  "settings.broker.title": "MQTT-broker",
  "settings.broker.subtitle": "Waar het apparaat publiceert, en waar Home Assistant het vindt.",
  "settings.broker.host.label": "Broker-host",
  "settings.broker.host.placeholder": "192.168.1.10 of homeassistant.local",
  "settings.broker.port.label": "Broker-poort",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Broker-gebruikersnaam",
  "settings.broker.password.label": "Broker-wachtwoord",
  "settings.broker.password.warning":
    "Deze verbinding is in deze versie niet versleuteld, dus dit wachtwoord is zichtbaar "
    + "voor alles op uw netwerk. Hergebruik geen wachtwoord dat u ook elders gebruikt.",
  "settings.broker.topicPrefix.label": "Topic-prefix",
  "settings.broker.discovery.label": "Home Assistant-discovery publiceren",
  "settings.broker.discovery.hint":
    "Het wijzigen van het prefix verplaatst elk topic. Home Assistant behoudt de entiteiten "
    + "die het al onder het oude prefix kende totdat het apparaat ze intrekt, dus verwacht "
    + "een tijdje beide te zien na een wijziging.",

  "settings.device.title": "Apparaat",
  "settings.device.subtitle": "Hoe het heet, en wie het mag wijzigen.",
  "settings.device.name.label": "Apparaatnaam",
  "settings.device.name.placeholder": "Ventilatie",
  "settings.device.name.hint":
    "Alleen een weergavenaam. De identiteit van het apparaat — zijn broker-client-id en "
    + "zijn Home Assistant-apparaat-id — wordt afgeleid van zijn MAC-adres, dus hernoemen "
    + "hier kan geen tweede apparaat in Home Assistant opleveren.",
  "settings.device.password.label": "Wachtwoord voor de webinterface",
  "settings.device.password.warnTitle": "Er is geen \"wachtwoord vergeten\" voor dit apparaat.",
  "settings.device.password.warn1a":
    "Als u het kwijtraakt, is de enige weg terug de knop op het apparaat: houd hem vijf "
    + "seconden ingedrukt ",
  "settings.device.password.warn1em": "nadat het is opgestart",
  "settings.device.password.warn1b":
    ". Dat wist de wifigegevens en dit wachtwoord samen en laat de broker-instellingen "
    + "ongemoeid.",
  "settings.device.password.warn2a": "De knop ingedrukt houden ",
  "settings.device.password.warn2em": "terwijl",
  "settings.device.password.warn2b":
    " het apparaat opstart doet iets heel anders — het zet de chip in zijn firmware-lader, "
    + "wat er precies uitziet als een apparaat dat kapot is. Laat het eerst opstarten, wacht, "
    + "houd dan pas in.",
  "settings.device.password.noneSet":
    "Er is geen wachtwoord ingesteld. Iedereen die dit apparaat op het netwerk kan "
    + "bereiken, kan deze instellingen wijzigen — een redelijke keuze voor een apparaat op "
    + "een thuisnetwerk, en bewust de standaardinstelling.",

  "settings.executor.title": "Regelaar",
  "settings.executor.subtitle": "De modus, de watchdog, de failsafe en de aanvoerband.",
  "settings.executor.mode.local": "Lokaal — de eigen bediening van dit apparaat",
  "settings.executor.mode.ha": "Home Assistant — heeft een broker nodig",
  "settings.executor.save": "Regelinstellingen opslaan",
  "settings.executor.saved": "Regelinstellingen opgeslagen",
  "settings.executor.label.control_mode": "Regelmodus",
  "settings.executor.label.watchdog_s": "Watchdog (s)",
  "settings.executor.label.failsafe_setpoint_dc": "Failsafe-aanvoertemperatuur (°C)",
  "settings.executor.label.failsafe_room_target_dc": "Failsafe-ruimtedoel (°C)",
  "settings.executor.label.failsafe_heat_days": "Failsafe-stookdagen",
  "settings.executor.label.failsafe_min_cycle_s": "Failsafe-minimumcyclus (s)",
  "settings.executor.label.flow_min_dc": "Laagste aanvoertemperatuur (°C)",
  "settings.executor.label.flow_max_dc": "Hoogste aanvoertemperatuur (°C)",
  "settings.executor.note.watchdog_s":
    "Hoe lang Home Assistant mag zwijgen over CH voordat de failsafe het overneemt.",
  "settings.executor.note.failsafe_setpoint_dc":
    "De aanvoertemperatuur waarmee de failsafe stookt, en de waarde die vanaf het opstarten "
    + "wordt aangehouden.",
  "settings.executor.note.failsafe_room_target_dc":
    "De ruimtetemperatuur die de failsafe aanhoudt zolang een ruimtesensor vers is.",
  "settings.executor.note.failsafe_heat_days":
    "De failsafe stookt alleen als Home Assistant binnen dit aantal dagen om warmte heeft "
    + "gevraagd.",
  "settings.executor.note.failsafe_min_cycle_s":
    "De kortste aan-tijd en de kortste uit-tijd van de failsafe.",
  "settings.executor.note.flow_min_dc":
    "Geen CH-instelpunt onder deze waarde wordt geaccepteerd, van niemand. Houd hem op of "
    + "boven de eigen parameter E van de ketel: een verzoek onder E wordt niet uitgevoerd.",
  "settings.executor.note.flow_max_dc": "Geen CH-instelpunt boven deze waarde wordt geaccepteerd, van niemand.",
  "settings.executor.problem": "{label}: {kind} is vereist.",
  "settings.executor.kind.decimal": "een temperatuur in graden, met hooguit één decimaal",
  "settings.executor.kind.whole": "een geheel getal",

  "settings.room.title": "Ruimtebron (MQTT)",
  "settings.room.subtitle":
    "Een ruimtetemperatuur die Home Assistant naar het apparaat publiceert (docs/ha-room-source.md).",
  "settings.room.role.room": "Ruimte — stuurt de failsafe aan",
  "settings.room.role.ambient": "Omgeving — alleen getoond, stuurt nooit aan",
  "settings.room.stale.hint":
    "Hoe lang het apparaat wacht zonder publicatie voordat deze bron als verouderd geldt.",
  "settings.room.forwarded.hint":
    "Indien aan, dwingt een verouderde waarde de failsafe om blind te stoken in plaats van "
    + "de laatste ruimtewaarde aan te houden (het ha_blind-geval).",
  "settings.room.save": "Ruimtebron-instellingen opslaan",
  "settings.room.saved": "Ruimtebron-instellingen opgeslagen",
  "settings.room.label.room_mqtt_enable": "Een MQTT-ruimtebron gebruiken",
  "settings.room.label.room_mqtt_role": "Rol",
  "settings.room.label.room_mqtt_stale_s": "Verouderd na (s)",
  "settings.room.label.room_mqtt_ha_forwarded": "Home Assistant stuurt een verouderde waarde door",
  "settings.room.problem": "{label}: een geheel getal is vereist.",

  "settings.wifi.title": "Wifi",
  "settings.wifi.subtitle": "De lijst toont wat dit apparaat kan horen, niet wat uw telefoon toont.",
  "settings.wifi.network.label": "Netwerk",
  "settings.wifi.network.choose": "— kies een netwerk —",
  "settings.wifi.network.currentlyConfigured": "momenteel geconfigureerd",
  "settings.wifi.network.open": "open",
  "settings.wifi.network.manualOption": "Ander / verborgen netwerk — typ de naam…",
  "settings.wifi.network.ssidLabel": "Netwerknaam (SSID)",
  "settings.wifi.network.ssidPlaceholder": "precies zoals de router hem uitzendt",
  "settings.wifi.scan.again": "Opnieuw scannen",
  "settings.wifi.scan.start": "Zoeken naar netwerken",
  "settings.wifi.chooseFromList": "Kiezen uit de lijst",
  "settings.wifi.typeInstead": "Naam in plaats daarvan typen",
  "settings.wifi.scan.hint":
    "Een naam die op uw telefoon staat maar niet in deze lijst, is de gebruikelijke reden "
    + "waarom een apparaat een duidelijk aanwezig netwerk \"niet kan vinden\": de meeste "
    + "routers zenden één naam uit op beide banden, en dit apparaat heeft geen 5 GHz-radio. "
    + "Typ hem in dat geval in — verborgen netwerken verschijnen in geen enkele scan, ze "
    + "kunnen alleen worden getypt.",
  "settings.wifi.password.label": "Wifi-wachtwoord",
  "settings.wifi.password.fallbackNetworkName": "het opgeslagen netwerk",
  "settings.wifi.notice.newNetworkFallback": "het nieuwe netwerk",
  "settings.wifi.password.hintUnencrypted":
    "Als u zich op het eigen installatienetwerk van het apparaat bevindt, is dat netwerk "
    + "open: deze sleutel steekt het eenmalig onversleuteld over. Niets in een via gewoon "
    + "HTTP geserveerde browserpagina kan dat voorkomen.",
  "settings.wifi.network.openHint": "{ssid} is open — laat het wachtwoordveld leeg.",
  "settings.wifi.network.securedWarning":
    "{ssid} is beveiligd en het wachtwoordveld is leeg. Het apparaat zal proberen te "
    + "verbinden en zal worden geweigerd.",
  "settings.wifi.beforeYouPress": "Voordat u hierop drukt",
  "settings.wifi.save": "Wifi opslaan en opnieuw verbinden",
  "settings.wifi.accepted": "Geaccepteerd — het apparaat probeert het nu",

  "settings.wifi.signal.notSeen": "niet gezien in deze scan",
  "settings.wifi.signal.strong": "sterk",
  "settings.wifi.signal.good": "goed",
  "settings.wifi.signal.weak": "zwak",
  "settings.wifi.signal.veryWeak": "zeer zwak",

  "settings.wifi.key.stored":
    "De sleutel die al voor {ssid} is opgeslagen, wordt opnieuw gebruikt. Hij wordt nooit "
    + "teruggestuurd naar deze pagina, dus het veld is leeg; typ om hem te vervangen.",
  "settings.wifi.key.empty":
    "Leeg: het apparaat probeert zonder sleutel te verbinden. Correct voor een open "
    + "netwerk, en voor niets anders.",
  "settings.wifi.key.typed": "Deze sleutel wordt zoals getypt naar het apparaat verzonden.",

  "settings.wifi.notice.line1":
    "Het apparaat beantwoordt dit formulier voordat het begint te verbinden, dus een "
    + "bevestiging hier betekent dat de gegevens zijn geaccepteerd — niet dat {target} ze "
    + "heeft overgenomen.",
  "settings.wifi.notice.line2":
    "Terwijl het probeert, delen het installatienetwerk waarop u nu zit en {target} één "
    + "radio. Deze pagina zal hoogstwaarschijnlijk binnen enkele seconden niet meer reageren "
    + "— dat is hier het normale resultaat, geen storing.",
  "settings.wifi.notice.line3":
    "Daarna bevindt het apparaat zich op {target}, met welk adres uw router het ook "
    + "toewijst. De lijst met verbonden clients van uw router is de plek om te zoeken.",
  "settings.wifi.notice.fallback":
    "Als de sleutel onjuist is, zet het apparaat {previous} zelf terug en keert daarnaar "
    + "terug — het is het laatste netwerk dat dit apparaat daadwerkelijk een adres gaf, wat "
    + "het de enige reden geeft om ernaar terug te keren. Er hoeft niets gereset te worden.",
  "settings.wifi.notice.noFallback":
    "Dit apparaat heeft nog nooit een netwerk bereikt, dus er is niets om naar terug te "
    + "keren. Als de sleutel onjuist is, blijft het proberen, en het eigen "
    + "installatienetwerk blijft ongeveer 15 minuten actief vanaf het moment dat het "
    + "verscheen. Daarna is het installatienetwerk weg totdat het apparaat opnieuw wordt "
    + "gestart, wat het voor een paar minuten terugbrengt.",

  "settings.secret.stored.text":
    "Opgeslagen op het apparaat. Wordt nooit teruggestuurd, dus het veld is leeg; typ om "
    + "het te vervangen.",
  "settings.secret.stored.placeholder": "ongewijzigd — typ om te vervangen",
  "settings.secret.willClear.text": "Geleegd. Opslaan verwijdert de opgeslagen waarde.",
  "settings.secret.willClear.placeholder": "leeg — opslaan verwijdert de opgeslagen waarde",
  "settings.secret.willReplace.text": "Opslaan vervangt de opgeslagen waarde.",
  "settings.secret.notSet.text": "Niet ingesteld.",
  "settings.secret.notSet.placeholder": "niet ingesteld",
  "settings.secret.willSet.text": "Opslaan stelt deze waarde voor het eerst in.",
  "settings.error.portRange": "De broker-poort moet een geheel getal tussen 1 en 65535 zijn.",

  "settings.error.notImplemented.headline": "Het apparaat heeft deze route, maar er zit nog geen code achter",
  "settings.error.noHandler.headline": "Deze firmware-versie bedient dat eindpunt niet",
  "settings.error.passwordNeeded.headline": "Het apparaat wil zijn wachtwoord voor de webinterface",
  "settings.error.writeRefused.headline": "Het apparaat heeft dit schrijven geweigerd",
  "settings.error.badRequest.headline": "Het apparaat wilde dit niet accepteren",
  "settings.error.fault.headline": "Het apparaat meldt een storing",
  "settings.error.browserGaveNoReason": "De browser gaf geen reden.",
  "settings.error.disconnectExpected.headline": "Geen antwoord — en dat is hier precies het succes",
  "settings.error.disconnectExpected.detail":
    "Het apparaat antwoordt voordat het zijn radio opnieuw afstemt, dus een verzoek dat "
    + "halverwege afbreekt betekent meestal dat het al bezig is. ({browserSaid})",
  "settings.error.offline.headline": "Geen antwoord van het apparaat",
  "settings.error.offline.detail": "Niets heeft het apparaat bereikt, dus het heeft hier niets over gezegd. ({browserSaid})",
} satisfies Partial<Record<string, Message>>;
