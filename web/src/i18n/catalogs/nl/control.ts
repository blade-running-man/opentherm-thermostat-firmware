// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Dutch translation of ../en/control.ts. Firmware identifiers and every {placeholder} are kept
// verbatim, as en/control.ts requires. Draft wording; the owner reviews it later (non-blocking).
import type { Message } from "../../format.ts";

export const control = {
  "failsafe.entries": { one: "vermelding", other: "vermeldingen" },

  "control.title": "Besturing",
  "control.loading": "Er wordt aan het apparaat gevraagd wat het aan het doen is…",
  "control.live": "Live: de wijzigingen van het apparaat komen binnen zodra ze gebeuren.",
  "control.poll": "De live-verbinding is verbroken; de kaart vraagt elke {n} s opnieuw, "
    + "inclusief het CH-commando.",
  "control.stack": "Stack-reserve van de thermostaattaak: {n} bytes.",
  "control.accepted": "{what}: geaccepteerd",

  "control.onoff.on": "aan",
  "control.onoff.off": "uit",

  "control.failsafe.count": "{count} failsafe-{word} sinds deze start",
  "control.failsafe.active": "Failsafe is actief",
  "control.failsafe.ran": "De failsafe is gelopen sinds deze start",
  "control.failsafe.last": "; de laatste duurde {duration}",

  "control.sinceHa.ha": "Laatst geaccepteerd CH-commando van Home Assistant: {counted} geleden.",
  "control.sinceHa.waiting": "Er is geen CH-commando van Home Assistant geaccepteerd sinds dit "
    + "apparaat is gestart of de Home Assistant-modus is ingegaan; de watchdog telt {counted} "
    + "(deze loopt door na een herstart).",
  "control.sinceHa.other": "De watchdog telt {counted} zonder een geaccepteerd CH-commando van "
    + "Home Assistant.",

  "control.season.label": "Stookseizoen",

  "control.heating.title": "Centrale verwarming",
  "control.ch.label": "CH",
  "control.ch.commandRow": "CH-commando",
  "control.ch.askedRow": "CH bij de ketel aangevraagd",
  "control.flow.label": "Aanvoer-instelpunt",
  "control.flow.heldRow": "Vastgehouden aanvoer-instelpunt",
  "control.flow.newRow": "Nieuw aanvoer-instelpunt",
  "control.ch.explain": "Het commando is wat de eigenaar van de ketel heeft aangevraagd; \"bij "
    + "de ketel aangevraagd\" is het CH-bit dat het apparaat het laatst heeft gestuurd. Ze "
    + "verschillen even na een nieuw instelpunt — CH wacht tot het instelpunt is verstuurd — "
    + "en telkens wanneer het seizoen of de failsafe het commando overschrijft.",

  "control.dhw.label": "Warm water",
  "control.dhw.commandRow": "Warm-watercommando",
  "control.dhw.askedRow": "Warm water bij de ketel aangevraagd",
  "control.dhw.setpointLabel": "Warm-waterinstelpunt",
  "control.dhw.newSetpointRow": "Nieuw warm-waterinstelpunt",
  "control.dhw.unset": "niet ingesteld: de ketel houdt zijn eigen waarde aan",

  "control.boost.label": "Boost",
  "control.boost.stopLabel": "Boost stoppen",
  "control.boost.runningRow": "Actief",
  "control.boost.running": "{sp}, nog {time}",
  "control.boost.none": "Er loopt geen boost.",
  "control.boost.bothNeeded": "beide vakken hebben een getal nodig",
  "control.boost.setpointAria": "Boost-aanvoerinstelpunt",
  "control.boost.lengthAria": "Boostduur",
  "control.boost.lengthAriaMinutes": "Boostduur, minuten",
  "control.boost.explain": "Een boost verwarmt op het aanvoer-instelpunt gedurende de opgegeven "
    + "minuten en geeft daarna de controle terug aan het CH-commando. Het apparaat bepaalt "
    + "wanneer dit mag lopen en zegt waarom niet als dat niet mag.",

  "control.button.on": "Aan",
  "control.button.off": "Uit",
  "control.button.set": "Instellen",
  "control.button.stop": "Stop",
  "control.button.start": "Start",
  "control.validation.notANumber": "geen getal",

  "control.hero.ariaLabel": "Ketelstatus",
  "control.hero.heatingOn": "▲ Verwarmt",
  "control.hero.heatingOff": "Verwarming uit",
  "control.hero.dhwOn": "▲ Warm water",
  "control.hero.dhwOff": "Warm water uit",
  "control.hero.flow": "Aanvoer",
  "control.hero.return": "Retour",
  "control.hero.modulation": "Modulatie",
  "control.hero.bar": "Bar",
  "control.hero.tag.flow": "AANVOER",
  "control.hero.tag.return": "RETOUR",
  "control.hero.tag.rad": "RAD",
  "control.hero.tag.sink": "WASBAK",
  "control.hero.tag.cold": "KOUD",
  "control.hero.tag.boiler": "KETEL",
  "control.hero.tag.standby": "Standby",

  "error.control.refused": "{what}: geweigerd door het apparaat",
  "error.control.valueRefused": "{what}: de waarde werd geweigerd",
  "error.control.notKept": "{what}: het apparaat kon dit niet bewaren",
  "error.control.notCarriedOut": "{what}: niets op het apparaat zou dit uitvoeren",
  "error.control.passwordNeeded": "Het apparaat vraagt om zijn webinterface-wachtwoord",
  "error.control.writeRefused": "{what}: het apparaat heeft dit schrijven geweigerd",
  "error.control.noRoute": "{what}: deze firmwareversie kent deze route niet",
  "error.control.badRequest": "{what}: het apparaat begreep het verzoek niet",
  "error.control.notAccepted": "{what}: het apparaat heeft het niet geaccepteerd",
  "error.control.offlineHeadline": "{what}: geen antwoord van het apparaat",
  "error.control.offlineDetail": "Er kwam niets terug, dus het is onbekend of het effect had; "
    + "de kaart toont dit zodra het apparaat weer antwoordt. ({reason})",
  "error.control.notStarted": "De executor is niet gestart",
  "error.control.notStartedDetail": "Het apparaat antwoordt, maar de thermostaattaak is niet "
    + "gelopen: er is geen status om te tonen, en elke besturing hier zou worden geweigerd met "
    + "503. Na de eerste seconden na een start betekent dit dat de taak helemaal niet is "
    + "gestart, en de Log-pagina zegt waarom.",
  "error.control.load.noRoute": "Deze firmwareversie biedt GET /api/control niet aan",
  "error.control.load.noRouteDetail": "Het document van de executor is niet beschikbaar. ({message})",
  "error.control.load.denied": "Het apparaat weigerde zijn besturingen te tonen",
  "error.control.load.badDocument": "Het antwoord van het apparaat is geen besturingsdocument "
    + "dat deze pagina kan lezen",
  "error.control.load.fault": "Het apparaat meldde een storing",
  "error.control.load.offlineHeadline": "Geen antwoord van het apparaat",
  "error.control.load.offlineDetail": "Het apparaat werd niet bereikt, dus het heeft niets "
    + "gezegd over zijn besturingen; de kaart bewaart het laatste antwoord dat het had. "
    + "({reason})",
  "error.control.browserSilent": "De browser gaf geen reden op.",
} satisfies Partial<Record<string, Message>>;
