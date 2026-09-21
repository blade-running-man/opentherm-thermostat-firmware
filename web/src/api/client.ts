// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Everything this UI asks the device for.
//
// The web UI has no privileged endpoint. Every call below is one any other
// client can make on the same terms -- there is no header, cookie or route here that curl
// cannot use. If a screen ever needs something this file cannot express, the API is wrong,
// not this file.

// Four groups of routes live outside this file: GET /api/ot/raw in api/otRaw.ts, the registry
// -- /api/state and /api/entities -- in api/registry.ts, GET /api/control in api/control.ts,
// and the configuration document -- /api/config -- in api/config.ts. None is a second API
// client: each imports request()/requestVoid() from here, so ApiError and the "an unreadable
// body is a fault" rule below are the same ones. They are separate only because this file is at
// the project's 350-line ceiling (CLAUDE.md), and each is a seam that already existed; what is
// left here is the device itself: its log, its operations and its network.

export class ApiError extends Error {
  // Written out rather than declared as a constructor parameter property: the project
  // compiles with erasableSyntaxOnly, which rules out the shorthand.
  readonly status: number;
  /**
   * The `field` of the device's error body, or null when it named none. POST /api/config names
   * what it refused (send_wire_error(), components/ot_http/ot_http_config.c): a JSON key when a
   * value was not a number at all, and otherwise the refusal's name -- "flow",
   * "mode-needs-broker" (ot_config_err_name()). A page uses it to point at a box; the sentence it
   * shows is still `message`, the device's own.
   */
  readonly field: string | null;

  constructor(status: number, message: string, field: string | null = null) {
    super(message);
    this.status = status;
    this.field = field;
  }
}

/**
 * How long a request may go unanswered before it is given up as silence.
 *
 * Without a limit, a request to a device that went away mid-request waits for as long as the
 * browser cares to -- minutes, on a dropped connection -- and the control card, which asks one
 * request at a time, would ask nothing more for that long. The device's slowest honest answer is
 * a few seconds: its one server task answers every route from RAM, and holds the next request up
 * only for a blocking Wi-Fi scan (esp_wifi_scan_start(), about two seconds over every channel) or
 * a wrong password's 1 s delay (OT_AUTH_FAIL_DELAY_MS). Ten seconds clears both with room, and
 * stays well inside the 35 s after which /ws calls the device gone (ws.ts, SILENCE_TIMEOUT_MS).
 *
 * The firmware upload does not come through here (pages/update calls fetch() itself), and must
 * not: it is one long POST.
 */
export const REQUEST_TIMEOUT_MS = 10_000;

async function send(path: string, init: RequestInit | undefined, timeoutMs: number): Promise<Response> {
  // A rejection here is a DOMException named TimeoutError -- not an ApiError, so every page's
  // explainer reads it as what it is: nothing came back.
  const response = await fetch(path, { ...init, signal: init?.signal ?? AbortSignal.timeout(timeoutMs) });
  if (response.ok) return response;

  // The device answers errors as JSON with an `error` field. It may also answer 501 for
  // a route the access policy allows but that is not implemented yet, which is a normal
  // state of this project rather than a fault.
  let detail = response.statusText;
  let field: string | null = null;
  try {
    const body = await response.json();
    if (typeof body?.error === "string") detail = body.error;
    if (typeof body?.field === "string") field = body.field;
  } catch {
    // Not JSON. Keep the status text; a parse failure here is not the interesting error.
  }
  throw new ApiError(response.status, detail, field);
}

/**
 * A call whose answer IS the result: the body carries the document the caller asked for.
 *
 * An unreadable body is therefore a fault, and it is reported as ONE -- as an ApiError with the
 * status the device actually gave. It used to reach the caller as the SyntaxError from
 * response.json(), which is not an ApiError, so pages/settings/errors.ts fell through to its
 * last case and told the owner "Nothing reached the device, so it has said nothing about
 * this". Something did reach the device: it answered. A page that cannot tell those two apart
 * sends the owner looking for a network fault that is not there.
 */
export async function request<T>(path: string, init?: RequestInit,
                                 timeoutMs = REQUEST_TIMEOUT_MS): Promise<T> {
  const response = await send(path, init, timeoutMs);
  const text = await response.text();
  try {
    return JSON.parse(text) as T;
  } catch {
    const what = text === "" ? "an empty body" : "a body this page could not read";
    throw new ApiError(response.status, `The device answered ${response.status} with ${what}.`);
  }
}

/**
 * A call whose answer is only "yes". The body is not read at all.
 *
 * A write that answered 200 and nothing else used to be reported as a device that had gone
 * missing, for no better reason than that request() ran every successful body through
 * JSON.parse. The status line is the whole answer here; there is nothing in the body this
 * caller would look at even if it arrived.
 */
export async function requestVoid(path: string, init?: RequestInit,
                                  timeoutMs = REQUEST_TIMEOUT_MS): Promise<void> {
  await send(path, init, timeoutMs);
}

/** The device's own log, oldest line first. The only way to read it without a USB cable. */
export function getLog(): Promise<string[]> {
  return request("/api/log");
}

/**
 * Runs one operation: the name is in the path, its parameters are the FLAT object in the body.
 *
 * The device answers 202 -- the operation was queued, not performed. A scan takes minutes and a
 * line test twenty seconds, so there is nothing to return but "accepted"; progress is read back
 * from GET /api/ot/raw. That is why this is requestVoid: the status line is the whole answer.
 *
 * DO NOT split an operation's parameters across two calls. They arrive together or the device
 * runs something nobody asked for -- a scan of a range the caller never named.
 */
