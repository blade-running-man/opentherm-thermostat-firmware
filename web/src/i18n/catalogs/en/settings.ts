// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Settings page (pages/settings/**): Settings.tsx, every sections/*.tsx, and the display-string
// helper modules (executor.ts, room.ts, wifi.ts, model.ts, errors.ts). EN values are moved
// VERBATIM from those files -- the settings/tests/* suites pin several of them byte-for-byte
// under setLocale("en"), so do NOT reword an existing key without
// checking that suite first.
//
// Config/entity keys shown as sub-text (the executor's eight, the room source's four) and the
// `Sends {keys}` join contents stay the device's own spelling -- they are not translated, and
// none of them appears as a VALUE in this file. Sentinel values (MANUAL_SSID_OPTION,
// CONFIG_UNCHANGED) are code, not catalog entries.
import type { Message } from "../../format.ts";

export const settings = {
  // --- Settings.tsx (page chrome) -----------------------------------------------------------
  "settings.title": "Settings",
  "settings.readOnly.headline": "These settings were written by a newer firmware",
  "settings.readOnly.detail":
    "This build does not understand the stored configuration's layout, so it refuses "
    + "to rewrite it rather than damage it. Reinstall the firmware this device had, "
    + "or clear its settings with the button, before changing anything here.",
  "settings.load.hint":
    "The broker and device settings cannot be shown until they can be read. The Wi-Fi "
    + "form above does not depend on them.",
  "settings.tryAgain": "Try again",
  "settings.loading": "Reading the device's configuration…",
  "settings.save": "Save settings",
  "settings.saved": "Settings saved",
  "settings.saveBar.hint": "Saves the broker and device settings together. It does not touch the network.",
  // Shared by Settings.tsx, ExecutorSection and RoomMqttSection: a failure this page invented
  // (a form value patchFromForm/executorPatch/roomPatch refused), not the device's answer.
  "settings.notSent": "Not sent — nothing left this page",
  // Shared by ExecutorSection and RoomMqttSection: the box-level refusal notice and the
  // save-bar's three states.
  "settings.refusedValue": "The device refused this value; its reason is in the notice below.",
  "settings.nothingChanged": "Nothing changed.",
  "settings.sends": "Sends {keys} and nothing else.",

  // --- BrokerSection ------------------------------------------------------------------------
  "settings.broker.title": "MQTT broker",
  "settings.broker.subtitle": "Where the device publishes, and where Home Assistant finds it.",
  "settings.broker.host.label": "Broker host",
  "settings.broker.host.placeholder": "192.168.1.10 or homeassistant.local",
  "settings.broker.port.label": "Broker port",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Broker username",
  "settings.broker.password.label": "Broker password",
  "settings.broker.password.warning":
    "This connection is not encrypted in this version, so this password is visible to "
    + "anything on your network. Do not reuse a password you use anywhere else.",
  "settings.broker.topicPrefix.label": "Topic prefix",
  "settings.broker.discovery.label": "Publish Home Assistant discovery",
  "settings.broker.discovery.hint":
    "Changing the prefix moves every topic. Home Assistant keeps the entities it already "
    + "learned about at the old prefix until the device retracts them, so expect to see both "
    + "for a while after a change.",

  // --- DeviceSection --------------------------------------------------------------------------
  "settings.device.title": "Device",
  "settings.device.subtitle": "What it is called, and who may change it.",
  "settings.device.name.label": "Device name",
  "settings.device.name.placeholder": "Ventilation",
  "settings.device.name.hint":
    "A display name only. The device's identity — its broker client id and its Home "
    + "Assistant device id — is derived from its MAC address, so renaming it here cannot "
    + "produce a second device in Home Assistant.",
  "settings.device.password.label": "Web interface password",
  "settings.device.password.warnTitle": "There is no \"forgot password\" for this device.",
  // Split around an <em>, so the emphasis stays a real element (JSX puts these back together).
  "settings.device.password.warn1a":
    "If you lose it, the only way back is the button on the device: hold it for five seconds ",
  "settings.device.password.warn1em": "after it has started up",
  "settings.device.password.warn1b":
    ". That clears the Wi-Fi credentials and this password together and leaves the broker "
    + "settings alone.",
  "settings.device.password.warn2a": "Holding the button ",
  "settings.device.password.warn2em": "while",
  "settings.device.password.warn2b":
    " power comes up does something else entirely — it puts the chip into its firmware "
    + "loader, which looks exactly like a device that has died. Power it up first, wait, then "
    + "hold.",
  "settings.device.password.noneSet":
    "No password is set. Anyone who can reach this device on the network can change "
    + "these settings — which is a reasonable choice for a device on a home network, and "
    + "it is the default deliberately.",

  // --- ExecutorSection + executor.ts ----------------------------------------------------------
  "settings.executor.title": "Controller",
  "settings.executor.subtitle": "The mode, the watchdog, the failsafe and the flow band.",
  "settings.executor.mode.local": "Local — this device's own controls",
  "settings.executor.mode.ha": "Home Assistant — needs a broker",
  "settings.executor.save": "Save controller settings",
  "settings.executor.saved": "Controller settings saved",
  // executor.ts's EXECUTOR_LABELS, moved to executorLabel(key) -- ExecutorKey's eight names.
  "settings.executor.label.control_mode": "Control mode",
  "settings.executor.label.watchdog_s": "Watchdog (s)",
  "settings.executor.label.failsafe_setpoint_dc": "Failsafe flow setpoint (°C)",
  "settings.executor.label.failsafe_room_target_dc": "Failsafe room target (°C)",
  "settings.executor.label.failsafe_heat_days": "Failsafe heat days",
  "settings.executor.label.failsafe_min_cycle_s": "Failsafe minimum cycle (s)",
  "settings.executor.label.flow_min_dc": "Lowest flow setpoint (°C)",
  "settings.executor.label.flow_max_dc": "Highest flow setpoint (°C)",
  // ExecutorSection's NOTES, moved to executorNote(key) -- the seven number boxes' captions.
  "settings.executor.note.watchdog_s":
    "How long Home Assistant may stay silent about CH before the failsafe takes over.",
  "settings.executor.note.failsafe_setpoint_dc":
    "The flow setpoint the failsafe heats at, and the one held from boot.",
  "settings.executor.note.failsafe_room_target_dc":
    "The room temperature the failsafe holds while a room sensor is fresh.",
  "settings.executor.note.failsafe_heat_days":
    "The failsafe heats only if Home Assistant asked for heat within this many days.",
  "settings.executor.note.failsafe_min_cycle_s":
    "The failsafe's shortest on time and shortest off time.",
  "settings.executor.note.flow_min_dc":
    "No CH setpoint below this is accepted, from anyone. Keep it at or above the "
    + "boiler's own parameter E: a request below E is not carried out.",
  "settings.executor.note.flow_max_dc": "No CH setpoint above this is accepted, from anyone.",
  // executorPatch()'s refusal sentence: "{label}: {kind} is needed." (executor.test.ts pins the
  // assembled string, not this template, so the pieces must reassemble it exactly).
  "settings.executor.problem": "{label}: {kind} is needed.",
  "settings.executor.kind.decimal": "a temperature in degrees, with at most one decimal",
  "settings.executor.kind.whole": "a whole number",

  // --- RoomMqttSection + room.ts ---------------------------------------------------------------
  "settings.room.title": "Room source (MQTT)",
  "settings.room.subtitle":
    "A room temperature Home Assistant publishes to the device (docs/ha-room-source.md).",
  "settings.room.role.room": "Room — steers the failsafe",
  "settings.room.role.ambient": "Ambient — shown only, never steers",
  "settings.room.stale.hint":
    "How long the device waits with nothing published before treating this source as stale.",
  "settings.room.forwarded.hint":
    "When on, a stale reading forces the failsafe to heat blind instead of holding the last "
    + "room value (the ha_blind case).",
  "settings.room.save": "Save room source settings",
  "settings.room.saved": "Room source settings saved",
  // room.ts's ROOM_LABELS, moved to roomLabel(key) -- RoomKey's four names.
  "settings.room.label.room_mqtt_enable": "Use an MQTT room source",
  "settings.room.label.room_mqtt_role": "Role",
  "settings.room.label.room_mqtt_stale_s": "Stale after (s)",
  "settings.room.label.room_mqtt_ha_forwarded": "Home Assistant forwards a stale reading",
  // roomPatch()'s refusal sentence: "{label}: a whole number is needed." (room.test.ts pins the
  // assembled string).
  "settings.room.problem": "{label}: a whole number is needed.",

  // --- WifiSection + wifi.ts -------------------------------------------------------------------
  "settings.wifi.title": "Wi-Fi",
  "settings.wifi.subtitle": "The list is what this device can hear, which is not what your phone shows.",
  "settings.wifi.network.label": "Network",
  "settings.wifi.network.choose": "— choose a network —",
  "settings.wifi.network.currentlyConfigured": "currently configured",
  "settings.wifi.network.open": "open",
  "settings.wifi.network.manualOption": "Other / hidden network — type the name…",
  "settings.wifi.network.ssidLabel": "Network name (SSID)",
  "settings.wifi.network.ssidPlaceholder": "exactly as the router broadcasts it",
  "settings.wifi.scan.again": "Scan again",
  "settings.wifi.scan.start": "Scan for networks",
  "settings.wifi.chooseFromList": "Choose from the list",
  "settings.wifi.typeInstead": "Type the name instead",
  "settings.wifi.scan.hint":
    "A name that is on your phone and not in this list is the usual reason a device "
    + "\"cannot find\" a network that is plainly there: most routers publish one name on both "
    + "bands, and this device has no 5 GHz radio. Type it in if that happens — and hidden "
    + "networks never appear in any scan, so they can only be typed.",
  "settings.wifi.password.label": "Wi-Fi password",
  // WifiSection.tsx's fallback name for describeWifiKey() when nothing is configured yet.
  "settings.wifi.password.fallbackNetworkName": "the stored network",
  // WifiSection.tsx's fallback name for provisionNotice()'s {target} when no SSID is chosen yet.
  "settings.wifi.notice.newNetworkFallback": "the new network",
  "settings.wifi.password.hintUnencrypted":
    "If you are on the device's own setup network, that network is open: this key crosses "
    + "it unencrypted, once. Nothing in a browser page served over plain HTTP can prevent "
    + "that.",
  "settings.wifi.network.openHint": "{ssid} is open — leave the password empty.",
  "settings.wifi.network.securedWarning":
    "{ssid} is secured and the password box is empty. The device will associate and be "
    + "turned away.",
  "settings.wifi.beforeYouPress": "Before you press this",
  "settings.wifi.save": "Save Wi-Fi and reconnect",
  "settings.wifi.accepted": "Accepted — the device is trying now",

  // wifi.ts's signalWords() -- a word for a dBm reading (settings/tests/wifi.test.ts pins each
  // exactly, and the thresholds stay in code).
  "settings.wifi.signal.notSeen": "not seen in this scan",
  "settings.wifi.signal.strong": "strong",
  "settings.wifi.signal.good": "good",
  "settings.wifi.signal.weak": "weak",
  "settings.wifi.signal.veryWeak": "very weak",

  // wifi.ts's describeWifiKey() -- the sentence under the Wi-Fi key box.
  "settings.wifi.key.stored":
    "The key already stored for {ssid} will be used again. It is never sent back to "
    + "this page, so the box is empty; type to replace it.",
  "settings.wifi.key.empty":
    "Empty: the device will try to join without a key. Correct for an open network, "
    + "and nothing else.",
  "settings.wifi.key.typed": "This key will be sent to the device as typed.",

  // wifi.ts's provisionNotice() -- the four sentences read before Save is pressed.
  "settings.wifi.notice.line1":
    "The device replies to this form before it starts connecting, so a confirmation here "
    + "means the credentials were accepted — not that {target} took them.",
  "settings.wifi.notice.line2":
    "While it tries, the setup network you are on now and {target} share a single radio. "
    + "This page will most likely stop responding within a few seconds — that is the normal "
    + "outcome here, not a failure.",
  "settings.wifi.notice.line3":
    "Afterwards the device is on {target}, with whatever address your router hands it. "
    + "Your router's list of connected clients is where to look for it.",
  "settings.wifi.notice.fallback":
    "If the key is wrong, the device puts {previous} back by itself and returns there — "
    + "it is the last network that actually gave this device an address, which is the only "
    + "thing that makes a pair worth going back to. Nothing needs to be reset.",
  "settings.wifi.notice.noFallback":
    "This device has no network it has ever reached, so there is nothing for it to go "
    + "back to. If the key is wrong it keeps trying, and its own setup network stays on "
    + "the air for about 15 minutes from when it appeared. After that the setup network "
    + "is gone until the device is restarted, which brings it back for a few minutes.",

  // --- model.ts ---------------------------------------------------------------------------------
  // describeSecret()'s five states (SecretField's note/placeholder pair). The two placeholders
  // are pinned byte-for-byte by settings/tests/model.test.ts.
  "settings.secret.stored.text":
    "Stored on the device. It is never sent back, so the box is empty; type to replace it.",
  "settings.secret.stored.placeholder": "unchanged — type to replace",
  "settings.secret.willClear.text": "Emptied. Saving will delete the stored value.",
  "settings.secret.willClear.placeholder": "empty — saving deletes the stored value",
  "settings.secret.willReplace.text": "Saving will replace the stored value.",
  "settings.secret.notSet.text": "Not set.",
  "settings.secret.notSet.placeholder": "not set",
  "settings.secret.willSet.text": "Saving will store this for the first time.",
  // patchFromForm()'s one client-side validation.
  "settings.error.portRange": "Broker port must be a whole number between 1 and 65535.",

  // --- errors.ts (explainError headlines; detail is always the device's own sentence) --------
  "settings.error.notImplemented.headline": "The device has this route and no code behind it yet",
  "settings.error.noHandler.headline": "This firmware build does not serve that endpoint",
  "settings.error.passwordNeeded.headline": "The device wants its web-interface password",
  "settings.error.writeRefused.headline": "The device refused this write",
  "settings.error.badRequest.headline": "The device would not accept this",
  "settings.error.fault.headline": "The device reported a fault",
  "settings.error.browserGaveNoReason": "The browser gave no reason.",
  "settings.error.disconnectExpected.headline": "No answer — which is what success looks like here",
  "settings.error.disconnectExpected.detail":
    "The device replies before it retunes its radio, so a request that dies mid-flight "
    + "usually means it is already trying. ({browserSaid})",
  "settings.error.offline.headline": "No answer from the device",
  "settings.error.offline.detail": "Nothing reached the device, so it has said nothing about this. ({browserSaid})",
} satisfies Record<string, Message>;
