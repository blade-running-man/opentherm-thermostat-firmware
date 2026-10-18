// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What actually leaves this page, and what it makes of what comes back.
//
// Run: node src/pages/settings/tests/transport.test.ts
//
// It covers api/client.ts and api/config.ts and lives here anyway, because harness.ts is here and nothing in
// this project may add a dependency to web/package.json (harness.ts explains why). client.ts
// imports only TYPES from ./entities and ./ws, so node's type stripping erases both and the
// module loads with nothing but fetch under it -- which is exactly what this file replaces.
//
// Two things are pinned. The first is the request: POST, the right path, and a body with no
// sentinel in it, because /api/provision is the one route that writes the household Wi-Fi key
// and "__UNCHANGED__" is a storable 13-byte string. The second is the answer: a 200 the page
// cannot parse must never be reported as "nothing reached the device", because a save that
// succeeded and a device that is not there are not the same news.

import { ApiError, REQUEST_TIMEOUT_MS, provisionWifi, request } from "../../../api/client.ts";
import { getConfig, saveConfig, saveExecutor, type ConfigPatch } from "../../../api/config.ts";
import { CONFIG_UNCHANGED } from "../../../api/secrets.ts";
import { explainError } from "../errors.ts";
import { provisionRequest } from "../wifi.ts";
import { eq, ok, report } from "./harness.ts";

// --- a fetch that answers whatever the case under test needs --------------------------------

interface Sent {
  path: string;
  method: string;
  headers: Record<string, string>;
  body: string;
}

let sent: Sent | null = null;

/** Installs a fetch that records the request and answers `status` with `body` verbatim. */
function answerWith(status: number, body: string): void {
  sent = null;
  globalThis.fetch = ((input: RequestInfo | URL, init?: RequestInit) => {
    const headers = (init?.headers ?? {}) as Record<string, string>;
    sent = {
      path: String(input),
      method: init?.method ?? "GET",
      headers,
      body: typeof init?.body === "string" ? init.body : "",
    };
    // A Response and not a hand-rolled object: .json() on an empty body has to reject the way
    // the browser's does, which is the whole subject of the second half of this file.
    return Promise.resolve(new Response(body, { status }));
  }) as typeof fetch;
}

/** The recorded request, or a failure that names itself rather than a null dereference. */
function lastRequest(): Sent {
  if (sent === null) throw new Error("fetch was never called");
  return sent;
}

/** Runs `run` and returns what it threw, or null if it did not. */
async function threw(run: () => Promise<unknown>): Promise<unknown> {
  try {
    await run();
    return null;
  } catch (err) {
    return err;
  }
}

/**
 * The same, flattened to a string so that a failure prints the message instead of `{}`.
 * JSON.stringify of an Error is an empty object, which is the least useful thing a red test
 * can say about a call that was supposed to succeed.
 */
async function failureOf(run: () => Promise<unknown>): Promise<string | null> {
  const err = await threw(run);
  return err === null ? null : String(err);
}

const PATCH: ConfigPatch = {
  mqtt_host: "192.168.50.10",
  mqtt_port: 1883,
  mqtt_user: "opentherm",
  mqtt_password: CONFIG_UNCHANGED,
  topic_prefix: "opentherm/a4c1385f2b90",
  ha_discovery: true,
  device_name: "Термостат OpenTherm",
  tz: "MSK-3",
  ntp_server: "pool.ntp.org",
  ui_password: CONFIG_UNCHANGED,
};

// --- the one route that carries the household Wi-Fi key -------------------------------------

{
  answerWith(200, "{}");
  await provisionWifi(provisionRequest({ ssid: "Kitchen", manual: false, psk: CONFIG_UNCHANGED }));
  const req = lastRequest();

  eq(req.path, "/api/provision", "the network goes to its own route and only there");
  eq(req.method, "POST",
     "POST, never GET: a PSK in a query string is written to the log ring and /api/log hands "
     + "that ring to anybody who can reach it -- the minimum price of an open setup access "
     + "point");
  eq(req.headers["Content-Type"], "application/json", "and it is announced as JSON");

  eq(req.body, "{\"wifi_ssid\":\"Kitchen\"}",
     "re-provisioning the SAME network sends the SSID and no key at all: an absent wifi_psk is "
     + "already defined as \"leave the stored value alone\" (ot_config.h:365-368), and "
     + "absence is the one value no handler can accidentally store");
  ok(!req.body.includes(CONFIG_UNCHANGED),
     "the sentinel never crosses this route. It passes ot_config_check_psk -- 13 bytes, "
     + "no NUL, not length 64 -- so a handler that stores the field verbatim writes it as the "
     + "household key, credentials then exist so no access point comes up, and the device is "
     + "gone (ot_provision.c)");
}

{
  answerWith(200, "{}");
  await provisionWifi(provisionRequest({ ssid: "Guest", manual: false, psk: "" }));
  eq(lastRequest().body, "{\"wifi_ssid\":\"Guest\",\"wifi_psk\":\"\"}",
     "an OPEN network sends an empty key rather than omitting it -- omitting means keep, and "
     + "half the guest networks in the world have no key (ot_config.c:155-158)");
}

// --- what a successful answer this page cannot read is allowed to look like ---------------------

