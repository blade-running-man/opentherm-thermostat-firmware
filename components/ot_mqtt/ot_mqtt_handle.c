// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What an accepted MQTT command makes happen (ot_mqtt.h, ot_mqtt_handle): the same four layers the
// web route glues -- a parsed value, ot_command_check() with an origin, and the executor or the bus.
#include "ot_mqtt.h"

// The words for the executor's final refusal: ot_command's, for the four verdicts both layers
// share, so a log line says the same thing whichever layer refused. Anything else -- the boost's
// two verdicts, which no MQTT command can earn, and the task layer's NO_TASK and NOT_SAVED -- is
// "not carried out", with the code in the caller's log line.
static const char *apply_reason(int code)
{
    switch (code) {
    // Unreachable from here: fx->apply is always called with OT_ORIGIN_HA (below), and
    // OT_CONTROL_OWNED_BY_HA is a WEB write's verdict (ot_control.h). Kept for a caller other
    // than MQTT that reuses this table with OT_ORIGIN_WEB.
    case OT_CONTROL_OWNED_BY_HA:        return ot_command_strerror(OT_CMD_OWNED_BY_HA);
    case OT_CONTROL_OWNED_BY_LOCAL:     return ot_command_strerror(OT_CMD_OWNED_BY_LOCAL);
    case OT_CONTROL_SEASON_ON_IS_LOCAL: return ot_command_strerror(OT_CMD_SEASON_ON_IS_LOCAL);
    case OT_CONTROL_OUT_OF_RANGE:       return ot_command_strerror(OT_CMD_OUT_OF_RANGE);
    default:                            return "accepted but not carried out by the executor";
    }
}

ot_mqtt_verdict_t ot_mqtt_handle(const ot_mqtt_in_t *in, const ot_mqtt_effects_t *fx)
{
    ot_mqtt_verdict_t v = {false, NULL};
    if (in == NULL || fx == NULL || in->kind != OT_MQTT_COMMAND)
        return v;

    // The configuration the executor last stepped with -- the early answer and the final one read
    // one store (ot_thermostat.h, ot_thermostat_control_cfg). DO NOT build one here.
    ot_control_cfg_t cfg;
    fx->cfg(&cfg);
    ot_command_out_t       out;
    const ot_command_err_t ce = ot_command_check(in->key, in->value, OT_ORIGIN_HA, &cfg, &out);
    if (ce != OT_CMD_OK) {
        v.reason = ot_command_strerror(ce);
        return v;
    }
    if (out.kind == OT_CMD_OUT_CONTROL) {
        // THE FINAL ANSWER: the mode may have flipped since the early one.
        const int r = fx->apply(OT_ORIGIN_HA, out.control, out.value);
        if (r != 0) {
            v.reason = apply_reason(r);
            return v;
        }
        v.accepted = true;
        return v;
    }
    if (out.kind == OT_CMD_OUT_FRAME) {
        fx->write(out.frame.data_id, out.frame.raw);
        v.accepted = true;
        return v;
    }
    v.reason = "unhandled command kind";
    return v;
}
