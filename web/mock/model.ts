// web/mock/model.ts
export type Mode = "local" | "ha";
export type ControlState =
  | "season_off" | "boost" | "local" | "ha_waiting" | "failsafe" | "ha" | "unknown";
export type ControlReason =
  | "none" | "watchdog" | "ha_blind" | "fs_disarmed" | "fs_blind"
  | "fs_room_cold" | "fs_room_warm" | "min_cycle" | "await_setpoint" | "unknown";
export type ControlCause = "none" | "watchdog" | "ha_blind";

// Dev-only scenario knobs. NOT part of the firmware contract.
export interface Scenario {
  provisioned: boolean;     // false => only /api/provision accepted
  passwordSet: boolean;     // true  => GET/POST need HTTP Basic; 401 + WWW-Authenticate
  password: string;         // the accepted Basic password (user "admin")
  mode: Mode;               // local | ha
  heatingSeason: boolean;
  boilerFault: boolean;     // raises ID 0 fault flag + oem_fault_code
  answering: boolean;       // false => bus "silent": ot/raw answering=false, state stale
  brokerUp: boolean;        // reserved for a future /api/status mqtt block; no route reads it yet
  failsafe: boolean;        // force the failsafe branch of the ladder
}

export interface BoilerState {
  scenario: Scenario;
  // dynamic model, all temperatures in tenths of a degree (dc)
  chEnable: boolean;
  heldSetpointDc: number;      // commanded TSet (ID 1)
  flowDc: number;              // ID 25 boiler flow temperature
  returnDc: number;            // ID 28 return temperature
  roomDc: number;              // ID 24 room temperature
  outdoorDc: number;           // ambient the room decays toward
  dhwEnable: boolean;
  dhwSetpointDc: number;
  dhwFlowDc: number;           // ID 26
  modulationPct: number;       // ID 17 relative modulation, 0..100
  flame: boolean;              // ID 0 flame status
  // control layer
  controlState: ControlState;
  reason: ControlReason;
  cause: ControlCause;
  watchdogOverdueS: number;    // counts up in HA mode when HA is "blind"
  boostActive: boolean;
  boostSetpointDc: number;
  boostRemainingS: number;
  failsafeCount: number;
  lastFailsafeDurationS: number;
  cycles: number; ok: number; failed: number; overdue: number;
  uptimeMs: number;
  // config document (the persisted knobs the SPA reads/writes)
  config: MockConfig;
  log: string[];
  scanDone: number; scanTotal: number;
  lineTestUntilMs: number;     // 0 = not running
}

export interface MockConfig {
  wifi_ssid: string; mqtt_host: string; mqtt_port: number; mqtt_user: string;
  topic_prefix: string; ha_discovery: boolean; device_name: string; tz: string; ntp_server: string;
  dhw_enable: boolean; control_mode: 0 | 1; heating_season: boolean;
  watchdog_s: number; failsafe_setpoint_dc: number; failsafe_room_target_dc: number;
  failsafe_heat_days: number; failsafe_min_cycle_s: number;
  flow_min_dc: number; flow_max_dc: number;
  local_ch_enable: boolean; local_ch_setpoint_dc: number; dhw_setpoint_dc: number;
  // secrets are stored but never projected verbatim
  mqtt_password: string; wifi_psk: string; ui_password: string;
}

export const SECRET_SENTINEL = "__UNCHANGED__"; // must equal OT_SECRET_SENTINEL / web secrets.ts

export interface MockRequest {
  method: string;              // upper-case
  path: string;                // pathname only, no query
  query: Record<string, string>;
  headers: Record<string, string>; // lower-cased keys
  body: unknown;               // parsed JSON, or undefined
}
export interface MockResponse {
  status: number;
  headers?: Record<string, string>;
  body?: unknown;              // serialized to JSON by the transport; string[] and objects both fine
}

export function initialState(): BoilerState {
  const config: MockConfig = {
    wifi_ssid: "home-wifi", mqtt_host: "192.168.1.10", mqtt_port: 1883, mqtt_user: "ha",
    topic_prefix: "opentherm", ha_discovery: true, device_name: "Термостат котла",
    tz: "UTC-2", ntp_server: "pool.ntp.org",
    dhw_enable: true, control_mode: 0, heating_season: true,
    watchdog_s: 900, failsafe_setpoint_dc: 450, failsafe_room_target_dc: 180,
    failsafe_heat_days: 3, failsafe_min_cycle_s: 300, flow_min_dc: 100, flow_max_dc: 800,
    local_ch_enable: true, local_ch_setpoint_dc: 550, dhw_setpoint_dc: 500,
    mqtt_password: "secret", wifi_psk: "secret", ui_password: "",
  };
  return {
    scenario: {
      provisioned: true, passwordSet: false, password: "admin", mode: "local",
      heatingSeason: true, boilerFault: false, answering: true, brokerUp: true, failsafe: false,
    },
    chEnable: true, heldSetpointDc: config.local_ch_setpoint_dc,
    flowDc: 300, returnDc: 280, roomDc: 205, outdoorDc: 50,
    dhwEnable: true, dhwSetpointDc: config.dhw_setpoint_dc, dhwFlowDc: 480,
    modulationPct: 0, flame: false,
    controlState: "local", reason: "none", cause: "none", watchdogOverdueS: 0,
    boostActive: false, boostSetpointDc: 0, boostRemainingS: 0,
    failsafeCount: 0, lastFailsafeDurationS: 0,
    cycles: 0, ok: 0, failed: 0, overdue: 0, uptimeMs: 0,
    config, log: ["boot: mock boiler simulator up", "ot: bus answering"],
    scanDone: 0, scanTotal: 0, lineTestUntilMs: 0,
  };
}
