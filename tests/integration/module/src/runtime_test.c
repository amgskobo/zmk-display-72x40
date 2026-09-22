/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for zmk-display-72x40, run inside a native_sim build. The panel
 * is a recording display, so each test checks both what the panel ends up
 * showing and what it took to get there.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <zmk/activity.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>
#include <zmk/keymap.h>

#include <zmk-display-72x40/display_72x40.h>
#include <zmk-display-72x40/display_72x40_core.h>

#include "recording_display.h"

/* Layer ids of native_sim.keymap. */
#define NUM_LAYER 1
#define HIDDEN_LAYER 2
#define UNNAMED_LAYER 3

/* Past CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS, with room for the pass. */
#define SETTLE_MS (CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS + 40)
#define SYNC_WINDOW_MS CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS

static const struct device *const sync_pad = DEVICE_DT_GET(DT_NODELABEL(sync_pad));

static uint8_t expected_buf[RECORDING_PAGES * RECORDING_WIDTH];
static const struct d72x40_canvas expected = {
    .buf = expected_buf,
    .width = RECORDING_WIDTH,
    .pages = RECORDING_PAGES,
};

static struct recording_display snapshot(void) {
    struct recording_display copy = *recording_display_lock();

    recording_display_unlock();
    return copy;
}

/*
 * This build has no USB, BLE, battery, split, indicators or Studio lock, so
 * the layer is all that varies: the host line is a lone cross and the lines
 * below it stay empty.
 */
static bool frame_is(const char *layer) {
    struct d72x40_status st = {
        .transport = D72X40_TRANSPORT_NONE,
        .link = D72X40_LINK_DOWN,
        .battery = D72X40_BATTERY_HIDDEN,
        .peer_battery = D72X40_BATTERY_HIDDEN,
    };

    d72x40_append(st.layer, sizeof(st.layer), layer);
    d72x40_render(&expected, &st);

    struct recording_display *rec = recording_display_lock();
    bool same = memcmp(rec->frame, expected_buf, sizeof(expected_buf)) == 0;

    recording_display_unlock();
    return same;
}

static void raise_activity(enum zmk_activity_state state) {
    raise_zmk_activity_state_changed((struct zmk_activity_state_changed){.state = state});
}

static void report_sync(void) {
    __ASSERT_NO_MSG(input_report(sync_pad, INPUT_EV_REL, INPUT_REL_X, 1, true, K_FOREVER) == 0);
}

static void test_boot_clears_then_draws(void) {
    struct recording_display rec = snapshot();

    __ASSERT(rec.malformed == 0, "malformed writes");
    __ASSERT(rec.contrast_set && rec.contrast == 99, "contrast comes from the devicetree");
    __ASSERT(rec.on, "the panel is on");
    __ASSERT(frame_is("BASE"), "boot frame");

    /* First every page is cleared, then the panel is switched on. */
    __ASSERT(rec.log_len >= RECORDING_PAGES + 1, "boot log");
    for (uint16_t page = 0; page < RECORDING_PAGES; page++) {
        const struct recording_entry *e = &rec.log[page];

        __ASSERT(e->kind == RECORDING_WRITE && e->page == page && e->x0 == 0 &&
                     e->len == RECORDING_WIDTH,
                 "page %u cleared first", page);
    }
    __ASSERT(rec.log[RECORDING_PAGES].kind == RECORDING_ON, "switched on after the clear");

    printk("PASS: boot clears the panel, switches it on and draws the status\n");
}

static void test_layer_change_sends_only_the_layer(void) {
    recording_display_clear_counts();
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(NUM_LAYER, false) == 0);
    k_msleep(SETTLE_MS);

    struct recording_display rec = snapshot();

    __ASSERT(frame_is("NUM"), "NUM shown");
    __ASSERT(rec.writes > 0, "something sent");
    __ASSERT(rec.page_writes[2] == 0 && rec.page_writes[3] == 0 && rec.page_writes[4] == 0,
             "only the layer pages are sent");
    __ASSERT(rec.bytes <= 2 * RECORDING_WIDTH, "%u bytes for a layer change", rec.bytes);

    printk("PASS: a layer change sends only changed columns of the layer pages (%u bytes)\n",
           rec.bytes);
}

static void test_unchanged_state_sends_nothing(void) {
    recording_display_clear_counts();
    raise_zmk_endpoint_changed(
        (struct zmk_endpoint_changed){.endpoint = zmk_endpoint_get_selected()});
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().writes == 0, "an event that changes nothing shown sends nothing");

    printk("PASS: an event that changes nothing shown sends nothing\n");
}

