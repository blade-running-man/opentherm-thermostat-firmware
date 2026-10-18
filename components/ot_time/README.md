# ot_time — the wall clock: SNTP and the time zone

A thin wrapper over ESP-IDF's SNTP service and the POSIX `TZ` mechanism. It starts
time-of-day synchronisation, applies the configured time zone, and answers "what time is
it, in local time?" — but only once a sync has actually happened.

## Purpose

Provide the firmware with a trustworthy local wall clock. The component turns a configured
NTP host name and a POSIX time-zone string into a synchronised, zoned clock, and reports the
current time as `{weekday, minute_of_day, epoch}` together with a flag that says whether that
answer can be believed.

## Responsibility

Owns:
- Starting the SNTP service against a configured NTP host name (`ot_time_start`).
- Applying a POSIX `TZ` string to the process (`ot_time_set_zone`).
- Reporting the current wall clock as `{weekday, minute_of_day, epoch}`, and reporting
  whether that answer is trustworthy (`ot_time_now`).
- The 0 = Monday weekday convention and the single conversion from `struct tm`'s
  0 = Sunday.

Does NOT do:
- Store or load the server / zone. It takes both as arguments and knows nothing about where
  they came from, so it does not depend on the configuration component. This lets it be
  driven from a test harness or a provisioning path with no configuration document present.
- Block or wait. Nothing here waits for a sync.
- Reboot or stop anything when SNTP is unreachable — an unreachable server just leaves the
  clock unsynchronised for ever.

Validity is **"a sync happened"**, never **"`time()` returned something"**. A device that
never reached an NTP server returns 1970 with complete confidence; a threshold on the epoch
would call that invalid by luck and could call a genuinely wrong clock valid. `ot_time_now`
answers `false` until the SNTP callback has fired at least once.

## Public API

`include/ot_time.h`:

| Symbol | Contract |
| --- | --- |
| `#define OT_TIME_SERVER_MAX 253` | Max NTP host-name length. Shares the same cap the configuration layer uses for a host name rather than inventing a second one. |
| `ot_wallclock_t` | `uint8_t weekday` (0 = Monday), `uint16_t minute_of_day` (0..1439, local), `int64_t epoch` (seconds since 1970-01-01 UTC). |
| `void ot_time_set_zone(const char *tz)` | Applies a POSIX `TZ` string via `setenv`+`tzset` (verbatim; `NULL`/`""` → UTC). **Every** call applies it — no "already started" guard. Any task; returns at once. |
| `void ot_time_start(const char *ntp_server)` | Starts SNTP against `ntp_server` (host name, copied). Idempotent — second and later calls do nothing. Returns immediately. A changed server therefore takes effect only at the next boot. |
| `bool ot_time_now(ot_wallclock_t *out)` | Fills `out` and returns `true` only if SNTP has synchronised at least once; otherwise `false`. Converts `struct tm`'s 0 = Sunday weekday to 0 = Monday here, once. |

`ot_time.h` must NOT be included from a pure (host-tested) component: including it drags
`esp_netif_sntp.h` into the compile. PlatformIO's dependency finder links a library because
one of its headers was included, and then compiles all of that library's sources — so a
single `#include` here would pull `esp_netif_sntp.h` into a host suite and stop it building.

## Implementation

Single source file `ot_time.c`.

Module state:
- `s_server[OT_TIME_SERVER_MAX + 1]` — **static**, not a copy taken by
  `esp_netif_sntp_init()`. `ESP_NETIF_SNTP_DEFAULT_CONFIG()` stores the *pointer* it is given
  in `servers[0]` and the SNTP service dereferences it long after `ot_time_start` returns, so
  a caller's buffer (for example a projection that is rewritten on every settings save) would
  leave the service reading freed or changed bytes.
- `volatile bool s_synced` — written from the SNTP callback, read from any task. A plain
  `volatile bool` is the whole synchronisation needed: both values are self-consistent and
  the transition happens once.
- `bool s_started` — guards the idempotent init.

SNTP handling (`ot_time_start`): guarded by `s_started`. An empty/`NULL` server is logged as
a warning (not an error — the configuration sanitiser normally restores the default) and the
clock is left unsynchronised. The server is copied into `s_server`, then
`ESP_NETIF_SNTP_DEFAULT_CONFIG(s_server)` is built with `start = true` and `sync_cb = on_sync`,
and `esp_netif_sntp_init()` is called. The station is already up when this runs, so there is
nothing to wait for. On init failure it logs an error and returns — nothing reboots, nothing
stops; no decision in the firmware depends on the wall clock. The `on_sync` callback sets
`s_synced = true` and logs once at INFO (it fires on every resync, but the first is the
interesting one).

Time-zone handling (`ot_time_set_zone`): always calls `setenv("TZ", …, 1)` then `tzset()`,
with `"UTC0"` for an empty/`NULL` zone (an explicit UTC string can't be mistaken for "never
applied"). The thermostat task calls this from its once-a-second config poll whenever the
stored zone differs from the one it last applied.

Validity logic (`ot_time_now`): returns `false` if `out` is `NULL` or `!s_synced`; otherwise
reads `time(NULL)`, converts with `localtime_r`, and fills `epoch`, `weekday` and
`minute_of_day`.

Invariants / DO NOTs:
- **DO NOT** add a "started" guard above `ot_time_set_zone`'s `setenv`. Applying the zone only
  on the first `ot_time_start` (behind the SNTP guard) makes a saved zone take effect only
  after a power cycle.
- **DO NOT** add `esp_netif_sntp_sync_wait()` to `ot_time_start` or to anything the OpenTherm
  bus waits behind. Silence from the master longer than 5 s makes the boiler treat the
  thermostat as short-circuited and demand heat — falling silent is the hottest state, not a
  safe one.
- **DO NOT** "simplify" the `(tm_wday + 6) % 7` weekday conversion to `-1`, which sends Sunday
  to 255.
- A changed server takes effect only at the next boot, deliberately: re-initialising SNTP from
  the thermostat poll would race the network reconnect callback and would need a lock for a
  value nothing reads yet.

Build (`CMakeLists.txt`): `REQUIRES esp_netif lwip` — `esp_netif` carries `esp_netif_sntp.h`,
`lwip` carries the SNTP implementation. Both are named explicitly so the component does not
break on a release that stops pulling them in for free. The configuration component is
deliberately not a dependency.

## Tests

No dedicated host suite. The component is not host-built — its sources pull in
`esp_netif_sntp.h`, which is why the header must not be included from a pure component.

## Notes

- `weekday` is 0 = Monday because that is how a weekly schedule is written down and read; the
  conversion from `struct tm`'s 0 = Sunday is done here, once, since an off-by-one weekday is
  a schedule that fires on the wrong day.
- The SNTP callback fires on every reconnection; init is therefore made idempotent so
  `esp_netif_sntp_init()` runs exactly once.
- Nothing reboots because SNTP is unreachable: an unreachable server simply leaves
  `ot_time_now` answering `false` for ever, and every other clock the firmware keeps is
  monotonic milliseconds by design.
