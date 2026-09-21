// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <string.h>
#include <unity.h>

#include "ot_ticket.h"

static ot_ticket_table_t t;

void setUp(void) { ot_ticket_reset(&t); }
void tearDown(void) {}

static void test_a_fresh_table_redeems_nothing(void)
{
    TEST_ASSERT_FALSE(ot_ticket_redeem(&t, "0123456789abcdef0123456789abcdef", 0));
}

static void test_an_issued_ticket_redeems_once(void)
{
    TEST_ASSERT_TRUE(ot_ticket_issue(&t, "0123456789abcdef0123456789abcdef", 1000));
    TEST_ASSERT_TRUE(ot_ticket_redeem(&t, "0123456789abcdef0123456789abcdef", 1500));
    // A second time -- no. The ticket is single-use: one intercepted from a proxy server's
    // log or from the browser history must not open a second socket.
    TEST_ASSERT_FALSE(ot_ticket_redeem(&t, "0123456789abcdef0123456789abcdef", 1600));
}

static void test_an_expired_ticket_is_refused(void)
{
    TEST_ASSERT_TRUE(ot_ticket_issue(&t, "0123456789abcdef0123456789abcdef", 1000));
    TEST_ASSERT_FALSE(ot_ticket_redeem(&t, "0123456789abcdef0123456789abcdef",
                                       1000 + OT_TICKET_TTL_MS + 1));
}

static void test_a_ticket_at_the_edge_of_its_life_still_works(void)
{
    TEST_ASSERT_TRUE(ot_ticket_issue(&t, "0123456789abcdef0123456789abcdef", 1000));
    TEST_ASSERT_TRUE(ot_ticket_redeem(&t, "0123456789abcdef0123456789abcdef",
                                      1000 + OT_TICKET_TTL_MS));
}

static void test_a_wrong_ticket_is_refused(void)
{
    TEST_ASSERT_TRUE(ot_ticket_issue(&t, "0123456789abcdef0123456789abcdef", 1000));
    TEST_ASSERT_FALSE(ot_ticket_redeem(&t, "ffffffffffffffffffffffffffffffff", 1100));
}

static void test_a_short_ticket_is_refused(void)
{
    TEST_ASSERT_FALSE(ot_ticket_issue(&t, "too-short", 1000));
    TEST_ASSERT_FALSE(ot_ticket_redeem(&t, "too-short", 1000));
    TEST_ASSERT_FALSE(ot_ticket_redeem(&t, NULL, 1000));
}

// This many client sessions the HTTP server accepts: OT_HTTP_MAX_CLIENTS,
// components/ot_http/ot_http_internal.h. Deliberately as a LITERAL -- ot_ticket is pure and
// knows nothing about the server, and the test exists precisely so that a divergence is
// visible: if the server grows and the table does not, tabs will start taking tickets away
// from one another.
#define SERVER_CLIENT_SESSIONS 7

static void fill(char *dst, char c)
{
    memset(dst, c, OT_TICKET_LEN);
    dst[OT_TICKET_LEN] = '\0';
}

// This many tickets may be in play at once -- one per socket the server agrees to accept.
// None of them has the right to evict another.
static void test_a_ticket_per_socket_the_server_accepts_survives(void)
{
    TEST_ASSERT_TRUE(OT_TICKET_SLOTS >= SERVER_CLIENT_SESSIONS);

    char v[SERVER_CLIENT_SESSIONS][OT_TICKET_LEN + 1];
    for (int i = 0; i < SERVER_CLIENT_SESSIONS; i++) {
        fill(v[i], (char)('a' + i));
        TEST_ASSERT_TRUE(ot_ticket_issue(&t, v[i], (uint32_t)(1000 + i)));
    }
    for (int i = 0; i < SERVER_CLIENT_SESSIONS; i++)
        TEST_ASSERT_TRUE(ot_ticket_redeem(&t, v[i], 1100));
}

