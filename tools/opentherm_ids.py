# SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
# SPDX-License-Identifier: Apache-2.0

"""The single list of OpenTherm entities. This is the only thing that gets edited.

REST, the MQTT topics, discovery for Home Assistant and the frontend types are generated
from here (CLAUDE.md, "One list of entities, ever").

The read and write direction is verified against §5.4 of the OpenTherm 2.2 specification --
that is the only source where every identifier is marked R- / -W / RW. Neither
opentherm_library nor schema.py from ESPHome carries the direction.

The codec is named explicitly on every row. There is no field width in the table and there
cannot be one: the sixteen bits of DATA-VALUE mean different things for different Data-IDs,
and that cannot be derived from the number.

The table describes the SPECIFICATION, not a particular boiler. What the boiler actually
supports is discovered at runtime from the unknown-dataid response; that does not land here,
otherwise the firmware would stop being one binary for any boiler.

Display names are ENGLISH (the canonical name that reaches Home Assistant via discovery).
English by the owner's instruction (Home Assistant sees these names via discovery).
tools/entity_names_i18n.py carries the DE/NL/UK web overlays keyed by the same keys.
"""
from __future__ import annotations

from dataclasses import dataclass

SCHEMA_VERSION = 1
SCHEMA_VERSION_KEY = "schema_version"
SCHEMA_VERSION_NAME = "Schema version"

C = "°C"
PCT = "%"
BAR = "bar"
LMIN = "L/min"
KW = "kW"
HOURS = "h"
SECONDS = "s"

MEASUREMENT = "measurement"
TOTAL_INCREASING = "total_increasing"
DIAGNOSTIC = "diagnostic"

F88 = "f88"
U16 = "u16"
S16 = "s16"
U8_HB = "u8_hb"
U8_LB = "u8_lb"
S8_HB = "s8_hb"
S8_LB = "s8_lb"


@dataclass(frozen=True)
class Value:
    """A numeric entity: one DATA-VALUE field, parsed by one codec."""

    data_id: int
    key: str
    name: str
    codec: str
    unit: str | None = None
    device_class: str | None = None
    state_class: str | None = None
    icon: str | None = None
    entity_category: str | None = None
    readable: bool = True
    writable: bool = False
    #: The Data-ID of the write, when it differs from data_id. None -- they coincide.
    write_id: int | None = None
    min_value: float | None = None
    max_value: float | None = None
    default: float | None = None
    #: The Data-ID whose response replaces min/max once read. Upper -- s8_hb, lower -- s8_lb.
    bounds_from: int | None = None
    #: The executor command a write to this entity is (ot_control_cmd_t), or
    #: None for a plain frame write. Names, not numbers: generate_registry.CONTROLS maps them.
    control: str | None = None


@dataclass(frozen=True)
class Flag:
    """One bit of a flag byte. A separate entity, because in Home Assistant a flag byte
    cannot be shown whole: eight meanings inside one number."""

    data_id: int
    key: str
    name: str
    high_byte: bool
    bit: int
    device_class: str | None = None
    icon: str | None = None
    entity_category: str | None = None
    readable: bool = True


@dataclass(frozen=True)
class Virtual:
    """An entity with no Data-ID: a synthetic entity, either an input or output of the
    executor or a value sourced from a device task (e.g. the DS18B20 reader).

    A THIRD TABLE, not rows of VALUES: a row there with readable=True puts its Data-ID into
    _poll_ids() and so into the poll ring, and there is no Data-ID to give it -- as ID 0 it
    would be decoded from every ID 0 reply, every other conversation. There is no data_id
    field here at all, so no row can be given one by a slip; the generator emits -1.
    """

    key: str
    name: str                      # English, like every display name (CLAUDE.md)
    kind: str                      # "switch" | "sensor" | "binary" | "enum"
    writable: bool = False
    control: str | None = None     # "CH_ENABLE" | "DHW_ENABLE" | "SEASON" -- ot_control_cmd_t
    options: tuple[str, ...] = ()  # kind "enum" only; the stored value is the option's INDEX
    unit: str | None = None
    device_class: str | None = None
    state_class: str | None = None
    icon: str | None = None
    entity_category: str | None = None


# --- ID 0. Status. We send the high byte, the boiler returns the low one. -------------
#
# The master side (the high byte) is NOT part of the registry: it is not an observation but
# our own decision, and its source is the executor, not the boiler. Showing it as an
# entity would mean creating a second place where the same thing lives.

