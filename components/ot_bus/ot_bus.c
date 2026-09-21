// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_bus.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ot_bus_sched.h"
#include "ot_bus_track.h"
#include "ot_codec.h"
#include "ot_master.h"
#include "ot_observe.h"
#include "ot_state.h"

static const char *TAG = "ot_bus";

// How many failed conversations in a row count as losing the boiler. Five is five
// seconds, that is, exactly the threshold beyond which the OpenTherm slave starts
// treating the master as short-circuited. Declaring a loss earlier is dishonest, later
// is useless.
#define LOST_AFTER 5u

// The state is THREE-VALUED, and that is not pedantry.
//
// The first edition announced a loss on the condition `boiler_answering && fails >= N`,
// and `boiler_answering` is false at start. So a boiler that had NEVER answered -- the
// most common case on the first connection -- produced not a single line, and "the bus
// works, the boiler is silent" became indistinguishable from "the bus did not start".
// Found by running it: thirty seconds without a single line from this component.
typedef enum { LINK_UNKNOWN = 0, LINK_ANSWERING, LINK_SILENT } link_state_t;

static link_state_t       s_link;
static ot_observe_t       s_observed;
static ot_bus_sched_t     s_sched;
static ot_bus_response_cb s_cb;
static void              *s_ctx;
static ot_bus_stats_t     s_stats;
static ot_bus_track_t     s_track;   // beside s_sched, under s_mux: every write goes through it

// Guards the scheduler s_sched and the tracker s_track as a whole -- the write queue, the ID 0
// status byte, the scan, the poll ring and the write bookkeeping: other tasks write, set the
// byte and start or stop a scan while the bus task steps the scheduler.
// A spinlock, not a mutex: every critical section is a call to a pure function a dozen
// lines long, and blocking the scheduler over them is cheaper than creating an object.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t s_test_until_ms;
static volatile uint32_t s_test_half_ms;
static volatile bool     s_test_on;

// How the master introduces itself to the boiler. Three writes that go out ONCE at
// start.
//
// Why once and not periodically: the boiler remembers the master's introduction for the
// whole session -- repeating it would mean spending, over and over, a meaningful slot
// of the polling ring on a message the slave has already accepted and will not forget
// until a power cycle. A slot costs about two seconds out of every turn (one
// conversation per second, status ID 0 takes every other one), and there is nothing to
// justify giving it away for a fact that has not changed. The introduction changes only
// together with the firmware, and that arrives with a reboot, that is, with a new start
// of the bus.
//
// DO NOT move this into the periodic ring "in case the boiler rebooted": a boiler that
// lost power loses the whole session too, and restoring it is a separate conversation
// that v1 does not have.
static const struct {
    uint8_t  data_id;
    uint16_t value;
} IDENTITY[] = {
    // ID 2, Master configuration / MemberID. The high byte is the master flags, the low
    // one the MemberID. Both are zeros: we have no flags, and we are not a member of the
    // OpenTherm association and have no right to assign ourselves someone else's number.
    { 2, 0x0000 },
    // ID 124, the version of the OpenTherm specification the master follows. f8.8, the
    // value is substituted in ot_bus_start(): 2.2 -- an initializer with a function call
    // is impossible here, and the magic constant 0x0233 would demand mental arithmetic
    // from every reader.
    { 124, 0x0000 },
    // ID 126, the master's product version: type 1 in the high byte, version 1 in the
    // low one. Our own numbering, checked against nothing -- the boiler needs it only as
    // a discriminator, should another device of ours ever be connected to it.
    { 126, 0x0101 },
};
#define IDENTITY_COUNT ((uint8_t)(sizeof IDENTITY / sizeof IDENTITY[0]))

static uint16_t  s_identity_124;      // ot_codec_float_to_f88(2.2f), computed at start
static uint8_t   s_identity_step;     // how many writes have ALREADY been queued

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

bool ot_bus_line_test_active(void) { return s_test_on; }

