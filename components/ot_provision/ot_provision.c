// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_provision.h"

#include <stddef.h>

// Every interval in this file is "has `limit` passed since `since`", never "is now past a
// stored deadline". A millisecond counter is 32 bits and wraps every 49.7 days; a ventilation
// unit runs for years. Unsigned subtraction is correct across exactly one wrap, an absolute
// comparison is not -- it would close the setup window the instant the clock passed
// 0xFFFFFFFF, seven weeks into an uptime nobody is watching. DO NOT "simplify" either of
// these into a deadline comparison; test_the_clock_wrapping_does_not_close_the_window_early
// is what stands guard over it.
static bool elapsed(uint32_t now, uint32_t since, uint32_t limit)
{
    return (uint32_t)(now - since) >= limit;
}

static uint32_t remaining(uint32_t now, uint32_t since, uint32_t limit)
{
    const uint32_t gone = (uint32_t)(now - since);
    return gone >= limit ? 0u : limit - gone;
}

// Zero is a legal millisecond count -- a caller that starts its clock at boot passes it on the
// first tick -- so nothing here uses 0 as "unset". Each timestamp has a bool beside it.

// RESTORE and COMMIT are durable writes to NVS; CONNECT is a nudge. So a pending write is
// never displaced by a nudge. The one thing that DOES displace a pending write is a new pair
// of credentials, which cancels it outright -- see ot_prov_on_credentials_saved().
//
// Returns whether it took, and the caller in tick() keeps the retry armed when it did not. A
// dropped CONNECT is a station that never tries again, which on a device with no access point
// is exactly the stranding the invariants forbid -- so the arming stays until the
// nudge is actually issued rather than being cleared on the assumption that it was.
static bool set_action(ot_prov_t *s, ot_prov_action_t action)
{
    if (action == OT_PROV_ACTION_CONNECT && s->action != OT_PROV_ACTION_NONE)
        return false;
    s->action = action;
    return true;
}

// The first window of a device's life is the long one; every window that comes back is the
// short one. The long one is for somebody who has just unboxed a device; the short one is for
// somebody standing in front of a device that already has an owner, and it is short because the
// access point it belongs to is open and sits on a unit holding that owner's data.
//
// Which one an access point gets is decided when it COMES UP, in on_ap_started(), and read from
// the latch here. DO NOT ask the live question instead: has_credentials goes true the moment the
// owner submits the form, so a live test would cut the owner's own fifteen-minute window down to
// five at the instant they pressed the button --
// test_credentials_saved_mid_window_do_not_shorten_the_window_around_them stands guard over it.
static uint32_t window_length(const ot_prov_t *s)
{
    // The never-closing window first, and it OUTRANKS the distinction below rather than being a
    // case of it. Zero is
    // the setting for the ESP32 sealed behind the front panel of a running ventilation unit, and
    // the device that needs it is exactly the device whose first window ran out unattended in a
    // plant room -- so `window_ever_closed` is true on it and reading the short window here would
    // put the one unrecoverable device permanently off the air. The field's comment in the header
    // promises this unconditionally; this is where that promise is kept.
    if (s->cfg.setup_window_ms == 0)
        return 0;

    return s->window_is_the_short_one ? s->cfg.reopen_window_ms : s->cfg.setup_window_ms;
}

static bool window_open_at(const ot_prov_t *s, uint32_t now)
{
    if (!s->ap_on_air || !s->window_running)
        return false;

    // The never-closing zero, and the ORDER is the point here rather than the zero itself (window_length()
    // above is where that is decided). DO NOT apply the ceiling before this test: the ceiling
    // exists to bound what client ACTIVITY can extend, and there is nothing to extend on a window
    // that never closes -- checking it first would put a silent 60-minute lockout on the one
    // device that cannot recover from one.
    const uint32_t length = window_length(s);
    if (length == 0)
        return true;

    if (s->cfg.setup_window_ceiling_ms != 0 &&
        elapsed(now, s->window_opened_at, s->cfg.setup_window_ceiling_ms))
        return false;

    return !elapsed(now, s->window_extended_at, length);
}

