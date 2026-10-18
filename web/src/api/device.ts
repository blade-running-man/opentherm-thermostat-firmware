// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The live device state, kept current by the push socket.
//
// One place holds the values; everything else reads a signal. This is the client-side
// mirror of `ot_state` on the device -- and, like it, it is not a second entity
// list: the keys come from the device, and `api/entities.ts` (generated from
// tools/opentherm_ids.py) is what will give them types.

import { signal } from "@preact/signals";
import { runOperation as postOperation } from "./client";
import { writeEntity } from "./registry";
import { DeviceSocket } from "./ws";
import type { ConnectionStatus, StateValue } from "./ws";

/**
 * Every entity the device has reported, by key.
 *
 * A key absent from this map has not been heard of; a key present with `null` exists and
 * has no value (see StateValue). The two are different and screens should render them
 * differently -- "unknown entity" is a bug, "--" is a normal reading.
 */
export const deviceState = signal<Record<string, StateValue>>({});

export const connection = signal<ConnectionStatus>("closed");

/**
 * True once a full state frame has arrived on the current connection.
 *
 * Screens need this to tell "the device says nothing is set" from "we have not been told
 * yet", which look identical if you only look at deviceState.
 */
export const stateReceived = signal(false);

const socket = new DeviceSocket({
  onStatus(status) {
    connection.value = status;
    // The next connection re-sends everything, so what we hold is provisional the moment
    // the socket drops. The values stay on screen deliberately -- blanking the UI on a
    // brief reconnect is worse than showing readings a few seconds old, and `connection`
    // is what tells the user which they are looking at.
    if (status !== "open") stateReceived.value = false;
  },

  onFrame(frame) {
    switch (frame.type) {
      case "state":
        // Replace, do not merge: a full frame is the whole truth, and merging would keep
        // an entity alive that the device has stopped reporting.
        deviceState.value = { ...frame.values };
        stateReceived.value = true;
        break;

      case "delta":
        // A new object every time, because signals compare by reference and mutating the
        // existing one would update nothing on screen.
        deviceState.value = { ...deviceState.value, ...frame.values };
        break;

      case "ping":
        // Nothing to do: receiving it is the point. DeviceSocket has already restarted
        // its silence timer by the time this runs.
        break;
    }
  },
});

/** Opens the connection. Called once at app start; safe to call again. */
export function connectDevice(): void {
  socket.start();
}

export function disconnectDevice(): void {
  socket.stop();
}

/**
 * Writes one entity.
 *
 * Over REST, not over the socket. The socket is push-only: a command arriving there would
 * be an endpoint the REST API does not have, and that is precisely the privileged path
 * forbidden by the no-privileged-handle rule. It also means a command
 * is not silently queued behind a reconnect -- a setpoint replayed a minute later is a
 * command the user no longer wants.
 *
 * The value is a number or a boolean and NOT a StateValue: a StateValue may also be a
 * string or null, and neither has a meaning the device could write into a sixteen-bit
 * DATA-VALUE. Rejects with ApiError; api/registry.ts lists what each status means.
 */
export function setEntity(key: string, value: number | boolean): Promise<void> {
  return writeEntity(key, value);
}

/**
 * Runs one named operation: `scan`, `linetest`, `boost` and `boost_off` are the four the
 * firmware has (components/ot_http/ot_http_ops.c). A boost is ladder row 2 of the executor and
 * LOCAL only: 409 in Home Assistant mode or with the heating season off, 422 for minutes outside
 * 1..480 or a setpoint outside flow_min_dc..flow_max_dc. Its progress is GET /api/control
 * (getControl() in api/control.ts).
 *
 * The name is in the path and the parameters are the FLAT object in the body, so an
 * operation's parameters arrive together or not at all. DO NOT split them across two calls
 * -- the device would run something nobody asked for, such as a scan of a range the caller
 * never named.
 */
export function runOperation(
  name: string,
  params?: Record<string, unknown>,
): Promise<void> {
  return postOperation(name, params);
}