bool ot_bus_line_test(uint32_t duration_ms, uint32_t half_period_ms)
{
    if (s_test_on) return false;
    if (duration_ms == 0u || duration_ms > OT_BUS_TEST_MAX_MS) return false;
    if (half_period_ms < OT_BUS_TEST_HALF_MIN_MS || half_period_ms > OT_BUS_TEST_HALF_MAX_MS)
        return false;
    s_test_half_ms  = half_period_ms;
    s_test_until_ms = now_ms() + duration_ms;
    s_test_on       = true;
    ESP_LOGW(TAG, "line test for %u ms, half period %u ms -- no frames meanwhile",
             (unsigned)duration_ms, (unsigned)half_period_ms);
    return true;
}

void ot_bus_write(uint8_t data_id, uint16_t value)
{
    taskENTER_CRITICAL(&s_mux);
    ot_bus_track_write(&s_track, &s_sched, data_id, value);
    taskEXIT_CRITICAL(&s_mux);
}

bool ot_bus_write_if_idle(uint8_t data_id, uint16_t value)
{
    taskENTER_CRITICAL(&s_mux);
    const bool queued = ot_bus_track_write_if_idle(&s_track, &s_sched, data_id, value);
    taskEXIT_CRITICAL(&s_mux);
    return queued;
}

// No log here, for the reason ot_bus_set_status() gives: the thermostat asks every second.
void ot_bus_write_state(ot_bus_write_state_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    out->id1_seq = s_track.id1_seq;
    out->id1_raw = s_track.id1_raw;
    taskEXIT_CRITICAL(&s_mux);
}

// The same critical section as the write queue, and for the same reason: the field is
// written by another task while the bus task reads it inside ot_bus_sched_step(). A byte
// would very likely be atomic anyway, but "very likely" is not a contract, and the section
// costs a dozen instructions.
//
// DO NOT log here. This is called by the control loop on every decision, and a line per
// second would push everything else out of the ring buffer of GET /api/log.
void ot_bus_set_status(uint8_t high)
{
    taskENTER_CRITICAL(&s_mux);
    ot_bus_sched_set_status(&s_sched, high);
    taskEXIT_CRITICAL(&s_mux);
}

// The copy is taken INSIDE s_mux, the same lock bus_task publishes s_stats under:
// bus_task once wrote the counters unlocked, so this reader (the HTTP status handler) could copy a
// snapshot with, say, boiler_answering already cleared but consecutive_fail not yet stepped. The
// struct is a few words; copying it with interrupts off is cheaper than a torn diagnostic.
void ot_bus_stats(ot_bus_stats_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    ot_bus_sched_scan_progress(&s_sched, &s_stats.scan_done, &s_stats.scan_total);
    *out = s_stats;
    taskEXIT_CRITICAL(&s_mux);
}

void ot_bus_scan(uint8_t from, uint8_t to)
{
    taskENTER_CRITICAL(&s_mux);
    ot_bus_sched_scan(&s_sched, from, to);
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "scanning data-ids %u..%u", (unsigned)from, (unsigned)to);
}

void ot_bus_scan_stop(void)
{
    taskENTER_CRITICAL(&s_mux);
    ot_bus_sched_scan_stop(&s_sched);
    taskEXIT_CRITICAL(&s_mux);
}

const ot_observe_t *ot_bus_observed(void) { return &s_observed; }

