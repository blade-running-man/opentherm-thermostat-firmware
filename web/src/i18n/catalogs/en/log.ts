// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Log page: the page chrome (Log.tsx) plus the best-effort log-text translation registry
// (i18n/firmwareText.ts's LOG_TEMPLATES, applied by pages/log/row.ts). Every LOG_TEMPLATES key
// has an entry here; a line that matches no template is passed through in English (row.ts).
import type { Message } from "../../format.ts";

export const log = {
  // Page chrome.
  "log.title": "Log",
  "log.follow": "Follow (every {n} s)",
  "log.hint": "The device keeps its last 80 lines. Times are since boot.",
  "log.loading": "Asking the device for its log…",
  "log.empty": "The log is empty: the device has just started.",

  // components/ot_bus/ot_bus.c: ESP_LOGI(TAG, "boiler is answering").
  "log.bus.answering": "boiler is answering",
  // components/ot_bus/ot_bus.c: ESP_LOGW(TAG, "boiler is not answering (%s) after %u tries; ...").
  "log.bus.not_answering": "boiler is not answering ({1}) after {2} tries; polling continues",
  // components/ot_bus/ot_bus.c: ESP_LOGI(TAG, "ID %3u is not supported by the boiler; ...").
  "log.bus.id_unsupported": "ID {1} is not supported by the boiler; removed from the poll ring",
  // components/ot_bus/ot_bus.c: ESP_LOGI(TAG, "line test finished, polling resumed").
  "log.bus.line_test_finished": "line test finished, polling resumed",

  // components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGI(TAG, "broker connected").
  "log.mqtt.connected": "broker connected",
  // components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGW(TAG, "broker lost; retrying every 10 s, ...").
  "log.mqtt.lost": "broker lost; retrying every 10 s, nothing else changes",
  // components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGE(TAG, "broker refused the connection: %s (code %u)").
  "log.mqtt.refused": "broker refused the connection: {1} (code {2})",
  // components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGW(TAG, "broker unreachable; %u attempts so far, ...").
  "log.mqtt.unreachable": "broker unreachable; {1} attempts so far, retrying every 10 s",
  // components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGI(TAG, "no broker configured; MQTT stays off").
  "log.mqtt.not_configured": "no broker configured; MQTT stays off",
  // components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGW(TAG, "command refused: %s (%u more since ...").
  "log.mqtt.command_refused": "command refused: {1} ({2} more since the last line)",
  // components/ot_mqtt_link/ot_mqtt_link.c: ESP_LOGE(TAG, "the broker refused the command subscription: ...").
  "log.mqtt.acl_refused": "the broker refused the command subscription: check its ACL",
  // components/ot_mqtt_link/ot_mqtt_link_publish.c: ESP_LOGE(TAG, "discovery for %s not published: ...").
  "log.mqtt.discovery_failed": "discovery for {1} not published: it does not render",

  // components/ot_net/ot_net_radio.c: ESP_LOGI(TAG, "captive portal up: every name resolves to " IPSTR).
  "log.net.captive_up": "captive portal up: every name resolves to {1}",
  // components/ot_net/ot_net_radio.c: ESP_LOGW(TAG, "captive DNS did not start (%s); the setup page ...").
  "log.net.captive_dns_failed": "captive DNS did not start ({1}); the setup page is still at {2}",
} satisfies Record<string, Message>;
