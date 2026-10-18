// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_command_check(): the entry point with an origin. A second file of the component
// rather than more of ot_command.c: that file encodes frames, this one translates ownership. One
// responsibility per file. The executor goes through neither for its re-sends of ID 1 and ID 56:
// it encodes them itself (see the DO NOT in ot_command.h).
#include "ot_command.h"

#include <math.h>
#include <stddef.h>

#include "ot_registry.h"
#include "ot_state.h"

static bool is_temperature(ot_control_cmd_t cmd)
{
    return cmd == OT_CONTROL_CMD_CH_SETPOINT || cmd == OT_CONTROL_CMD_DHW_SETPOINT;
}

// A float from outside into the int16_t ot_control speaks: tenths of a degree for a temperature,
// the number itself for a switch. false when there is no honest int16_t for it.
//
// lroundf(value * 10.0f) IN FLOAT, and that was measured, not chosen for style. The value arrives
// as the float nearest to a decimal the client wrote, and 45.05 is stored as 45.0499992. The
// float product rounds back to exactly 450.5 and lroundf gives 451, what the client wrote;
// promoted to double the product stays 450.49999924 and lround gives 450. Over every x.x5 in
// ±1000 the float form is right 20000 times out of 20000 and the double form wrong 8000 times.
// DO NOT "improve the precision" by computing in double.
//
// A switch value must be a whole number: 0.5 is neither on nor off, and rounding it would turn
// a malformed request into a guess (ot_http_registry.c refuses to guess at "true" for the same
// reason). A whole number other than 0 or 1 is passed through: refusing it is ot_control's
// decision, so the two callers of ot_control_check() cannot disagree about it -- and so it is
// an executor's bound, answered after ownership (the rule in ot_command.h).
static bool to_control_value(ot_control_cmd_t cmd, float value, int16_t *out)
{
    // FIRST: no comparison with NaN is true, so the int16_t test below would wave it through,
    // and lroundf(NaN) is unspecified -- 0 on the hosts measured, a CH setpoint of 0 °C.
    if (!isfinite(value))
        return false;
    const float scaled = is_temperature(cmd) ? value * 10.0f : value;
    if (!is_temperature(cmd) && scaled != floorf(scaled))
        return false;
    // The cast below is undefined outside int16_t: a million degrees is a refusal, not a wrap
    // into something ot_control would accept (6553.6 wraps to exactly 0).
    if (scaled <= -32768.5f || scaled >= 32767.5f)
        return false;
    *out = (int16_t)lroundf(scaled);
    return true;
}

// One-to-one where the meaning is the same. No default: a later enumerator appended to
// ot_control_err_t must be met here by -Wswitch, not swallowed.
static ot_command_err_t from_control(ot_control_err_t e)
{
    switch (e) {
    case OT_CONTROL_OK:                 return OT_CMD_OK;
    case OT_CONTROL_OWNED_BY_HA:        return OT_CMD_OWNED_BY_HA;
    case OT_CONTROL_OWNED_BY_LOCAL:     return OT_CMD_OWNED_BY_LOCAL;
    case OT_CONTROL_SEASON_ON_IS_LOCAL: return OT_CMD_SEASON_ON_IS_LOCAL;
    case OT_CONTROL_OUT_OF_RANGE:       return OT_CMD_OUT_OF_RANGE;
    case OT_CONTROL_SEASON_IS_OFF:
    case OT_CONTROL_BAD_MINUTES:
        // The boost's refusals; ot_control_check() never returns them.
        break;
    }
    // Fail-closed: a code with no meaning here is a refusal, never an OK.
    return OT_CMD_OUT_OF_RANGE;
}

