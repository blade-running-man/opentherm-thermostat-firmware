// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// State page: table column headers, the write action, the page chrome, and its failures.
import type { Message } from "../../format.ts";

export const state = {
  "state.col.entity": "Entiteit",
  "state.col.value": "Waarde",
  "state.col.availability": "Beschikbaarheid",
  "state.col.age": "Leeftijd",
  "state.col.write": "Schrijven",
  "state.write": "Schrijven: {name}",

  "state.title": "Status",
  "state.loading": "Vraagt het apparaat naar zijn entiteitsregister…",
  "state.hint": "Waarden verversen elke {seconds} s. De invoergrenzen zijn de grenzen die de "
    + "ketel zelf heeft gemeld; zolang hij ze niet heeft gemeld, gelden de eigen grenzen van de "
    + "tabel. Een schrijfactie antwoordt met „queued”: de buswachtrij houdt één schrijfactie "
    + "vast, en of de ketel deze accepteerde, blijkt uit de waarde in de tabel één peilronde "
    + "later.",
  "state.value.yes": "ja",
  "state.value.no": "nee",
  "state.readOnly": "alleen-lezen",
  "state.writeAction": "Schrijven",
  "state.notANumber": "geen getal",
  "state.queued": "{name}: in wachtrij",

  "error.state.noRoute": "Deze firmwareversie biedt het entiteitsregister niet aan",
  "error.state.noRouteDetail": "De endpoints /api/entities en /api/state kwamen met het "
    + "statusmodel; de firmware die nu op het apparaat draait, heeft ze niet. ({message})",
  "error.state.denied": "Het apparaat weigerde zijn status te tonen",
  "error.state.fault": "Het apparaat meldde een storing",
  "error.state.offlineHeadline": "Geen antwoord van het apparaat",
  "error.state.offlineDetail": "Het apparaat werd niet bereikt, dus het heeft niets gezegd "
    + "over zijn status. ({reason})",
  "error.state.browserSilent": "De browser gaf geen reden op.",

  "error.state.write.readOnly": "Deze entiteit is alleen-lezen",
  "error.state.write.refused": "Dit schrijven wordt geweigerd, ongeacht de waarde",
  "error.state.write.refusedDetail": "Het ligt niet aan het getal: ofwel antwoordde de ketel "
    + "tweemaal dat hij deze Data-ID niet kent en stopte het apparaat met vragen, ofwel geeft "
    + "de huidige besturingsmodus deze waarde aan een andere bron. ({message})",
  "error.state.write.outOfBounds": "De waarde valt buiten de grenzen",
  "error.state.write.outOfBoundsDetail": "Het verzoek werd begrepen; het is het getal dat werd "
    + "geweigerd. De grenzen naast het invoerveld zijn die welke de ketel zelf heeft gemeld, of "
    + "anders die van de tabel zelf. ({message})",
  "error.state.write.noEntity": "Deze firmwareversie kent deze entiteit niet",
  "error.state.write.badRequest": "Het apparaat begreep het verzoek niet",
  "error.state.write.denied": "Het apparaat weigerde dit schrijven",
  "error.state.write.notAccepted": "Het apparaat accepteerde het schrijven niet",
  "error.state.write.offlineDetail": "Het apparaat werd niet bereikt. Of de waarde werd "
    + "geschreven is onbekend; controleer dit in de tabel zodra de verbinding terug is. "
    + "({reason})",
} satisfies Partial<Record<string, Message>>;