// Doubling, with a floor and a ceiling. The ceiling is what keeps "retry for ever" from
// meaning "and it will notice the router came back an hour later"; there is no attempt limit
// at all, because the alternative to retrying is rebooting and nothing here reboots because a
// peer is absent (CLAUDE.md; limitation A11 of the retired firmware).
static uint32_t backoff_ms(const ot_prov_t *s, uint8_t attempt)
{
    uint32_t delay = s->cfg.retry_backoff_min_ms;
    for (uint8_t i = 0; i < attempt; i++) {
        // The second test is not redundant with the first. `delay * 2` above 2^31 wraps to a
        // SMALLER number, so with a ceiling near UINT32_MAX the first test never fires and the
        // thirty-third attempt computes 1000 << 32 -- which is zero, and elapsed(now, retry_at, 0)
        // is true on every call. The backoff would collapse into a station hammering a network
        // that has been refusing it for weeks, which is the opposite of what arm_retry() promises.
        if (delay >= s->cfg.retry_backoff_max_ms || delay > UINT32_MAX / 2u)
            break;
        delay *= 2u;
    }
    return delay > s->cfg.retry_backoff_max_ms ? s->cfg.retry_backoff_max_ms : delay;
}

// The by-itself return names three of the four answers and excludes the fourth by name: the
// access point comes back "only if the attempts fail because the network was not found or
// authentication was refused -- not because there is no IP". Four places read that line -- both
// clocks in tick(),
// the countdown the API publishes, and the disconnect path -- so it is drawn once. Two spellings
// of it is how "no access point for a no-address failure" quietly becomes "usually".
static bool failure_earns_the_access_point(ot_prov_failure_t failure)
{
    return failure == OT_PROV_FAIL_NETWORK_ABSENT ||
           failure == OT_PROV_FAIL_WRONG_PASSWORD ||
           failure == OT_PROV_FAIL_REFUSED;
}

// The one place a failure becomes the standing answer, because the disconnect path and tick()'s
// address timeout both reach it and both feed the same clock.
static void note_failure(ot_prov_t *s, ot_prov_failure_t failure, uint8_t reason,
                         uint32_t now)
{
    // The by-itself return measures a run of failures on ONE side of its line, not "any failure for thirty
    // minutes". Twenty-nine minutes of "the network is not there" followed by the station
    // associating and waiting on DHCP is not thirty minutes of no-address, and carrying the older
    // timestamp across the change of answer would spend the escape below on a router whose DHCP
    // server merely took a moment to wake up after a power cut. Repeated failures of the SAME
    // kind must not re-anchor it either, or a clock that measures "sustained" would restart every
    // thirty seconds and never reach anything.
    if (!s->failing ||
        failure_earns_the_access_point(s->failure) != failure_earns_the_access_point(failure)) {
        s->failing    = true;
        s->failing_at = now;
    }

    s->failure        = failure;
    s->failure_reason = reason;
}

static void arm_retry(ot_prov_t *s, uint32_t now)
{
    s->retry_delay_ms = backoff_ms(s, s->attempts);
    s->retry_at       = now;
    s->retry_armed    = true;
    // Saturate rather than wrap: an attempt count that rolls over to 0 would silently reset
    // the backoff to a second and hammer a network that has been refusing us for weeks.
    if (s->attempts < 32u)
        s->attempts++;
}

void ot_prov_config_defaults(ot_prov_config_t *cfg)
{
    if (cfg == NULL)
        return;

    cfg->setup_window_ms         = 15u * 60u * 1000u;
    cfg->setup_window_ceiling_ms = 60u * 60u * 1000u;
    cfg->reopen_window_ms        = 5u * 60u * 1000u;
    cfg->fallback_after_ms       = 30u * 60u * 1000u;
    cfg->trial_timeout_ms        = 90u * 1000u;
    cfg->address_timeout_ms      = 30u * 1000u;
    cfg->retry_backoff_min_ms    = 1000u;
    cfg->retry_backoff_max_ms    = 30u * 1000u;
}

