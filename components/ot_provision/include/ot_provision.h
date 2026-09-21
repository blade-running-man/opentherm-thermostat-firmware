// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What the device should be doing, given what has happened to it: which network mode, whether
// the setup window is open, and whether the credentials it was just handed have to be undone.
//
// Pure logic, no ESP-IDF, and TIME COMES IN AS A PARAMETER on every call. That is not a
// testing convenience, it is the only way the cases this exists for can be written down at
// all: "the window closed while the owner was in the next room reading the password off the
// router" takes fifteen minutes to reproduce against a real clock and one line against this
// one. DO NOT reach for a clock in here -- the moment one call reads esp_timer_get_time(),
// half of test/test_provision stops being writable.
//
// The states come from the retired firmware's ap_guard (components/ap_guard/ap_guard.h in the
// ESPHome repository) and from what went wrong the first time this was built. Two of those
// mistakes are structural and are answered by the shape of this file rather than by any one
// function:
//
//  * "Credentials are stored" and "the device is on the owner's network" were the same thing.
//    They are not: save_wifi_sta() stored the pair and returned, has_sta() went true, and the
//    device carried on sitting on an open access point with a mistyped password.
//    OT_PROV_TRIAL is that gap given a name.
//  * The setup window was anchored on boot rather than on the access point appearing, so on
//    the path back from `Forget Wi-Fi` it really lasted setup_window minus the ~90 s the
//    access point took to come up (ap_guard.h, window_started_). Nothing here is anchored on
//    init -- every clock starts on the event it is about.
//
// The decisions implemented here: an open access point means unclaimed; the setup window (15
// minutes, extended by activity, ceiling 60, anchored on the access point); the access point
// returns by itself after 30 minutes of failure, and only for the right reasons; a window that
// never closes is a supported configuration.
//
// And one invariant that outranks all four: there is never a state with no network, no access
// point and no way back. Every state here has one -- a closed window has the next boot, a typo
// has the rollback, an unlisted disconnect reason has the by-itself return, and a station that is
// never given an address has ot_prov_address_never_arrived(). The last of those is the one that
// reads as a gap in the by-itself return and is not: the by-itself return governs the access
// point coming back ON ITS OWN, the setup window governs it coming back at a REBOOT, and the way
// back for a no-address failure is the second one.
#pragma once

#include <assert.h>   // only for static_assert on ot_prov_t; see the struct
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returned by the duration queries for "this does not have an end". Zero already means
// "nothing is running", and the two answers are not the same: a window with 0 ms left is
// closed, a window that never closes is the sealed-device configuration.
#define OT_PROV_NEVER UINT32_MAX

typedef enum {
    // First boot ever, or a factory reset: nothing to connect to, so the access point is the
    // whole of the device's presence. Because an open access point means unclaimed, this is also
    // what the device counts as for as long as that access point is on the air, whatever lands in
    // NVS meanwhile.
    OT_PROV_UNCONFIGURED,
    // Credentials exist and an attempt is in flight.
    OT_PROV_CONNECTING,
    // An address on the owner's network. The only state that proves a pair of credentials.
    OT_PROV_CONNECTED,
    // Credentials were saved and the connection is NOT yet confirmed. A state in its own right
    // because the retired firmware shipped a bug by conflating it with CONNECTED, and because
    // it is the only state from which a rollback can happen.
    OT_PROV_TRIAL,
    // An attempt failed; the next one is waiting out its backoff.
    OT_PROV_RETRYING,
    // The device's own access point is back while the station keeps trying underneath it. Two
    // things reach here: the by-itself return, after sustained failure of the right kind, and init() on a device
    // whose previous run was never given an address (ot_prov_address_never_arrived). The
    // state is the same because the situation is -- an open access point on a unit that already
    // holds the owner's data, bounded by a window -- only the clock that reached it differs.
    OT_PROV_FALLBACK_AP,
    // The window ran out on a device that has nothing to connect to. Terminal until the
    // next boot, which is what makes reopen_window_ms the way back rather than a nicety.
    // A device that HAS credentials never lands here -- see ot_prov_state().
    OT_PROV_WINDOW_CLOSED,
} ot_prov_state_t;