ot_command_err_t ot_command_check(const char *key, float value, ot_origin_t origin,
                                  const ot_control_cfg_t *cfg, ot_command_out_t *out)
{
    const ot_entity_t *e = ot_registry_by_key(key);
    if (!e || !cfg || !out)
        return OT_CMD_UNKNOWN_KEY;

    // An entity that is no executor input is written exactly as before -- by the WEB. Home
    // Assistant is refused: it drives the executor, and a raw OpenTherm frame is not its to send:
    // Home Assistant drives the executor, never the bus. DO NOT add a MODE check here "for symmetry" with the rows
    // below: a room setpoint or a maximum modulation is nobody's, and refusing the web in HA mode
    // would take a working web control away.
    if (e->control == 0) {
        ot_command_frame_t f;
        const ot_command_err_t err = ot_command_encode(key, value, &f);
        if (err != OT_CMD_OK)
            return err;
        // AFTER the encode, never before: the order of refusals is one rule (ot_command.h) --
        // what is wrong with the request whoever sends it, and only then who may send it.
        if (origin == OT_ORIGIN_HA)
            return OT_CMD_NOT_FOR_HA;
        *out = (ot_command_out_t){.kind = OT_CMD_OUT_FRAME, .frame = f};
        return OT_CMD_OK;
    }

    // Steps 1-4 of the rule in ot_command.h: what is wrong with the request whoever sends it.
    // Writability, then the boiler's support (ot_command_encode()'s order). A synthetic row has
    // no Data-ID to be unsupported, so ID 1 falling silent does not take ch_enable down.
    //
    // ch_setpoint skips the support check, by the same argument that governs the executor's
    // re-sends. Its value never leaves here as a frame: the executor holds it and sends ID 1
    // itself every 10 s, and src/main.cpp feeds those replies into ot_state like any other, so
    // two stray UNKNOWN-DATAID answers raise a flag that never clears (ot_state.c). Asked here,
    // it would block no frame -- the executor goes on sending the old value -- and would only
    // freeze that value: every web and HA setpoint refused until a reboot, and an HA automation
    // that refreshes only the setpoint starving the watchdog into failsafe. DO NOT restore
    // the check "for symmetry" with dhw_setpoint: the "hand writes keep the command layer's
    // check" rule is about a frame a writer puts on the bus, and a CONTROL answer is not one.
    //
    // dhw_setpoint keeps it (no DHW value persisted that the boiler would refuse), and
    // carries the same risk, recorded here rather than solved: two stray UNKNOWN-DATAID answers
    // to the executor's own ID 56 writes freeze the DHW setpoint until a reboot. Dropping this
    // line would not lift it -- ot_command_encode() below asks the same flag.
    if (!e->writable)
        return OT_CMD_NOT_WRITABLE;
    const ot_control_cmd_t cmd = (ot_control_cmd_t)e->control;
    if (e->data_id >= 0 && cmd != OT_CONTROL_CMD_CH_SETPOINT) {
        const uint8_t id = (e->write_id >= 0) ? (uint8_t)e->write_id : (uint8_t)e->data_id;
        if (ot_state_is_unsupported(id))
            return OT_CMD_UNSUPPORTED_BY_BOILER;
    }

    int16_t v = 0;
    if (!to_control_value(cmd, value, &v))
        return OT_CMD_OUT_OF_RANGE;

    // The boiler's own DHW bounds (ID 48, or the table until it has spoken), BEFORE ownership --
    // a boiler's refusal holds in either mode -- and on the value that will be PERSISTED, v / 10,
    // not the float that came in, so the executor never stores a DHW setpoint the boiler would
    // refuse. The frame is thrown away: the executor writes ID 56 itself, by
    // reconciliation: it holds the desired DHW setpoint, writes it only when the ID 56 readback
    // disagrees, and retries at most once per 60 s.
    if (cmd == OT_CONTROL_CMD_DHW_SETPOINT) {
        ot_command_frame_t unused;
        const ot_command_err_t err = ot_command_encode(key, (float)v / 10.0f, &unused);
        if (err != OT_CMD_OK)
            return err;
    }

    const ot_command_err_t err = from_control(ot_control_check(cfg, origin, cmd, v));
    if (err != OT_CMD_OK)
        return err;
    *out = (ot_command_out_t){.kind = OT_CMD_OUT_CONTROL, .control = cmd, .value = v};
    return OT_CMD_OK;
}