void ot_prov_init(ot_prov_t *s, const ot_prov_config_t *cfg,
                        const ot_prov_boot_t *boot)
{
    if (s == NULL)
        return;

    const ot_prov_t empty = {0};
    *s = empty;

    if (cfg != NULL)
        s->cfg = *cfg;
    else
        ot_prov_config_defaults(&s->cfg);

    if (boot != NULL) {
        s->has_credentials       = boot->has_credentials;
        s->has_known_good        = boot->has_known_good;
        s->window_ever_closed    = boot->window_closed_before;
        s->address_never_arrived = boot->address_never_arrived;
    }

    // Deliberately NOT credentials_committed = has_known_good. Whether the pair in NVS is the
    // known-good one is unknowable from here: a power cut in the middle of a trial leaves an
    // untried pair stored and the old one in the known-good slot, and nothing in NVS says so.
    // Assuming the worst costs one redundant write on the first successful connection after a
    // boot, and makes that connection re-confirm whatever actually works. Assuming the best
    // would leave a device whose known-good pair is a network it has never reached.
    s->credentials_committed = false;

    s->state  = s->has_credentials ? OT_PROV_CONNECTING : OT_PROV_UNCONFIGURED;
    s->action = s->has_credentials ? OT_PROV_ACTION_CONNECT : OT_PROV_ACTION_NONE;

    // The invariant that outranks every decision here: there is never a state with no network, no
    // access point and no way back. A station that associates and is never given an address had
    // all three -- it is unreachable, the by-itself return excludes it from the thirty-minute
    // return BY NAME, and this line is where the previous boot's answer stopped it being
    // reproduced for ever. The return is not touched by it: the by-itself return decides when the
    // access point comes back BY ITSELF, and the reboot window has already decided that after a
    // window closes it comes back at each REBOOT. Those are different mechanisms, and this is the
    // second one.
    //
    // FALLBACK_AP rather than a state of its own because it is exactly that state: the access
    // point is up (mode() answers AP_STA) and the station keeps trying underneath it -- an access
    // point that stopped trying the network would be a trap rather than a rescue. tick() closes
    // the window after reopen_window_ms and does NOT start the by-itself return's clock for it, so
    // the exposure is five minutes per boot and not an open access point for ever.
    if (s->has_credentials && s->address_never_arrived)
        s->state = OT_PROV_FALLBACK_AP;
}

// --- events ------------------------------------------------------------------------------------

void ot_prov_on_ap_started(ot_prov_t *s, uint32_t now_ms)
{
    if (s == NULL || s->ap_on_air)
        return;  // a repeated event must not hand out a second window

    // The anchoring correction, and the whole reason this is an event rather than a field set at
    // init: the clock starts when the access point is ON THE AIR. In the retired firmware it
    // started at boot, so on the path back from `Forget Wi-Fi` -- where the access point only
    // appears ~90 s in -- the owner really got setup_window minus a minute and a half
    // (ap_guard.h, window_started_). That was written down as an accepted price; it is not one
    // any more.
    s->ap_on_air           = true;
    s->window_running      = true;
    s->window_opened_at    = now_ms;
    s->window_extended_at  = now_ms;

    // Which of the two window lengths this access point gets, decided here and not read live --
    // see window_length(). has_credentials is in the test as well as window_ever_closed because a
    // device that provisioned inside its first window never had one run out, and its fallback
    // access point would otherwise get the first-run fifteen minutes: fifteen open minutes in
    // every forty-five, on a unit that already holds the owner's data.
    s->window_is_the_short_one = s->window_ever_closed || s->has_credentials;
}

void ot_prov_on_ap_stopped(ot_prov_t *s, uint32_t now_ms)
{
    (void)now_ms;
    if (s == NULL)
        return;

    // The access point leaving the air is not the window running out, and only the second one
    // shortens the next window. The usual reason to be here is a successful connection.
    s->ap_on_air      = false;
    s->window_running = false;
}

void ot_prov_on_associated(ot_prov_t *s, uint32_t now_ms)
{
    if (s == NULL)
        return;

    // Associated is not connected. The fourth answer to the owner -- "it joined the network and
    // was never given an address" -- has no disconnect code behind it; it is the absence of the
    // next event, which is why this timestamp exists.
    //
    // DO NOT clear s->failure here on the reasoning that a fresh attempt is in flight. Associating
    // is not evidence that the previous answer was wrong: a wrong PSK associates perfectly well
    // and fails afterwards, at the four-way handshake (WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,
    // esp_wifi_types_generic.h:131). Clearing it would blank the status page to "no failure" on
    // every retry of a network that is not going to work, which is how an owner ends up with a
    // device that says nothing is wrong and never connects. The answer is replaced when the next
    // outcome is known, and by nothing else.
    s->associated    = true;
    s->associated_at = now_ms;
}

