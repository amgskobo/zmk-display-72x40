/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * A status panel for a monochrome, page-tiled display, drawn without LVGL.
 *
 * Nothing here redraws on a clock. An event that changes something the panel
 * shows marks the frame stale and schedules one pass on ZMK's low-priority
 * work queue. That pass reads the state, draws a changed state into RAM
 * (360 bytes on a 72x40 panel), and sends only the columns that differ from
 * what the panel already shows. Events that change nothing shown - layers
 * listed in hidden-layers, for one - cost a bit test.
 *
 * A panel that shares its bus with a touch controller must not talk over the
 * controller's reads. With sync-input-device set, and while that device is
 * reporting, each write waits for its next report and goes out right after
 * it, one write per report.
 */

#define DT_DRV_COMPAT zmk_display_72x40

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>
#include <zmk/keymap.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <dt-bindings/zmk/hid_usage.h>
#include <zmk/hid_indicators.h>
#include <zmk/events/hid_indicators_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCKING)
#include <zmk/studio/core.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_PEER_POWER)
#include <zmk-display-72x40/power_state.h>
#endif

#include <zmk-display-72x40/display_72x40.h>
#include <zmk-display-72x40/display_72x40_core.h>

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH)
#include <zmk-display-72x40/display_72x40_splash.h>
#endif

LOG_MODULE_REGISTER(zmk_display_72x40_display_72x40, CONFIG_ZMK_DISPLAY_72X40_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "declare exactly one zmk,display-72x40 node");

#define D72X40_NODE DT_DRV_INST(0)
#define D72X40_PANEL_NODE DT_PHANDLE(D72X40_NODE, display)

/* The longest panel timeout a setter takes, as the Kconfig range allows. */
#define D72X40_BLANK_AFTER_MAX_MS 600000U
#define D72X40_WIDTH DT_PROP(D72X40_PANEL_NODE, width)
#define D72X40_HEIGHT DT_PROP(D72X40_PANEL_NODE, height)
#define D72X40_PAGES (D72X40_HEIGHT / D72X40_PAGE_HEIGHT)

BUILD_ASSERT(D72X40_WIDTH == D72X40_PANEL_WIDTH && D72X40_HEIGHT == D72X40_PANEL_HEIGHT,
             "zmk,display-72x40 requires the fixed 72x40 panel");
BUILD_ASSERT(!DT_NODE_HAS_PROP(D72X40_NODE, rotation),
             "rotation was removed: the panel uses a fixed portrait layout");

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_72X40_INIT_PRIORITY > CONFIG_DISPLAY_INIT_PRIORITY,
             "the panel clear must run after the display driver has initialised");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_72X40_INIT_PRIORITY > CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
             "the panel clear must run after ZMK's low-priority work queue has started");

#define D72X40_RETRIES 3

#define D72X40_HIDDEN_BIT(node_id, prop, idx) | BIT(DT_PROP_BY_IDX(node_id, prop, idx))
#define D72X40_HIDDEN_CHECK(node_id, prop, idx)                                                        \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < 32,                                          \
                 "zmk,display-72x40: hidden-layers takes layer ids below 32");

COND_CODE_1(DT_NODE_HAS_PROP(D72X40_NODE, hidden_layers),
            (DT_FOREACH_PROP_ELEM(D72X40_NODE, hidden_layers, D72X40_HIDDEN_CHECK)), ())

static const zmk_keymap_layers_state_t d72x40_hidden_layers =
    0 COND_CODE_1(DT_NODE_HAS_PROP(D72X40_NODE, hidden_layers),
                  (DT_FOREACH_PROP_ELEM(D72X40_NODE, hidden_layers, D72X40_HIDDEN_BIT)), ());

static const struct device *const d72x40_panel = DEVICE_DT_GET(D72X40_PANEL_NODE);

#if DT_NODE_HAS_PROP(D72X40_NODE, reset_gpios)

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_72X40_RESET_INIT_PRIORITY < CONFIG_DISPLAY_INIT_PRIORITY,
             "the panel reset must be pulsed before the display driver initialises");

static const struct gpio_dt_spec d72x40_reset = GPIO_DT_SPEC_GET(D72X40_NODE, reset_gpios);

/*
 * Start the panel from a known state at every firmware start, not only when
 * its own power-up circuit sees power come up, and give it time to answer
 * afterwards: an SSD1306 NACKs for a while after reset, and the display
 * driver, which sends its first command the moment it releases the line, has
 * no wait of its own and fails for good when that happens. So the line is
 * pulsed here, before the driver runs, and the driver is given no reset pin.
 */
static int d72x40_reset_panel(void) {
    if (!gpio_is_ready_dt(&d72x40_reset)) {
        LOG_ERR("display-72x40: reset pin %s is not ready", d72x40_reset.port->name);
        return 0;
    }

    int err = gpio_pin_configure_dt(&d72x40_reset, GPIO_OUTPUT_ACTIVE);

    if (err < 0) {
        LOG_ERR("display-72x40: cannot drive the reset pin (%d)", err);
        return 0;
    }

    k_sleep(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_RESET_PULSE_MS));
    err = gpio_pin_set_dt(&d72x40_reset, 0);
    if (err < 0) {
        LOG_ERR("display-72x40: cannot release the reset pin (%d)", err);
        return 0;
    }
    k_sleep(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_RESET_SETTLE_MS));
    LOG_DBG("display-72x40: panel reset on %s pin %u", d72x40_reset.port->name, d72x40_reset.pin);
    return 0;
}