static void bus_task(void *arg)
{
    (void)arg;
    for (;;) {
        // The line test runs instead of conversations and ends by itself. Leaving the
        // mode is unconditional on time: a stuck flag would leave the bus silent
        // forever, and that is exactly what the boiler turns into a heat demand.
        if (s_test_on) {
            if ((int32_t)(now_ms() - s_test_until_ms) >= 0) {
                s_test_on = false;
                ot_master_drive_line(false);
                ESP_LOGI(TAG, "line test finished, polling resumed");
                continue;
            }
            static bool level;
            level = !level;
            ot_master_drive_line(level);
            vTaskDelay(pdMS_TO_TICKS(s_test_half_ms));
            continue;
        }

        ot_bus_step_t step;
        uint32_t      gen;
        taskENTER_CRITICAL(&s_mux);
        // The write queue is sized for ONE (ot_bus_sched.h), so the introduction goes
        // out one write at a time: the next one is queued only when the previous has
        // left the queue.
        //
        // The counter advances AT THE MOMENT OF QUEUEING, not on the fact of the
        // boiler's reply, and that is load-bearing: ot_bus_sched_done() clears
        // write_pending regardless of the outcome of the conversation, so a silent
        // boiler does not hold the counter. If a step counted only on a reply, a
        // non-answering boiler would make the bus queue the same write forever -- the
        // meaningful slot of the polling ring would never be freed, while the 5 s silence
        // rule requires us to talk, not to re-ask the same thing.
        //
        // Someone else's write (ot_bus_write from HTTP or from the control loop) is not
        // overwritten: our own introduction will wait for the next turn, whereas a
        // setpoint must not be delayed. The reverse -- someone else's write evicting our
        // introduction -- is possible and acceptable: that is exactly the eviction rule
        // the queue-of-one exists for.
        if (s_identity_step < IDENTITY_COUNT && !s_sched.write_pending) {
            const uint8_t  i  = s_identity_step++;
            const uint16_t v  = (IDENTITY[i].data_id == 124) ? s_identity_124
                                                             : IDENTITY[i].value;
            ot_bus_track_write(&s_track, &s_sched, IDENTITY[i].data_id, v);
        }
        step = ot_bus_sched_step(&s_sched, now_ms());
        // In THIS section, with the step: ot_bus_track_done() tells by it whether a write was
        // queued while the exchange was on the wire (ot_bus_track.h, "the race this closes").
        gen  = s_track.gen;
        taskEXIT_CRITICAL(&s_mux);

        if (step.verb == OT_BUS_WAIT) {
            // At least one tick: a delay of zero ticks does not give up the processor,
            // and this task has no right to spin idly at a priority above idle.
            TickType_t d = pdMS_TO_TICKS(step.delay_ms);
            vTaskDelay(d == 0 ? 1 : d);
            continue;
        }

        if (step.overdue) {
            // Missing the 1150 ms deadline means something was holding the task up.
            // Silently catching up is not allowed: the next such case must be
            // connectable to whatever was going on in the system.
            ESP_LOGW(TAG, "deadline missed; conversation is late");
        }

        const ot_frame_t req = {
            .type       = step.is_write ? OT_MSG_WRITE_DATA : OT_MSG_READ_DATA,
            .data_id    = step.data_id,
            .data_value = step.value,
        };
        ot_frame_t resp;

        const uint32_t start = now_ms();
        const ot_exchange_result_t r = ot_master_exchange(&req, &resp);
        const uint32_t end = now_ms();
        const bool     ok  = r == OT_EXCHANGE_OK;

        // ONE critical section per cycle finishes the exchange through the tracker AND publishes
        // every s_stats field this cycle touches. bus_task once wrote these counters
        // unlocked while ot_bus_stats() copied them unlocked, so the HTTP status handler could read
        // a half-updated snapshot. s_link and every ESP_LOG stay OUTSIDE the section: s_link is this
        // task's own, and a log with interrupts off is forbidden; consecutive_fail is captured here
        // for the log below. Answered = any parsed reply (ot_bus_track_done() says why only that
        // counts as sent). cycles is stepped here, not before the exchange: the count is diagnostics
        // and its exact moment within a cycle does not matter, only that no reader sees it torn.
        uint32_t cfail = 0;
        taskENTER_CRITICAL(&s_mux);
        ot_bus_track_done(&s_track, &s_sched, &step, gen, r == OT_EXCHANGE_OK, start, end);
        s_stats.cycles++;
        if (step.overdue) s_stats.overdue++;
        if (ok) {
            s_stats.ok++;
            s_stats.consecutive_fail = 0;
            s_stats.boiler_answering = true;
        } else {
            s_stats.failed++;
            if (s_stats.consecutive_fail < UINT32_MAX) s_stats.consecutive_fail++;
            cfail = s_stats.consecutive_fail;
            // Idempotent with the original transition: once LOST_AFTER is reached the flag stays
            // down, so clearing it every fail cycle at or past the threshold is the same end state.
            if (cfail >= LOST_AFTER) s_stats.boiler_answering = false;
        }
        taskEXIT_CRITICAL(&s_mux);

        if (ok) {
            if (s_link != LINK_ANSWERING) {
                ESP_LOGI(TAG, "boiler is answering");
                s_link = LINK_ANSWERING;
            }
            // ANY parsed reply is recorded, including UNKNOWN-DATAID: "I do not have
            // that" is also an answer to the question "what can the boiler do", and half
            // the diagnostic value is in exactly that.
            ot_observe_record(&s_observed, resp.data_id, resp.type, resp.data_value, end);
            if (s_cb != NULL) s_cb(resp.data_id, resp.type, resp.data_value, s_ctx);

            // The model learned about the reply before us and decided whether the
            // identifier is supported. Asking an unsupported one further means spending
            // a ring slot once a minute on a knowingly empty answer.
            //
            // The question is asked OUTSIDE the critical section: ot_state takes its own
            // lock, and taking a mutex inside taskENTER_CRITICAL is not allowed --
            // interrupts are off. Only ot_bus_sched_disable() goes inside the section:
            // the scheduler is touched by other tasks, and it is protected by the same
            // s_mux as the write queue.
            if (ot_state_is_unsupported(resp.data_id)) {
                taskENTER_CRITICAL(&s_mux);
                const uint8_t before = s_sched.poll_count;
                ot_bus_sched_disable(&s_sched, resp.data_id);
                const uint8_t after = s_sched.poll_count;
                taskEXIT_CRITICAL(&s_mux);
                // The line is printed once per identifier, not on every reply from it.
                // The mark lives until a reboot, while a Data-ID sweep asks a marked ID
                // again -- without this check the log would fill with a repeat on every
                // scan. The sign is whether the ring got shorter: no own list of marked
                // ones is kept here, it would be a copy of a fact that already exists
                // both in ot_state and in the scheduler.
                if (after < before)
                    ESP_LOGI(TAG, "ID %3u is not supported by the boiler; removed from the poll ring",
                             (unsigned)resp.data_id);
            }
        } else {
            if (s_link != LINK_SILENT && cfail >= LOST_AFTER) {
                // Said both when the boiler answered before and when it never answered
                // at all.
                ESP_LOGW(TAG, "boiler is not answering (%s) after %u tries; polling continues",
                         ot_exchange_result_str(r), (unsigned)cfail);
                s_link = LINK_SILENT;
            }
        }
        // And no exit from the loop. See the header.
    }
}

