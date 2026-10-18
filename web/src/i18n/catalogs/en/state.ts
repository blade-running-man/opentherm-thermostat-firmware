// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// State page: table column headers, the write action, the page chrome, and its failures
// (State.tsx, errors.ts). Availability labels are NOT here -- they are `token.avail.*`
// (en/tokens.ts), the firmware-contract labels the Control page's mode text also reads.
import type { Message } from "../../format.ts";

export const state = {
  "state.col.entity": "Entity",
  "state.col.value": "Value",
  "state.col.availability": "Availability",
  "state.col.age": "Age",
  "state.col.write": "Write",
  "state.write": "Write: {name}",

  "state.title": "State",
  "state.loading": "Asking the device for its entity registry…",
  "state.hint": "Values refresh every {seconds} s. The input bounds are the ones the boiler "
    + "itself reported; until it reports them, the table's own bounds stand. A write answers "
    + "\"queued\": the bus queue holds one write, and whether the boiler accepted it shows in "
    + "the table's value one polling round later.",
  "state.value.yes": "yes",
  "state.value.no": "no",
  "state.readOnly": "read-only",
  "state.writeAction": "Write",
  "state.notANumber": "not a number",
  "state.queued": "{name}: queued",

  // Read failures: GET /api/entities or GET /api/state (errors.ts, explainStateFailure).
  "error.state.noRoute": "This firmware build does not serve the entity registry",
  "error.state.noRouteDetail": "The /api/entities and /api/state endpoints arrive with the "
    + "state model; the firmware now on the device does not have them. ({message})",
  "error.state.denied": "The device refused to show its state",
  "error.state.fault": "The device reported a fault",
  "error.state.offlineHeadline": "No answer from the device",
  "error.state.offlineDetail": "Nothing reached the device, so it has said nothing about its "
    + "state. ({reason})",
  "error.state.browserSilent": "The browser gave no reason.",

  // Write failures: POST /api/entities/<key> (errors.ts, explainWriteFailure).
  "error.state.write.readOnly": "This entity is read-only",
  "error.state.write.refused": "This write is refused whatever the value",
  "error.state.write.refusedDetail": "It is not the number: either the boiler twice answered "
    + "that it has no such Data-ID and the device stopped asking, or the current control mode "
    + "gives this value to another source. ({message})",
  "error.state.write.outOfBounds": "The value is out of bounds",
  "error.state.write.outOfBoundsDetail": "The request was parsed and understood; it is the "
    + "number that was refused. The bounds beside the input field are the ones the boiler "
    + "itself reported, or the table's own until it has. ({message})",
  "error.state.write.noEntity": "This firmware build has no such entity",
  "error.state.write.badRequest": "The device did not understand the request",
  "error.state.write.denied": "The device refused this write",
  "error.state.write.notAccepted": "The device did not accept the write",
  "error.state.write.offlineDetail": "Nothing reached the device. Whether the value was "
    + "written is unknown; check it in the table once the connection is back. ({reason})",
} satisfies Record<string, Message>;