void ot_prov_on_connected(ot_prov_t *s, uint32_t now_ms)
{
    (void)now_ms;
    if (s == NULL)
        return;

    s->state          = OT_PROV_CONNECTED;
    s->associated     = false;
    s->trialling      = false;
    s->retry_armed    = false;
    s->attempts       = 0;
    // The by-itself return measures SUSTAINED failure; one address ends the run, both for the
    // thirty-minute return in tick() step 5 and for the no-address escape in step 6.
    // test_an_address_ends_the_run_of_failures_that_preceded_it stands guard over this line
    // -- and note that the countdown alone does not, because the failure is cleared below
    // and a cleared failure answers NEVER whatever this flag says.
    s->failing        = false;
    s->failure        = OT_PROV_FAIL_NONE;
    s->failure_reason = 0;
    s->has_credentials = true;
    // An address is the one thing that disproves "this network never gives us one". Without this
    // the caller would keep the fact in NVS and every boot for the rest of the device's life
    // would put an open access point on the air for five minutes.
    s->address_never_arrived = false;

    // The access point comes down. That is not tidiness: the device counts as unclaimed for as
    // long as an open access point is on the air, so a connection that left one up would leave the
    // device writable by the neighbourhood for the rest of the window.
    s->window_running = false;

    // An address on the owner's network is the ONLY thing that makes a pair known-good. Once
    // per pair, not once per reconnection -- a flapping router would otherwise spend a flash
    // cycle every few minutes rewriting the same two strings.
    if (!s->credentials_committed) {
        s->credentials_committed = true;
        s->has_known_good        = true;
        set_action(s, OT_PROV_ACTION_COMMIT_CREDENTIALS);
    }
}

void ot_prov_on_disconnected(ot_prov_t *s, uint32_t now_ms, uint8_t reason)
{
    if (s == NULL)
        return;

    // Nothing stored, nothing to fail at: the radio raises this event in situations that have
    // no stored network behind them, a mode change among them, and such an event carries no
    // answer for the owner. Ignoring it whole is load-bearing rather than tidy -- classifying
    // it would start the thirty-minute clock, and the by-itself return would then raise an access
    // point on a device whose setup window had deliberately closed for good.
    if (!s->has_credentials)
        return;

    s->associated = false;

    // A disconnect this machine asked for is not a verdict on anything, and ot_prov_classify()
    // answers OT_PROV_FAIL_NONE for those. The header's contract for
    // OT_PROV_ACTION_CONNECT orders the caller to disconnect an associated station before
    // reconnecting it, so the machine's own recovery nudge comes straight back here as reason 8 --
    // and recording it would answer "the router will not have us" for a dead DHCP server, overwrite
    // the NO_ADDRESS answer tick() had just reached, and start the thirty-minute clock for the one
    // failure the by-itself return excludes by name. The retry below is still armed for it: a
    // disconnect that arms nothing is a station that never tries again, which is the stranding the
    // invariants forbid.
    const ot_prov_failure_t verdict = ot_prov_classify(reason);
    if (verdict != OT_PROV_FAIL_NONE)
        note_failure(s, verdict, reason, now_ms);  // and this is where the by-itself return's clock starts

    // A trial survives its own disconnections: the rollback is on a clock, not on the first
    // discouraging event. One 4-way-handshake timeout is also what a crowded 2.4 GHz band
    // produces on a first attempt, and undoing the owner's correct new password over that is
    // worse than waiting out trial_timeout_ms.
    if (!s->trialling)
        s->state = OT_PROV_RETRYING;

    arm_retry(s, now_ms);
}

void ot_prov_on_credentials_saved(ot_prov_t *s, uint32_t now_ms)
{
    if (s == NULL)
        return;

    s->has_credentials       = true;
    s->credentials_committed = false;
    s->trialling             = true;
    s->trial_at              = now_ms;
    s->state                 = OT_PROV_TRIAL;

    // Whoever submitted the form is standing there watching. Making them wait out a
    // half-minute backoff earned by the previous network is how a working device looks broken.
    s->attempts    = 0;
    s->retry_armed = false;

    // A fresh pair gets a fresh 30 minutes before the by-itself return raises the access point,
    // and no stale reason on the status page while a new attempt is in flight. The no-address escape goes with
    // them: whether a network hands out addresses is a property of the network, and the owner has
    // just named a different one.
    s->failing               = false;
    s->failure               = OT_PROV_FAIL_NONE;
    s->failure_reason        = 0;
    s->address_never_arrived = false;

    // DO NOT let a pending action survive this. Both of them name "the pair in NVS", and the
    // pair in NVS has just changed underneath them: an un-drained COMMIT would promote the
    // untried new pair to known-good and destroy the very thing the rollback restores, and an
    // un-drained RESTORE would throw away what the owner just typed.
    s->action = OT_PROV_ACTION_CONNECT;

    // Submitting the form is the strongest evidence a human is present. If the window ran out
    // mid-trial the owner would lose the page that tells them what went wrong.
    if (s->window_running)
        s->window_extended_at = now_ms;
}