_STATUS_FLAGS = (
    Flag(0, "fault", "Fault", False, 0, "problem", "mdi:alert"),
    Flag(0, "ch_active", "Central heating active", False, 1, "heat", "mdi:radiator"),
    Flag(0, "dhw_active", "Hot water active", False, 2, "heat", "mdi:water-boiler"),
    Flag(0, "flame", "Flame on", False, 3, "heat", "mdi:fire"),
    Flag(0, "cooling_active", "Cooling active", False, 4, "cold",
         "mdi:snowflake", DIAGNOSTIC),
    Flag(0, "diagnostic_indication", "Service needed", False, 6, "problem",
         "mdi:wrench", DIAGNOSTIC),
)

# --- ID 3. What the boiler can do. High byte -- flags, low byte -- MemberID. ----------

_CONFIG_FLAGS = (
    Flag(3, "dhw_present", "Hot water present", True, 0, None, "mdi:water", DIAGNOSTIC),
    Flag(3, "control_type_on_off", "On/off control", True, 1, None,
         "mdi:toggle-switch", DIAGNOSTIC),
    Flag(3, "cooling_supported", "Cooling supported", True, 2, None,
         "mdi:snowflake", DIAGNOSTIC),
    Flag(3, "dhw_storage_tank", "Hot water storage tank", True, 3, None, "mdi:storage-tank",
         DIAGNOSTIC),
    Flag(3, "pump_control_allowed", "Pump control allowed", True, 4, None,
         "mdi:pump", DIAGNOSTIC),
)

# --- ID 5. Faults. High byte -- flags, low byte -- the manufacturer's code. -----------
#
# The manufacturer's code is shown as a NUMBER. The decoding into text lives on the panel
# and is tied to the MemberID: that the code matches what the boiler shows on its
# display has not been checked on this unit and can only be checked by a real fault.

_FAULT_FLAGS = (
    Flag(5, "service_request", "Service request", True, 0, "problem", "mdi:wrench"),
    Flag(5, "lockout_reset", "Lockout reset needed", True, 1, "problem",
         "mdi:lock-reset"),
    Flag(5, "low_water_pressure", "Low water pressure", True, 2, "problem",
         "mdi:gauge-low"),
    Flag(5, "flame_fault", "Flame fault", True, 3, "problem", "mdi:fire-off"),
    Flag(5, "air_pressure_fault", "Air pressure fault", True, 4, "problem",
         "mdi:weather-windy"),
    Flag(5, "water_over_temperature", "Water over-temperature", True, 5, "problem",
         "mdi:thermometer-alert"),
)

# --- ID 6. Which remote parameters the boiler allows reading and writing. -------------

_REMOTE_FLAGS = (
    Flag(6, "dhw_setpoint_transfer_enabled", "Hot water setpoint transfer enabled", True, 0, None,
         "mdi:import", DIAGNOSTIC),
    Flag(6, "max_ch_setpoint_transfer_enabled", "Max CH setpoint transfer enabled", True, 1,
         None, "mdi:import", DIAGNOSTIC),
    Flag(6, "dhw_setpoint_writable", "Hot water setpoint writable", False, 0, None,
         "mdi:pencil", DIAGNOSTIC),
    Flag(6, "max_ch_setpoint_writable", "Max CH setpoint writable", False, 1, None,
         "mdi:pencil", DIAGNOSTIC),
)

# --- ID 100. What the remote setpoint override (ID 9) means. --------------------------

_OVERRIDE_FLAGS = (
    Flag(100, "override_manual_priority", "Manual override priority", False, 0, None,
         "mdi:hand-back-right", DIAGNOSTIC),
    Flag(100, "override_program_priority", "Program override priority", False, 1, None,
         "mdi:calendar-clock", DIAGNOSTIC),
)

FLAGS: tuple[Flag, ...] = (
    _STATUS_FLAGS + _CONFIG_FLAGS + _FAULT_FLAGS + _REMOTE_FLAGS + _OVERRIDE_FLAGS
)

