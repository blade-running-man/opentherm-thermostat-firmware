// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_control_io.h"

#include <math.h>
#include <string.h>

// The executor's translations. Each function is a mapping with its reason written beside it; the
// contracts are in the header, and test_ot_control_io pins every one of them against the real
// components on either side.

// ot_config may not include ot_control.h -- it would put the executor into every settings suite --
// so the one number they share is pinned here, where both are in view.
_Static_assert(OT_CONFIG_MODE_LOCAL == OT_CONTROL_MODE_LOCAL, "control_mode 0 is LOCAL in both");
_Static_assert(OT_CONFIG_MODE_HA == OT_CONTROL_MODE_HA, "control_mode 1 is HA in both");

// uint16_t in the store, int16_t in the executor. Saturated, not wrapped: 40000 wrapped is a
// NEGATIVE setpoint, which ot_control would clamp up to flow_min and hold as if it were meant.
static int16_t dc_of(uint16_t v)
{
    return v > INT16_MAX ? INT16_MAX : (int16_t)v;
}

void ot_control_io_cfg(const ot_config_public_t *pub, ot_control_cfg_t *out)
{
    memset(out, 0, sizeof *out);
    // Only the store's HA is HA: a damaged number is the mode in which HA owns nothing.
    out->mode = pub->control_mode == OT_CONFIG_MODE_HA ? OT_CONTROL_MODE_HA : OT_CONTROL_MODE_LOCAL;
    out->heating_season          = pub->heating_season;
    out->watchdog_s              = pub->watchdog_s;
    out->failsafe_setpoint_dc    = dc_of(pub->failsafe_setpoint_dc);
    out->failsafe_room_target_dc = dc_of(pub->failsafe_room_target_dc);
    out->failsafe_heat_days =
        pub->failsafe_heat_days > UINT8_MAX ? UINT8_MAX : (uint8_t)pub->failsafe_heat_days;
    out->failsafe_min_cycle_s    = pub->failsafe_min_cycle_s;
    out->flow_min_dc             = dc_of(pub->flow_min_dc);
    out->flow_max_dc             = dc_of(pub->flow_max_dc);
    out->local_ch_enable         = pub->local_ch_enable;
    out->local_ch_setpoint_dc    = dc_of(pub->local_ch_setpoint_dc);
    out->dhw_enable              = pub->dhw_enable;
    out->dhw_setpoint_set        = pub->dhw_setpoint_dc != 0;
    out->dhw_setpoint_dc         = dc_of(pub->dhw_setpoint_dc);
}

// A negative value reaches the store as one its range refuses, never as a wrap into one it
// accepts: (uint32_t)-5 is 4294967291 today, but an int16_t-to-uint16_t cast on the way would make
// it 65531, and the store's u16 fields sit one narrowing away. UINT32_MAX says what it is.
static uint32_t stored(int16_t dc)
{
    return dc < 0 ? UINT32_MAX : (uint32_t)dc;
}

bool ot_control_io_patch(const ot_control_persist_t *p, ot_config_patch_t *patch)
{
    memset(patch, 0, sizeof *patch);
    // The FLAG decides, not whatever sits in the value beside it: a member present by accident is
    // a stored value overwritten by accident.
    if (p->set_local_ch_enable) {
        patch->has_local_ch_enable = true;
        patch->local_ch_enable     = p->local_ch_enable;
    }
    if (p->set_local_ch_setpoint) {
        patch->has_local_ch_setpoint_dc = true;
        patch->local_ch_setpoint_dc     = stored(p->local_ch_setpoint_dc);
    }
    if (p->set_dhw_enable) {
        patch->has_dhw_enable = true;
        patch->dhw_enable     = p->dhw_enable;
    }
    if (p->set_dhw_setpoint) {
        patch->has_dhw_setpoint_dc = true;
        patch->dhw_setpoint_dc     = stored(p->dhw_setpoint_dc);
    }
    if (p->set_heating_season) {
        patch->has_heating_season = true;
        patch->heating_season     = p->heating_season;
    }
    return p->any;
}

