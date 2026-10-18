// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

// PRIVATE to components/ot_control: the seams between its sources. Not under include/, so no
// other component and no suite can reach these -- the suites test the public contract only, and
// a suite that called otc_failsafe_ch() directly would pin a layout instead of a behaviour.

#include "ot_control.h"

#define OTC_HOUR_MS 3600000u

// ot_control_failsafe.c
uint32_t otc_add_sat(uint32_t a, uint32_t b);
bool     otc_advance(ot_control_t *c, const ot_control_cfg_t *cfg, uint32_t dt_ms);
bool     otc_failsafe_ch(ot_control_t *c, const ot_control_cfg_t *cfg,
                         const ot_control_in_t *in, ot_control_reason_t *reason);

// ot_control.c
int16_t  otc_bound(const ot_control_cfg_t *cfg, int32_t dc);
void     otc_forget_ha(ot_control_t *c, const ot_control_cfg_t *cfg);

// ot_control_boost.c
void     otc_boost_expire(ot_control_t *c, uint32_t now_ms);
