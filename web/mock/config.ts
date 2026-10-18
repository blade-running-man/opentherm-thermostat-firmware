// web/mock/config.ts
// Handlers for /api/config (GET/POST), /api/provision, /api/wifi/scan, /api/log. Secrets are
// write-only: a GET returns the sentinel when a secret is stored, "" when none. The single POST
// route carries either a ConfigPatch or an ExecutorPatch; executor-owned keys are refused by
// name if sent as a config patch (the firmware answers 422 read-only-field).
import { SECRET_SENTINEL, type BoilerState, type MockResponse } from "./model.ts";

const EXECUTOR_KEYS = new Set([
  "control_mode", "watchdog_s", "failsafe_setpoint_dc", "failsafe_room_target_dc",
  "failsafe_heat_days", "failsafe_min_cycle_s", "flow_min_dc", "flow_max_dc",
]);
const CONFIG_KEYS = new Set([
  "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_password", "topic_prefix",
  "ha_discovery", "device_name", "tz", "ntp_server", "ui_password",
]);
// Executor-OWNED fields: written only through /api/entities/<key>, so a config patch naming one
// is refused 422 "read-only-field" -- NOT 400 "unknown field" (src/api/config.ts:45-49). They are
// unreachable via the SPA's typed ConfigPatch, but a curl probe must see the same code as hardware.
const EXECUTOR_OWNED = new Set([
  "dhw_enable", "heating_season", "local_ch_enable", "local_ch_setpoint_dc", "dhw_setpoint_dc",
]);

function redactSecret(stored: string): string { return stored ? SECRET_SENTINEL : ""; }

export function getConfig(s: BoilerState): MockResponse {
  const c = s.config;
  return {
    status: 200,
    body: {
      wifi_ssid: c.wifi_ssid, wifi_psk: redactSecret(c.wifi_psk),
      mqtt_host: c.mqtt_host, mqtt_port: c.mqtt_port, mqtt_user: c.mqtt_user,
      mqtt_password: redactSecret(c.mqtt_password), topic_prefix: c.topic_prefix,
      ha_discovery: c.ha_discovery, device_name: c.device_name, tz: c.tz, ntp_server: c.ntp_server,
      dhw_enable: c.dhw_enable, control_mode: c.control_mode, heating_season: c.heating_season,
      watchdog_s: c.watchdog_s, failsafe_setpoint_dc: c.failsafe_setpoint_dc,
      failsafe_room_target_dc: c.failsafe_room_target_dc, failsafe_heat_days: c.failsafe_heat_days,
      failsafe_min_cycle_s: c.failsafe_min_cycle_s, flow_min_dc: c.flow_min_dc, flow_max_dc: c.flow_max_dc,
      local_ch_enable: c.local_ch_enable, local_ch_setpoint_dc: c.local_ch_setpoint_dc,
      dhw_setpoint_dc: c.dhw_setpoint_dc, ui_password: redactSecret(c.ui_password),
      ui_password_set: !!c.ui_password || s.scenario.passwordSet,
      read_only: false, wifi_known_good: true,
    },
  };
}

export function postConfig(s: BoilerState, body: unknown): MockResponse {
  if (typeof body !== "object" || body === null) return { status: 400, body: { error: "bad body" } };
  const patch = body as Record<string, unknown>;
  const isExecutor = Object.keys(patch).every((k) => EXECUTOR_KEYS.has(k));

  if (isExecutor) {
    if ("flow_min_dc" in patch || "flow_max_dc" in patch) {
      const lo = Number(patch.flow_min_dc ?? s.config.flow_min_dc);
      const hi = Number(patch.flow_max_dc ?? s.config.flow_max_dc);
      if (lo >= hi) return { status: 422, body: { error: "flow_min must be below flow_max", field: "flow" } };
    }
    for (const [k, v] of Object.entries(patch)) (s.config as unknown as Record<string, unknown>)[k] = v;
    return { status: 200, body: { saved: true } };
  }

  for (const k of Object.keys(patch)) {
    if (EXECUTOR_KEYS.has(k) || EXECUTOR_OWNED.has(k)) return { status: 422, body: { error: "field is read-only here", field: k } };
    if (!CONFIG_KEYS.has(k)) return { status: 400, body: { error: "unknown field", field: k } };
  }
  for (const [k, v] of Object.entries(patch)) {
    if ((k === "mqtt_password" || k === "ui_password") && v === SECRET_SENTINEL) continue;
    (s.config as unknown as Record<string, unknown>)[k] = v;
  }
  if (patch.ui_password !== undefined && patch.ui_password !== SECRET_SENTINEL) {
    s.scenario.passwordSet = String(patch.ui_password).length > 0;
    if (s.scenario.passwordSet) s.scenario.password = String(patch.ui_password);
  }
  return { status: 200, body: { saved: true } };
}

export function provision(s: BoilerState, body: unknown): MockResponse {
  if (typeof body !== "object" || body === null) return { status: 400, body: { error: "bad body", field: "wifi_ssid" } };
  const p = body as { wifi_ssid?: unknown; wifi_psk?: unknown };
  if (typeof p.wifi_ssid !== "string" || p.wifi_ssid.length === 0) {
    return { status: 422, body: { error: "ssid required", field: "wifi_ssid" } };
  }
  s.config.wifi_ssid = p.wifi_ssid;
  if (typeof p.wifi_psk === "string" && p.wifi_psk !== SECRET_SENTINEL) s.config.wifi_psk = p.wifi_psk;
  s.scenario.provisioned = true;
  return { status: 200, body: { ok: true } };
}

export function wifiScan(_s: BoilerState): MockResponse {
  return {
    status: 200,
    body: [
      { ssid: "home-wifi", rssi: -48, secure: true },
      { ssid: "neighbour", rssi: -71, secure: true },
      { ssid: "", rssi: -80, secure: false },
    ],
  };
}

export function getLog(s: BoilerState): MockResponse { return { status: 200, body: s.log.slice() }; }
