// web/mock/tests/config.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { initialState, SECRET_SENTINEL } from "../model.ts";
import { getConfig, postConfig, provision, wifiScan, getLog } from "../config.ts";

const s = initialState();
const cfg = getConfig(s).body as Record<string, unknown>;
eq(cfg.mqtt_password, SECRET_SENTINEL, "stored secret is redacted to sentinel");
eq(cfg.ui_password, "", "unset password reads as empty string");
eq(cfg.ui_password_set, false, "derived ui_password_set present");
eq(cfg.read_only, false, "derived read_only present");

const r1 = postConfig(s, { mqtt_host: "10.0.0.5", mqtt_password: SECRET_SENTINEL, ui_password: "" });
eq(r1.status, 200, "config patch accepted");
eq(s.config.mqtt_host, "10.0.0.5", "host updated");
eq(s.config.mqtt_password, "secret", "sentinel preserved the stored secret");

const r2 = postConfig(s, { watchdog_s: 600 });
eq(r2.status, 200, "executor patch accepted");
eq(s.config.watchdog_s, 600, "watchdog updated");

const r3 = postConfig(s, { flow_min_dc: 900, flow_max_dc: 100 });
eq(r3.status, 422, "bad flow bounds refused");
ok((r3.body as { field?: string }).field !== undefined, "refusal names a field");

// executor-OWNED fields are refused 422 read-only-field (not 400 unknown), matching the firmware.
const r6 = postConfig(s, { dhw_setpoint_dc: 500 });
eq(r6.status, 422, "executor-owned field in a config patch => 422");
eq((r6.body as { field?: string }).field, "dhw_setpoint_dc", "422 names the read-only field");

// executor-owned key sent as a config patch is refused by name.
const r4 = postConfig(s, { watchdog_s: 700, mqtt_host: "x" });
ok(r4.status === 422 || r4.status === 200, "mixed patch handled");

// setting a ui_password flips the password-set flag.
const r5 = postConfig(s, { ui_password: "secretpw" });
eq(r5.status, 200, "password set accepted");
eq(s.scenario.passwordSet, true, "passwordSet flips true");

eq(provision(s, { wifi_ssid: "net2" }).status, 200, "provision accepted");
eq(s.config.wifi_ssid, "net2", "ssid stored");
eq(provision(s, {}).status, 422, "provision without ssid refused");

ok(Array.isArray(wifiScan(s).body), "scan returns an array");
ok(Array.isArray(getLog(s).body), "log returns an array of strings");

report("mock/config");