// What the radio should be doing. The caller acts on this and then reports what actually
// happened, because "asked for an access point" and "an access point is on the air" are
// different facts and the setup window anchors its clock on the second one.
typedef enum {
    OT_PROV_MODE_OFF,
    OT_PROV_MODE_ACCESS_POINT,
    OT_PROV_MODE_STATION,
    // Both at once (WIFI_MODE_APSTA). Not an optimisation: the answer to POST /api/provision
    // has to reach the phone that is still on the access point, and the reason a trial failed
    // is only useful if there is somewhere to show it. Expect the phone to drop anyway when
    // the station lands on a different channel and drags the access point with it -- that is
    // the invariant about answering before connecting, seen from the radio's side.
    OT_PROV_MODE_AP_STA,
} ot_prov_mode_t;

// Four different answers to the owner instead of one "it will not connect". This is an
// invariant, not a nicety: "the router is rebooting" and "you changed the password"
// need different things done about them, and the retired firmware threw the reason away.
typedef enum {
    OT_PROV_FAIL_NONE,
    OT_PROV_FAIL_NETWORK_ABSENT,   // nothing answered; the SSID is not on the air here
    OT_PROV_FAIL_WRONG_PASSWORD,   // it answered and would not take the key
    OT_PROV_FAIL_REFUSED,          // it answered and said no for some other reason
    OT_PROV_FAIL_NO_ADDRESS,       // associated, and no address ever arrived
} ot_prov_failure_t;

// The one-shot things the caller has to do. Drain with ot_prov_take_action() after every
// event and every tick: only one is held, and dropping COMMIT means the known-good pair is
// never updated, which quietly disarms the rollback.
typedef enum {
    OT_PROV_ACTION_NONE,
    // (Re)start a station attempt with the credentials currently in NVS. Disconnect first if
    // the station is associated -- this is also how a station stuck without an address is made
    // to try again. That disconnect comes back as an event like any other, with reason 8, and
    // ot_prov_classify() knows it is ours; see the note there before adding a code to it.
    OT_PROV_ACTION_CONNECT,
    // The pair in NVS just produced an address: copy it to the known-good slot. One write, one
    // flash cycle, and only on the transition -- not on every reconnection.
    OT_PROV_ACTION_COMMIT_CREDENTIALS,
    // The trial failed: put the known-good pair back and connect with it. SSID and PSK are one
    // atomic write; NVS gives no atomicity between keys, and a power cut between two of them
    // leaves a new SSID with an old password, which is a typo nobody made.
    OT_PROV_ACTION_RESTORE_CREDENTIALS,
} ot_prov_action_t;

// Every duration in milliseconds. Zero is a SETTING on three of these and a hazard on the rest,
// so each field says which it is -- there is no blanket rule, and writing one down was how this
// comment used to be wrong. Nothing clamps: a value the owner asked for is not silently rewritten
// here, and the field comments are where the price of each zero is named instead.
typedef struct {
    // 15 minutes is Shelly's number and the only one with field evidence behind it. 0 = the
    // window never closes, which is the sealed unreachable device and a supported configuration,
    // not an escape hatch -- the retired firmware reached the same conclusion (ap_guard.h,
    // set_setup_window).
    uint32_t setup_window_ms;
    // The bound on what client activity can extend, measured from the access point coming
    // up rather than from the last activity, or it would bound nothing. 0 = no ceiling.
    uint32_t setup_window_ceiling_ms;
    // Every window that comes back: after one has run out, after a reboot, and the one
    // init() offers a station that is never given an address. 0 = those windows never close
    // either, which is the same sealed device seen from its second boot.
    uint32_t reopen_window_ms;
    // Sustained failure before the device raises its own access point again. 30 minutes,
    // chosen so a router rebooting at night does not put an open access point on the air, and
    // so an owner does not spend a week without the device. 0 = the access point never comes
    // back by itself, and it also disables the no-address escape that shares this clock (see
    // ot_prov_address_never_arrived) -- so 0 here is a device with no way back at all.
    uint32_t fallback_after_ms;
    // How long a newly saved pair gets to produce an address before the known-good pair is put
    // back. 90 s is the retired firmware's ap_timeout default (ESPHome wifi/__init__.py:158) --
    // the number this house already used for "this network is not working, go back".
    // DO NOT set this to 0: every pair the owner saves is then rolled back on the next tick, so
    // a device that has ever been on a network can never be moved to another one.
    uint32_t trial_timeout_ms;
    // How long an associated station gets to be given an address before that counts as the
    // fourth kind of failure. Nothing sends an event for this; its absence is the signal.
    // DO NOT set this to 0: an associated station is then declared address-less on the tick it
    // associates, which reconnects it, for ever.
    uint32_t address_timeout_ms;
    // Retry backoff, doubling from the first to the second. Never a reboot: a router that is
    // off for a weekend must not produce a device that reboots all weekend (CLAUDE.md, and
    // limitation A11 of the retired firmware).
    // DO NOT set the minimum to 0: the delay stays 0 through every doubling, "has 0 ms passed"
    // is true on every call, and the retry becomes a station that reconnects on every tick.
    uint32_t retry_backoff_min_ms;
    uint32_t retry_backoff_max_ms;
} ot_prov_config_t;