VALUES: tuple[Value, ...] = (
    # The flow setpoint. We write it, the boiler does not hand it back.
    # bounds_from is NOT set here, and that is not an oversight. The field means "the
    # Data-ID whose s8_hb and s8_lb give the ceiling and the floor" -- that is the shape of
    # ID 48 and 49. The ceiling of the flow setpoint lives in ID 57 as ONE f8.8 value, that
    # is, a different mechanism. Clamping against it is the business of the executor,
    # not of the registry's write bounds; besides, this boiler does not support ID 57 at all.
    Value(1, "ch_setpoint", "CH setpoint", F88, C, "temperature", None,
          "mdi:thermometer", None, readable=False, writable=True,
          min_value=10.0, max_value=90.0, default=40.0, control="CH_SETPOINT"),
    Value(3, "member_id", "Manufacturer member ID", U8_LB, None, None, None,
          "mdi:identifier", DIAGNOSTIC),
    Value(5, "oem_fault_code", "OEM fault code", U8_LB, None, None,
          None, "mdi:numeric", DIAGNOSTIC),
    Value(9, "remote_override_setpoint", "Remote room setpoint", F88, C,
          "temperature", MEASUREMENT, "mdi:thermostat", DIAGNOSTIC),
    Value(14, "max_relative_modulation", "Max relative modulation", F88, PCT, None, None,
          "mdi:speedometer", None, readable=False, writable=True,
          min_value=0.0, max_value=100.0, default=100.0),
    Value(15, "max_capacity", "Max capacity", U8_HB, KW, "power", None,
          "mdi:lightning-bolt", DIAGNOSTIC),
    Value(15, "min_modulation", "Min modulation", U8_LB, PCT, None, None,
          "mdi:speedometer-slow", DIAGNOSTIC),
    Value(16, "room_setpoint", "Room setpoint", F88, C, "temperature", None,
          "mdi:home-thermometer", None, readable=False, writable=True,
          min_value=5.0, max_value=30.0, default=20.0),
    Value(17, "modulation", "Modulation level", F88, PCT, None, MEASUREMENT,
          "mdi:speedometer"),
    Value(18, "ch_pressure", "CH water pressure", F88, BAR, "pressure", MEASUREMENT,
          "mdi:gauge"),
    Value(19, "dhw_flow_rate", "Hot water flow rate", F88, LMIN, None, MEASUREMENT,
          "mdi:waves-arrow-right"),
    Value(24, "room_temperature", "Room temperature", F88, C, "temperature", None,
          "mdi:home-thermometer", None, readable=False, writable=True,
          min_value=-40.0, max_value=60.0),
    Value(25, "flow_temperature", "Flow temperature", F88, C, "temperature",
          MEASUREMENT, "mdi:thermometer"),
    Value(26, "dhw_temperature", "Hot water temperature", F88, C, "temperature", MEASUREMENT,
          "mdi:water-thermometer"),
    Value(27, "outside_temperature", "Outside temperature", F88, C, "temperature",
          MEASUREMENT, "mdi:thermometer"),
    Value(28, "return_temperature", "Return temperature", F88, C, "temperature",
          MEASUREMENT, "mdi:thermometer-minus"),
    Value(33, "exhaust_temperature", "Exhaust temperature", S16, C, "temperature",
          MEASUREMENT, "mdi:smoke", DIAGNOSTIC),
    Value(48, "dhw_setpoint_max", "Hot water setpoint max", S8_HB, C, "temperature", None,
          "mdi:arrow-collapse-up", DIAGNOSTIC),
    Value(48, "dhw_setpoint_min", "Hot water setpoint min", S8_LB, C, "temperature", None,
          "mdi:arrow-collapse-down", DIAGNOSTIC),
    Value(49, "ch_setpoint_max_bound", "CH setpoint max", S8_HB, C,
          "temperature", None, "mdi:arrow-collapse-up", DIAGNOSTIC),
    Value(49, "ch_setpoint_min_bound", "CH setpoint min", S8_LB, C, "temperature",
          None, "mdi:arrow-collapse-down", DIAGNOSTIC),
    Value(56, "dhw_setpoint", "Hot water setpoint", F88, C, "temperature", None,
          "mdi:water-thermometer", None, writable=True,
          min_value=30.0, max_value=80.0, default=55.0, bounds_from=48,
          control="DHW_SETPOINT"),
    Value(57, "max_ch_setpoint", "Max CH setpoint", F88, C, "temperature", None,
          "mdi:arrow-collapse-up", None, writable=True,
          min_value=30.0, max_value=90.0, default=70.0, bounds_from=49),
    Value(113, "unsuccessful_burner_starts", "Unsuccessful burner starts", U16, None,
          None, TOTAL_INCREASING, "mdi:fire-alert", DIAGNOSTIC),
    Value(114, "flame_low_signal_count", "Flame failures", U16, None, None,
          TOTAL_INCREASING, "mdi:fire-off", DIAGNOSTIC),
    Value(116, "burner_starts", "Burner starts", U16, None, None, TOTAL_INCREASING,
          "mdi:fire", DIAGNOSTIC),
    Value(117, "ch_pump_starts", "CH pump starts", U16, None, None,
          TOTAL_INCREASING, "mdi:pump", DIAGNOSTIC),
    Value(118, "dhw_pump_starts", "Hot water pump starts", U16, None, None,
          TOTAL_INCREASING, "mdi:pump", DIAGNOSTIC),
    Value(119, "dhw_burner_starts", "Hot water burner starts", U16, None, None,
          TOTAL_INCREASING, "mdi:fire", DIAGNOSTIC),
    Value(120, "burner_hours", "Burner hours", U16, HOURS, "duration",
          TOTAL_INCREASING, "mdi:clock-outline", DIAGNOSTIC),
    Value(121, "ch_pump_hours", "CH pump hours", U16, HOURS, "duration",
          TOTAL_INCREASING, "mdi:clock-outline", DIAGNOSTIC),
    Value(122, "dhw_pump_hours", "Hot water pump hours", U16, HOURS, "duration",
          TOTAL_INCREASING, "mdi:clock-outline", DIAGNOSTIC),
    Value(123, "dhw_burner_hours", "Hot water burner hours", U16, HOURS,
          "duration", TOTAL_INCREASING, "mdi:clock-outline", DIAGNOSTIC),
    # 124 and 126 are what the master reports about ITSELF. Reading them is pointless,
    # writing them is necessary: without it the boiler does not know which version of the
    # protocol it is talking to.
    Value(124, "master_ot_version", "Master OpenTherm version", F88, None, None, None,
          "mdi:tag", DIAGNOSTIC, readable=False, writable=True,
          min_value=0.0, max_value=255.0, default=2.2),
    Value(125, "slave_ot_version", "Boiler OpenTherm version", F88, None, None, None,
          "mdi:tag", DIAGNOSTIC),
    Value(126, "master_product_version", "Master product version", U16, None, None,
          None, "mdi:tag", DIAGNOSTIC, readable=False, writable=True,
          min_value=0.0, max_value=65535.0, default=1.0),
    Value(127, "slave_product_type", "Boiler product type", U8_HB, None, None, None,
          "mdi:tag", DIAGNOSTIC),
    Value(127, "slave_product_version", "Boiler product version", U8_LB, None, None, None,
          "mdi:tag", DIAGNOSTIC),
)