SYS_INIT(d72x40_reset_panel, POST_KERNEL, CONFIG_ZMK_DISPLAY_72X40_RESET_INIT_PRIORITY);

#endif /* DT_NODE_HAS_PROP(D72X40_NODE, reset_gpios) */

/*
 * The frame to show, and what the panel is known to show. Only the work
 * handler touches these once D72X40_READY is set; before that, only the boot-time
 * clear does. d72x40_valid has a bit per page whose d72x40_shown matches the panel.
 */
static uint8_t d72x40_want[D72X40_PAGES * D72X40_WIDTH];
static uint8_t d72x40_shown[D72X40_PAGES * D72X40_WIDTH];
static uint32_t d72x40_valid;
static bool d72x40_panel_on;
static uint8_t d72x40_failures;
static struct d72x40_render_cache d72x40_render_state;

static const struct d72x40_canvas d72x40_canvas = {.buf = d72x40_want};

enum {
    /* The boot-time clear has run; the work handler may use the panel. */
    D72X40_READY,
    /* Something the panel shows has changed. */
    D72X40_REDRAW,
    /* What the panel holds is unknown: send the whole frame. */
    D72X40_FULL,
    /* The keyboard is powering off; the panel stays dark. */
    D72X40_SUSPENDED,
    /* The panel's timeout has run out since the last key, encoder or pointer:
     * the only thing that darkens it while the keyboard runs. */
    D72X40_QUIET,
    /* A contrast a setter asked for is waiting to be sent. */
    D72X40_CONTRAST,
    /* A write is waiting for the sync device's next report. */
    D72X40_WAIT_PEER,
    /* That report has come. */
    D72X40_PEER_KICK,
    /* A report was received; timestamp0 is valid at boot and uptime wrap. */
    D72X40_PEER_SEEN,
};

/* The panel starts lit, and its timeout counts from boot. */
static atomic_t d72x40_flags = ATOMIC_INIT(BIT(D72X40_REDRAW));

static void d72x40_work_handler(struct k_work *work);
static void d72x40_schedule_now(void);

/* Defined at compile time: listeners may schedule it before any init runs. */
static K_WORK_DELAYABLE_DEFINE(d72x40_work, d72x40_work_handler);

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH)
/* One word keeps a supplied pattern/brightness/speed tuple together. */
static struct k_spinlock d72x40_splash_lock;
static atomic_t d72x40_led_settings;
static atomic_t d72x40_led_changed_at_ms;
static atomic_t d72x40_led_splash_seen;
static atomic_t d72x40_led_splash_ms = ATOMIC_INIT(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH_MS);

static void d72x40_led_splash_handler(struct k_work *work);
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_TEST)
void zmk_display_72x40_test_splash_candidate(void);
#endif
static K_WORK_DELAYABLE_DEFINE(d72x40_led_splash_work, d72x40_led_splash_handler);

static void d72x40_led_splash_handler(struct k_work *work) {
    ARG_UNUSED(work);
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_TEST)
    zmk_display_72x40_test_splash_candidate();
#endif
    const k_spinlock_key_t key = k_spin_lock(&d72x40_splash_lock);
    const uint32_t duration = (uint32_t)atomic_get(&d72x40_led_splash_ms);
    if (duration == 0 || atomic_get(&d72x40_led_splash_seen) == 0) {
        k_spin_unlock(&d72x40_splash_lock, key);
        return;
    }
    const uint32_t remaining = d72x40_splash_remaining_ms(
        k_uptime_get_32(), (uint32_t)atomic_get(&d72x40_led_changed_at_ms), duration);
    if (remaining != 0U) {
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_led_splash_work,
                                    K_MSEC(remaining));
        k_spin_unlock(&d72x40_splash_lock, key);
        return;
    }
    atomic_set(&d72x40_led_splash_seen, 0);
    k_spin_unlock(&d72x40_splash_lock, key);
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    d72x40_schedule_now();
}
#endif

static void d72x40_schedule(k_timeout_t delay) {
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_work, delay);
}

static void d72x40_schedule_now(void) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_work, K_NO_WAIT);
}

void zmk_display_72x40_status_changed(void) {
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    d72x40_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS));
}

/*
 * The panel's timeout, contrast and optional LED splash duration can change
 * while the keyboard runs. Each starts at its build-time default and is changed
 * through the setters in display_72x40.h - display_72x40_custom_settings.c calls
 * them for a Studio client - and read where it matters rather than latched.
 */
/* Keep each timeout policy and its timer admission in the same order. */
static struct k_spinlock d72x40_quiet_policy_lock;
static atomic_t d72x40_blank_after_ms = ATOMIC_INIT(CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS);

