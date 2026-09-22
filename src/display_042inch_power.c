/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Carrying "this half is on external power" across a split.
 *
 * A split peripheral has no USB stack of its own - it cannot expose USB HID -
 * so it does not know it is plugged in the way a central does. The SoC's USB
 * regulator does: on an nRF with a USB peripheral, VBUS detection is one
 * register bit, readable whether or not the USB stack is built. This watches
 * that bit and relays a change to the central, which draws it as a bolt
 * beside the peripheral's battery level.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/workqueue.h>

#include <zmk-display-042inch/power_state.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(zmk_display_042inch_power_state_changed);

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_POWER_REPORTER)

#include <hal/nrf_power.h>

#include <zmk/events/split_peripheral_status_changed.h>

#if !NRF_POWER_HAS_USBREG
#error "CONFIG_ZMK_DISPLAY_042INCH_POWER_REPORTER needs a SoC with USB VBUS detection"
#endif

/*
 * A link is reported up before the central has discovered and subscribed to
 * this half's relay characteristic, and a notify sent before that is dropped
 * without saying so: the send queues the event and returns success either
 * way. So the state goes out a few times after a link comes up, from a second
 * in, and the central redraws nothing for the repeats it already knows.
 */
#define D042_POWER_SYNC_ATTEMPTS 4
#define D042_POWER_SYNC_FIRST_MS 1000
#define D042_POWER_SYNC_RETRY_MS 1500

static bool d042_power_state;
static bool d042_power_known;
static uint8_t d042_power_sync_left;

static void d042_power_work_handler(struct k_work *work);
static void d042_power_sync_handler(struct k_work *work);

/* Compile-time initialised: a listener may schedule them before any init runs. */
static K_WORK_DELAYABLE_DEFINE(d042_power_work, d042_power_work_handler);
static K_WORK_DELAYABLE_DEFINE(d042_power_sync_work, d042_power_sync_handler);

static void d042_power_report(bool powered) {
    LOG_DBG("display-042inch: telling the central this half is %s external power",
            powered ? "on" : "off");
    raise_zmk_display_042inch_power_state_changed((struct zmk_display_042inch_power_state_changed){
        .source = ZMK_RELAY_EVENT_SOURCE_SELF,
        .powered = powered,
    });
}

static void d042_power_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    bool powered = nrf_power_usbregstatus_vbusdet_get(NRF_POWER);

    if (!d042_power_known || powered != d042_power_state) {
        d042_power_known = true;
        d042_power_state = powered;
        d042_power_report(powered);
    }

    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_power_work,
                              K_MSEC(CONFIG_ZMK_DISPLAY_042INCH_POWER_POLL_MS));
}

static void d042_power_sync_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!d042_power_known || d042_power_sync_left == 0) {
        return;
    }

    d042_power_report(d042_power_state);
    if (--d042_power_sync_left > 0) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_power_sync_work,
                                  K_MSEC(D042_POWER_SYNC_RETRY_MS));
    }
}

/* The central learns nothing while the link is down, so say it again. */
static int d042_power_listener(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->connected) {
        d042_power_sync_left = D042_POWER_SYNC_ATTEMPTS;
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_power_sync_work,
                                    K_MSEC(D042_POWER_SYNC_FIRST_MS));
    } else {
        d042_power_sync_left = 0;
        k_work_cancel_delayable(&d042_power_sync_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(display_042inch_power, d042_power_listener);
ZMK_SUBSCRIPTION(display_042inch_power, zmk_split_peripheral_status_changed);

ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(zmk_display_042inch_power_state_changed, dlp, source)

static int d042_power_init(void) {
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_power_work, K_NO_WAIT);
    return 0;
}

SYS_INIT(d042_power_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_POWER_REPORTER) */

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER)

/* Raises the peripheral's event here, with its source id filled in. */
ZMK_RELAY_EVENT_HANDLE(zmk_display_042inch_power_state_changed, dlp, source)

#endif /* IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER) */