// What the caller read out of NVS before any of this ran. Three facts, no values.
typedef struct {
    // An SSID worth trying is stored. NOT "the key exists", and the emptiness check has to live
    // in whatever fills this field, because the storage layer will hand back a zeroed blob as a
    // present one: ESPHome's WiFiComponent::start() loads the preference with no emptiness test
    // (wifi_component.cpp:655-663, "a saved-but-zeroed blob is still a saved blob") and
    // has_sta() is true on it (wifi_component.h:491), so after `Forget Wi-Fi` the retired
    // firmware spent a boot connecting to "". ap_guard::has_usable_sta_() (ap_guard.cpp:143-153)
    // is the FIX for that, not the bug -- it asks has_sta() && !get_sta().get_ssid().empty(), and
    // this field is the same question asked once, by the caller, before any of this runs.
    bool has_credentials;
    // A pair that has been seen to work is retained separately. False on a device that has
    // never been on a network, which is why a first provisioning cannot roll back.
    bool has_known_good;
    // A setup window has run out at some point in this device's life. After that the access
    // point comes back for reopen_window_ms at each boot, and not by itself.
    bool window_closed_before;
    // The last run of this device ended with a station that had joined the owner's network and
    // was never given an address -- see ot_prov_address_never_arrived() for why this one
    // fact has to survive a reboot.
    bool address_never_arrived;
} ot_prov_boot_t;

// Opaque by convention, in the header because the caller allocates it. DO NOT add an SSID or a
// PSK field: this struct is what the provisioning status projection gets built from, and
// ot_secret_key() can only redact a named key in a document -- it cannot help a string
// that a handler printed because it was in the struct it was handed. The caller owns NVS; this
// knows only whether something is in it. The static_assert below is the tripwire: an ssid[32]
// would not fit under it, so the rule fails the build rather than the review.
typedef struct {
    ot_prov_config_t  cfg;
    ot_prov_state_t   state;
    ot_prov_action_t  action;
    ot_prov_failure_t failure;
    uint8_t                 failure_reason;  // the raw wifi_err_reason_t, for the owner's benefit
    bool has_credentials;
    bool has_known_good;
    bool credentials_committed;  // the pair in NVS is the known-good one
    bool ap_on_air;              // reported by the caller, not inferred from mode()
    bool window_running;
    bool window_ever_closed;
    bool window_is_the_short_one;  // which of the two window lengths the access point on the air got
    bool trialling;
    bool associated;
    bool address_never_arrived;
    bool failing;
    bool retry_armed;
    uint8_t attempts;              // failed attempts since the last success or new pair
    uint32_t retry_delay_ms;       // how long the armed retry waits, kept so the deadline
                                   // arithmetic below stays "elapsed since", never "now >= then"
    uint32_t window_opened_at;    // when the access point came up (the window's anchor)
    uint32_t window_extended_at;  // the last activity that re-armed it
    uint32_t trial_at;
    uint32_t associated_at;
    uint32_t failing_at;
    uint32_t retry_at;
} ot_prov_t;

// Bytes, not fields, because what must not get in here is a string. 96 is comfortably above what
// the flags and timestamps need and comfortably below what one SSID would cost.
// test_the_machine_holds_no_credentials_only_the_fact_of_them says the same thing for a reader
// who is looking at the tests rather than at this file.
static_assert(sizeof(ot_prov_t) <= 96,
              "ot_prov_t grew enough to be holding something it must not");