# --- The executor's entities. No Data-ID: nothing here is on the wire. -------
#
# The ID 0 high byte is still not an entity: the decision lives once, in ot_control. What
# becomes entities are its INPUTS (the three switches) and what it decided (the rest). The
# option strings are the executor's own English names, in its enum order: the stored value
# is the option index, and ot_control hands in its enum ordinal.

VIRTUALS: tuple[Virtual, ...] = (
    Virtual("ch_enable", "Central heating enabled", "switch", writable=True,
            control="CH_ENABLE", icon="mdi:radiator"),
    Virtual("dhw_enable", "Hot water enabled", "switch", writable=True,
            control="DHW_ENABLE", icon="mdi:water-boiler"),
    Virtual("heating_season", "Heating season", "switch", writable=True,
            control="SEASON", icon="mdi:sun-snowflake-variant"),
    Virtual("control_mode", "Control mode", "enum", options=("local", "ha"),
            device_class="enum", icon="mdi:account-switch"),
    Virtual("control_state", "Control state", "enum",
            options=("season_off", "boost", "local", "ha_waiting", "failsafe", "ha"),
            device_class="enum", icon="mdi:state-machine"),
    Virtual("ch_setpoint_effective", "Effective CH setpoint", "sensor", unit=C,
            device_class="temperature", state_class=MEASUREMENT, icon="mdi:thermometer"),
    Virtual("room_temperature_effective", "Room temperature", "sensor", unit=C,
            device_class="temperature", state_class=MEASUREMENT,
            icon="mdi:home-thermometer"),
    Virtual("room_source", "Room source", "enum", options=("none", "shield", "mqtt"),
            icon="mdi:home-search"),
    Virtual("ch_enable_effective", "Heat demand to boiler", "binary",
            icon="mdi:radiator"),
    Virtual("failsafe_count", "Failsafe entries", "sensor",
            state_class=TOTAL_INCREASING, icon="mdi:shield-alert",
            entity_category=DIAGNOSTIC),
    Virtual("last_failsafe_duration_s", "Last failsafe duration",
            "sensor", unit=SECONDS, device_class="duration", state_class=MEASUREMENT,
            icon="mdi:timer-alert-outline", entity_category=DIAGNOSTIC),
)


def _poll_ids() -> tuple[int, ...]:
    """The poll ring is computed FROM the table, not listed next to it.

    ID 0 is excluded deliberately: the scheduler sends it on every second step (§4.3.1),
    and its presence in the ring would mean the status goes out more often than the rule.
    """
    ids = {r.data_id for r in VALUES if r.readable}
    ids |= {r.data_id for r in FLAGS if r.readable}
    ids.discard(0)
    return tuple(sorted(ids))


POLL_IDS: tuple[int, ...] = _poll_ids()