void zmk_display_72x40_set_led_splash_ms(uint32_t ms) {
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH)
    ms = MIN(ms, 10000U);
    const k_spinlock_key_t key = k_spin_lock(&d72x40_splash_lock);
    if ((uint32_t)atomic_set(&d72x40_led_splash_ms, (atomic_val_t)ms) == ms) {
        k_spin_unlock(&d72x40_splash_lock, key);
        return;
    }
    if (ms == 0) {
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_led_splash_work,
                                    K_FOREVER);
        atomic_set(&d72x40_led_splash_seen, 0);
    } else if (atomic_get(&d72x40_led_splash_seen) != 0) {
        const uint32_t remaining = d72x40_splash_remaining_ms(
            k_uptime_get_32(), (uint32_t)atomic_get(&d72x40_led_changed_at_ms), ms);
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_led_splash_work,
                                    K_MSEC(remaining));
    }
    k_spin_unlock(&d72x40_splash_lock, key);
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    d72x40_schedule_now();
#else
    ARG_UNUSED(ms);
#endif
}
/* The contrast asked for, and the one the panel was last set to; -1 is
 * "never", which is where a node without a contrast property starts. */
static atomic_t d72x40_contrast_want = ATOMIC_INIT(DT_PROP_OR(D72X40_NODE, contrast, -1));
static int d72x40_contrast_sent = -1;

/*
 * The panel's timeout, which is all that darkens it while the keyboard runs:
 * the screen is what an OLED keyboard spends its current on. It follows the
 * same inputs ZMK's idle does - keys, encoders and pointer reports - but runs
 * on its own length, so no second switch that follows ZMK's idle is needed:
 * shorter than the idle timeout it always darkens the panel first, and longer
 * it keeps the panel lit for as long as asked, until the keyboard powers off,
 * which darkens it anyway. Those inputs only stamp the time here; the timer
 * below reads that stamp and re-arms itself, so typing costs one store and
 * never touches the work queue. A length of zero switches it off, and then no
 * clock runs at all and the panel stays lit until power-off.
 */
static atomic_t d72x40_active_at_ms;

static void d72x40_quiet_handler(struct k_work *work);

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_TEST)
/* The owner native fixture can pause the quiet decision; no test policy lives here. */
void zmk_display_72x40_test_quiet_candidate(struct k_work_delayable *quiet);
#endif

static K_WORK_DELAYABLE_DEFINE(d72x40_quiet_work, d72x40_quiet_handler);

static void d72x40_note_activity(void) {
    atomic_set(&d72x40_active_at_ms, (atomic_val_t)k_uptime_get_32());
    /* Only the first key after the panel went dark has anything to do. */
    if (atomic_test_and_clear_bit(&d72x40_flags, D72X40_QUIET)) {
        d72x40_schedule_now();
    }
}

static void d72x40_quiet_handler(struct k_work *work) {
    ARG_UNUSED(work);

    const uint32_t after_ms = (uint32_t)atomic_get(&d72x40_blank_after_ms);

    /* Switched off, or nothing lit to switch off. This runs on the work
     * handler's own queue, so reading d72x40_panel_on here is safe. */
    if (after_ms == 0 || !d72x40_panel_on) {
        return;
    }

    const uint32_t quiet_ms = k_uptime_get_32() - (uint32_t)atomic_get(&d72x40_active_at_ms);

    if (quiet_ms < after_ms) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_quiet_work,
                                  K_MSEC(after_ms - quiet_ms));
        return;
    }
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_TEST)
    zmk_display_72x40_test_quiet_candidate(&d72x40_quiet_work);
#endif
    /* Publish before the final sample: activity/disable either appears in that
     * sample or sees the candidate and clears it. This queue cannot draw the
     * candidate until this handler has returned. */
    atomic_set_bit(&d72x40_flags, D72X40_QUIET);
    const uint32_t current_after = (uint32_t)atomic_get(&d72x40_blank_after_ms);
    const uint32_t current_quiet =
        k_uptime_get_32() - (uint32_t)atomic_get(&d72x40_active_at_ms);
    if (current_after == 0 || current_quiet < current_after) {
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_TEST)
        if (current_after == 0) {
            zmk_display_72x40_test_quiet_candidate(&d72x40_quiet_work);
        }
#endif
        atomic_clear_bit(&d72x40_flags, D72X40_QUIET);
        if (current_after != 0) {
            k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_quiet_work,
                                      K_MSEC(current_after - current_quiet));
        }
        return;
    }
    d72x40_schedule_now();
}

/* Armed only while the panel is lit, so a dark panel runs no clock at all. */
static void d72x40_arm_quiet(void) {
    const uint32_t after_ms = (uint32_t)atomic_get(&d72x40_blank_after_ms);

    if (after_ms > 0) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_quiet_work,
                                  K_MSEC(after_ms));
    }
}

static void d72x40_activity_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(evt);
    ARG_UNUSED(user_data);

    d72x40_note_activity();
}

/* Every pointing device, this half's and the other's alike. */
INPUT_CALLBACK_DEFINE(NULL, d72x40_activity_input_cb, NULL);

/*
 * The setters. Each may be called from any thread - a Studio client's arrive
 * on its RPC thread - so they only record the value and schedule; the bus is
 * touched by the work handler alone, which keeps a write clear of the sync
 * device's reads. Each ignores a value it already has, because a settings
 * change for anything at all sends all of them round again.
 */