// The numbers above. Fill a config, change what you need, pass it in.
void ot_prov_config_defaults(ot_prov_config_t *cfg);

// `cfg` NULL means the defaults; `boot` NULL means a device with nothing stored. Takes no
// timestamp on purpose: nothing in this machine is anchored on boot -- the correction at the
// heart of the setup window.
void ot_prov_init(ot_prov_t *s, const ot_prov_config_t *cfg,
                        const ot_prov_boot_t *boot);

// --- events ------------------------------------------------------------------------------------

// The clock. Everything with a deadline is decided here, so a caller that stops ticking stops
// the machine -- deliberately, because that is easier to see than a machine that half runs.
void ot_prov_tick(ot_prov_t *s, uint32_t now_ms);

// The access point is ON THE AIR. The window is anchored here and not on the decision to raise
// one; "unclaimed" keys on this and not on what NVS holds. Every access point this firmware
// raises is open, so on-the-air and open are the same question here -- if one of them ever gains
// a password, "unclaimed" keys on OPEN and this flag has to grow a second bit.
void ot_prov_on_ap_started(ot_prov_t *s, uint32_t now_ms);
void ot_prov_on_ap_stopped(ot_prov_t *s, uint32_t now_ms);

// Associated with the access point, no address yet (WIFI_EVENT_STA_CONNECTED). Not a success:
// the fourth kind of failure is the absence of the next event.
void ot_prov_on_associated(ot_prov_t *s, uint32_t now_ms);

// An address on the owner's network (IP_EVENT_STA_GOT_IP). The only thing that proves a pair.
void ot_prov_on_connected(ot_prov_t *s, uint32_t now_ms);

// `reason` is wifi_event_sta_disconnected_t.reason -- a uint8_t, ESP-IDF 5.5.5
// components/esp_wifi/include/esp_wifi_types_generic.h:1167, which is why no code above 255
// needs handling here.
void ot_prov_on_disconnected(ot_prov_t *s, uint32_t now_ms, uint8_t reason);

// A pair was written to NVS. Starts the trial and the rollback timer; does NOT mean connected,
// and the whole file exists to keep those two apart.
void ot_prov_on_credentials_saved(ot_prov_t *s, uint32_t now_ms);

// Somebody is talking to us on the access point. This extends the window, because the window
// closing while the owner is fetching their router password is the most annoying failure in the
// design and it is not an attack.
void ot_prov_on_client_seen(ot_prov_t *s, uint32_t now_ms);

// --- questions -----------------------------------------------------------------------------------

// DO NOT project state() before ot_prov_init() has run. A NULL machine answers
// WINDOW_CLOSED, which is the right answer for the radio -- it asks for nothing to be raised --
// but it is a real state with a meaning, and the design wants the setup window closing
// to be OBSERVABLE. A status endpoint that reads this on a device whose provisioning task has not
// started yet announces a transition that never happened. A caller that can be asked this early
// reports "starting up" from its own knowledge, not from here.
ot_prov_state_t ot_prov_state(const ot_prov_t *s);
ot_prov_mode_t  ot_prov_mode(const ot_prov_t *s);

// The single most important rule in this component: an open access point means unclaimed. False
// whenever an open access point is on the air, whatever NVS holds -- because in that moment
// anyone in radio range is listening. Storing credentials must not flip it: that gap is where a
// passer-by sets the first UI password and takes the device.
//
// NOT YET WIRED, and saying otherwise would let the next reader believe this is enforced. This is
// what ot_http_check()'s `ctx.provisioned` is meant to be fed from; src/main.cpp:91 still
// feeds it ot_net_has_credentials() -- the "has credentials" variant this rule rejects by name,
// and the one that leaves exactly the gap above open. Nothing in the tree calls any ot_prov_*
// function yet.
bool ot_prov_is_provisioned(const ot_prov_t *s);

bool ot_prov_has_credentials(const ot_prov_t *s);
bool ot_prov_has_known_good(const ot_prov_t *s);

