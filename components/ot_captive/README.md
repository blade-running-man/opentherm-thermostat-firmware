# ot_captive — the captive-portal layer of the setup access point

Two halves of one decision: an HTTP probe table that tells every phone-OS connectivity check
that the network is fine, and a DNS responder that resolves every name to the access point.
Together they keep the OS from opening its own sign-in window while letting a real browser
reach the setup page. Read `include/ot_captive.h` first — the two halves are one design.

## Responsibility

What it owns:

- **The probe table** (`ot_captive_probe.c`): the byte-exact answers Apple, Windows, Firefox
  and Android connectivity probes are compared against, so each OS concludes the network works.
- **The DNS reply builder** (`ot_captive_dns.c`): a pure function that turns one DNS datagram
  into a reply pointing every A-record lookup at the access point's own address.
- **The DNS socket and task** (`ot_captive_dns_server.c`): the UDP:53 listener that carries
  those replies, bound to the AP address only.

What it does NOT do:

- **It does not register HTTP handlers.** It exports a table and a lookup; the caller wires them
  into the HTTP server. If it could see `esp_http_server` it could add a handler, and the access
  policy would have a second door (`CMakeLists.txt`: `ot_http` is deliberately not a REQUIRES).
- **It does not redirect probes.** Every probe is answered with 2xx success, never a 3xx to the
  setup page — a redirect brings back all three captive-portal failures the header enumerates.
- **It holds no `esp_netif` and no hard-coded `192.168.4.1`.** The caller passes the AP address.
- **It never reboots or aborts** on any failure: a portal that cannot start is a
  device the owner still reaches by typing an address.
- **It does not fix the third failure** — Android's HTTPS half of the probe (see Notes).

## Public API

### `ot_captive.h` — the HTTP probe table

| Symbol | Contract |
| --- | --- |
| `ot_captive_answer_t` | One table row: `path`, `status`, `content_type` (NULL if none), `body` (NULL for 204s), `body_len` (byte-exact, asserted against `strlen` in tests), `cache_control` (always `"no-store"`), `asked_by` (which OS, for logging). All fields are constants. |
| `const ot_captive_answer_t *ot_captive_answer(const char *path)` | Returns the row for a probe path, or `NULL` when the path belongs to the SPA/API. Path is matched **whole** (never prefix, never query string) and arrives already stripped by the HTTP layer. Safe to serve ahead of the access policy because answers are constant — no name/address/reading can leak. `NULL` path returns `NULL`. |
| `const ot_captive_answer_t *ot_captive_table(size_t *count)` | Returns the whole table; writes the row count to `count`. `count` may be `NULL` (nothing written) — the code tolerates it, pinned by test. |

### `ot_captive_dns.h` — the pure DNS reply builder

| Symbol | Contract |
| --- | --- |
| `OT_CAPTIVE_DNS_PORT` (53) | The UDP port the responder binds. |
| `OT_CAPTIVE_DNS_MAX_MESSAGE` (512) | The size of the socket loop's receive buffer (classic DNS over UDP, RFC 1035 4.2.1). **Not** a check on `query_len` — the builder never compares against it. |
| `size_t ot_captive_dns_reply(const uint8_t *query, size_t query_len, const uint8_t address[4], uint8_t *out, size_t out_cap)` | Builds the reply to one datagram; returns the byte count to send, or `0` for "say nothing" (any packet not a readable question). `address` is the four octets of the AP address in wire order, caller-owned. `out` may **be** the query's own buffer (question moved with `memmove`); `out_cap` must be the buffer size, never `query_len`. Reply is at most `query_len + 16` and never exceeds `OT_CAPTIVE_DNS_MAX_MESSAGE`. Pure over a buffer and a length. |

### `ot_captive_dns_server.h` — the socket and task (device-only)

