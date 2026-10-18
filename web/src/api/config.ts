// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The configuration document: GET and POST /api/config.
//
// request() and requestVoid() come from api/client.ts, so ApiError and "an
// unreadable body is a fault" are the same ones, and there is still one API client.
//
// The import carries the .ts extension so that node can load this module when a suite runs it
// directly (pages/settings/tests/harness.ts); Vite is indifferent.
//
// THE GET MUST ANSWER WITH A JSON BODY: its body is the answer, and a 200 carrying nothing is a
// fault this page can only report as one. The POST may answer with an empty body; `{}` is still
// the better habit, because a field added to the answer later has somewhere to go.

import { request, requestVoid } from "./client.ts";

/**
 * The document `GET /api/config` answers with.
 *
 * The keys are the `name` column of FIELDS, the field table in
 * components/ot_config/ot_config.c, which that file defines as "what the API
 * and the owner see". This interface is a transcription of that column, not a second list of
 * fields: a field added there and not here is simply not shown, and a name that disagrees is
 * a field that silently never saves.
 *
 * Every secret arrives redacted -- CONFIG_UNCHANGED when one is stored, "" when none is, never
 * the value (ot_secret_redact, ot_secrets.h:36-38).
 */
export interface DeviceConfig {
  wifi_ssid: string;
  wifi_psk: string;
  mqtt_host: string;
  mqtt_port: number;
  mqtt_user: string;
  mqtt_password: string;
  topic_prefix: string;
  ha_discovery: boolean;
  device_name: string;
  // POSIX TZ, sign inverted against the one people say out loud: MSK-3 is UTC+3.
  tz: string;
  ntp_server: string;
  // The executor's settings, in the order GET renders them. Every temperature is
  // an INTEGER in tenths (`_dc`: 450 is 45.0 °C); control_mode is 0 (local) or 1 (Home
  // Assistant), not the string the control_mode entity shows.
  //
  // FIVE of them the executor OWNS: dhw_enable, heating_season, local_ch_enable,
  // local_ch_setpoint_dc and dhw_setpoint_dc. GET shows them; POST refuses each by name with 422
  // read-only-field (EXECUTOR_OWNED, components/ot_wire/ot_wire_config.c) and they are written
  // through POST /api/entities/<key> -- dhw_enable, heating_season, ch_enable, ch_setpoint,
  // dhw_setpoint. So none of the five is in ConfigPatch below.
  dhw_enable: boolean;
  control_mode: 0 | 1; // nothing else survives ot_config_check_range() or ot_config_sanitize()
  heating_season: boolean;
  watchdog_s: number;
  failsafe_setpoint_dc: number;
  failsafe_room_target_dc: number;
  failsafe_heat_days: number;
  failsafe_min_cycle_s: number;
  flow_min_dc: number;
  flow_max_dc: number;
  // The room-MQTT source's four settings (docs/ha-room-source.md), rendered right after the flow
  // band (ot_wire_render_config(), components/ot_wire/ot_wire_config.c) and, like the eight
  // above, ordinary NS_APP fields the executor does not own -- POST accepts them the same way it
  // accepts any other setting, so they travel in RoomPatch below, sent only when changed, for the
  // same stale-write reason ExecutorPatch exists.
  room_mqtt_enable: boolean;
  // 1 = room (steers the failsafe), 0 = ambient (shown, never steers).
  room_mqtt_role: 0 | 1;
  room_mqtt_stale_s: number;
  room_mqtt_ha_forwarded: boolean;
  local_ch_enable: boolean;
  local_ch_setpoint_dc: number;
  dhw_setpoint_dc: number; // 0: nobody has written it yet -- "unset", not a temperature
  ui_password: string;
  // The last three are DERIVED by the device and appear in the projection only (the first two
  // are ot_config_public_t's, the third is explained below). They are read, never written back.
  // `ui_password_set` is what the page needs to warn about locking the device before the
  // password exists; `read_only` is true when the flash holds a schema this build does not
  // understand -- an OTA rolled back under a newer configuration -- and every write will be
  // refused until that is resolved.
  ui_password_set: boolean;
  read_only: boolean;
  /**
   * ot_prov_has_known_good() (ot_provision.h:299): a stored credential pair has
   * actually produced an address on the owner's network.
   *
   * SHAPE THE FIRMWARE HAS TO MATCH -- this key does not exist yet, and it is here because the
   * settings page cannot tell the truth without it. It is the ONLY condition under which the
   * device rolls a failed provisioning back (ot_provision.c:442-451), and the flag is set
   * only where an address arrives (ot_provision.c:304-311). `wifi_ssid !== ""` is not a
   * substitute: an SSID stored beside a mistyped key has never produced an address, the trial
   * that fails goes to RETRYING with nothing restored, and a page that promises a rollback
   * there tells the owner to wait for a return that is not coming.
   *
   * It comes from the provisioning state machine and not from the stored record, so it is a
   * projection field like the two above -- ot_config has no business holding it.
   */
  wifi_known_good: boolean;
}

