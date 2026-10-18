// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Host suite for the ONE pure piece of ot_thermostat's room-source glue (B4, design
// "ot_thermostat -- cut first, then init from config"): the config->ot_room mapping,
// ot_thermostat_room_build_cfg(). Everything else in components/ot_thermostat is FreeRTOS and the
// bus, so it is not host-built. Include ONLY ot_thermostat_room_cfg.h -- NOT
// ot_thermostat_internal.h, which pulls in ot_bus.h and, through it, ot_master/ot_net/ot_api
// (ot_thermostat_room_cfg.h says why).
#include <string.h>

#include <unity.h>

#include "ot_thermostat_room_cfg.h"

void setUp(void) {}
void tearDown(void) {}

// The DS18B20 shield is slot 0 in every case -- AMBIENT, not ha_forwarded, 60000 ms -- whatever
// the MQTT slot's configuration is (design: "slot 0 is always the DS18B20 shield").
static void assert_slot0_is_the_shield(const ot_room_cfg_t *out)
{
    TEST_ASSERT_EQUAL(OT_ROOM_AMBIENT, out->cfg[0].role);
    TEST_ASSERT_FALSE(out->cfg[0].ha_forwarded);
    TEST_ASSERT_EQUAL_UINT32(60000, out->cfg[0].stale_after_ms);
}

static void test_disabled_yields_one_slot(void)
{
    // role/stale/ha_forwarded are set to non-default values on purpose: disabled must ignore
    // them entirely, not merely leave slot 1 unconfigured.
    ot_config_room_mqtt_t m;
    m.enable       = false;
    m.role         = 1;
    m.stale_s      = 900;
    m.ha_forwarded = true;

    ot_room_cfg_t out;
    memset(&out, 0xAA, sizeof out);   // catch an implementation that forgets to set count
    ot_thermostat_room_build_cfg(&m, &out);

    TEST_ASSERT_EQUAL(1, out.count);
    assert_slot0_is_the_shield(&out);
}

static void test_enabled_role_room(void)
{
    ot_config_room_mqtt_t m;
    m.enable       = true;
    m.role         = 1;   // OT_ROOM_ROOM
    m.stale_s      = 120;
    m.ha_forwarded = true;

    ot_room_cfg_t out;
    ot_thermostat_room_build_cfg(&m, &out);

    TEST_ASSERT_EQUAL(2, out.count);
    assert_slot0_is_the_shield(&out);
    TEST_ASSERT_EQUAL(OT_ROOM_ROOM, out.cfg[1].role);
    TEST_ASSERT_TRUE(out.cfg[1].ha_forwarded);
    TEST_ASSERT_EQUAL_UINT32(120000, out.cfg[1].stale_after_ms);   // seconds -> ms
}

static void test_enabled_role_ambient(void)
{
    ot_config_room_mqtt_t m;
    m.enable       = true;
    m.role         = 0;   // OT_ROOM_AMBIENT
    m.stale_s      = 900;
    m.ha_forwarded = false;

    ot_room_cfg_t out;
    ot_thermostat_room_build_cfg(&m, &out);

    TEST_ASSERT_EQUAL(2, out.count);
    assert_slot0_is_the_shield(&out);
    TEST_ASSERT_EQUAL(OT_ROOM_AMBIENT, out.cfg[1].role);
    TEST_ASSERT_FALSE(out.cfg[1].ha_forwarded);
    TEST_ASSERT_EQUAL_UINT32(900000, out.cfg[1].stale_after_ms);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_disabled_yields_one_slot);
    RUN_TEST(test_enabled_role_room);
    RUN_TEST(test_enabled_role_ambient);
    return UNITY_END();
}