export function runOperation(
  name: string,
  params: Record<string, unknown> = {},
): Promise<void> {
  return requestVoid(`/api/ops/${encodeURIComponent(name)}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(params),
  });
}

// --- the network ---------------------------------------------------------------------------
//
// /api/provision and /api/wifi/scan. The configuration document that shared this section -- GET
// and POST /api/config, DeviceConfig and ConfigPatch -- lives in api/config.ts.
//
// THE SCAN MUST ANSWER WITH A JSON BODY: its body is the answer, and a 200 carrying nothing is a
// fault this page can only report as one. The provisioning write may answer with an empty body;
// what it must NOT do is answer a status this file cannot tell from silence -- see request() and
// requestVoid() above for why that distinction is worth two functions.

/**
 * The body of POST /api/provision, and the three states `wifi_psk` has.
 *
 * SHAPE THE FIRMWARE HAS TO MATCH:
 *
 *   {"wifi_ssid": "Kitchen", "wifi_psk": "hunter2"}   store this key
 *   {"wifi_ssid": "Guest",   "wifi_psk": ""}          an OPEN network: no key at all
 *   {"wifi_ssid": "Kitchen"}                          keep the key already stored
 *
 * The third line is the one to get right, and it is the same rule ot_config_patch_t
 * already states for every other string: "A NULL string means the key was ABSENT... absent
 * leaves the stored value alone, empty clears it" (ot_config.h, above the struct). Run the field
 * through ot_secret_decide() -- absent is KEEP, "" is CLEAR, anything else is STORE --
 * and this route needs no vocabulary of its own.
 *
 * DO NOT accept OT_SECRET_SENTINEL as a key here, and DO NOT store this field without
 * deciding first. "__UNCHANGED__" is 13 printable bytes and passes ot_config_check_psk
 * whole (>= PSK_MIN 8, no NUL, not length 64 so no hex check), so a handler that writes it
 * through leaves the device holding credentials that cannot associate -- and credentials
 * existing is what stops the access point from coming back up. That is the disappearance
 * that is the likeliest catastrophe in this project, reached with
 * nobody having made a typo. provisionRequest() in pages/settings/wifi.ts keeps the sentinel
 * off the wire from this side; this note is the other half, because a handler is not obliged
 * to trust its clients.
 *
 * An absent key with nothing stored is KEEP of nothing, which is an open-network attempt. That
 * is the correct reading and it needs no special case.
 */
export interface ProvisionRequest {
  wifi_ssid: string;
  /** Absent means "keep the stored key". "" means the network has none. */
  wifi_psk?: string;
}

/**
 * Wi-Fi credentials, and the only route that carries them.
 *
 * POST, never GET. A PSK in a query string is written to the log ring by the server, and
 * /api/log hands that ring to anybody who can reach it -- "provisioning by POST only" is
 * the minimum price of leaving the setup access point open.
 *
 * It is also the ONLY write an unprovisioned device accepts (ot_http_policy.c:31-46), so
 * it is the route that has to work in the one situation that matters: a fresh device on its
 * own open access point. Routing the network through POST /api/config instead would give a
 * form that works on the bench and is refused in the kitchen.
 *
 * The keys are the config document's, deliberately: one vocabulary for the network on both
 * routes means the firmware decodes it in one place instead of two that drift.
 *
 * The device answers BEFORE it starts connecting, and the access point stays up for the
 * length of the setup window so the answer can arrive. A 200 here means ACCEPTED, never
 * CONNECTED, and the caller
 * has to say so; the caller must also expect the request to fail with no answer at all, which
 * after this particular call is what success looks like from the browser's side.
 */
export function provisionWifi(creds: ProvisionRequest): Promise<void> {
  return requestVoid("/api/provision", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(creds),
  });
}

/** One network the device's radio heard. */
export interface ScanResult {
  /** The SSID as sent in the beacon. "" for a hidden network -- see scanNetworks(). */
  ssid: string;
  /** dBm, so negative and larger is better: -40 is the same room, -90 is barely there. */
  rssi: number;
  /** False ONLY for a genuinely open network (authmode WIFI_AUTH_OPEN). */
  secure: boolean;
}

/**
 * The networks the DEVICE can see, which is not the list the phone can see.
 *
 * That difference is the most common support request for any 2.4 GHz device and the reason
 * this list is an invariant rather than a feature: a router
 * publishes one name on both bands, the C6 has only 2.4 GHz radio, and the owner reasonably
 * concludes the device is broken.
 *
 * SHAPE THE FIRMWARE HAS TO MATCH -- written here first so the handler is built to it:
 *
 *   200 -> [{"ssid": "Kitchen", "rssi": -52, "secure": true}, ...]
 *
 *   * A JSON array at the top level, possibly empty. Not an object with a `networks` key: the
 *     handler renders one thing and this is it.
 *   * DUPLICATE SSIDs ARE EXPECTED AND MUST NOT BE FILTERED by the device. One scan sweep
 *     returns one record per BSSID, so a mesh answers several times under one name; which of
 *     them to show is a presentation question and mergeScan() in pages/settings/wifi.ts owns
 *     it. A device that deduplicates has to choose, and it has less information than the page.
 *   * A hidden network answers with an empty `ssid`. Send it as "" rather than dropping the
 *     record -- the page discards it, but "the scan saw seven things and can name five" is a
 *     true statement worth being able to make.
 *   * The call can take SECONDS. A full 2.4 GHz sweep is thirteen channels, and on a device
 *     serving this page from its own access point the radio leaves that channel while it
 *     sweeps -- the phone may lose the page mid-request. The page therefore treats a failed
 *     scan as "no list this time" and keeps manual entry available; the firmware must not let
 *     a failed or slow scan restart, reboot or drop anything (CLAUDE.md: nothing reboots
 *     because a peer is absent, and here the absent peer is the browser).
 */
export function scanNetworks(): Promise<ScanResult[]> {
  return request("/api/wifi/scan");
}