void ot_prov_on_client_seen(ot_prov_t *s, uint32_t now_ms)
{
    // The live question, not the last tick's answer. window_running is only cleared by tick(), so
    // asking it here handed a whole fresh window to a client that knocked in the second between a
    // window running out and the tick that closes it -- and again at the next expiry, up to the
    // ceiling. A closed window does not reopen because somebody knocked, and this is the line that
    // makes that sentence true rather than nearly true.
    if (s == NULL || !window_open_at(s, now_ms))
        return;

    s->window_extended_at = now_ms;
}

// --- the clock ----------------------------------------------------------------------------------

void ot_prov_tick(ot_prov_t *s, uint32_t now_ms)
{
    if (s == NULL)
        return;

    // 1. The setup window ran out.
    if (s->window_running && s->ap_on_air && !window_open_at(s, now_ms)) {
        s->window_running     = false;
        s->window_ever_closed = true;

        if (s->state == OT_PROV_UNCONFIGURED) {
            // Nothing to connect to, so the radio has nothing left to do. This is the one
            // state with no network and no access point, and it is paid for with the short
            // window every reboot gives -- which is the "way back" the invariants demand.
            s->state = OT_PROV_WINDOW_CLOSED;
        } else if (s->state == OT_PROV_FALLBACK_AP) {
            // A device with credentials never lands in WINDOW_CLOSED. Its window closing means
            // one thing: the access point goes off the air and the station carries on. The
            // by-itself return then brings the access point back thirty minutes later, so the
            // exposure is a duty cycle rather than an open access point for ever -- and the clock
            // for that restarts HERE, when the access point actually goes away, rather than at the
            // disconnect thirty minutes ago, or the two access points would run back to back.
            //
            // The test around it is not tidiness. This branch is also reached by the access point
            // that init() raises for a station that is never given an address, and there the
            // restart would do two wrong things at once: bring that access point back by itself
            // thirty minutes later, which is forbidden for a no-address failure, and push back the
            // escape clock in step 6 by the length of every window it serves.
            s->state = OT_PROV_RETRYING;
            if (failure_earns_the_access_point(s->failure)) {
                s->failing    = true;
                s->failing_at = now_ms;
            }
        }
    }

    // 2. The trial ran out: the pair the owner just saved has not produced an address.
    if (s->trialling && elapsed(now_ms, s->trial_at, s->cfg.trial_timeout_ms)) {
        s->trialling = false;
        if (s->has_known_good) {
            // The likeliest catastrophe in this whole project is not an attack, it is a typo:
            // the owner changes networks from the settings page, mistypes one character, the
            // old pair is already overwritten, no access point comes up because credentials
            // exist -- and the device simply disappears.
            s->state       = OT_PROV_CONNECTING;
            s->retry_armed = false;
            s->attempts    = 0;
            s->credentials_committed = true;  // what is about to be in NVS is the known-good pair
            set_action(s, OT_PROV_ACTION_RESTORE_CREDENTIALS);
        } else {
            // A first provisioning has nothing to roll back to, and erasing the SSID they
            // picked out of the scan list would only make them do it all again. The way back
            // here is the access point still on the air, and after that the by-itself return's.
            s->state = OT_PROV_RETRYING;
        }
    }

    // 3. Associated, and no address ever arrived.
    if (s->associated && elapsed(now_ms, s->associated_at, s->cfg.address_timeout_ms)) {
        // Starts a clock, but not the by-itself return's: step 5 below refuses this answer by
        // name, and step 6 is the one it feeds. `0` because this is the one answer with no reason
        // code behind it --
        // it is the absence of the next event, not an event.
        note_failure(s, OT_PROV_FAIL_NO_ADDRESS, 0, now_ms);
        if (s->state != OT_PROV_FALLBACK_AP)
            s->state = s->trialling ? OT_PROV_TRIAL : OT_PROV_CONNECTING;

        // Clear `associated` only once the nudge has actually been issued, exactly as step 4
        // keeps its retry armed. set_action() refuses a CONNECT while a durable write waits to be
        // drained -- and clearing the flag first threw the refused nudge away, leaving a machine
        // that has forgotten it was associated and has no timer left. That is a station that
        // never tries again, on a device with no access point.
        if (set_action(s, OT_PROV_ACTION_CONNECT))
            s->associated = false;
    }

    // 4. The backoff expired: try again.
    if (s->retry_armed && elapsed(now_ms, s->retry_at, s->retry_delay_ms) &&
        set_action(s, OT_PROV_ACTION_CONNECT)) {
        s->retry_armed = false;
        if (s->state == OT_PROV_RETRYING)
            s->state = OT_PROV_CONNECTING;
    }

    // 5. Sustained failure of the right kind brings the access point back by itself.
    //
    // "Of the right kind" is failure_earns_the_access_point(), and it is the term that keeps the
    // exclusion spelled out above. Every disconnect used to reach this, because the classifier
    // could only answer one of the three that count -- so the fourth answer had no way in and the
    // exclusion held by accident. It no longer does: the address timeout in step 3 feeds this
    // same clock now, and reason 8 no longer answers anything at all.
    //
    // has_credentials is the line between the by-itself return and the reboot window. The
    // by-itself return is for a device that HAS a network and cannot reach it; a device with
    // nothing stored has no network to fail at, and its window comes back at a reboot and not on
    // its own -- without which a closed window would be a fifteen-minute pause rather than a bound
    // and the whole decision would buy nothing. Today it is a belt: ot_prov_on_disconnected()
    // returns before setting `failing` on such a device, so nothing can reach here with
    // credentials missing and a failure standing. DO NOT drop it on that reasoning -- a future
    // "forget Wi-Fi" gesture puts this device there, and the event that implements it is exactly
    // what makes the two facts come apart.
    if (s->has_credentials && s->failing && failure_earns_the_access_point(s->failure) &&
        !s->ap_on_air && s->state != OT_PROV_FALLBACK_AP && s->cfg.fallback_after_ms != 0 &&
        elapsed(now_ms, s->failing_at, s->cfg.fallback_after_ms)) {
        // Thirty minutes, because a router rebooting at night must not put an open access
        // point on the air, and because a week without the device is the other failure. The
        // station keeps trying underneath -- an access point that stopped trying the network
        // would be a trap rather than a rescue.
        s->state = OT_PROV_FALLBACK_AP;
    }

    // 6. The way back for the failure step 5 excludes, and the reason step 5 can afford to
    // exclude it at all.
    //
    // "Not on \"no IP\"" is kept to the letter: nothing here raises an access point. What it does
    // is record, for the caller to put in NVS, that this device has associated with the owner's
    // network and been refused an address for as long as the return calls sustained. init() reads
    // it back at the next boot and offers the reopen window then -- the reboot window's mechanism
    // ("the access point comes back at each reboot, and not by itself"), not the by-itself
    // return's. Without it the state is no network, no access point and no way back, reproduced
    // exactly by every reboot, with recovery through the front panel of a running ventilation
    // unit.
    if (s->failing && s->failure == OT_PROV_FAIL_NO_ADDRESS &&
        s->cfg.fallback_after_ms != 0 && elapsed(now_ms, s->failing_at, s->cfg.fallback_after_ms))
        s->address_never_arrived = true;
}