void zmk_display_72x40_set_blank_after_ms(uint32_t ms) {
    ms = MIN(ms, D72X40_BLANK_AFTER_MAX_MS);
    const k_spinlock_key_t key = k_spin_lock(&d72x40_quiet_policy_lock);
    if ((uint32_t)atomic_set(&d72x40_blank_after_ms, (atomic_val_t)ms) == ms) {
        k_spin_unlock(&d72x40_quiet_policy_lock, key);
        return;
    }
    if (ms == 0) {
        /* Retire its timeout without rejecting an immediate later re-enable. */
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_quiet_work,
                                    K_FOREVER);
        /* A panel the old timeout darkened is lit again. */
        const bool wake = atomic_test_and_clear_bit(&d72x40_flags, D72X40_QUIET);
        k_spin_unlock(&d72x40_quiet_policy_lock, key);
        if (wake) {
            d72x40_schedule_now();
        }
        return;
    }
    /* Measured again from the last key, against the new length. */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_quiet_work, K_NO_WAIT);
    k_spin_unlock(&d72x40_quiet_policy_lock, key);
}

void zmk_display_72x40_set_contrast(uint8_t contrast) {
    if (atomic_set(&d72x40_contrast_want, (atomic_val_t)contrast) == (atomic_val_t)contrast) {
        return;
    }
    atomic_set_bit(&d72x40_flags, D72X40_CONTRAST);
    d72x40_schedule_now();
}

#if DT_NODE_HAS_PROP(D72X40_NODE, sync_input_device)

#define D72X40_PEER_NODE DT_PHANDLE(D72X40_NODE, sync_input_device)

/* k_uptime_get_32() of the sync device's last completed report. */
static atomic_t d72x40_peer_last_ms;

static void d72x40_peer_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    if (!evt->sync) {
        return;
    }
    atomic_set(&d72x40_peer_last_ms, (atomic_val_t)k_uptime_get_32());
    atomic_set_bit(&d72x40_flags, D72X40_PEER_SEEN);
    /* The device's read is over and its next sample is a report period
     * away: the one moment a write cannot run into it. */
    if (atomic_test_bit(&d72x40_flags, D72X40_WAIT_PEER)) {
        atomic_set_bit(&d72x40_flags, D72X40_PEER_KICK);
        d72x40_schedule_now();
    }
}

COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(D72X40_PEER_NODE),
            (INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(D72X40_PEER_NODE), d72x40_peer_input_cb, NULL);), ())

static bool d72x40_peer_busy(void) {
    if (!atomic_test_bit(&d72x40_flags, D72X40_PEER_SEEN)) {
        return false;
    }
    uint32_t last = (uint32_t)atomic_get(&d72x40_peer_last_ms);

    return (uint32_t)(k_uptime_get_32() - last) < CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS;
}

#else

static bool d72x40_peer_busy(void) { return false; }

#endif

static void d72x40_wait_peer(void) {
    atomic_set_bit(&d72x40_flags, D72X40_WAIT_PEER);
    /* If no report comes, the device is no longer busy when this fires. */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_work,
                                K_MSEC(CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS));
}

/* The peripheral whose battery and power the panel shows. */
#define D72X40_PEER_SOURCE 0

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_PEER_POWER)

/* What that peripheral last said about its own power; see power_state.h.
 * Kept without battery fetching too, since the event handler records it. */
static atomic_t d72x40_peer_powered;

#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

/*
 * Whether that peripheral is connected. ZMK raises no event for it and keeps
 * no public flag, but every split transport can list the sources it is
 * carrying, and the transports are an iterable section.
 */
static bool d72x40_peer_connected(void) {
    uint8_t sources[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];

    STRUCT_SECTION_FOREACH(zmk_split_transport_central, transport) {
        if (transport->api == NULL || transport->api->get_available_source_ids == NULL) {
            continue;
        }

        int count = transport->api->get_available_source_ids(sources);

        for (int i = 0; i < count; i++) {
            if (sources[i] == D72X40_PEER_SOURCE) {
                return true;
            }
        }
    }

    return false;
}

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING) */

/*
 * The layer to name: the highest active one not in hidden-layers, stopping
 * at the default layer as ZMK's own highest-layer lookup does.
 */
static void d72x40_collect_layer(struct d72x40_status *st) {
    zmk_keymap_layer_id_t fallback = zmk_keymap_layer_default();
    zmk_keymap_layer_id_t shown = fallback;
    int shown_index = 0;

    for (int index = ZMK_KEYMAP_LAYERS_LEN - 1; index >= 0; index--) {
        zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id((zmk_keymap_layer_index_t)index);

        if (id == ZMK_KEYMAP_LAYER_ID_INVAL) {
            continue;
        }
        if (id == fallback) {
            shown_index = index;
            break;
        }
        if (id < 32 && (d72x40_hidden_layers & BIT(id))) {
            continue;
        }
        if (zmk_keymap_layer_active(id)) {
            shown = id;
            shown_index = index;
            break;
        }
    }

    const char *name = zmk_keymap_layer_name(shown);

    st->layer[0] = '\0';
    if (name != NULL && name[0] != '\0') {
        d72x40_append(st->layer, sizeof(st->layer), name);
    } else {
        d72x40_append(st->layer, sizeof(st->layer), "LAYER ");
        d72x40_append_u8(st->layer, sizeof(st->layer), (uint8_t)shown_index);
    }
}

