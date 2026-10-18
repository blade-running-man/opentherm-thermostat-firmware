// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Boiler page. See en/boiler.ts for what stays untranslated (row.type, decode columns,
// formatAge, entity names).
import type { Message } from "../../format.ts";

export const boiler = {
  "boiler.title": "Ketel",
  "boiler.loading": "Het apparaat wordt gevraagd naar de bus…",

  "boiler.counter.cycles": "cycli",
  "boiler.counter.answers": "antwoorden",
  "boiler.counter.failures": "storingen",
  "boiler.counter.overdue": "te laat",
  "boiler.uptimeHint": "Het apparaat draait al {uptime}. Ververst elke {seconds} s.",

  "boiler.answering": "De ketel antwoordt",
  "boiler.silent": "De ketel antwoordt niet",
  "boiler.silentHint": "De master stuurt aanvragen{cycles} en er komt geen antwoord. "
    + "Controleer in deze volgorde: de voeding van de interfaceplaat, de polariteit van het "
    + "paar, de ingangsinversie.",
  "boiler.silentHint.cyclesSuffix": " ({count} cycli)",

  "boiler.line": "Lijn: {text}.",
  "boiler.line.idle": "ingang inactief (0 %) — zoals het hoort terwijl de ketel zwijgt",
  "boiler.line.inverted": "ingang voortdurend actief ({duty} %) — de ingangspolariteit lijkt "
    + "geïnverteerd",
  "boiler.line.toggling": "ingang wisselt ({duty} %) — er staan frames op de lijn",

  "boiler.scan.run": "Elke Data-ID bevragen (~4 min)",
  "boiler.scan.running": "Sweep loopt: {done} van {total}",
  "boiler.scan.hint": "Vraagt de ketel eenmaal naar elke identifier van 0 tot 127 en zet de "
    + "antwoorden in de tabel hieronder. Alleen-lezen — de sweep schrijft niets. Een "
    + "identifier die na de sweep ontbreekt in de tabel heeft helemaal niet geantwoord; een "
    + "rij met \"unknown-dataid\" betekent dat de ketel uitdrukkelijk heeft gezegd dat zoiets "
    + "niet bestaat. Dat zijn verschillende dingen, en beide zijn nuttig.",
  "boiler.scan.forbidden": "Het apparaat zit nog niet op een netwerk: tijdens de eerste "
    + "instelling is alleen de instelling zelf toegestaan. Verbind Wi-Fi op de "
    + "instellingenpagina.",
  "boiler.scan.failed": "De aanvraag is niet doorgekomen.",

  "boiler.lineTest.run": "Lijn testen met een multimeter",
  "boiler.lineTest.running": "Test loopt, 20 s…",
  "boiler.lineTest.hint": "Twintig seconden lang wisselt de lijn langzaam, twee seconden per "
    + "toestand. Meet de spanning op de aansluitingen van de ketel: deze moet dalen van "
    + "15–24 V naar zeven of lager, en weer terugkeren. Verandert er niets, dan werkt de "
    + "uitgang van de adapter niet of heeft de bus geen voeding.",
  "boiler.lineTest.warning": "Ondertussen gaan er geen frames uit, dus de ketel zal "
    + "waarschijnlijk aanslaan",
  "boiler.lineTest.warningNote": " — dat doet de ketel wanneer de thermostaat zwijgt "
    + "(een zwijgende thermostaat wordt gelezen als een warmtevraag). Dat is verwacht.",
  "boiler.lineTest.failed": "De aanvraag is niet doorgekomen: er loopt mogelijk al een test.",

  "boiler.raw.empty": "Nog geen Data-ID heeft geantwoord. Zolang de ketel zwijgt is de tabel "
    + "leeg — dat is een toestand, geen fout van de pagina.",
  "boiler.raw.answers": "{count} antwoorden",
  "boiler.raw.col.id": "ID",
  "boiler.raw.col.name": "naam",
  "boiler.raw.col.type": "antwoordtype",
  "boiler.raw.col.raw": "ruw",
  "boiler.raw.col.f88": "f8.8",
  "boiler.raw.col.u16": "u16",
  "boiler.raw.col.s16": "s16",
  "boiler.raw.col.hiLo": "hoge / lage byte",
  "boiler.raw.col.flags": "vlaggen",
  "boiler.raw.col.age": "leeftijd",
} satisfies Partial<Record<string, Message>>;