// --- questions -----------------------------------------------------------------------------------

ot_prov_state_t ot_prov_state(const ot_prov_t *s)
{
    // Every question below is reachable from an HTTP handler on a device whose provisioning
    // task has not started yet, so none of them may fall over. WINDOW_CLOSED rather than the
    // zero value: it is the answer that asks for nothing to be raised, and a machine that does
    // not exist should not be telling the radio to do anything. It is the right answer for the
    // radio and the wrong one for a status page -- see the DO NOT on this function in the header.
    return s == NULL ? OT_PROV_WINDOW_CLOSED : s->state;
}

ot_prov_mode_t ot_prov_mode(const ot_prov_t *s)
{
    if (s == NULL)
        return OT_PROV_MODE_OFF;

    switch (s->state) {
    case OT_PROV_UNCONFIGURED:
        return OT_PROV_MODE_ACCESS_POINT;
    case OT_PROV_WINDOW_CLOSED:
        return OT_PROV_MODE_OFF;
    case OT_PROV_CONNECTED:
        return OT_PROV_MODE_STATION;
    case OT_PROV_FALLBACK_AP:
        return OT_PROV_MODE_AP_STA;
    default:
        // CONNECTING, TRIAL, RETRYING. The access point stays up for as long as the window
        // does, and not one moment longer: the answer to POST /api/provision has to reach a
        // phone that is still on it, and a trial that fails is only useful if there is
        // somewhere to say why.
        return s->window_running ? OT_PROV_MODE_AP_STA : OT_PROV_MODE_STATION;
    }
}

