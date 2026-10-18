# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""DE/NL/UK translations of the entity display names, keyed by entity key.

The English name is the canonical one and lives in opentherm_ids.py (it is what reaches Home
Assistant via discovery). This file adds ONLY the other three languages, for the web UI's
language switcher -- these never touch discovery, so Home Assistant stays English.

This is a second table keyed by entity key, so it CAN drift from opentherm_ids.py. It does
not, because render_entity_names_ts.py fails generation (and so the build) unless the key set
here is exactly the registry's -- no missing key, no orphan. Add an entity to opentherm_ids.py
and the build tells you to add its three translations here. See CLAUDE.md, "One list of
entities": the English name is that one list; this is its translation, pinned to it by key.
"""
from __future__ import annotations

# key -> {"de": ..., "nl": ..., "uk": ...}. English is opentherm_ids.py's `name`.
ENTITY_NAMES_I18N: dict[str, dict[str, str]] = {
    # --- ID 0 status flags ---
    "fault": {"de": "Störung", "nl": "Storing", "uk": "Несправність"},
    "ch_active": {"de": "Heizung aktiv", "nl": "Verwarming actief", "uk": "Опалення активне"},
    "dhw_active": {"de": "Warmwasser aktiv", "nl": "Warmwater actief", "uk": "Гаряча вода активна"},
    "flame": {"de": "Flamme an", "nl": "Vlam aan", "uk": "Полум'я горить"},
    "cooling_active": {"de": "Kühlung aktiv", "nl": "Koeling actief", "uk": "Охолодження активне"},
    "diagnostic_indication": {"de": "Wartung erforderlich", "nl": "Onderhoud nodig", "uk": "Потрібне обслуговування"},
    # --- ID 3 config flags ---
    "dhw_present": {"de": "Warmwasser vorhanden", "nl": "Warmwater aanwezig", "uk": "Контур гарячої води наявний"},
    "control_type_on_off": {"de": "Ein/Aus-Regelung", "nl": "Aan/uit-regeling", "uk": "Керування ввімк/вимк"},
    "cooling_supported": {"de": "Kühlung unterstützt", "nl": "Koeling ondersteund", "uk": "Охолодження підтримується"},
    "dhw_storage_tank": {"de": "Warmwasserspeicher", "nl": "Warmwaterboiler", "uk": "Бойлер гарячої води"},
    "pump_control_allowed": {"de": "Pumpensteuerung erlaubt", "nl": "Pompsturing toegestaan", "uk": "Керування насосом дозволено"},
    # --- ID 5 fault flags ---
    "service_request": {"de": "Serviceanforderung", "nl": "Serviceverzoek", "uk": "Запит на сервіс"},
    "lockout_reset": {"de": "Entriegelung erforderlich", "nl": "Ontgrendeling nodig", "uk": "Потрібне скидання блокування"},
    "low_water_pressure": {"de": "Niedriger Wasserdruck", "nl": "Lage waterdruk", "uk": "Низький тиск води"},
    "flame_fault": {"de": "Flammenstörung", "nl": "Vlamstoring", "uk": "Несправність полум'я"},
    "air_pressure_fault": {"de": "Luftdruckstörung", "nl": "Luchtdrukstoring", "uk": "Несправність тиску повітря"},
    "water_over_temperature": {"de": "Wasserübertemperatur", "nl": "Water-oververhitting", "uk": "Перегрів води"},
    # --- ID 6 remote flags ---
    "dhw_setpoint_transfer_enabled": {"de": "Warmwasser-Sollwertübertragung aktiv", "nl": "Warmwater-setpointoverdracht aan", "uk": "Передавання уставки гарячої води увімкнено"},
    "max_ch_setpoint_transfer_enabled": {"de": "Max. Vorlauf-Sollwertübertragung aktiv", "nl": "Max. CV-setpointoverdracht aan", "uk": "Передавання макс. уставки подачі увімкнено"},
    "dhw_setpoint_writable": {"de": "Warmwasser-Sollwert beschreibbar", "nl": "Warmwater-setpoint beschrijfbaar", "uk": "Уставка гарячої води запи́сувана"},
    "max_ch_setpoint_writable": {"de": "Max. Vorlauf-Sollwert beschreibbar", "nl": "Max. CV-setpoint beschrijfbaar", "uk": "Макс. уставка подачі запи́сувана"},
    # --- ID 100 override flags ---
    "override_manual_priority": {"de": "Priorität manuelle Übersteuerung", "nl": "Prioriteit handmatige overschrijving", "uk": "Пріоритет ручного перевизначення"},
    "override_program_priority": {"de": "Priorität Programm-Übersteuerung", "nl": "Prioriteit programma-overschrijving", "uk": "Пріоритет програмного перевизначення"},
    # --- values ---
    "ch_setpoint": {"de": "Vorlauf-Sollwert", "nl": "CV-setpoint", "uk": "Уставка подачі"},
    "member_id": {"de": "Hersteller-Member-ID", "nl": "Fabrikant member-ID", "uk": "Member ID виробника"},
    "oem_fault_code": {"de": "OEM-Fehlercode", "nl": "OEM-storingscode", "uk": "Код несправності виробника"},
    "remote_override_setpoint": {"de": "Remote-Raumsollwert", "nl": "Remote kamersetpoint", "uk": "Віддалена уставка кімнати"},
    "max_relative_modulation": {"de": "Max. relative Modulation", "nl": "Max. relatieve modulatie", "uk": "Стеля модуляції"},
    "max_capacity": {"de": "Maximale Leistung", "nl": "Maximaal vermogen", "uk": "Максимальна потужність"},
    "min_modulation": {"de": "Min. Modulation", "nl": "Min. modulatie", "uk": "Мінімальна модуляція"},
    "room_setpoint": {"de": "Raumsollwert", "nl": "Kamersetpoint", "uk": "Уставка кімнати"},
    "modulation": {"de": "Modulationsgrad", "nl": "Modulatieniveau", "uk": "Рівень модуляції"},
    "ch_pressure": {"de": "Heizungsdruck", "nl": "CV-waterdruk", "uk": "Тиск у контурі"},
    "dhw_flow_rate": {"de": "Warmwasser-Durchfluss", "nl": "Warmwaterdebiet", "uk": "Витрата гарячої води"},
    "room_temperature": {"de": "Raumtemperatur", "nl": "Kamertemperatuur", "uk": "Температура в кімнаті"},
    "flow_temperature": {"de": "Vorlauftemperatur", "nl": "Aanvoertemperatuur", "uk": "Температура подачі"},
    "dhw_temperature": {"de": "Warmwassertemperatur", "nl": "Warmwatertemperatuur", "uk": "Температура гарячої води"},
    "outside_temperature": {"de": "Außentemperatur", "nl": "Buitentemperatuur", "uk": "Температура надворі"},
    "return_temperature": {"de": "Rücklauftemperatur", "nl": "Retourtemperatuur", "uk": "Температура зворотки"},
    "exhaust_temperature": {"de": "Abgastemperatur", "nl": "Rookgastemperatuur", "uk": "Температура відхідних газів"},
    "dhw_setpoint_max": {"de": "Warmwasser-Sollwert max.", "nl": "Warmwater-setpoint max.", "uk": "Стеля уставки гарячої води"},
    "dhw_setpoint_min": {"de": "Warmwasser-Sollwert min.", "nl": "Warmwater-setpoint min.", "uk": "Мінімум уставки гарячої води"},
    "ch_setpoint_max_bound": {"de": "Vorlauf-Sollwert max.", "nl": "CV-setpoint max.", "uk": "Стеля уставки подачі"},
    "ch_setpoint_min_bound": {"de": "Vorlauf-Sollwert min.", "nl": "CV-setpoint min.", "uk": "Мінімум уставки подачі"},
    "dhw_setpoint": {"de": "Warmwasser-Sollwert", "nl": "Warmwater-setpoint", "uk": "Уставка гарячої води"},
    "max_ch_setpoint": {"de": "Max. Vorlauf-Sollwert", "nl": "Max. CV-setpoint", "uk": "Макс. уставка подачі"},
    "unsuccessful_burner_starts": {"de": "Fehlgeschlagene Brennerstarts", "nl": "Mislukte branderstarts", "uk": "Невдалі запуски пальника"},
    "flame_low_signal_count": {"de": "Flammenausfälle", "nl": "Vlamuitvallen", "uk": "Зриви полум'я"},
    "burner_starts": {"de": "Brennerstarts", "nl": "Branderstarts", "uk": "Запуски пальника"},
    "ch_pump_starts": {"de": "Heizungspumpenstarts", "nl": "CV-pompstarts", "uk": "Запуски насоса опалення"},
    "dhw_pump_starts": {"de": "Warmwasserpumpenstarts", "nl": "Warmwaterpompstarts", "uk": "Запуски насоса гарячої води"},
    "dhw_burner_starts": {"de": "Warmwasser-Brennerstarts", "nl": "Warmwater-branderstarts", "uk": "Запуски пальника для гарячої води"},
    "burner_hours": {"de": "Brennerstunden", "nl": "Branderuren", "uk": "Години роботи пальника"},
    "ch_pump_hours": {"de": "Heizungspumpenstunden", "nl": "CV-pompuren", "uk": "Години роботи насоса опалення"},
    "dhw_pump_hours": {"de": "Warmwasserpumpenstunden", "nl": "Warmwaterpompuren", "uk": "Години роботи насоса гарячої води"},
    "dhw_burner_hours": {"de": "Warmwasser-Brennerstunden", "nl": "Warmwater-branderuren", "uk": "Години роботи пальника для гарячої води"},
    "master_ot_version": {"de": "Master-OpenTherm-Version", "nl": "Master OpenTherm-versie", "uk": "Версія OpenTherm майстра"},
    "slave_ot_version": {"de": "Kessel-OpenTherm-Version", "nl": "Ketel OpenTherm-versie", "uk": "Версія OpenTherm котла"},
    "master_product_version": {"de": "Master-Produktversion", "nl": "Master productversie", "uk": "Версія виробу майстра"},
    "slave_product_type": {"de": "Kessel-Produkttyp", "nl": "Ketel producttype", "uk": "Тип виробу котла"},
    "slave_product_version": {"de": "Kessel-Produktversion", "nl": "Ketel productversie", "uk": "Версія виробу котла"},
    # --- virtuals (executor) ---
    "ch_enable": {"de": "Heizung freigegeben", "nl": "Verwarming ingeschakeld", "uk": "Опалення дозволено"},
    "dhw_enable": {"de": "Warmwasser freigegeben", "nl": "Warmwater ingeschakeld", "uk": "Гаряча вода дозволена"},
    "heating_season": {"de": "Heizsaison", "nl": "Stookseizoen", "uk": "Опалювальний сезон"},
    "control_mode": {"de": "Regelungsmodus", "nl": "Regelmodus", "uk": "Режим керування"},
    "control_state": {"de": "Regelungszustand", "nl": "Regeltoestand", "uk": "Стан керування"},
    "ch_setpoint_effective": {"de": "Effektiver Vorlauf-Sollwert", "nl": "Effectief CV-setpoint", "uk": "Чинна уставка подачі"},
    "room_temperature_effective": {"de": "Raumtemperatur", "nl": "Kamertemperatuur", "uk": "Температура кімнати"},
    "room_source": {"de": "Raumquelle", "nl": "Kamerbron", "uk": "Джерело кімнати"},
    "ch_enable_effective": {"de": "Wärmeanforderung an Kessel", "nl": "Warmtevraag naar ketel", "uk": "Запит тепла до котла"},
    "failsafe_count": {"de": "Notbetrieb-Auslösungen", "nl": "Failsafe-activeringen", "uk": "Входи в аварійний режим"},
    "last_failsafe_duration_s": {"de": "Dauer des letzten Notbetriebs", "nl": "Duur laatste failsafe", "uk": "Тривалість останнього аварійного режиму"},
}

LOCALES = ("de", "nl", "uk")