/* Checks identify the selected HID route, not every connected transport. */
static void d72x40_collect_output(struct d72x40_status *st) {
    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    enum zmk_transport transport = selected.transport;

    st->transport = D72X40_TRANSPORT_NONE;
    st->link = D72X40_LINK_DOWN;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    st->ble_available = true;
    st->profile = (uint8_t)(zmk_ble_active_profile_index() + 1);
    st->ble_link = zmk_ble_active_profile_is_connected()
                      ? (transport == ZMK_TRANSPORT_BLE ? D72X40_LINK_UP : D72X40_LINK_OPEN)
                      : zmk_ble_active_profile_is_open() ? D72X40_LINK_OPEN : D72X40_LINK_DOWN;
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
    st->usb_available = true;
    st->usb_link = zmk_usb_get_conn_state() == ZMK_USB_CONN_HID
                      ? (transport == ZMK_TRANSPORT_USB ? D72X40_LINK_UP : D72X40_LINK_OPEN)
                      : D72X40_LINK_DOWN;
#endif
    switch (transport) {
    case ZMK_TRANSPORT_USB:
        st->transport = D72X40_TRANSPORT_USB;
        st->link = st->usb_link;
        break;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        st->transport = D72X40_TRANSPORT_BLE;
        st->link = st->ble_link;
        break;
#endif
    default:
        break;
    }
}

#if IS_ENABLED(CONFIG_ZMK_USB) && IS_ENABLED(CONFIG_ZMK_BLE)
/* ZMK emits endpoint_changed only when the actual endpoint changes. A
 * preference change during USB fallback emits nothing, so observe the public
 * setter without changing ZMK or polling the display. */
int __real_zmk_endpoint_set_preferred_transport(enum zmk_transport transport);
int __wrap_zmk_endpoint_set_preferred_transport(enum zmk_transport transport);

int __wrap_zmk_endpoint_set_preferred_transport(enum zmk_transport transport) {
    enum zmk_transport before = zmk_endpoint_get_preferred_transport();
    int result = __real_zmk_endpoint_set_preferred_transport(transport);
    if (before != zmk_endpoint_get_preferred_transport()) {
        atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
        d72x40_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS));
    }
    return result;
}

/* The toggle calls the setter from endpoints.c itself; those local calls need
 * not pass through the linker's setter wrapper. Observe this public entry too. */
int __real_zmk_endpoint_toggle_preferred_transport(void);
int __wrap_zmk_endpoint_toggle_preferred_transport(void);

int __wrap_zmk_endpoint_toggle_preferred_transport(void) {
    enum zmk_transport before = zmk_endpoint_get_preferred_transport();
    int result = __real_zmk_endpoint_toggle_preferred_transport();
    if (before != zmk_endpoint_get_preferred_transport()) {
        atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
        d72x40_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS));
    }
    return result;
}
#endif

static void d72x40_collect(struct d72x40_status *st) {
    memset(st, 0, sizeof(*st));
    d72x40_collect_layer(st);
    d72x40_collect_output(st);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    st->battery = zmk_battery_state_of_charge();
#else
    st->battery = D72X40_BATTERY_HIDDEN;
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    /*
     * ZMK reports 0 both for a peripheral that has just disconnected and for
     * one whose battery really is flat (its own reading is 0% below 3.45 V),
     * so ask the transport which peripherals are connected: "--" means no
     * peripheral, and a connected one shows its level, 0% included.
     */
    if (!d72x40_peer_connected() ||
        zmk_split_central_get_peripheral_battery_level(D72X40_PEER_SOURCE, &st->peer_battery) < 0) {
        st->peer_battery = D72X40_BATTERY_UNKNOWN;
    }
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_PEER_POWER)
    else {
        st->peer_charging = atomic_get(&d72x40_peer_powered) != 0;
    }
#endif
#else
    st->peer_battery = D72X40_BATTERY_HIDDEN;
#endif

#if IS_ENABLED(CONFIG_ZMK_USB)
    st->charging = zmk_usb_is_powered();
#endif

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    st->caps_lock = (zmk_hid_indicators_get_current_profile() &
                     BIT(HID_USAGE_LED_CAPS_LOCK - HID_USAGE_LED_NUM_LOCK)) != 0;
#endif

#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCKING)
    st->studio_lock_available = true;
    st->unlocked = zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
#endif
    const struct zmk_display_72x40_lock_status external =
        zmk_display_72x40_get_external_lock_status();
    st->messenger_lock_available = external.available;
    st->messenger_unlocked = external.unlocked;
}

static bool d72x40_frame_differs(void) {
    const uint32_t all_pages = BIT(D72X40_PAGES) - 1;
    return (d72x40_valid & all_pages) != all_pages ||
           memcmp(d72x40_want, d72x40_shown, sizeof(d72x40_want)) != 0;
}

/* What one pass sent, for the debug log. */
struct d72x40_pass {
    unsigned int runs;
    unsigned int bytes;
};