bool ot_prov_is_provisioned(const ot_prov_t *s)
{
    if (s == NULL)
        return false;

    // The single most important line in this component: an open access point means unclaimed. Not
    // "are there credentials" and not "is there an address": the question is whether anybody in
    // radio range is being listened to right now. ot_http_policy.c decides who may write from this
    // flag, and the moment it goes true while the access point is still up is the moment a
    // passer-by can set the first UI password and own the device.
    return s->has_credentials && !s->ap_on_air;
}

bool ot_prov_has_credentials(const ot_prov_t *s)
{
    return s != NULL && s->has_credentials;
}

bool ot_prov_has_known_good(const ot_prov_t *s)
{
    return s != NULL && s->has_known_good;
}

bool ot_prov_window_is_open(const ot_prov_t *s)
{
    // Reports what the last tick decided rather than re-deciding it here, so that every reader
    // of the status -- the API, the log line, the LED -- sees the same window closing at the
    // same instant. A caller that stops ticking stops the machine, which is deliberate: that
    // is far easier to notice than a machine that half runs.
    return s != NULL && s->ap_on_air && s->window_running;
}

bool ot_prov_window_has_closed(const ot_prov_t *s)
{
    return s != NULL && s->window_ever_closed;
}

bool ot_prov_address_never_arrived(const ot_prov_t *s)
{
    return s != NULL && s->address_never_arrived;
}

uint32_t ot_prov_window_remaining_ms(const ot_prov_t *s, uint32_t now_ms)
{
    if (s == NULL || !window_open_at(s, now_ms))
        return 0;

    const uint32_t length = window_length(s);
    if (length == 0)
        return OT_PROV_NEVER;  // the never-closing window

    const uint32_t by_window  = remaining(now_ms, s->window_extended_at, length);
    const uint32_t by_ceiling = s->cfg.setup_window_ceiling_ms == 0
                                    ? OT_PROV_NEVER
                                    : remaining(now_ms, s->window_opened_at,
                                                s->cfg.setup_window_ceiling_ms);
    return by_window < by_ceiling ? by_window : by_ceiling;
}

bool ot_prov_should_roll_back(const ot_prov_t *s)
{
    // Latched with the action, so it answers false again the moment the caller has done it.
    return s != NULL && s->action == OT_PROV_ACTION_RESTORE_CREDENTIALS;
}

ot_prov_failure_t ot_prov_failure(const ot_prov_t *s)
{
    return s == NULL ? OT_PROV_FAIL_NONE : s->failure;
}

uint8_t ot_prov_failure_reason(const ot_prov_t *s)
{
    return s == NULL ? 0 : s->failure_reason;
}

const char *ot_prov_failure_name(ot_prov_failure_t failure)
{
    switch (failure) {
    case OT_PROV_FAIL_NETWORK_ABSENT: return "network-not-found";
    case OT_PROV_FAIL_WRONG_PASSWORD: return "wrong-password";
    case OT_PROV_FAIL_REFUSED:        return "refused";
    case OT_PROV_FAIL_NO_ADDRESS:     return "no-address";
    case OT_PROV_FAIL_NONE:           break;
    }
    return "none";
}

uint32_t ot_prov_fallback_in_ms(const ot_prov_t *s, uint32_t now_ms)
{
    // ap_on_air as well as the state: counting down to an access point while one is already
    // up would be a status line that contradicts the radio, and tick() will not raise a second
    // one anyway.
    if (s == NULL || s->state == OT_PROV_FALLBACK_AP || s->ap_on_air || !s->failing ||
        !failure_earns_the_access_point(s->failure) || s->cfg.fallback_after_ms == 0)
        return OT_PROV_NEVER;

    return remaining(now_ms, s->failing_at, s->cfg.fallback_after_ms);
}

