// web/mock/entities.ts
// Projections of BoilerState into the SPA's JSON shapes, reusing the generated registry so the
// mock and the UI share one entity list (CLAUDE.md: "one list of entities, ever"). A value is
// "ok" only for the entities the model actually drives; every other Data-ID in the registry
// (bus counters, product IDs, the OEM product string, ...) projects as unavailable, the same
// way a real boiler leaves most of the registry unanswered.
import { ENTITIES, type EntityMeta } from "../src/api/entities.ts";
import type { BoilerState, MockResponse } from "./model.ts";

type Availability = "ok" | "invalid" | "unsupported" | "unknown";
type Scalar = number | boolean | string | null;
interface Cell { value: Scalar; availability: Availability; ageMs: number | null; }

// Keys the model drives, mapped to the current scalar. A key here MUST exist in ENTITIES
// (reconciled against `node -e "import('./src/api/entities.ts')..."` before this was written);
// a key ABSENT from this map simply projects as unknown, same as an unmodeled Data-ID.
function driven(s: BoilerState): Record<string, Scalar> {
  const heatingNow = s.chEnable && s.scenario.heatingSeason && !s.scenario.failsafe;
  return {
    ch_enable: s.chEnable,
    dhw_enable: s.dhwEnable,
    heating_season: s.scenario.heatingSeason,
    control_mode: s.scenario.mode,
    control_state: s.controlState,
    ch_setpoint_effective: s.heldSetpointDc / 10,
    ch_enable_effective: heatingNow,
    failsafe_count: s.failsafeCount,
    last_failsafe_duration_s: s.lastFailsafeDurationS,
    ch_setpoint: s.heldSetpointDc / 10,
    flow_temperature: s.flowDc / 10,
    return_temperature: s.returnDc / 10,
    room_temperature: s.roomDc / 10,
    outside_temperature: s.outdoorDc / 10,
    dhw_temperature: s.dhwFlowDc / 10,
    dhw_setpoint: s.dhwSetpointDc / 10,
    modulation: s.modulationPct,
    fault: s.scenario.boilerFault,
    flame: s.flame,
    ch_active: s.chEnable && s.flame,
    dhw_active: s.dhwEnable && s.dhwFlowDc >= s.dhwSetpointDc - 20,
    oem_fault_code: s.scenario.boilerFault ? 5 : 0,
  };
}

// Entities the executor holds while Home Assistant is in control (V2 spec §2.2): a local write
// here would race the executor's own held command, so the mock refuses it as the firmware does.
const HA_OWNED = new Set(["ch_enable", "ch_setpoint"]);

function cellFor(meta: EntityMeta, map: Record<string, Scalar>, answering: boolean): Cell {
  if (!(meta.key in map)) return { value: null, availability: "unknown", ageMs: null };
  if (!answering) return { value: null, availability: "unknown", ageMs: null };
  return { value: map[meta.key], availability: "ok", ageMs: 500 };
}

export function renderState(
  s: BoilerState,
): { schema: number; state: Record<string, { availability: Availability; value: Scalar; age_ms: number | null }> } {
  const map = driven(s);
  const state: Record<string, { availability: Availability; value: Scalar; age_ms: number | null }> = {};
  for (const meta of ENTITIES) {
    const c = cellFor(meta, map, s.scenario.answering);
    state[meta.key] = { availability: c.availability, value: c.value, age_ms: c.ageMs };
  }
  return { schema: 1, state };
}

export function renderWsValues(s: BoilerState): Record<string, Scalar> {
  const map = driven(s);
  const out: Record<string, Scalar> = {};
  for (const meta of ENTITIES) out[meta.key] = cellFor(meta, map, s.scenario.answering).value;
  return out;
}

interface ApiMeta {
  key: string; name: string; data_id: number | null; writable: boolean;
  unit?: string; device_class?: string; entity_category?: string;
  options?: readonly string[]; min?: number; max?: number;
}
function apiMeta(meta: EntityMeta): ApiMeta {
  const m: ApiMeta = { key: meta.key, name: meta.name, data_id: meta.dataId, writable: meta.writable };
  if (meta.unit) m.unit = meta.unit;
  if (meta.deviceClass) m.device_class = meta.deviceClass;
  if (meta.entityCategory) m.entity_category = meta.entityCategory;
  if (meta.options) m.options = meta.options;
  if (meta.min !== null) m.min = meta.min;
  if (meta.max !== null) m.max = meta.max;
  return m;
}

export function renderEntities(s: BoilerState): { schema: number; entities: ApiMeta[] } {
  void s; // the entity list is static; kept for a uniform call shape with the other renderers
  return { schema: 1, entities: ENTITIES.map(apiMeta) };
}

export function renderEntity(s: BoilerState, key: string): MockResponse {
  const meta = ENTITIES.find((e) => e.key === key);
  if (!meta) return { status: 404, body: { error: "no such entity" } };
  const c = cellFor(meta, driven(s), s.scenario.answering);
  return { status: 200, body: { meta: apiMeta(meta), value: { availability: c.availability, value: c.value, age_ms: c.ageMs } } };
}

// writeEntity mirrors the firmware's refusals in order: 404 unknown, 405 read-only, 409 HA
// ownership, 422 out of range, 202 accepted (ot_command's ladder, CLAUDE.md's ot_command entry).
export function writeEntity(s: BoilerState, key: string, value: number | boolean): MockResponse {
  const meta = ENTITIES.find((e) => e.key === key);
  if (!meta) return { status: 404, body: { error: "no such entity" } };
  if (!meta.writable) return { status: 405, body: { error: "entity is read-only" } };
  if (s.scenario.mode === "ha" && HA_OWNED.has(key)) {
    return { status: 409, body: { error: "owned by home assistant" } };
  }
  if (typeof value === "number" && meta.min !== null && meta.max !== null) {
    if (value < meta.min || value > meta.max) return { status: 422, body: { error: "value out of range" } };
  }
  applyWrite(s, key, value);
  if (meta.dataId !== null) return { status: 202, body: { queued: { data_id: meta.dataId, raw: Math.round(Number(value) * 256) } } };
  return { status: 202, body: { applied: { command: key } } };
}

function applyWrite(s: BoilerState, key: string, value: number | boolean): void {
  switch (key) {
    case "ch_setpoint": s.heldSetpointDc = Math.round(Number(value) * 10); break;
    case "ch_enable": s.chEnable = Boolean(value); break;
    case "dhw_enable": s.dhwEnable = Boolean(value); break;
    case "dhw_setpoint": s.dhwSetpointDc = Math.round(Number(value) * 10); break;
    case "heating_season": s.scenario.heatingSeason = Boolean(value); break;
    case "room_temperature": s.roomDc = Math.round(Number(value) * 10); break;
    default: break; // a registry entity the model does not track: accepted, has no visible effect
  }
}