/*
 * Sends changed runs until none is left or *budget (when not negative) runs
 * out. Returns 0 when the panel matches the frame, 1 when runs remain, or a
 * negative errno.
 */
static int d72x40_flush(int *budget, struct d72x40_pass *pass) {
    struct d72x40_span span;
    uint16_t page = 0;
    uint16_t x = 0;

    while (d72x40_next_span(d72x40_want, d72x40_shown, D72X40_WIDTH, D72X40_PAGES, d72x40_valid, &page, &x, &span)) {
        if (*budget == 0) {
            return 1;
        }

        uint16_t len = (uint16_t)(span.x1 - span.x0 + 1);
        size_t at = (size_t)span.page * D72X40_WIDTH + span.x0;
        const struct display_buffer_descriptor desc = {
            .buf_size = len,
            .width = len,
            .height = D72X40_PAGE_HEIGHT,
            .pitch = len,
        };
        int err = display_write(d72x40_panel, span.x0, (uint16_t)(span.page * D72X40_PAGE_HEIGHT), &desc,
                                &d72x40_want[at]);

        if (err < 0) {
            /* Part of the page may have landed: its content is unknown. */
            d72x40_valid &= ~BIT(span.page);
            return err;
        }
        memcpy(&d72x40_shown[at], &d72x40_want[at], len);
        if (len == D72X40_WIDTH) {
            d72x40_valid |= BIT(span.page);
        }
        pass->runs++;
        pass->bytes += len;
        if (*budget > 0) {
            (*budget)--;
        }
    }

    return 0;
}

static void d72x40_retry(int err) {
    if (d72x40_failures < UINT8_MAX) {
        d72x40_failures++;
    }
    if (d72x40_failures == 1) {
        LOG_WRN("display-72x40: panel write failed (%d), retrying", err);
    }
    if (d72x40_failures <= D72X40_RETRIES) {
        d72x40_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_RETRY_MS));
    } else if (d72x40_failures == D72X40_RETRIES + 1) {
        LOG_ERR("display-72x40: panel write failed (%d), waiting for the next change", err);
    }
}

/* Sends the contrast a setter asked for, if the panel is not at it already. */
static int d72x40_send_contrast(void) {
    const int want = (int)atomic_get(&d72x40_contrast_want);

    if (want < 0 || want == d72x40_contrast_sent) {
        return 0;
    }

    int err = display_set_contrast(d72x40_panel, (uint8_t)want);

    if (err == 0) {
        d72x40_contrast_sent = want;
    } else {
        LOG_WRN("display-72x40: contrast %d not taken (%d)", want, err);
    }
    return err;
}

static void d72x40_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!atomic_test_bit(&d72x40_flags, D72X40_READY) || atomic_test_bit(&d72x40_flags, D72X40_SUSPENDED)) {
        return;
    }

    bool kicked = atomic_test_and_clear_bit(&d72x40_flags, D72X40_PEER_KICK);

    atomic_clear_bit(&d72x40_flags, D72X40_WAIT_PEER);

    if (atomic_test_and_clear_bit(&d72x40_flags, D72X40_FULL)) {
        d72x40_valid = 0;
    }

    if (atomic_test_and_clear_bit(&d72x40_flags, D72X40_REDRAW)) {
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH)
        const k_spinlock_key_t splash_key = k_spin_lock(&d72x40_splash_lock);
        const bool show_splash = atomic_get(&d72x40_led_splash_seen) != 0 &&
            atomic_get(&d72x40_led_splash_ms) != 0 &&
            d72x40_splash_remaining_ms(k_uptime_get_32(),
                                      (uint32_t)atomic_get(&d72x40_led_changed_at_ms),
                                      (uint32_t)atomic_get(&d72x40_led_splash_ms)) != 0U;
        const uint32_t settings = (uint32_t)atomic_get(&d72x40_led_settings);
        k_spin_unlock(&d72x40_splash_lock, splash_key);
        if (show_splash) {
            d72x40_render_led_cached(&d72x40_canvas, &d72x40_render_state, (uint8_t)settings,
                                    (uint8_t)(settings >> 8), (uint16_t)(settings >> 16));
        } else