static void test_hidden_layer_is_ignored(void) {
    recording_display_clear_counts();
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(HIDDEN_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().writes == 0, "a hidden layer sends nothing");
    __ASSERT(frame_is("NUM"), "the layer below a hidden one is shown");

    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(HIDDEN_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().writes == 0, "a hidden layer going down sends nothing");

    printk("PASS: hidden layers are neither shown nor sent\n");
}

static void test_unnamed_layer_shows_its_index(void) {
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(UNNAMED_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    __ASSERT(frame_is("LAYER 3"), "an unnamed layer shows its index");
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(UNNAMED_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    __ASSERT(frame_is("NUM"), "back to NUM");

    printk("PASS: an unnamed layer shows its index\n");
}

/*
 * Only the panel's timeout darkens it, and only a key, an encoder or a pointer
 * lights it again: ZMK's idle and activity events on their own do neither. This
 * build has no timeout, so the test sets one at run time.
 */
static void test_dark_panel_catches_up(void) {
    recording_display_clear_counts();
    raise_activity(ZMK_ACTIVITY_IDLE);
    k_msleep(SETTLE_MS);

    struct recording_display rec = snapshot();

    __ASSERT(rec.on && rec.blank_on == 0 && rec.writes == 0, "ZMK's idle leaves the panel lit");

    zmk_display_72x40_set_blank_after_ms(200);
    k_msleep(200 + SETTLE_MS);
    rec = snapshot();
    __ASSERT(!rec.on && rec.blank_on == 1 && rec.writes == 0, "the timeout switches it off");

    /* A dark panel is not written to. */
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(NUM_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().writes == 0, "nothing sent while dark");

    raise_activity(ZMK_ACTIVITY_ACTIVE);
    k_msleep(SETTLE_MS);
    __ASSERT(!snapshot().on, "ZMK's activity event alone does not light it");

    /* An encoder turn does, as a key or a pointer report would. */
    __ASSERT_NO_MSG(raise_zmk_sensor_event((struct zmk_sensor_event){
                        .sensor_index = 0,
                        .timestamp = k_uptime_get(),
                    }) == 0);
    k_msleep(SETTLE_MS);
    rec = snapshot();
    zmk_display_72x40_set_blank_after_ms(0);
    __ASSERT(rec.on && rec.blank_off == 1, "an encoder turn switches the panel on");
    __ASSERT(rec.writes > 0 && frame_is("BASE"), "the change made while dark is drawn");

    uint32_t switched_on = 0;

    for (uint32_t i = 0; i < rec.log_len; i++) {
        if (rec.log[i].kind == RECORDING_ON) {
            switched_on = rec.log[i].sequence;
        }
    }
    for (uint32_t i = 0; i < rec.log_len; i++) {
        if (rec.log[i].kind == RECORDING_WRITE) {
            __ASSERT(rec.log[i].sequence < switched_on, "the frame is ready before switch-on");
        }
    }

    printk("PASS: only the timeout darkens the panel, and it wakes with the new frame drawn\n");
}

static void test_writes_follow_the_sync_device(void) {
    /* The sync device starts reporting, then a layer changes. */
    report_sync();
    k_msleep(2);
    recording_display_clear_counts();
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(NUM_LAYER, false) == 0);
    k_msleep(CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS + 10);
    __ASSERT(snapshot().writes == 0, "nothing is sent between the device's reports");

    /* Each report lets exactly one write through, right after it. */
    uint32_t sent = 0;
    int reports = 0;

    while (!frame_is("NUM") && reports < 20) {
        uint32_t reported_at = k_uptime_get_32();

        report_sync();
        k_msleep(5);

        struct recording_display rec = snapshot();

        __ASSERT(rec.writes == sent + 1, "one write per report, not %u", rec.writes - sent);
        __ASSERT(rec.log[rec.log_len - 1].at_ms - reported_at <= 2,
                 "the write follows the report");
        sent = rec.writes;
        reports++;
    }
    __ASSERT(frame_is("NUM"), "NUM drawn one report at a time");
    __ASSERT(reports >= 2, "both layer pages changed");

    /* A device that falls quiet no longer holds writes back. */
    report_sync();
    k_msleep(2);
    recording_display_clear_counts();
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(UNNAMED_LAYER, false) == 0);
    k_msleep(CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS + 10);
    __ASSERT(snapshot().writes == 0, "held while the device is reporting");
    k_msleep(SYNC_WINDOW_MS + 10);
    __ASSERT(frame_is("LAYER 3"), "sent once the device is quiet");

    /* And a quiet device holds nothing back at all. */
    recording_display_clear_counts();
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(UNNAMED_LAYER, false) == 0);
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(NUM_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    __ASSERT(frame_is("BASE") && snapshot().writes >= 2, "a quiet device lets every write go");

    printk("PASS: while the sync device reports, one write follows each report (%d reports)\n",
           reports);
}