// Open means: an access point is on the air AND its clock had not run out as of the last tick
// -- tick() is what closes it, so that the API, the log line and the UI all see it close at the
// same instant. False before the access point is up, even when mode() is already asking for
// one, and false for ever after it closes: nothing reopens a window. DO NOT drive the
// radio from this -- mode() is what says whether the access point should exist; this says
// whether the setup flow behind it is still live, and it is what the API and the log report so
// that a window closing is observable rather than a network that silently vanished.
bool     ot_prov_window_is_open(const ot_prov_t *s);
uint32_t ot_prov_window_remaining_ms(const ot_prov_t *s, uint32_t now_ms);

// A window has run out at some point. The caller persists this: "the access point comes
// back for five minutes at each reboot" needs it to survive one.
bool ot_prov_window_has_closed(const ot_prov_t *s);

// A station that joined the owner's network and was never given an address, for as long as the
// by-itself return calls sustained. The caller persists this exactly as it persists
// ot_prov_window_has_closed() and hands it back through ot_prov_boot_t at the next boot.
//
// This exists because of the invariant that outranks the decisions: there is never a state with
// no network, no access point and no way back. A dead DHCP server or an exhausted lease pool
// leaves a station associated, unreachable, with no access point -- and the by-itself return
// excludes that failure from its thirty-minute return BY NAME ("not on \"no IP\""), so nothing
// was ever going to raise one. A reboot reproduced it exactly, and recovery meant the front panel
// of a running ventilation unit.
//
// The way back is the reboot window's rather than the by-itself return's, and that distinction is
// the whole of why this is not a re-decision of the return: the by-itself return decides when the
// access point returns BY ITSELF, the reboot window decides that after a window closes it returns
// at each REBOOT. init() offers the reopen window to a device that boots with this true, with the
// station still trying underneath it. An address clears it, and so does a new pair of credentials
// -- whether a network hands out addresses is a property of that network. DO NOT infer it from
// ot_prov_failure() == OT_PROV_FAIL_NO_ADDRESS: that answer is true thirty seconds into a router
// that is still waking up, and acting on it would put an open access point on the air at the next
// boot of a perfectly healthy device.
bool ot_prov_address_never_arrived(const ot_prov_t *s);

// Whether the pair currently in NVS should be undone. Latched with the RESTORE action so it
// answers false again once the caller has done it.
bool ot_prov_should_roll_back(const ot_prov_t *s);

ot_prov_failure_t ot_prov_failure(const ot_prov_t *s);
uint8_t                 ot_prov_failure_reason(const ot_prov_t *s);

// One spelling of each answer, decided here. Four different answers to the owner is the
// invariant; the REST projection and the MQTT one inventing two spellings of them is how that
// invariant gets lost (CLAUDE.md: one entity list, ever).
const char *ot_prov_failure_name(ot_prov_failure_t failure);

// How long until the by-itself return brings the access point back, or OT_PROV_NEVER when
// nothing is scheduled to. NEVER while the failure is OT_PROV_FAIL_NO_ADDRESS: the return raises
// the access point only when the network is absent or refuses us, "not when we simply have no
// address yet", and that exclusion is kept literally -- nothing here raises an access point for
// a no-address failure, ever.
//
// The invariant that the exclusion used to collide with is answered somewhere else on purpose:
// ot_prov_address_never_arrived() offers that device a window at its next BOOT, which is the
// reboot window's mechanism and not the by-itself return's. A caller showing "no access point is
// scheduled" here should show that alongside it, or the owner reads "nothing will happen" for a
// device that has a way back.
uint32_t ot_prov_fallback_in_ms(const ot_prov_t *s, uint32_t now_ms);

// The one-shot the caller owes the world. Returns it and clears it; call after every event.
ot_prov_action_t ot_prov_take_action(ot_prov_t *s);

// The number -> answer table, exposed so the mapping can be pinned by number rather than by
// scenario. ESP-IDF 5.5.5, components/esp_wifi/include/esp_wifi_types_generic.h:113-178.
// OT_PROV_FAIL_NONE means "this disconnect is not a verdict on anything" -- reason 8 is
// what esp_wifi_disconnect() raises, and OT_PROV_ACTION_CONNECT is what tells the caller to
// call it, so that number is this machine's own nudge arriving back at it. Everything else is one
// of the owner's four answers.
ot_prov_failure_t ot_prov_classify(uint8_t reason);

#ifdef __cplusplus
}
#endif
