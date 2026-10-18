// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The entity registry and its values: `/api/state`, `/api/entities[/<key>]` and writing.
//
// A separate module rather than lines in client.ts, for the same reason otRaw.ts became a
// separate module: that one is up against the 350-line ceiling (CLAUDE.md). request() and
// requestVoid() are imported from there, so ApiError and the rule "an unreadable body is a
// failure" are the very same here, and no second API client appears.
//
// WHAT IS DESCRIBED HERE IS THE SHAPE OF THE WIRE, NOT THE PAGE'S IDEA OF IT. Every field
// below is checked against the firmware's renderer components/ot_api/ot_api.c — that is the
// single place these three documents are produced, and it also serves as their specification.
//
// The generated api/entities.ts describes the registry TABLE, not these documents, and its
// EntityMeta does not fit here: there it is `dataId`, here `data_id`; there `min`/`max` are
// always present and hold the generator's constants, here they may be absent altogether, and
// when present they are the bounds THE BOILER named (ot_api.c:68-70 calls ot_state_bounds,
// which hands back the bounds_from it read once they have arrived). Hence the rule: the range
// for an input field comes from this endpoint's answer, not from the generated table. The
// Availability and EntityValue types are taken from there — they are one and the same, and
// there must not be a second declaration of them.

import { request, requestVoid } from "./client";
import type { Availability, EntityValue } from "./entities";

/**
 * The metadata of one entity, as emit_meta() in ot_api.c prints it.
 *
 * The optional fields are genuinely optional: the renderer omits `unit`, `device_class` and
 * `entity_category` when the table does not have them, `min`/`max` when the entity declares
 * no range, and `options` for every kind but an enum. Declaring them `| null` would promise a
 * key the document will not carry.
 *
 * `data_id` is the opposite case: ALWAYS present, and `null` for a synthetic entity -- one
 * the executor decides rather than the boiler reports. It has no Data-ID, and
 * no number here would be true.
 */
export interface ApiEntityMeta {
  key: string;
  name: string;
  data_id: number | null;
  writable: boolean;
  unit?: string;
  device_class?: string;
  entity_category?: string;
  options?: string[];
  min?: number;
  max?: number;
}

/**
 * The value of one entity, as ot_api.c:74-92 prints it.
 *
 * `value` is ALWAYS null when `availability !== "ok"`, and that is not the same as zero:
 * zero would mean "it is 0 °C outside" where the boiler said nothing at all. Availability
 * beyond "ok" has three states, and the three are different diagnoses the page is obliged
 * to keep apart: `invalid` — the boiler answered "the data is invalid", `unsupported` — the
 * boiler answered "I have no such Data-ID", `unknown` — there has been no answer at all.
 *
 * `age_ms` is null while there has never been a value; otherwise milliseconds since the
 * last answer.
 */
export interface ApiEntityState {
  availability: Availability;
  value: EntityValue;
  age_ms: number | null;
}

/** The body of `GET /api/entities`. */
export interface EntitiesDocument {
  schema: number;
  entities: ApiEntityMeta[];
}

/**
 * The body of `GET /api/state`.
 *
 * A WRAPPER, NOT A FLAT MAP. This used to say `Record<string, StateValue>` — the shape of
 * the socket frame, not of this document — and the state page would have tripped over it on
 * the very first answer: the values sit inside `state`, and each of them is an object, not
 * a scalar. The flat map of scalars is what `/ws` sends (api/ws.ts), and there it belongs:
 * the frame has room for neither availability nor age, which is why the state table reads
 * this endpoint and not the socket.
 */
export interface StateDocument {
  schema: number;
  state: Record<string, ApiEntityState>;
}

/** The body of `GET /api/entities/<key>`: metadata and value together (ot_api.c:130-149). */
export interface EntityDocument {
  meta: ApiEntityMeta;
  value: ApiEntityState;
}

/** The whole registry's metadata. Its bounds are the boiler's already; see the file header. */
export function getEntities(): Promise<EntitiesDocument> {
  return request("/api/entities");
}

/** Every entity's value at once: availability and age for each. */
export function getState(): Promise<StateDocument> {
  return request("/api/state");
}

/** One entity. A 404 means the registry has no such key, not that there is no value. */
export function getEntity(key: string): Promise<EntityDocument> {
  return request(`/api/entities/${encodeURIComponent(key)}`);
}

/**
 * Writes one entity.
 *
 * Over REST, not over the socket: the socket only receives, and a command arriving there
 * would be an endpoint the REST API does not have — exactly the privileged path
 * the no-privileged-handle rule forbids.
 *
 * The device answers **202**, not 200, and the response body (`{"queued":{…}}`) is
 * deliberately not read here: the bus queue holds ONE write, the next one evicts the
 * pending one, and at the moment of the answer nobody knows whether the boiler took it
 * (components/ot_bus/include/ot_bus_sched.h). This promise resolving means "queued" and
 * nothing more.
 *
 * Failures arrive as an ApiError and they DIFFER: 404 — no such key, 405 — the entity is
 * read-only, 409 — the boiler does not support its Data-ID, 422 — the value is out of
 * bounds, 400 — the body is bad. Checking any of that here is NOT allowed: the single place
 * where a write's legality is decided is ot_command on the device, and a check duplicated
 * on the surface will one day drift from the check in the depths, and drift silently
 * (components/ot_command/include/ot_command.h).
 */
export function writeEntity(key: string, value: number | boolean): Promise<void> {
  return requestVoid(`/api/entities/${encodeURIComponent(key)}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ value }),
  });
}