static void test_failed_write_is_retried(void) {
    recording_display_lock()->fail_writes = 1;
    recording_display_unlock();
    recording_display_clear_counts();

    __ASSERT_NO_MSG(zmk_keymap_layer_activate(NUM_LAYER, false) == 0);
    k_msleep(SETTLE_MS + 5 * CONFIG_ZMK_DISPLAY_72X40_RETRY_MS);

    struct recording_display rec = snapshot();

    __ASSERT(rec.failed == 1, "one write failed");
    __ASSERT(frame_is("NUM"), "the retry completed the frame");

    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(NUM_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    __ASSERT(frame_is("BASE"), "back to BASE");

    printk("PASS: a failed write is retried\n");
}

/* Suspends both power-off devices in the given order, as a power-off would. */
static void suspend_both(const struct device *first, const struct device *second) {
    __ASSERT_NO_MSG(pm_device_action_run(first, PM_DEVICE_ACTION_SUSPEND) == 0);
    __ASSERT_NO_MSG(pm_device_action_run(second, PM_DEVICE_ACTION_SUSPEND) == 0);
}

static void resume_both(const struct device *first, const struct device *second) {
    __ASSERT_NO_MSG(pm_device_action_run(first, PM_DEVICE_ACTION_RESUME) == 0);
    __ASSERT_NO_MSG(pm_device_action_run(second, PM_DEVICE_ACTION_RESUME) == 0);
    k_msleep(SETTLE_MS);
}

static void test_suspend_switches_the_panel_off(void) {
    const struct device *pm = device_get_binding("display_72x40_pm");
    const struct device *pm_late = device_get_binding("display_72x40_pm_late");

    __ASSERT(pm != NULL && pm_late != NULL, "both power-off devices exist");

    /* &soft_off's order: the device before the bus goes first. */
    recording_display_clear_counts();
    suspend_both(pm, pm_late);

    struct recording_display rec = snapshot();

    __ASSERT(!rec.on && rec.blank_on == 1, "suspend switches the panel off, once");

    __ASSERT_NO_MSG(zmk_keymap_layer_activate(NUM_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    rec = snapshot();
    __ASSERT(!rec.on && rec.writes == 0 && rec.blank_off == 0, "a suspended panel stays dark");

    resume_both(pm, pm_late);
    rec = snapshot();
    __ASSERT(rec.on && frame_is("NUM"), "resume draws and switches on");

    /* Idle sleep's order: the device after the bus goes first. */
    recording_display_clear_counts();
    suspend_both(pm_late, pm);
    rec = snapshot();
    __ASSERT(!rec.on && rec.blank_on == 1, "the reverse order switches it off once too");
    resume_both(pm_late, pm);

    /* A panel already dark is left alone: its bus may be down by then. */
    zmk_display_72x40_set_blank_after_ms(100);
    k_msleep(100 + SETTLE_MS);
    __ASSERT(!snapshot().on, "the timeout darkened the panel");
    recording_display_clear_counts();
    suspend_both(pm_late, pm);
    rec = snapshot();
    __ASSERT(rec.blank_on == 0 && rec.writes == 0, "a dark panel is not touched at power-off");
    resume_both(pm_late, pm);
    __ASSERT(!snapshot().on, "resume does not light a panel its timeout darkened");
    zmk_display_72x40_set_blank_after_ms(0);
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().on, "switching the timeout off after resume lights it again");

    printk("PASS: either power-off order switches the panel off once, and a dark one not at all\n");
}

#if CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS > 0

#define BLANK_AFTER_MS CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS

static void tap_key(uint32_t position) {
    for (int state = 1; state >= 0; state--) {
        __ASSERT_NO_MSG(raise_zmk_position_state_changed((struct zmk_position_state_changed){
                            .position = position,
                            .state = state == 1,
                            .timestamp = k_uptime_get(),
                        }) == 0);
        k_msleep(2);
    }
}

/*
 * The panel's timeout at its build-time length, which is why this build
 * exists: everything else is covered by the build without it, where the panel
 * darkens only when a test sets a timeout.
 */
static void test_panel_timeout_is_its_own(void) {
    report_sync();
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().on, "lit while in use");

    /* A pointer in use keeps it lit, one report at a time. */
    for (int i = 0; i < 4; i++) {
        k_msleep(BLANK_AFTER_MS / 2);
        report_sync();
    }
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().on, "a pointer in use keeps the panel lit");

    /* Left alone it goes dark, with ZMK still calling the keyboard active. */
    recording_display_clear_counts();
    k_msleep(BLANK_AFTER_MS + SETTLE_MS);

    struct recording_display rec = snapshot();

    __ASSERT(!rec.on && rec.blank_on == 1, "the panel's own timeout darkens it");
    __ASSERT(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE, "ZMK still counts it active");

    /* A change while dark is drawn, not lit. */
    recording_display_clear_counts();
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(NUM_LAYER, false) == 0);
    k_msleep(SETTLE_MS);
    rec = snapshot();
    __ASSERT(!rec.on && rec.writes == 0 && rec.blank_off == 0, "a change does not light it");

    /* A key brings it back with that change already on it. */
    recording_display_clear_counts();
    tap_key(0);
    k_msleep(SETTLE_MS);
    rec = snapshot();
    __ASSERT(rec.on && rec.blank_off == 1, "a key wakes the panel");
    __ASSERT(frame_is("NUM"), "the change made while dark is drawn");

    /* And it darkens again once that key is old news. */
    k_msleep(BLANK_AFTER_MS + SETTLE_MS);
    __ASSERT(!snapshot().on, "the timeout runs again after the key");

    printk("PASS: the panel keeps a timeout of its own, %d ms, and a key or pointer ends it\n",
           BLANK_AFTER_MS);
}