| Symbol | Contract |
| --- | --- |
| `esp_err_t ot_captive_dns_start(const uint8_t address[4])` | Starts the DNS task answering lookups on port 53, bound to `address` (not INADDR_ANY). Returns `ESP_ERR_INVALID_STATE` if already running, `ESP_ERR_INVALID_ARG` on NULL address, `ESP_FAIL` if the socket cannot be opened/bound, `ESP_ERR_NO_MEM` if the task cannot be created. Never aborts, never reboots. |
| `void ot_captive_dns_stop(void)` | Asks the task to finish and waits briefly. Safe to call when nothing is running. Call it when the AP goes off the air. |

## Implementation

### `ot_captive_probe.c` — the table

- Seven rows of byte-exact bodies, **fetched from the live originals**, each with
  its recorded `Content-Length` in the comment. Lengths use `sizeof - 1` so the literal and its
  length cannot drift.
- Rows: `/generate_204` and `/gen_204` (Android, 204 no body); `/hotspot-detect.html` (Apple,
  69-byte body ending in a newline); `/ncsi.txt` (Windows 8-, 14 bytes, no newline);
  `/connecttest.txt` (Windows 10+, 22 bytes, no newline); `/success.txt` (Firefox, 8 bytes,
  **with** newline); `/canonical.html` (Firefox, 90-byte meta-refresh).
- The newline asymmetry between the Windows strings and Firefox's `success\n` is real, not a
  typo — which is why the tests pin all three lengths rather than one newline rule.
- `ot_captive_answer()` is a **linear, exact** `strcmp` match — no prefix matching, for the same
  leak class `ot_http_policy.c` avoids (`/generate_204/x` must not inherit `/generate_204`).
- A block comment records paths **deliberately absent** (`/redirect`, `/library/test/...`,
  Kindle/vendor mirrors, and the NCSI DNS half) so they read as considered, not forgotten.

### `ot_captive_dns.c` — the reply builder (pure)

- Constants: `HEADER_LEN` 12, `MAX_NAME` 255, `ANSWER_LEN` 16, `TYPE_A` 1, `CLASS_IN` 1. The
  255-byte name bound makes the largest possible reply `12 + 259 + 16 = 287`, so truncation and
  the 512 limit never need to be reasoned about.
- `question_len()` walks the name label-by-label carrying the remaining length. Refuses any
  non-label byte (top two bits set) — **compression pointers included**: following one is the
  decompression loop, and `0xC00C` on a single-core device stops it answering its owner.
- Refusal guards in `ot_captive_dns_reply()`: NULL args → 0; `query_len < 12` → 0; QR set (already
  an answer, avoids two portals trading packets) → 0; opcode not QUERY → 0; question count != 1 → 0;
  unreadable question → 0; `reply_len > out_cap` → 0.
- A-question in class IN is answered with one A record (the AP address); everything else (notably
  AAAA) gets a well-formed NOERROR with zero records, so the client does not burn a resolver
  timeout. EDNS OPT is not echoed; the client falls back to 512-byte UDP.
- The answer record is a compression pointer (`0xC0 0x0C`) back to the question at offset 12,
  **TTL 0** (RFC 2181 §8 — use once, do not cache), rdata = the four AP octets.
- The question is copied back **byte for byte including case** — resolvers randomise name case
  and drop mismatched replies (0x20 anti-spoofing). DO NOT normalise it.
- Uses `memmove` (not `memcpy`) so `out` may alias the query buffer.

### `ot_captive_dns_server.c` — the socket and task (device-only)

- Entirely wrapped in `#ifdef ESP_PLATFORM`: the host test build compiles every `.c` in the
  directory, and this one needs lwip. What can be tested on a laptop lives in the pure files.
- Two static 512-byte buffers `s_rx`/`s_tx` (one task owns both — a line in the map file that
  cannot fail to exist when a packet arrives). State: `s_address`, `s_sock`, `s_task`, volatile
  `s_running`/`s_alive`, `s_recv_errors`.