#endif
        {
            struct d72x40_status st;

            d72x40_collect(&st);
            d72x40_render_cached(&d72x40_canvas, &d72x40_render_state, &st);
        }
    }

    /* A dark panel is not written: the frame goes out when it wakes. */
    bool awake = !atomic_test_bit(&d72x40_flags, D72X40_QUIET);
    bool stale = awake && d72x40_frame_differs();
    const bool contrast = atomic_test_bit(&d72x40_flags, D72X40_CONTRAST);

    if (!stale && awake == d72x40_panel_on && !contrast) {
        d72x40_failures = 0;
        return;
    }

    /* One bus operation per report while the sync device is reporting. */
    int budget = -1;

    if (d72x40_peer_busy()) {
        if (!kicked) {
            d72x40_wait_peer();
            return;
        }
        budget = 1;
    }

    /* A new contrast is one more bus operation, and counts as one. */
    if (contrast) {
        atomic_clear_bit(&d72x40_flags, D72X40_CONTRAST);
        int contrast_err = d72x40_send_contrast();

        if (contrast_err < 0) {
            atomic_set_bit(&d72x40_flags, D72X40_CONTRAST);
            d72x40_retry(contrast_err);
            return;
        }
        if (budget > 0) {
            budget--;
        }
        if (!stale && awake == d72x40_panel_on) {
            d72x40_failures = 0;
            return;
        }
    }

    struct d72x40_pass pass = {0};
    int err = 0;

    if (!awake) {
        if (budget == 0) {
            d72x40_wait_peer();
            return;
        }
        err = display_blanking_on(d72x40_panel);
        if (err == 0) {
            d72x40_panel_on = false;
            LOG_DBG("display-72x40: panel off");
        }
    } else {
        int left = d72x40_flush(&budget, &pass);

        if (left < 0) {
            err = left;
        } else if (left > 0) {
            LOG_DBG("display-72x40: sent %u run(s), %u byte(s), waiting for a report",
                    pass.runs, pass.bytes);
            d72x40_wait_peer();
            return;
        } else if (!d72x40_panel_on) {
            /* Switch on only once the new frame is in place. */
            if (budget == 0) {
                d72x40_wait_peer();
                return;
            }
            err = display_blanking_off(d72x40_panel);
            if (err == 0) {
                d72x40_panel_on = true;
            }
        }
        if (err == 0) {
            LOG_DBG("display-72x40: sent %u run(s), %u byte(s)%s", pass.runs, pass.bytes,
                    budget == 0 ? " after a report" : "");
        }
    }

    if (err < 0) {
        d72x40_retry(err);
        return;
    }
    if (d72x40_panel_on) {
        d72x40_arm_quiet();
    }
    d72x40_failures = 0;
}

int zmk_display_72x40_refresh(void) {
    if (!atomic_test_bit(&d72x40_flags, D72X40_READY)) {
        return -ENODEV;
    }
    atomic_set_bit(&d72x40_flags, D72X40_FULL);
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    d72x40_schedule_now();
    return 0;
}

void zmk_display_72x40_show_led_settings(uint8_t pattern, uint8_t brightness, uint16_t speed) {
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH)
    if (atomic_test_bit(&d72x40_flags, D72X40_READY)) {
        const k_spinlock_key_t key = k_spin_lock(&d72x40_splash_lock);
        const uint32_t duration = (uint32_t)atomic_get(&d72x40_led_splash_ms);
        if (duration == 0) {
            k_spin_unlock(&d72x40_splash_lock, key);
            return;
        }
        const uint32_t settings = (uint32_t)pattern | ((uint32_t)brightness << 8) |
                                  ((uint32_t)speed << 16);
        atomic_set(&d72x40_led_settings, (atomic_val_t)settings);
        atomic_set(&d72x40_led_changed_at_ms, (atomic_val_t)k_uptime_get_32());
        atomic_set(&d72x40_led_splash_seen, 1);
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d72x40_led_splash_work,
                                    K_MSEC(duration));
        k_spin_unlock(&d72x40_splash_lock, key);
        d72x40_note_activity();
        atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
        d72x40_schedule_now();
    }
#else
    ARG_UNUSED(pattern);
    ARG_UNUSED(brightness);
    ARG_UNUSED(speed);
#endif
}

static int d72x40_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(eh);

    if (layer != NULL) {
        if (layer->layer < 32 && (d72x40_hidden_layers & BIT(layer->layer))) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
        d72x40_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS));
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Keys and encoders, this half's and the other's alike, as ZMK's idle
     * counts them; pointers arrive through the input callback above. */
    if (as_zmk_position_state_changed(eh) != NULL || as_zmk_sensor_event(eh) != NULL) {
        d72x40_note_activity();
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_PEER_POWER)
    const struct zmk_display_72x40_power_state_changed *power =
        as_zmk_display_72x40_power_state_changed(eh);

    if (power != NULL) {
        /* The relay numbers peripherals from one; the central itself is 0. */
        if (power->source != D72X40_PEER_SOURCE + 1) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        atomic_set(&d72x40_peer_powered, power->powered ? 1 : 0);
    }