/**
 * What `POST /api/config` carries, and the three omissions ARE the design.
 *
 * `wifi_ssid`/`wifi_psk` are absent because Wi-Fi goes through provisionWifi() alone. An
 * absent key means "leave the stored value alone" -- ot_config_patch_t spells this out
 * (ot_config.h: "A NULL string means the key was ABSENT... absent leaves the
 * stored value alone, empty clears it") -- so saving the broker cannot reach the network under
 * any circumstance, including a bug in this page.
 *
 * `ui_password_set`/`read_only`/`wifi_known_good` are absent because the device derives them.
 * Echoing back a value the device computed invites a firmware that stores it, and a stored "a
 * password is set" flag that disagrees with the stored record is a device locked by nobody,
 * for ever (ot_config_public_t, the comment on ui_password_set).
 *
 * Every executor setting is absent. The five it owns are refused by name (DeviceConfig above); a
 * patch carrying one fails whole. The other eight travel in ExecutorPatch below -- the changed
 * ones only, in a POST of their own -- because this whole-document patch would re-send what the
 * page loaded, a stale control_mode above all. tests/model.test.ts pins the keys.
 *
 * DO NOT make this a Partial<DeviceConfig> "so the page can send less". The page submits the
 * whole document every time; that is what the sentinel scheme is for, and a page that sends
 * only what it thinks changed is the design the sentinel replaced.
 */
export interface ConfigPatch {
  mqtt_host: string;
  mqtt_port: number;
  mqtt_user: string;
  mqtt_password: string;
  topic_prefix: string;
  ha_discovery: boolean;
  device_name: string;
  tz: string;
  ntp_server: string;
  ui_password: string;
}

export function getConfig(): Promise<DeviceConfig> {
  return request("/api/config");
}

export function saveConfig(patch: ConfigPatch): Promise<void> {
  return requestVoid("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(patch),
  });
}

/**
 * The controller's settings: the eight numbers POST /api/config accepts beyond the broker's and
 * the device's (read_number() in components/ot_wire/ot_wire_config.c), in the
 * order GET renders them. NOT the five the executor owns (EXECUTOR_OWNED, same file): those are
 * refused here by name and written through POST /api/entities/<key>.
 */
export const EXECUTOR_KEYS = [
  "control_mode",
  "watchdog_s",
  "failsafe_setpoint_dc",
  "failsafe_room_target_dc",
  "failsafe_heat_days",
  "failsafe_min_cycle_s",
  "flow_min_dc",
  "flow_max_dc",
] as const;

export type ExecutorKey = (typeof EXECUTOR_KEYS)[number];

/**
 * What the settings page's controller card sends: ONLY the keys the owner changed, each a whole
 * number in the device's own unit -- tenths for `_dc`, seconds, days, 0 or 1 for the mode.
 *
 * The opposite of ConfigPatch, on purpose. That document carries secrets, so it is sent whole and
 * the sentinel keeps an untouched secret; these carry none, and they are shared with every other
 * writer -- curl, a second tab, the owner's own script. A value sent because it was on screen puts
 * back whatever the page loaded: a control_mode somebody has since changed, a band since narrowed.
 * An absent key leaves the stored value alone (ot_config_patch_t), so absence is the one value
 * that cannot be stale.
 *
 * DO NOT merge this into ConfigPatch "to save a request": the broker card's whole-document save
 * would then re-send these, which is the stale write this type exists to prevent.
 */
export type ExecutorPatch = Partial<Record<ExecutorKey, number>>;

/** The controller's settings, the changed ones only. The card never sends an empty patch. */
export function saveExecutor(patch: ExecutorPatch): Promise<void> {
  return requestVoid("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(patch),
  });
}

/**
 * The room-MQTT source's four settings (docs/ha-room-source.md), in the order GET renders them.
 * Unlike EXECUTOR_KEYS these are not refused-by-name -- POST takes them like any other field --
 * but they get their own card and their own changed-only patch for the same reason: this document
 * carries no secret, so nothing forces a whole-document send, and a whole-document send would
 * resubmit whatever the broker or device card has queued unsaved.
 */
export const ROOM_KEYS = [
  "room_mqtt_enable",
  "room_mqtt_role",
  "room_mqtt_stale_s",
  "room_mqtt_ha_forwarded",
] as const;

export type RoomKey = (typeof ROOM_KEYS)[number];

/** What the room-MQTT card sends: only the keys the owner changed, each in the device's own type. */
export type RoomPatch = Partial<{
  room_mqtt_enable: boolean;
  room_mqtt_role: number;
  room_mqtt_stale_s: number;
  room_mqtt_ha_forwarded: boolean;
}>;

/** The room-MQTT source's settings, the changed ones only. The card never sends an empty patch. */
export function saveRoomMqtt(patch: RoomPatch): Promise<void> {
  return requestVoid("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(patch),
  });
}