int16_t ot_control_io_f88_dc(uint16_t raw)
{
    // Signed without the implementation-defined uint16_t -> int16_t conversion.
    const int32_t value  = raw >= 0x8000u ? (int32_t)raw - 0x10000 : (int32_t)raw;
    const int32_t tenths = value * 10;
    // C division truncates toward zero, so the half is added on the side of the sign.
    return (int16_t)((tenths + (tenths < 0 ? -128 : 128)) / 256);
}

uint16_t ot_control_io_dc_f88(int16_t dc)
{
    // dc * 25.6 in integers. 256 * dc is even, so its last digit is never 5 and there is no tie to
    // round: half away from zero and the codec's lrintf() agree everywhere.
    const int32_t num = (int32_t)dc * 256;
    const int32_t raw = (num + (num < 0 ? -5 : 5)) / 10;
    if (raw > 32767)
        return 0x7FFFu;
    if (raw < -32768)
        return 0x8000u;
    return (uint16_t)(raw & 0xFFFF);
}

void ot_control_io_confirm(uint32_t id1_seq, uint16_t id1_raw, uint32_t *seen,
                           ot_control_in_t *in)
{
    // DO NOT compare the value with the held one here: that is ot_control_step()'s decision, and a
    // filter here ("only report ours") is exactly how a foreign ID 1 would stop un-confirming it.
    in->setpoint_confirmed = id1_seq != *seen;
    in->confirmed_dc       = in->setpoint_confirmed ? ot_control_io_f88_dc(id1_raw) : 0;
    *seen                  = id1_seq;
}

void ot_control_io_readback(ot_control_io_readback_t *rb, uint8_t data_id, ot_msg_type_t type,
                            uint16_t raw)
{
    // DO NOT take a WRITE-ACK "because ot_state does": it echoes the value WE wrote, and every
    // write would then agree with itself (the header says what that costs; the probe counts it).
    if (data_id != OT_CONTROL_IO_ID_TDHW_SET || type != OT_MSG_READ_ACK)
        return;
    rb->valid = true;
    rb->raw   = raw;
}

void ot_control_io_heard(ot_control_io_heard_t *h, uint8_t data_id, ot_msg_type_t type,
                         uint16_t raw)
{
    ot_control_io_readback(&h->rb, data_id, type, raw);
    // The FIRST refusal is kept: the task's one line names the value the boiler first refused.
    // DO NOT take UNKNOWN-DATAID here too: that is ot_state's mark, warned about apart, and it says
    // the boiler does not know ID 1 at all -- not that the value is outside its range.
    if (data_id == OT_CONTROL_IO_ID_TSET && type == OT_MSG_DATA_INVALID && !h->id1_invalid) {
        h->id1_invalid     = true;
        h->id1_invalid_raw = raw;
    }
}

void ot_control_io_readback_in(const ot_control_io_readback_t *rb, ot_control_in_t *in)
{
    in->dhw_readback_valid = rb->valid;
    in->dhw_readback_dc    = rb->valid ? ot_control_io_f88_dc(rb->raw) : 0;
}

