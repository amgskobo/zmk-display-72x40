/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * display_72x40_power.c on a peripheral that reports whether it is on
 * external power: a poll that reports changes, and a few repeats after the
 * split link comes up so the central learns the state it missed.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ARG_UNUSED(x) ((void)(x))
#define LOG_DBG(fmt, ...) ((void)sizeof(printf(fmt, ##__VA_ARGS__)))
#define K_MSEC(ms) ((k_timeout_t){(ms)})
#define K_NO_WAIT ((k_timeout_t){0})
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_RELAY_EVENT_SOURCE_SELF 0xFF
#define NRF_POWER (&power_peripheral)
#define CONFIG_ZMK_DISPLAY_72X40_POWER_POLL_MS 2000
#define D72X40_POWER_SYNC_ATTEMPTS 4
#define D72X40_POWER_SYNC_FIRST_MS 1000
#define D72X40_POWER_SYNC_RETRY_MS 1500

typedef long atomic_t;
typedef long atomic_val_t;
typedef struct { int64_t ms; } k_timeout_t;
typedef struct { int kind; } zmk_event_t;
struct k_work { void (*handler)(struct k_work *work); };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct k_work_q { int unused; };
struct nrf_power { bool vbus; };
struct zmk_display_72x40_power_state_changed { uint8_t source; bool powered; };
struct zmk_split_peripheral_status_changed { zmk_event_t header; bool connected; };

static struct k_work_q lowprio_queue;
static struct nrf_power power_peripheral;
static int reports;
static struct zmk_display_72x40_power_state_changed last_report;

static atomic_val_t atomic_get(const atomic_t *target) { return *target; }
static atomic_val_t atomic_set(atomic_t *target, atomic_val_t value) {
    const atomic_val_t old = *target;
    *target = value;
    return old;
}
/* Zephyr's returns the value from before the decrement. */
static atomic_val_t atomic_dec(atomic_t *target) { return (*target)--; }
static struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio_queue; }
static int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                                     k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    if (work->scheduled) {
        return 0;
    }
    work->scheduled = true;
    work->delay_ms = delay.ms;
    return 1;
}
static int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                                       k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    work->scheduled = true;
    work->delay_ms = delay.ms;
    return 1;
}
static int k_work_cancel_delayable(struct k_work_delayable *work) {
    work->scheduled = false;
    return 0;
}
static bool nrf_power_usbregstatus_vbusdet_get(const struct nrf_power *power) {
    return power->vbus;
}
static int raise_zmk_display_72x40_power_state_changed(
    struct zmk_display_72x40_power_state_changed event) {
    last_report = event;
    reports++;
    return 0;
}
static const struct zmk_split_peripheral_status_changed *
as_zmk_split_peripheral_status_changed(const zmk_event_t *eh) {
    return eh->kind == 1 ? (const void *)eh : NULL;
}

static atomic_t d72x40_power_state;
static atomic_t d72x40_power_known;
static atomic_t d72x40_power_sync_left;
static void d72x40_power_work_handler(struct k_work *work);
static void d72x40_power_sync_handler(struct k_work *work);
static struct k_work_delayable d72x40_power_work = {.work = {.handler = d72x40_power_work_handler}};
static struct k_work_delayable d72x40_power_sync_work = {
    .work = {.handler = d72x40_power_sync_handler}};

/* DRIVER_FUNCTIONS */

static void run(struct k_work_delayable *work) {
    work->scheduled = false;
    work->work.handler(&work->work);
}

static void link(bool connected) {
    const struct zmk_split_peripheral_status_changed ev = {{1}, connected};
    assert(d72x40_power_listener(&ev.header) == ZMK_EV_EVENT_BUBBLE);
}

int main(void) {
    /* A link before the first poll has nothing to repeat yet. */
    link(true);
    run(&d72x40_power_sync_work);
    assert(reports == 0);

    /* The first poll always reports; later ones only on a change. */
    assert(d72x40_power_init() == 0 && d72x40_power_work.scheduled);
    run(&d72x40_power_work);
    assert(reports == 1 && !last_report.powered &&
           last_report.source == ZMK_RELAY_EVENT_SOURCE_SELF);
    assert(d72x40_power_work.delay_ms == CONFIG_ZMK_DISPLAY_72X40_POWER_POLL_MS);
    run(&d72x40_power_work);
    assert(reports == 1);
    power_peripheral.vbus = true;
    run(&d72x40_power_work);
    assert(reports == 2 && last_report.powered);

    /* A new link repeats the state a few times, then stops. */
    link(true);
    assert(d72x40_power_sync_work.delay_ms == D72X40_POWER_SYNC_FIRST_MS);
    for (int attempt = 1; attempt <= D72X40_POWER_SYNC_ATTEMPTS; attempt++) {
        run(&d72x40_power_sync_work);
        assert(reports == 2 + attempt && last_report.powered);
        assert(d72x40_power_sync_work.scheduled == (attempt < D72X40_POWER_SYNC_ATTEMPTS));
    }
    run(&d72x40_power_sync_work); /* spent */
    assert(reports == 2 + D72X40_POWER_SYNC_ATTEMPTS && d72x40_power_sync_left == 0);

    /* A dropped link stops the repeats; another event is not a link. */
    link(true);
    link(false);
    assert(!d72x40_power_sync_work.scheduled && d72x40_power_sync_left == 0);
    const zmk_event_t other = {2};
    assert(d72x40_power_listener(&other) == ZMK_EV_EVENT_BUBBLE);
    puts("display power reporter: PASS");
    return 0;
}