ot_prov_action_t ot_prov_take_action(ot_prov_t *s)
{
    if (s == NULL)
        return OT_PROV_ACTION_NONE;

    const ot_prov_action_t action = s->action;
    s->action = OT_PROV_ACTION_NONE;
    return action;
}

// Numbers, not scenarios. These are wifi_err_reason_t from ESP-IDF 5.5.5,
// components/esp_wifi/include/esp_wifi_types_generic.h:113-178, and they arrive as
// wifi_event_sta_disconnected_t.reason -- a uint8_t (:1167), which is why nothing above 255
// needs a branch here. The retired firmware threw this byte away entirely
// (ot_net_prov.c, `(void)data`), which is how "the router is rebooting" and "you changed the
// password" became one message saying neither.
ot_prov_failure_t ot_prov_classify(uint8_t reason)
{
    switch (reason) {
    // Nothing answered. BEACON_TIMEOUT belongs here rather than with the refusals: it means
    // the access point we were talking to stopped being heard, which is what a router reboot
    // and a router that has been unplugged both look like from a station.
    case 200:  // WIFI_REASON_BEACON_TIMEOUT (:165)
    case 201:  // WIFI_REASON_NO_AP_FOUND (:166)
    case 210:  // WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY (:175)
    case 211:  // WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD (:176)
    case 212:  // WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD (:177)
        return OT_PROV_FAIL_NETWORK_ABSENT;

    // It answered and would not take the key. There is no reason code that says "password":
    // a wrong PSK fails the MIC check on message 2 of the four-way handshake and the access
    // point simply stops replying, so what a station reports is a timeout.
    case 14:   // WIFI_REASON_MIC_FAILURE (:130)
    case 15:   // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT (:131)
    case 202:  // WIFI_REASON_AUTH_FAIL (:167) -- WPA3-SAE rejects the passphrase at auth
    case 204:  // WIFI_REASON_HANDSHAKE_TIMEOUT (:169)
        return OT_PROV_FAIL_WRONG_PASSWORD;

    // Ours, and therefore not an answer to anybody. esp_wifi_disconnect() raises
    // WIFI_EVENT_STA_DISCONNECTED with this code, and the header's contract for
    // OT_PROV_ACTION_CONNECT is what makes the caller call it: "disconnect first if the
    // station is associated". Sending it to the default arm below made the machine's own recovery
    // nudge come back as a router refusal -- the wrong one of the owner's four answers, and worse,
    // one of the three that count toward the by-itself return's clock, so a no-address fault
    // raised an open access point half an hour later. DO NOT add the other locally-flavoured codes
    // here on the same reasoning: 3 (AUTH_LEAVE, :116) and 47 (AP_INITIATED, :157) can equally be
    // a router kicking us for good, and a code that stops counting toward that clock is a device
    // that never raises its access point again. Reason 8 earns the exception because THIS FILE is
    // what causes it.
    case 8:    // WIFI_REASON_ASSOC_LEAVE (:124)
        return OT_PROV_FAIL_NONE;

    // Everything else. Named ones, so the reasoning is visible: the access point is full (5),
    // this is an enterprise network our provisioning form cannot express (23) -- telling the
    // owner "wrong password" there would send them round in circles retyping a PSK that was
    // never going to be asked for -- association was rejected (203), or the attempt simply
    // failed (205).
    //
    // The DEFAULT matters more than the list. Sixty-odd codes exist and this maps a dozen; an
    // unlisted one has to land in a class that counts toward the by-itself return's clock, or a
    // device failing for a reason nobody enumerated would never raise its access point again --
    // which is the state the invariants forbid outright. "The router will not have us" is also the
    // honest thing to say when there is nothing better to say.
    case 5:    // WIFI_REASON_ASSOC_TOOMANY (:119)
    case 23:   // WIFI_REASON_802_1X_AUTH_FAILED (:139)
    case 203:  // WIFI_REASON_ASSOC_FAIL (:168)
    case 205:  // WIFI_REASON_CONNECTION_FAIL (:170)
    default:
        return OT_PROV_FAIL_REFUSED;
    }
}