// An abandoned ticket -- the tab was closed between the REST response and the handshake --
// holds its slot until the end of the TTL. That is exactly why there are twice as many slots
// as sockets: a full turnover of tabs has no right to take the entrance away from whoever is
// opening the page right now.
static void test_an_abandoned_ticket_does_not_cost_a_live_one(void)
{
    char abandoned[SERVER_CLIENT_SESSIONS][OT_TICKET_LEN + 1];
    for (int i = 0; i < SERVER_CLIENT_SESSIONS; i++) {
        fill(abandoned[i], (char)('a' + i));
        TEST_ASSERT_TRUE(ot_ticket_issue(&t, abandoned[i], 1000));
    }

    char live[SERVER_CLIENT_SESSIONS][OT_TICKET_LEN + 1];
    for (int i = 0; i < SERVER_CLIENT_SESSIONS; i++) {
        fill(live[i], (char)('A' + i));
        TEST_ASSERT_TRUE(ot_ticket_issue(&t, live[i], 2000));
    }
    for (int i = 0; i < SERVER_CLIENT_SESSIONS; i++)
        TEST_ASSERT_TRUE(ot_ticket_redeem(&t, live[i], 2100));
}

// The table is finite after all. When there is no free slot, the room is freed by the
// OLDEST ticket: it has gone unspent the longest, that is, it is the most likely to have been
// abandoned. A refusal to issue would hold an abandoned ticket for the whole thirty seconds
// and for exactly that long would keep the owner off the page.
static void test_a_full_table_evicts_the_oldest(void)
{
    char v[OT_TICKET_SLOTS + 1][OT_TICKET_LEN + 1];
    for (int i = 0; i <= OT_TICKET_SLOTS; i++) {
        memset(v[i], 'a' + i, OT_TICKET_LEN);
        v[i][OT_TICKET_LEN] = '\0';
        TEST_ASSERT_TRUE(ot_ticket_issue(&t, v[i], (uint32_t)(1000 + i)));
    }
    // The oldest is evicted, the newest is alive.
    TEST_ASSERT_FALSE(ot_ticket_redeem(&t, v[0], 2000));
    TEST_ASSERT_TRUE(ot_ticket_redeem(&t, v[OT_TICKET_SLOTS], 2000));
}

// An expired slot is a free slot: it has no right to take up room and evict a live ticket.
static void test_an_expired_slot_is_reused_before_a_live_one(void)
{
    char old_v[OT_TICKET_LEN + 1];
    memset(old_v, 'x', OT_TICKET_LEN);
    old_v[OT_TICKET_LEN] = '\0';
    TEST_ASSERT_TRUE(ot_ticket_issue(&t, old_v, 1000));

    char live[OT_TICKET_LEN + 1];
    memset(live, 'y', OT_TICKET_LEN);
    live[OT_TICKET_LEN] = '\0';
    const uint32_t later = 1000 + OT_TICKET_TTL_MS + 1;
    TEST_ASSERT_TRUE(ot_ticket_issue(&t, live, later));

    char more[OT_TICKET_LEN + 1];
    memset(more, 'z', OT_TICKET_LEN);
    more[OT_TICKET_LEN] = '\0';
    TEST_ASSERT_TRUE(ot_ticket_issue(&t, more, later));

    TEST_ASSERT_TRUE(ot_ticket_redeem(&t, live, later));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_table_redeems_nothing);
    RUN_TEST(test_an_issued_ticket_redeems_once);
    RUN_TEST(test_an_expired_ticket_is_refused);
    RUN_TEST(test_a_ticket_at_the_edge_of_its_life_still_works);
    RUN_TEST(test_a_wrong_ticket_is_refused);
    RUN_TEST(test_a_short_ticket_is_refused);
    RUN_TEST(test_a_ticket_per_socket_the_server_accepts_survives);
    RUN_TEST(test_an_abandoned_ticket_does_not_cost_a_live_one);
    RUN_TEST(test_a_full_table_evicts_the_oldest);
    RUN_TEST(test_an_expired_slot_is_reused_before_a_live_one);
    return UNITY_END();
}