void ot_control_io_send(ot_control_io_owed_t *owed, const ot_control_cfg_t *cfg,
                        const ot_control_out_t *out, ot_control_io_write_fn write)
{
    // Recorded BEFORE anything is offered: the ask is a one-shot, and the bus may refuse it.
    if (out->send_dhw_setpoint)
        owed->dhw = true;
    // 0 is the store's "nobody wrote it", never 0 degrees. DO NOT drop it on dhw_enable or on a
    // mode flip as well: the header says why ID 56 is written in both.
    if (!cfg->dhw_setpoint_set)
        owed->dhw = false;
    // ID 1 FIRST: CH waits on it. While it waits for its slot the offer below is refused
    // and ID 56 stays owed. DO NOT put ID 56 first "because it is a one-shot": owed, it no longer
    // is, and CH would wait a slot longer behind every DHW write.
    if (out->send_setpoint)
        (void)write(OT_CONTROL_IO_ID_TSET, ot_control_io_dc_f88(out->held_setpoint_dc));
    // The CURRENT target, not the one asked for: a newer one accepted meanwhile is what the boiler
    // gets, once. Discharged only when the bus took it.
    if (owed->dhw && write(OT_CONTROL_IO_ID_TDHW_SET, ot_control_io_dc_f88(cfg->dhw_setpoint_dc)))
        owed->dhw = false;
}

bool ot_control_io_dc(float celsius, int16_t *dc)
{
    // FIRST: no comparison with NaN is true, and lroundf(NaN) is unspecified.
    if (!isfinite(celsius))
        return false;
    const float scaled = celsius * 10.0f;
    // The cast below is undefined outside int16_t: a refusal, not a wrap.
    if (scaled <= -32768.5f || scaled >= 32767.5f)
        return false;
    *dc = (int16_t)lroundf(scaled);
    return true;
}

bool ot_control_io_minutes(float minutes, uint32_t *out)
{
    // !(>= 0) folds NaN into the refusal. 2^32 is the first float the cast cannot take.
    if (!(minutes >= 0.0f) || minutes >= 4294967296.0f || minutes != floorf(minutes))
        return false;
    *out = (uint32_t)minutes;
    return true;
}

static uint32_t check_of(uint32_t magic, uint32_t overdue_ms, uint32_t hh_ms)
{
    return ~(magic ^ overdue_ms ^ hh_ms);
}

void ot_control_io_rtc_store(ot_control_io_rtc_t *rtc, uint32_t overdue_ms, uint32_t hh_ms)
{
    rtc->magic      = OT_CONTROL_IO_RTC_MAGIC;
    rtc->overdue_ms = overdue_ms;
    rtc->hh_ms      = hh_ms;
    rtc->check      = check_of(OT_CONTROL_IO_RTC_MAGIC, overdue_ms, hh_ms);
}

// The resets a reboot loop is made of. BROWNOUT is one: a brown-out loop is the
// commonest reboot loop of an ESP32-C3, and the check word, not the reason, rejects RTC RAM the
// dip corrupted. DO NOT add POWERON "because the blob is intact anyway": power-on is not a loop.
// Anything unlisted -- EXT, DEEPSLEEP, USB, JTAG, a reason a later ESP-IDF invents -- starts from
// zero, which only costs a watchdog period.
static bool keeps_ram(int reason)
{
    switch (reason) {
    case OT_CONTROL_IO_RST_SW:
    case OT_CONTROL_IO_RST_PANIC:
    case OT_CONTROL_IO_RST_INT_WDT:
    case OT_CONTROL_IO_RST_TASK_WDT:
    case OT_CONTROL_IO_RST_WDT:
    case OT_CONTROL_IO_RST_BROWNOUT:
        return true;
    default:
        return false;
    }
}

void ot_control_io_restore(int reset_reason, const ot_control_io_rtc_t *rtc, bool hh_found,
                           uint16_t hh, ot_control_restore_t *out)
{
    memset(out, 0, sizeof *out);
    const bool intact = rtc != NULL && rtc->magic == OT_CONTROL_IO_RTC_MAGIC &&
                        rtc->check == check_of(rtc->magic, rtc->overdue_ms, rtc->hh_ms);
    // Both or neither: they were written together, and a blob that fails for one fails for both.
    out->overdue_valid = keeps_ram(reset_reason) && intact;
    out->overdue_ms    = out->overdue_valid ? rtc->overdue_ms : 0;
    out->hh_valid      = out->overdue_valid;
    out->hh_ms         = out->hh_valid ? rtc->hh_ms : 0;
    out->heat_hours    = hh_found ? hh : UINT16_MAX;
}