#endif

    /* Every other subscribed event changes something the panel shows. */
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    d72x40_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS));
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(display_72x40, d72x40_listener);
ZMK_SUBSCRIPTION(display_72x40, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(display_72x40, zmk_position_state_changed);
ZMK_SUBSCRIPTION(display_72x40, zmk_sensor_event);
ZMK_SUBSCRIPTION(display_72x40, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
/* No endpoint change comes when the active profile connects or drops. */
ZMK_SUBSCRIPTION(display_72x40, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(display_72x40, zmk_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(display_72x40, zmk_peripheral_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_PEER_POWER)
ZMK_SUBSCRIPTION(display_72x40, zmk_display_72x40_power_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(display_72x40, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(display_72x40, zmk_hid_indicators_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCKING)
ZMK_SUBSCRIPTION(display_72x40, zmk_studio_core_lock_state_changed);
#endif

/*
 * The controller powers up showing whatever its memory holds. Clear it as
 * soon as the driver is up, rather than when ZMK has started and the first
 * status is drawn, so that boot shows a dark panel instead of noise.
 */
static int d72x40_panel_init(void) {
    if (!device_is_ready(d72x40_panel)) {
        LOG_WRN("display-72x40: panel %s is not ready", d72x40_panel->name);
        return 0;
    }

#if DT_NODE_HAS_PROP(D72X40_NODE, contrast)
    if (display_set_contrast(d72x40_panel, (uint8_t)DT_PROP(D72X40_NODE, contrast)) == 0) {
        d72x40_contrast_sent = DT_PROP(D72X40_NODE, contrast);
    }
#endif

    unsigned int cleared = 0;

    memset(&d72x40_render_state, 0, sizeof(d72x40_render_state));
    memset(d72x40_shown, 0, sizeof(d72x40_shown));
    d72x40_valid = 0;
    for (uint16_t page = 0; page < D72X40_PAGES; page++) {
        const struct display_buffer_descriptor desc = {
            .buf_size = D72X40_WIDTH,
            .width = D72X40_WIDTH,
            .height = D72X40_PAGE_HEIGHT,
            .pitch = D72X40_WIDTH,
        };

        if (display_write(d72x40_panel, 0, (uint16_t)(page * D72X40_PAGE_HEIGHT), &desc,
                          &d72x40_shown[(size_t)page * D72X40_WIDTH]) == 0) {
            d72x40_valid |= BIT(page);
            cleared++;
        }
    }
    int on = display_blanking_off(d72x40_panel);

    d72x40_panel_on = on == 0;
    LOG_INF("display-72x40: %s %ux%u, cleared %u of %u page(s), switch-on %d", d72x40_panel->name,
            (unsigned int)D72X40_WIDTH, (unsigned int)D72X40_HEIGHT, cleared, (unsigned int)D72X40_PAGES, on);

    atomic_set_bit(&d72x40_flags, D72X40_READY);
    d72x40_schedule_now();
    return 0;
}

SYS_INIT(d72x40_panel_init, POST_KERNEL, CONFIG_ZMK_DISPLAY_72X40_INIT_PRIORITY);

#if IS_ENABLED(CONFIG_PM_DEVICE)

#if DT_ON_BUS(D72X40_PANEL_NODE, i2c)
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_72X40_PM_INIT_PRIORITY < CONFIG_I2C_INIT_PRIORITY,
             "the first power-off device must come before the I2C bus in init order");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_72X40_PM_LATE_INIT_PRIORITY > CONFIG_I2C_INIT_PRIORITY,
             "the second power-off device must come after the I2C bus in init order");
#endif
#if DT_ON_BUS(D72X40_PANEL_NODE, spi)
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_72X40_PM_INIT_PRIORITY < CONFIG_SPI_INIT_PRIORITY,
             "the first power-off device must come before the SPI bus in init order");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_72X40_PM_LATE_INIT_PRIORITY > CONFIG_SPI_INIT_PRIORITY,
             "the second power-off device must come after the SPI bus in init order");
#endif

/*
 * ZMK powers off along two paths and neither raises an event: &soft_off
 * suspends every device in init order, idle sleep suspends them in the
 * reverse order. The panel keeps its supply and its picture after the SoC
 * powers off, so a suspend here is the last word: it stops the work handler
 * and switches the panel off, which needs the panel's bus. Two devices sit
 * either side of that bus in init order, so on either path one of them finds
 * the bus still up; the other finds the panel already dark and leaves it, as
 * it must, since by then the bus is down. Soft-off selects PM_DEVICE; without
 * it there is no suspend to hook.
 */
static int d72x40_pm_action(const struct device *dev, enum pm_device_action action) {
    ARG_UNUSED(dev);

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND: {
        struct k_work_sync sync;

        atomic_set_bit(&d72x40_flags, D72X40_SUSPENDED);
        k_work_cancel_delayable_sync(&d72x40_work, &sync);
        if (atomic_test_bit(&d72x40_flags, D72X40_READY) && d72x40_panel_on &&
            display_blanking_on(d72x40_panel) == 0) {
            d72x40_panel_on = false;
        }
        return 0;
    }
    case PM_DEVICE_ACTION_RESUME:
        atomic_clear_bit(&d72x40_flags, D72X40_SUSPENDED);
        d72x40_schedule_now();
        return 0;
    default:
        return -ENOTSUP;
    }
}

static int d72x40_pm_device_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

PM_DEVICE_DEFINE(display_72x40_pm, d72x40_pm_action);

DEVICE_DEFINE(display_72x40_pm, "display_72x40_pm", d72x40_pm_device_init,
              PM_DEVICE_GET(display_72x40_pm), NULL, NULL, POST_KERNEL,
              CONFIG_ZMK_DISPLAY_72X40_PM_INIT_PRIORITY, NULL);

PM_DEVICE_DEFINE(display_72x40_pm_late, d72x40_pm_action);

DEVICE_DEFINE(display_72x40_pm_late, "display_72x40_pm_late", d72x40_pm_device_init,
              PM_DEVICE_GET(display_72x40_pm_late), NULL, NULL, POST_KERNEL,
              CONFIG_ZMK_DISPLAY_72X40_PM_LATE_INIT_PRIORITY, NULL);

#endif /* IS_ENABLED(CONFIG_PM_DEVICE) */
