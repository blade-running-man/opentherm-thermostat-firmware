// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The ONE place firmware English is best-effort translated: the log text (pages/log) and the
// error detail sentences (every errors.ts). The firmware sends prose, not a machine code
// (api/client.ts ApiError), and ESP-IDF's own log lines are not ours to restructure -- so a line
// that matches no entry is passed through in English. NOTHING here is a contract: a firmware
// string that changes silently reverts that one line to English until its pattern is updated.
//
// Node-safe (no @preact/signals, no DOM, no import of i18n/index.ts) so tests reach it and
// tests/firmwareText.test.ts can run it directly under node; t() is applied by the CALLER
// (i18n/detail.ts and pages/log/row.ts), which keeps this module pure like format.ts. Relative
// imports carry .ts (node's own type stripping needs the literal specifier).

import type { MsgKey } from "./catalogs/en.ts";

export interface Template {
  /** When set, a caller may pre-filter the registry to entries whose tag matches the log
   *  line's own tag (pages/log/row.ts), so two components' same-looking line cannot cross-match. */
  tag?: string;
  pattern: RegExp;
  key: MsgKey;
}
export interface Match {
  key: MsgKey;
  /** Captured groups, 1-indexed as strings ("1", "2", …), bound to {1}/{2}/… in the catalog. */
  params: Record<string, string>;
}

/** First matching template, with capture groups bound to {1},{2},… params; null on no match. */
export function matchTemplate(text: string, registry: readonly Template[]): Match | null {
  for (const t of registry) {
    const m = t.pattern.exec(text);
    if (!m) continue;
    const params: Record<string, string> = {};
    for (let i = 1; i < m.length; i++) params[String(i)] = m[i] ?? "";
    return { key: t.key, params };
  }
  return null;
}

// Firmware log formats we translate. Enumerated by grepping ESP_LOG in ot_bus, ot_net,
// ot_mqtt_link, ot_captive, ot_http. Diagnostic spam nobody reads is skipped on purpose -- errno-level socket/DNS
// internals, boot stack-watermark lines, radio-event drop counters and NVS repair notices pass
// through English, because a user reading the log has no action to take on them. Patterns are
// anchored (^...$) so a longer, unrelated line cannot partially match; `%3u`'s field width pads
// with spaces, hence `\s+` rather than a single literal space before the digits it captures.
export const LOG_TEMPLATES: readonly Template[] = [
  // components/ot_bus/ot_bus.c.
  { tag: "ot_bus", pattern: /^boiler is answering$/, key: "log.bus.answering" },
  {
    tag: "ot_bus",
    pattern: /^boiler is not answering \((.+)\) after (\d+) tries; polling continues$/,
    key: "log.bus.not_answering",
  },
  {
    tag: "ot_bus",
    pattern: /^ID\s+(\d+) is not supported by the boiler; removed from the poll ring$/,
    key: "log.bus.id_unsupported",
  },
  { tag: "ot_bus", pattern: /^line test finished, polling resumed$/, key: "log.bus.line_test_finished" },

  // components/ot_mqtt_link/ (TAG "mqtt").
  { pattern: /^broker connected$/, key: "log.mqtt.connected" },
  { pattern: /^broker lost; retrying every 10 s, nothing else changes$/, key: "log.mqtt.lost" },
  {
    tag: "mqtt",
    pattern: /^broker refused the connection: (.+) \(code (\d+)\)$/,
    key: "log.mqtt.refused",
  },
  {
    tag: "mqtt",
    pattern: /^broker unreachable; (\d+) attempts so far, retrying every 10 s$/,
    key: "log.mqtt.unreachable",
  },
  { tag: "mqtt", pattern: /^no broker configured; MQTT stays off$/, key: "log.mqtt.not_configured" },
  {
    tag: "mqtt",
    pattern: /^command refused: (.+) \((\d+) more since the last line\)$/,
    key: "log.mqtt.command_refused",
  },
  {
    tag: "mqtt",
    pattern: /^the broker refused the command subscription: check its ACL$/,
    key: "log.mqtt.acl_refused",
  },
  {
    tag: "mqtt",
    pattern: /^discovery for (.+) not published: it does not render$/,
    key: "log.mqtt.discovery_failed",
  },

  // components/ot_net/ot_net_radio.c (TAG "net"); IPSTR expands to "%d.%d.%d.%d".
  {
    tag: "net",
    pattern: /^captive portal up: every name resolves to (\d+\.\d+\.\d+\.\d+)$/,
    key: "log.net.captive_up",
  },
  {
    tag: "net",
    pattern: /^captive DNS did not start \((.+)\); the setup page is still at (\d+\.\d+\.\d+\.\d+)$/,
    key: "log.net.captive_dns_failed",
  },
];

// Firmware error detail sentences (ot_command_strerror() in components/ot_command/ot_command.c;
// ot_http_control.c's ad-hoc ones). Fixed sentences match exactly; interpolated ones use capture
// groups. This is a STARTER set; every page's errors.ts (Tasks 4-7) may extend it as more
// firmware sentences reach the SPA -- unmatched sentences stay English (the honest floor).
export const DETAIL_TEMPLATES: readonly Template[] = [
  { pattern: /^no such entity$/, key: "error.detail.no_entity" },
  { pattern: /^entity is read-only$/, key: "error.detail.read_only" },
  { pattern: /^value out of range$/, key: "error.detail.out_of_range" },
  { pattern: /^boiler does not support this data-id$/, key: "error.detail.unsupported_id" },
  { pattern: /^owned by Home Assistant: (control_mode is ha)$/, key: "error.detail.owned_ha" },
  { pattern: /^owned by the thermostat: (control_mode is local)$/, key: "error.detail.owned_local" },
  { pattern: /^heating_season is off: a boost would not heat$/, key: "error.detail.season_off_boost" },
];