const char *ot_control_io_reset_name(int reset_reason)
{
    // Designated, so each name is tied to its number and not to a position.
    static const char *const NAMES[] = {
        [OT_CONTROL_IO_RST_UNKNOWN] = "unknown",   [OT_CONTROL_IO_RST_POWERON] = "poweron",
        [OT_CONTROL_IO_RST_EXT] = "ext",           [OT_CONTROL_IO_RST_SW] = "sw",
        [OT_CONTROL_IO_RST_PANIC] = "panic",       [OT_CONTROL_IO_RST_INT_WDT] = "int_wdt",
        [OT_CONTROL_IO_RST_TASK_WDT] = "task_wdt", [OT_CONTROL_IO_RST_WDT] = "wdt",
        [OT_CONTROL_IO_RST_DEEPSLEEP] = "deepsleep", [OT_CONTROL_IO_RST_BROWNOUT] = "brownout",
    };
    const int n = (int)(sizeof NAMES / sizeof NAMES[0]);
    return reset_reason >= 0 && reset_reason < n ? NAMES[reset_reason] : "other";
}

size_t ot_control_io_virtuals(const ot_control_cfg_t *cfg, const ot_control_out_t *out,
                              bool ch_command, bool id1_known, int16_t id1_dc,
                              ot_control_io_virtual_t v[OT_CONTROL_IO_VIRTUAL_MAX])
{
    const bool ha = cfg->mode == OT_CONTROL_MODE_HA;
    size_t     n  = 0;
    v[n++] = (ot_control_io_virtual_t){"ch_enable", ch_command ? 1.0f : 0.0f};
    v[n++] = (ot_control_io_virtual_t){"dhw_enable", cfg->dhw_enable ? 1.0f : 0.0f};
    v[n++] = (ot_control_io_virtual_t){"heating_season", cfg->heating_season ? 1.0f : 0.0f};
    v[n++] = (ot_control_io_virtual_t){"control_mode",
                                       (float)(ha ? OT_CONTROL_MODE_HA : OT_CONTROL_MODE_LOCAL)};
    v[n++] = (ot_control_io_virtual_t){"control_state", (float)out->state};
    v[n++] = (ot_control_io_virtual_t){"ch_enable_effective",
                                       (out->status_high & OT_STATUS_CH_ENABLE) ? 1.0f : 0.0f};
    v[n++] = (ot_control_io_virtual_t){"failsafe_count", (float)out->failsafe_count};
    v[n++] = (ot_control_io_virtual_t){"last_failsafe_duration_s",
                                       (float)out->last_failsafe_duration_s};
    if (id1_known)
        v[n++] = (ot_control_io_virtual_t){"ch_setpoint_effective", (float)id1_dc / 10.0f};
    return n;
}

void ot_control_io_document(const ot_control_cfg_t *cfg, const ot_control_out_t *out,
                            ot_api_control_t *doc)
{
    memset(doc, 0, sizeof *doc);
    doc->mode   = cfg->mode == OT_CONTROL_MODE_HA ? OT_CONTROL_MODE_HA : OT_CONTROL_MODE_LOCAL;
    doc->state  = out->state;
    doc->reason = out->reason;
    doc->cause  = out->cause;
    doc->heating_season           = cfg->heating_season;
    doc->status_high              = out->status_high;
    doc->held_setpoint_dc         = out->held_setpoint_dc;
    doc->dhw_enable               = cfg->dhw_enable;
    doc->dhw_setpoint_set         = cfg->dhw_setpoint_set;
    doc->dhw_setpoint_dc          = cfg->dhw_setpoint_dc;
    doc->failsafe_count           = out->failsafe_count;
    doc->last_failsafe_duration_s = out->last_failsafe_duration_s;
    doc->watchdog_overdue_s       = out->overdue_ms / 1000u;
}