esp_err_t ot_bus_start(const uint8_t *poll, uint8_t count, ot_bus_response_cb cb, void *ctx)
{
    ot_bus_sched_init(&s_sched, poll, count);
    memset(&s_track, 0, sizeof s_track);
    ot_observe_reset(&s_observed);
    s_link = LINK_UNKNOWN;
    s_cb  = cb;
    s_ctx = ctx;
    memset(&s_stats, 0, sizeof s_stats);

    // 2.2 in f8.8 is 563, that is, 0x0233. It is computed by the codec rather than
    // written in as a constant: the version reads as "2.2" exactly where it is named.
    s_identity_124  = ot_codec_float_to_f88(2.2f);
    s_identity_step = 0;

    // A priority above the ordinary application level: this task being late is a
    // violation of the OpenTherm timing, not a sluggish interface. 4 KiB of stack: a conversation is
    // not recursive and allocates nothing, the headroom is for ESP_LOG.
    const BaseType_t ok = xTaskCreate(bus_task, "ot_bus", 4096, NULL, 10, NULL);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    // Said out loud that the bus has started: otherwise its silence with no boiler
    // present is indistinguishable from the task not having been created.
    ESP_LOGI(TAG, "bus task started, %u ids in the poll ring", (unsigned)count);
    return ESP_OK;
}