- `dns_task()`: `recvfrom` with a **1 s receive timeout** (`RECV_TIMEOUT_MS`) so the loop can poll
  `s_running` — this, not a `close()` from another task, is how it stops (closing a socket a task
  is blocked inside is a config-dependent race). Errors back off 1 s (`ERROR_BACKOFF_MS`) so a
  wedged socket costs one log line per outage, not one per second. Send is best-effort/unchecked.
- `ot_captive_dns_start()`: opens the UDP socket, sets `SO_RCVTIMEO` (fatal if it fails — the stop
  path depends on it), **binds to the AP address** (security decision, not plumbing), creates the
  task at **priority 3** (below CAN receive at 10 and the push channel at 4, so a query flood from
  the pavement cannot delay a PDO off the bus), 3 KB stack (both buffers are static).
- `ot_captive_dns_stop()`: sets `s_running` false, waits up to `RECV_TIMEOUT_MS + 500` for
  `s_alive`. If the task did not stop, **leaves `s_task` set** so a later `start()` refuses rather
  than putting a second task on the same buffers.

Key "DO NOT" constraints:

- DO NOT log the name asked for, at any level — `/api/log` is reachable by anyone in radio range.
- DO NOT follow compression pointers in a question; DO NOT relax the non-label refusal.
- DO NOT raise the TTL above 0, or normalise the echoed question case.
- DO NOT replace the receive-timeout stop with a cross-task `close()`.
- DO NOT redirect a probe or add a 3xx row.
- DO NOT let a second caller reach the static buffers.

## Tests

`test/test_captive` (host suite) covers the two pure files — `ot_captive_probe.c` and
`ot_captive_dns.c` — including every malformed-packet shape, which is the only way those cases
can be written without a radio. Named pins referenced in the sources:
`test_the_table_survives_a_caller_that_does_not_want_the_count`,
`test_the_512_is_the_socket_loops_buffer_and_not_a_check_in_here`,
`test_the_reply_may_be_built_on_top_of_the_query_it_answers`,
`test_the_table_is_seven_paths_and_the_lookup_finds_every_one`. The suite places datagrams against
an unmapped guard page so a read past `query_len` faults. `ot_captive_dns_server.c` has no host
suite (it compiles to nothing off the device via `#ifdef ESP_PLATFORM`).

## Notes

- **Two of three failures fixed, honestly.** The probe table stops the OS opening its captive
  sign-in window and stops that window closing mid-password. It does **not** fix Android's HTTPS
  probe (`https://www.google.com/generate_204`): DNS resolves the name to the AP where nothing
  listens on 443, so HTTP-success + HTTPS-fail yields PARTIAL_CONNECTIVITY ("limited
  connectivity"). No fix exists from inside a DNS responder and an HTTP table — it would need a
  TLS listener on 443 with a `www.google.com` certificate. The honest statement is a README
  sentence: if the phone says the Wi-Fi has no internet, tap through; the device is telling the
  truth about itself.
- **Discovery is entirely via DNS.** Because the OS window is suppressed, the only way to the
  setup page is a real browser plus a resolver that answers every name — the DNS half is the
  entire discovery mechanism.
- **The DNS offer is by accident, not configuration.** For anything to be asked, the AP's DHCP
  server must hand out this address as the resolver. It does because `CONFIG_LWIP_DHCPS_ADD_DNS`
  is on; turning it off kills the portal silently. The header records the four IDF calls
  (`dhcps_stop` / `dhcps_option` / `esp_netif_set_dns_info` / `dhcps_start`) to set it deliberately
  if needed.
- **The AP address is bound, never INADDR_ANY** — in AP_STA mode a `0.0.0.0` socket would also
  hijack the household's DNS through the STA side. The lie belongs on the access point only.
- **`REQUIRES lwip` in CMakeLists is load-bearing** — lwip is not one of the IDF's common
  requirements, so without it `#include "lwip/sockets.h"` does not resolve.
- **Editor hazard:** the probe bodies are byte-compared on the client end (Windows to a string,
  Apple to a document, Firefox to both), so an editor that normalises a trailing newline breaks
  provisioning for exactly one vendor.