{
  // The failure: request() parsed every 2xx as JSON, so a 200 with an empty body threw a
  // SyntaxError -- not an ApiError -- and explainError painted the save that had just SUCCEEDED
  // as "Nothing reached the device, so it has said nothing about this". The page asserted
  // something it cannot know.
  answerWith(200, "");
  eq(await failureOf(() => saveConfig(PATCH)), null,
     "a write that answers 200 with an empty body has succeeded, and the page says nothing "
     + "about a device that has said nothing");

  answerWith(200, "   \n ");
  eq(await failureOf(() => provisionWifi({ wifi_ssid: "Kitchen" })), null,
     "and a write's body is not read at all: the status line is the whole answer, so nothing "
     + "in it can turn a success into a report of silence");
}

{
  // A GET is the other half: its body IS the answer, so an unreadable one is a fault. It must
  // still not be reported as silence, because the device demonstrably spoke.
  answerWith(200, "");
  const err = await threw(() => getConfig());
  ok(err instanceof ApiError,
     "a document route with no document is an ApiError, not a SyntaxError");
  if (err instanceof ApiError) {
    eq(err.status, 200, "carrying the status the device actually gave");
    ok(explainError(err).tone !== "offline",
       "so the owner is not told nothing reached the device when something did");
  }
}

{
  answerWith(200, "<!doctype html>");
  const err = await threw(() => getConfig());
  ok(err instanceof ApiError,
     "the same for HTML, which is what a proxy or a captive portal answers with when it has "
     + "swallowed the request");
}

// --- and the contract that was already right, so that the above cannot quietly break it --------

{
  answerWith(400, "{\"error\":\"mqtt port out of range\"}");
  const err = await threw(() => saveConfig(PATCH));
  ok(err instanceof ApiError && err.status === 400 && err.message === "mqtt port out of range",
     "a refusal still arrives with the DEVICE'S sentence: ot_config_strerror() is the "
     + "only thing that knows which field was refused");
}

{
  answerWith(500, "not json at all");
  const err = await threw(() => saveConfig(PATCH));
  ok(err instanceof ApiError && err.status === 500,
     "and an error body that is not JSON is still an ApiError, so the status survives");
}

{
  answerWith(200, "{\"wifi_ssid\":\"Kitchen\",\"mqtt_port\":1883}");
  const cfg = await getConfig();
  eq(cfg.wifi_ssid, "Kitchen", "a document that IS readable is still parsed and returned");
  eq(cfg.mqtt_port, 1883, "numbers included");
}

// --- the controller's settings, and the field a refusal names ---------------------------------

{
  answerWith(200, "{\"saved\":true}");
  await saveExecutor({ watchdog_s: 600 });
  const req = lastRequest();
  eq([req.path, req.method], ["/api/config", "POST"],
     "the controller's settings go to /api/config, like the rest of the configuration");
  eq(req.body, "{\"watchdog_s\":600}",
     "carrying ONLY the changed key: an absent key leaves the stored value alone, and absence is "
     + "the one value that cannot be stale");
}

{
  answerWith(422, JSON.stringify({
    error: "Home Assistant mode (control_mode 1) needs a broker: set mqtt_host first, and "
      + "return to local mode (control_mode 0) before clearing it",
    field: "mode-needs-broker",
  }));
  const err = await threw(() => saveExecutor({ control_mode: 1 }));
  ok(err instanceof ApiError && err.status === 422 && err.field === "mode-needs-broker",
     "a refusal keeps the field the device named (send_wire_error(), ot_http_config.c)");
  ok(err instanceof ApiError && err.message.startsWith("Home Assistant mode (control_mode 1)"),
     "and the sentence shown is still the device's own");
}

{
  answerWith(409, "{\"error\":\"these settings were written by a newer firmware\"}");
  const err = await threw(() => saveConfig(PATCH));
  ok(err instanceof ApiError && err.field === null,
     "a body that names no field gives null -- not undefined, and not the sentence");
}

// --- a request the device never answers -------------------------------------------------------

{
  // A fetch that answers nothing until its signal gives up: a device gone mid-request. The ref'd
  // timer only keeps node alive -- node's AbortSignal.timeout() timer is unref'd, and a suite
  // waiting on nothing else would exit before it fires. A browser needs no such thing.
  globalThis.fetch = ((_input: RequestInfo | URL, init?: RequestInit) =>
    new Promise<Response>((_resolve, reject) => {
      init?.signal?.addEventListener("abort", () => reject(init?.signal?.reason));
    })) as typeof fetch;
  const keepAlive = setTimeout(() => {}, 5000);
  const started = Date.now();
  const err = await threw(() => request("/api/control", undefined, 50));
  clearTimeout(keepAlive);
  ok(err instanceof Error && err.name === "TimeoutError",
     "a request nobody answers is given up after its timeout");
  ok(!(err instanceof ApiError),
     "as silence, not as an answer: the pages then say there was no answer from the device");
  ok(Date.now() - started < 2000,
     "and promptly, so one hung request cannot hold back every refresh of the control card");
}

{
  const seen: { signal?: AbortSignal | null } = {};
  globalThis.fetch = ((_input: RequestInfo | URL, init?: RequestInit) => {
    seen.signal = init?.signal;
    return Promise.resolve(new Response("{}", { status: 200 }));
  }) as typeof fetch;
  await getConfig();
  ok(seen.signal instanceof AbortSignal,
     "every request carries a timeout by default, not only the ones that ask for one");
  eq(REQUEST_TIMEOUT_MS, 10000, "ten seconds: api/client.ts says why");
}

report("transport");
