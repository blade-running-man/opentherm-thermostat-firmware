// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// firmwareText.ts is the pure best-effort prose registry shared by the Log page (row.ts, Task 8)
// and every page's errors.ts (via detail.ts). Written before firmwareText.ts exists (TDD).
//
// Uses the project's own harness (eq/ok/report), not node:test/node:assert -- see
// pages/settings/tests/harness.ts for why a bare node:* import would break `npm run build`.
//
// Run: node src/i18n/tests/firmwareText.test.ts

import { matchTemplate, LOG_TEMPLATES, DETAIL_TEMPLATES } from "../firmwareText.ts";
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";

// components/ot_bus/ot_bus.c: ESP_LOGW(TAG, "boiler is not answering (%s) after %u tries; ...").
const busLine = matchTemplate(
  "boiler is not answering (timeout) after 3 tries; polling continues",
  LOG_TEMPLATES
);
ok(busLine !== null, "a known log line matches and binds params");
if (busLine !== null) {
  eq(busLine.key, "log.bus.not_answering", "matched the not_answering template");
  eq(busLine.params["1"], "timeout", "bound the first capture group");
  eq(busLine.params["2"], "3", "bound the second capture group");
}

// components/ot_bus/ot_bus.c: ESP_LOGI(TAG, "boiler is answering").
const answering = matchTemplate("boiler is answering", LOG_TEMPLATES);
ok(answering !== null, "the fixed 'boiler is answering' line matches");
if (answering !== null) eq(answering.key, "log.bus.answering", "matched the answering template");

// components/ot_command/ot_command.c: OT_CMD_UNKNOWN_KEY -> "no such entity".
const detail = matchTemplate("no such entity", DETAIL_TEMPLATES);
ok(detail !== null, "a known error detail matches");
if (detail !== null) eq(detail.key, "error.detail.no_entity", "matched the no_entity template");

// ESP-IDF's own boot log, not ours to translate: English passthrough (null = "no match").
eq(matchTemplate("wifi:mode : sta", LOG_TEMPLATES), null, "an unknown line returns null");

// components/ot_bus/ot_bus.c: ESP_LOGI(TAG, "ID %3u is not supported by the boiler; ..."). %3u
// pads with spaces, so the pattern must tolerate more than one space before the digits.
const idUnsupported = matchTemplate(
  "ID   1 is not supported by the boiler; removed from the poll ring",
  LOG_TEMPLATES
);
ok(idUnsupported !== null, "a padded %3u data-id still matches");
if (idUnsupported !== null) {
  eq(idUnsupported.key, "log.bus.id_unsupported", "matched the id_unsupported template");
  eq(idUnsupported.params["1"], "1", "the padding is not part of the captured id");
}

// components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGE(TAG, "broker refused the connection: %s (code %u)").
const refused = matchTemplate("broker refused the connection: Not authorized (code 5)", LOG_TEMPLATES);
ok(refused !== null, "the broker-refused line matches and binds both groups");
if (refused !== null) {
  eq(refused.key, "log.mqtt.refused", "matched the refused template");
  eq(refused.params["1"], "Not authorized", "bound the reason");
  eq(refused.params["2"], "5", "bound the code");
}

// Task 8 review follow-up (a): the tightened "broker lost" pattern matches the real firmware
// string exactly and no longer accepts an arbitrary suffix via a bare `.*`.
const lost = matchTemplate("broker lost; retrying every 10 s, nothing else changes", LOG_TEMPLATES);
ok(lost !== null, "the exact firmware string for broker-lost still matches");
eq(
  matchTemplate("broker lost; retrying every 10 s, and also does something else entirely", LOG_TEMPLATES),
  null,
  "a suffix the firmware never actually sends no longer matches (was a bare .*)"
);

// Task 8 review follow-up (b): a tag-gated template must not match when the caller (row.ts's
// translateLogText) has filtered the registry down to a DIFFERENT tag -- this is what stops one
// component's fixed phrase from cross-matching a same-looking line logged by another.
const busOnly = LOG_TEMPLATES.filter((entry) => !entry.tag || entry.tag === "ot_bus");
const mqttOnly = LOG_TEMPLATES.filter((entry) => !entry.tag || entry.tag === "mqtt");
ok(matchTemplate("boiler is answering", busOnly) !== null,
   "an ot_bus-tagged template matches when filtered for its own tag");
eq(matchTemplate("boiler is answering", mqttOnly), null,
   "the same ot_bus-tagged template does NOT match when filtered for a different tag");

report("i18n/firmwareText");
