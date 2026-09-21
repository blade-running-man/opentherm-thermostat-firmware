// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Dutch translation of ../en/tokens.ts. Firmware identifiers (watchdog_s, failsafe_heat_days,
// control_mode ha/local) and every {placeholder} are kept verbatim, as en/tokens.ts requires.
// Draft wording; the owner reviews it later (non-blocking).
import type { Message } from "../../format.ts";

export const tokens = {
  "token.state.season_off.label": "Stookseizoen uit",
  "token.state.season_off.note": "Er wordt geen warmte gevraagd, ongeacht de CH-schakelaar. Warm water wordt niet beïnvloed.",
  "token.state.boost.label": "Boost",
  "token.state.boost.note": "Een tijdelijke boost houdt zijn aanvoer-setpoint vast en vraagt warmte totdat hij eindigt.",
  "token.state.local.label": "Lokale besturing",
  "token.state.local.note": "De eigen CH-schakelaar en het aanvoer-setpoint van het apparaat sturen de ketel aan.",
  "token.state.ha_waiting.label": "Wachten op Home Assistant",
  "token.state.ha_waiting.note": "Home Assistant beheert de ketel en heeft sinds de overname geen CH-commando gestuurd. CH blijft uit totdat dat gebeurt, of totdat de watchdog verloopt.",
  "token.state.failsafe.label": "Failsafe",
  "token.state.failsafe.note": "Home Assistant is stil of blind geworden, en het apparaat voert zijn eigen failsafe uit.",
  "token.state.ha.label": "Home Assistant heeft de controle",
  "token.state.ha.note": "De commando's van Home Assistant sturen de ketel aan.",
  "token.state.unknown.label": "Onbekende status \"{state}\"",
  "token.state.unknown.note": "Deze pagina kent de door het apparaat gemelde status niet; de eigen naam van het apparaat ervoor wordt getoond.",

  "token.reason.await_setpoint": "CH is gewenst; het apparaat wacht tot het nieuwe aanvoer-setpoint de ketel bereikt voordat het warmte vraagt.",
  "token.reason.fs_disarmed": "CH wordt uitgehouden: Home Assistant heeft binnen failsafe_heat_days geen warmte gevraagd.",
  "token.reason.fs_blind": "Geen actuele kamertemperatuur: blind verwarmen op het failsafe-setpoint.",
  "token.reason.fs_room_cold": "De kamer is onder het failsafe-doel gezakt: verwarmen.",
  "token.reason.fs_room_warm": "De kamer heeft het failsafe-doel bereikt: CH is uit.",
  "token.reason.min_cycle": "Het CH-bit wordt vastgehouden voor de minimale failsafe-cyclustijd.",
  "token.reason.other": "Reden: {reason}",

  "token.cause.watchdog": "Al langer dan watchdog_s geen CH-commando van Home Assistant.",
  "token.cause.ha_blind": "De eigen kamersensor van Home Assistant is verouderd terwijl de commando's blijven binnenkomen.",
  "token.cause.other": "Oorzaak: {cause}",

  "token.mode.ha": "Home Assistant beheert de CH- en warmwatercommando's (control_mode ha). Een schrijfactie vanaf deze pagina wordt toch verstuurd, en het antwoord van het apparaat wordt getoond.",
  "token.mode.local": "De eigen bedieningselementen van dit apparaat beheren de CH- en warmwatercommando's (control_mode local): deze pagina, of een willekeurige REST-client. Commando's van Home Assistant worden geweigerd.",

  "token.avail.ok": "beschikbaar",
  "token.avail.invalid": "ongeldige gegevens",
  "token.avail.unsupported": "niet ondersteund door de ketel",
  "token.avail.unknown": "nog geen antwoord",

  "error.detail.no_entity": "geen zodanige entiteit",
  "error.detail.read_only": "entiteit is alleen-lezen",
  "error.detail.out_of_range": "waarde buiten bereik",
  "error.detail.unsupported_id": "ketel ondersteunt deze data-ID niet",
  "error.detail.owned_ha": "eigendom van Home Assistant: {1}",
  "error.detail.owned_local": "eigendom van de thermostaat: {1}",
  "error.detail.season_off_boost": "heating_season staat uit: een boost zou niet verwarmen",
} satisfies Partial<Record<string, Message>>;