#endif /* CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS > 0 */

/*
 * The setters a Studio client reaches through custom settings, called directly:
 * each takes effect at once, and a value the module already has changes nothing.
 */
static void test_settings_apply_at_run_time(void) {
    /* Contrast: sent once, and not again for the same value. */
    recording_display_lock()->contrast_set = false;
    recording_display_unlock();
    zmk_display_72x40_set_contrast(77);
    k_msleep(SETTLE_MS);

    struct recording_display rec = snapshot();

    __ASSERT(rec.contrast_set && rec.contrast == 77, "a new contrast reaches the panel");
    recording_display_lock()->contrast_set = false;
    recording_display_unlock();
    zmk_display_72x40_set_contrast(77);
    k_msleep(SETTLE_MS);
    __ASSERT(!snapshot().contrast_set, "the same contrast is not sent twice");

    /* The panel's own timeout, switched on and off while running. */
    report_sync();
    zmk_display_72x40_set_blank_after_ms(300);
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().on, "a fresh report keeps it lit at first");
    k_msleep(300 + SETTLE_MS);
    __ASSERT(!snapshot().on, "the new timeout darkens it");
    zmk_display_72x40_set_blank_after_ms(0);
    k_msleep(SETTLE_MS);
    __ASSERT(snapshot().on, "switching the timeout off lights it again");

    printk("PASS: contrast and the panel's timeout apply at run time\n");
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    __ASSERT_NO_MSG(device_is_ready(sync_pad));
    __ASSERT(snapshot().malformed == 0, "malformed writes at boot");

#if CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS > 0
    test_panel_timeout_is_its_own();

    __ASSERT(snapshot().malformed == 0, "malformed writes");

    printk("display-72x40 runtime tests: PASS\n");
    exit(0);
#endif

    test_boot_clears_then_draws();
    test_layer_change_sends_only_the_layer();
    test_unchanged_state_sends_nothing();
    test_hidden_layer_is_ignored();
    test_unnamed_layer_shows_its_index();
    test_dark_panel_catches_up();
    test_writes_follow_the_sync_device();
    test_failed_write_is_retried();
    test_settings_apply_at_run_time();
    test_suspend_switches_the_panel_off();

    __ASSERT(snapshot().malformed == 0, "malformed writes");

    printk("display-72x40 runtime tests: PASS\n");
    exit(0);
}

/* Room for a few copies of the recording, which the tests take by value. */
K_THREAD_DEFINE(display_72x40_runtime_tests, 32768, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 300);
