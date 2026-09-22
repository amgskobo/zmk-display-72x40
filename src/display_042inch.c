/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * A status panel for a monochrome, page-tiled display, drawn without LVGL.
 *
 * Nothing here redraws on a clock. An event that changes something the panel
 * shows marks the frame stale and schedules one pass on ZMK's low-priority
 * work queue. That pass reads the state, draws the whole frame into RAM
 * (360 bytes on a 72x40 panel), and sends only the columns that differ from
 * what the panel already shows. Events that change nothing shown - layers
 * listed in hidden-layers, for one - cost a bit test.
 *
 * A panel that shares its bus with a touch controller must not talk over the
 * controller's reads. With sync-input-device set, and while that device is
 * reporting, each write waits for its next report and goes out right after
 * it, one write per report.
 */

#define DT_DRV_COMPAT zmk_display_042inch

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

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER)
#include <zmk-display-042inch/power_state.h>
#endif

#include <zmk-display-042inch/display_042inch.h>
#include <zmk-display-042inch/display_042inch_core.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "declare exactly one zmk,display-042inch node");

#define D042_NODE DT_DRV_INST(0)
#define D042_PANEL_NODE DT_PHANDLE(D042_NODE, display)

/* The longest panel timeout a setter takes, as the Kconfig range allows. */
#define D042_BLANK_AFTER_MAX_MS 600000U
#define D042_WIDTH DT_PROP(D042_PANEL_NODE, width)
#define D042_HEIGHT DT_PROP(D042_PANEL_NODE, height)
#define D042_PAGES (D042_HEIGHT / D042_PAGE_HEIGHT)

/* Turning the drawing swaps the canvas's sides; the panel keeps its own. */
#define D042_ROTATION DT_PROP_OR(D042_NODE, rotation, 0)
#define D042_DRAW_WIDTH D042_CANVAS_WIDTH(D042_WIDTH, D042_HEIGHT, D042_ROTATION)
#define D042_DRAW_PAGES (D042_CANVAS_HEIGHT(D042_WIDTH, D042_HEIGHT, D042_ROTATION) / D042_PAGE_HEIGHT)

BUILD_ASSERT(D042_HEIGHT % D042_PAGE_HEIGHT == 0,
             "zmk,display-042inch: the panel height must be a multiple of 8");
BUILD_ASSERT(D042_PAGES >= 1 && D042_PAGES <= 32,
             "zmk,display-042inch: the panel must be 8 to 256 rows tall");
BUILD_ASSERT(D042_WIDTH >= D042_CELL && D042_WIDTH <= 1024,
             "zmk,display-042inch: unsupported panel width");

BUILD_ASSERT(D042_ROTATION == 0 || D042_ROTATION == 90 || D042_ROTATION == 180 || D042_ROTATION == 270,
             "zmk,display-042inch: rotation must be 0, 90, 180 or 270");
BUILD_ASSERT(!D042_TURNED(D042_ROTATION) || D042_WIDTH % D042_PAGE_HEIGHT == 0,
             "zmk,display-042inch: a turned panel must be a multiple of 8 columns wide");

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_042INCH_INIT_PRIORITY > CONFIG_DISPLAY_INIT_PRIORITY,
             "the panel clear must run after the display driver has initialised");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_042INCH_INIT_PRIORITY > CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
             "the panel clear must run after ZMK's low-priority work queue has started");

#define D042_RETRIES 3

#define D042_HIDDEN_BIT(node_id, prop, idx) | BIT(DT_PROP_BY_IDX(node_id, prop, idx))
#define D042_HIDDEN_CHECK(node_id, prop, idx)                                                        \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < 32,                                          \
                 "zmk,display-042inch: hidden-layers takes layer ids below 32");

COND_CODE_1(DT_NODE_HAS_PROP(D042_NODE, hidden_layers),
            (DT_FOREACH_PROP_ELEM(D042_NODE, hidden_layers, D042_HIDDEN_CHECK)), ())

static const zmk_keymap_layers_state_t d042_hidden_layers =
    0 COND_CODE_1(DT_NODE_HAS_PROP(D042_NODE, hidden_layers),
                  (DT_FOREACH_PROP_ELEM(D042_NODE, hidden_layers, D042_HIDDEN_BIT)), ());

static const struct device *const d042_panel = DEVICE_DT_GET(D042_PANEL_NODE);

#if DT_NODE_HAS_PROP(D042_NODE, reset_gpios)

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_042INCH_RESET_INIT_PRIORITY < CONFIG_DISPLAY_INIT_PRIORITY,
             "the panel reset must be pulsed before the display driver initialises");

static const struct gpio_dt_spec d042_reset = GPIO_DT_SPEC_GET(D042_NODE, reset_gpios);

/*
 * Start the panel from a known state at every firmware start, not only when
 * its own power-up circuit sees power come up, and give it time to answer
 * afterwards: an SSD1306 NACKs for a while after reset, and the display
 * driver, which sends its first command the moment it releases the line, has
 * no wait of its own and fails for good when that happens. So the line is
 * pulsed here, before the driver runs, and the driver is given no reset pin.
 */
static int d042_reset_panel(void) {
    if (!gpio_is_ready_dt(&d042_reset)) {
        LOG_ERR("display-042inch: reset pin %s is not ready", d042_reset.port->name);
        return 0;
    }

    int err = gpio_pin_configure_dt(&d042_reset, GPIO_OUTPUT_ACTIVE);

    if (err < 0) {
        LOG_ERR("display-042inch: cannot drive the reset pin (%d)", err);
        return 0;
    }

    k_sleep(K_MSEC(CONFIG_ZMK_DISPLAY_042INCH_RESET_PULSE_MS));
    (void)gpio_pin_set_dt(&d042_reset, 0);
    k_sleep(K_MSEC(CONFIG_ZMK_DISPLAY_042INCH_RESET_SETTLE_MS));
    LOG_DBG("display-042inch: panel reset on %s pin %u", d042_reset.port->name, d042_reset.pin);
    return 0;
}

SYS_INIT(d042_reset_panel, POST_KERNEL, CONFIG_ZMK_DISPLAY_042INCH_RESET_INIT_PRIORITY);

#endif /* DT_NODE_HAS_PROP(D042_NODE, reset_gpios) */

/*
 * The frame to show, and what the panel is known to show. Only the work
 * handler touches these once D042_READY is set; before that, only the boot-time
 * clear does. d042_valid has a bit per page whose d042_shown matches the panel.
 */
static uint8_t d042_want[D042_PAGES * D042_WIDTH];
static uint8_t d042_shown[D042_PAGES * D042_WIDTH];
static uint32_t d042_valid;
static bool d042_panel_on;
static uint8_t d042_failures;

#if D042_ROTATION == 0

/* Nothing to turn: the drawing is the frame. */
#define D042_DRAW_BUF d042_want

#else

static uint8_t d042_drawing[D042_DRAW_PAGES * D042_DRAW_WIDTH];

#define D042_DRAW_BUF d042_drawing

#endif

static const struct d042_canvas d042_canvas = {
    .buf = D042_DRAW_BUF,
    .width = D042_DRAW_WIDTH,
    .pages = D042_DRAW_PAGES,
};

enum {
    /* The boot-time clear has run; the work handler may use the panel. */
    D042_READY,
    /* Something the panel shows has changed. */
    D042_REDRAW,
    /* What the panel holds is unknown: send the whole frame. */
    D042_FULL,
    /* The keyboard is powering off; the panel stays dark. */
    D042_SUSPENDED,
    /* The panel's timeout has run out since the last key, encoder or pointer:
     * the only thing that darkens it while the keyboard runs. */
    D042_QUIET,
    /* A contrast a setter asked for is waiting to be sent. */
    D042_CONTRAST,
    /* A write is waiting for the sync device's next report. */
    D042_WAIT_PEER,
    /* That report has come. */
    D042_PEER_KICK,
};

/* The panel starts lit, and its timeout counts from boot. */
static atomic_t d042_flags = ATOMIC_INIT(BIT(D042_REDRAW));

static void d042_work_handler(struct k_work *work);

/* Defined at compile time: listeners may schedule it before any init runs. */
static K_WORK_DELAYABLE_DEFINE(d042_work, d042_work_handler);

static void d042_schedule(k_timeout_t delay) {
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_work, delay);
}

static void d042_schedule_now(void) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_work, K_NO_WAIT);
}

/*
 * The two values that can change while the keyboard runs: the panel's timeout
 * and its contrast. Each starts at its build-time default and is changed
 * through the setters in display_042inch.h - display_042inch_custom_settings.c calls
 * them for a Studio client - and read where it matters rather than latched.
 */
static atomic_t d042_blank_after_ms = ATOMIC_INIT(CONFIG_ZMK_DISPLAY_042INCH_BLANK_AFTER_MS);
/* The contrast asked for, and the one the panel was last set to; -1 is
 * "never", which is where a node without a contrast property starts. */
static atomic_t d042_contrast_want = ATOMIC_INIT(DT_PROP_OR(D042_NODE, contrast, -1));
static int d042_contrast_sent = -1;

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
static atomic_t d042_active_at_ms;

static void d042_quiet_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(d042_quiet_work, d042_quiet_handler);

static void d042_note_activity(void) {
    atomic_set(&d042_active_at_ms, (atomic_val_t)k_uptime_get_32());
    /* Only the first key after the panel went dark has anything to do. */
    if (atomic_test_and_clear_bit(&d042_flags, D042_QUIET)) {
        d042_schedule_now();
    }
}

static void d042_quiet_handler(struct k_work *work) {
    ARG_UNUSED(work);

    const uint32_t after_ms = (uint32_t)atomic_get(&d042_blank_after_ms);

    /* Switched off, or nothing lit to switch off. This runs on the work
     * handler's own queue, so reading d042_panel_on here is safe. */
    if (after_ms == 0 || !d042_panel_on) {
        return;
    }

    const uint32_t quiet_ms = k_uptime_get_32() - (uint32_t)atomic_get(&d042_active_at_ms);

    if (quiet_ms < after_ms) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_quiet_work,
                                  K_MSEC(after_ms - quiet_ms));
        return;
    }
    atomic_set_bit(&d042_flags, D042_QUIET);
    d042_schedule_now();
}

/* Armed only while the panel is lit, so a dark panel runs no clock at all. */
static void d042_arm_quiet(void) {
    const uint32_t after_ms = (uint32_t)atomic_get(&d042_blank_after_ms);

    if (after_ms > 0) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_quiet_work,
                                  K_MSEC(after_ms));
    }
}

static void d042_activity_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(evt);
    ARG_UNUSED(user_data);

    d042_note_activity();
}

/* Every pointing device, this half's and the other's alike. */
INPUT_CALLBACK_DEFINE(NULL, d042_activity_input_cb, NULL);

/*
 * The setters. Each may be called from any thread - a Studio client's arrive
 * on its RPC thread - so they only record the value and schedule; the bus is
 * touched by the work handler alone, which keeps a write clear of the sync
 * device's reads. Each ignores a value it already has, because a settings
 * change for anything at all sends all of them round again.
 */
void zmk_display_042inch_set_blank_after_ms(uint32_t ms) {
    ms = MIN(ms, D042_BLANK_AFTER_MAX_MS);
    if ((uint32_t)atomic_set(&d042_blank_after_ms, (atomic_val_t)ms) == ms) {
        return;
    }
    if (ms == 0) {
        (void)k_work_cancel_delayable(&d042_quiet_work);
        /* A panel the old timeout darkened is lit again. */
        if (atomic_test_and_clear_bit(&d042_flags, D042_QUIET)) {
            d042_schedule_now();
        }
        return;
    }
    /* Measured again from the last key, against the new length. */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_quiet_work, K_NO_WAIT);
}

void zmk_display_042inch_set_contrast(uint8_t contrast) {
    if (atomic_set(&d042_contrast_want, (atomic_val_t)contrast) == (atomic_val_t)contrast) {
        return;
    }
    atomic_set_bit(&d042_flags, D042_CONTRAST);
    d042_schedule_now();
}

#if DT_NODE_HAS_PROP(D042_NODE, sync_input_device)

#define D042_PEER_NODE DT_PHANDLE(D042_NODE, sync_input_device)

/* k_uptime_get_32() of the sync device's last completed report. */
static atomic_t d042_peer_last_ms;

static void d042_peer_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    if (!evt->sync) {
        return;
    }
    atomic_set(&d042_peer_last_ms, (atomic_val_t)k_uptime_get_32());
    /* The device's read is over and its next sample is a report period
     * away: the one moment a write cannot run into it. */
    if (atomic_test_bit(&d042_flags, D042_WAIT_PEER)) {
        atomic_set_bit(&d042_flags, D042_PEER_KICK);
        d042_schedule_now();
    }
}

COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(D042_PEER_NODE),
            (INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(D042_PEER_NODE), d042_peer_input_cb, NULL);), ())

static bool d042_peer_busy(void) {
    uint32_t last = (uint32_t)atomic_get(&d042_peer_last_ms);

    return last != 0 &&
           (uint32_t)(k_uptime_get_32() - last) < CONFIG_ZMK_DISPLAY_042INCH_SYNC_WINDOW_MS;
}

#else

static bool d042_peer_busy(void) { return false; }

#endif

static void d042_wait_peer(void) {
    atomic_set_bit(&d042_flags, D042_WAIT_PEER);
    /* If no report comes, the device is no longer busy when this fires. */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &d042_work,
                                K_MSEC(CONFIG_ZMK_DISPLAY_042INCH_SYNC_WINDOW_MS));
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

/* The peripheral whose battery the panel shows. */
#define D042_PEER_SOURCE 0

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER)

/* What that peripheral last said about its own power; see power_state.h. */
static atomic_t d042_peer_powered;

#endif

/*
 * Whether that peripheral is connected. ZMK raises no event for it and keeps
 * no public flag, but every split transport can list the sources it is
 * carrying, and the transports are an iterable section.
 */
static bool d042_peer_connected(void) {
    uint8_t sources[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];

    STRUCT_SECTION_FOREACH(zmk_split_transport_central, transport) {
        if (transport->api == NULL || transport->api->get_available_source_ids == NULL) {
            continue;
        }

        int count = transport->api->get_available_source_ids(sources);

        for (int i = 0; i < count; i++) {
            if (sources[i] == D042_PEER_SOURCE) {
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
static void d042_collect_layer(struct d042_status *st) {
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
        if (id < 32 && (d042_hidden_layers & BIT(id))) {
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
        d042_append(st->layer, sizeof(st->layer), name);
    } else {
        d042_append(st->layer, sizeof(st->layer), "LAYER ");
        d042_append_u8(st->layer, sizeof(st->layer), (uint8_t)shown_index);
    }
}

/* As ZMK's output widget: with no host selected, the preferred transport. */
static void d042_collect_output(struct d042_status *st) {
    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    enum zmk_transport transport = selected.transport;
    bool connected = transport != ZMK_TRANSPORT_NONE;

    if (!connected) {
        transport = zmk_endpoint_get_preferred_transport();
    }

    st->transport = D042_TRANSPORT_NONE;
    st->link = D042_LINK_DOWN;
    switch (transport) {
    case ZMK_TRANSPORT_USB:
        st->transport = D042_TRANSPORT_USB;
        st->link = connected ? D042_LINK_UP : D042_LINK_DOWN;
        break;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        st->transport = D042_TRANSPORT_BLE;
        st->profile = (uint8_t)(zmk_ble_active_profile_index() + 1);
        if (zmk_ble_active_profile_is_connected()) {
            st->link = D042_LINK_UP;
        } else if (zmk_ble_active_profile_is_open()) {
            st->link = D042_LINK_OPEN;
        }
        break;
#endif
    default:
        break;
    }
}

static void d042_collect(struct d042_status *st) {
    memset(st, 0, sizeof(*st));
    d042_collect_layer(st);
    d042_collect_output(st);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    st->battery = zmk_battery_state_of_charge();
#else
    st->battery = D042_BATTERY_HIDDEN;
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    /*
     * ZMK reports 0 both for a peripheral that has just disconnected and for
     * one whose battery really is flat (its own reading is 0% below 3.45 V),
     * so ask the transport which peripherals are connected: "--" means no
     * peripheral, and a connected one shows its level, 0% included.
     */
    if (!d042_peer_connected() ||
        zmk_split_central_get_peripheral_battery_level(D042_PEER_SOURCE, &st->peer_battery) < 0) {
        st->peer_battery = D042_BATTERY_UNKNOWN;
    }
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER)
    else {
        st->peer_charging = atomic_get(&d042_peer_powered) != 0;
    }
#endif
#else
    st->peer_battery = D042_BATTERY_HIDDEN;
#endif

#if IS_ENABLED(CONFIG_ZMK_USB)
    st->charging = zmk_usb_is_powered();
#endif

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    st->caps_lock = (zmk_hid_indicators_get_current_profile() &
                     BIT(HID_USAGE_LED_CAPS_LOCK - HID_USAGE_LED_NUM_LOCK)) != 0;
#endif

#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCKING)
    st->unlocked = zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
#endif
}

static bool d042_frame_differs(void) {
    struct d042_span span;
    uint16_t page = 0;
    uint16_t x = 0;

    return d042_next_span(d042_want, d042_shown, D042_WIDTH, D042_PAGES, d042_valid, &page, &x, &span);
}

/* What one pass sent, for the debug log. */
struct d042_pass {
    unsigned int runs;
    unsigned int bytes;
};

/*
 * Sends changed runs until none is left or *budget (when not negative) runs
 * out. Returns 0 when the panel matches the frame, 1 when runs remain, or a
 * negative errno.
 */
static int d042_flush(int *budget, struct d042_pass *pass) {
    struct d042_span span;
    uint16_t page = 0;
    uint16_t x = 0;

    while (d042_next_span(d042_want, d042_shown, D042_WIDTH, D042_PAGES, d042_valid, &page, &x, &span)) {
        if (*budget == 0) {
            return 1;
        }

        uint16_t len = (uint16_t)(span.x1 - span.x0 + 1);
        size_t at = (size_t)span.page * D042_WIDTH + span.x0;
        const struct display_buffer_descriptor desc = {
            .buf_size = len,
            .width = len,
            .height = D042_PAGE_HEIGHT,
            .pitch = len,
        };
        int err = display_write(d042_panel, span.x0, (uint16_t)(span.page * D042_PAGE_HEIGHT), &desc,
                                &d042_want[at]);

        if (err < 0) {
            /* Part of the page may have landed: its content is unknown. */
            d042_valid &= ~BIT(span.page);
            return err;
        }
        memcpy(&d042_shown[at], &d042_want[at], len);
        if (len == D042_WIDTH) {
            d042_valid |= BIT(span.page);
        }
        pass->runs++;
        pass->bytes += len;
        if (*budget > 0) {
            (*budget)--;
        }
    }

    return 0;
}

static void d042_retry(int err) {
    if (d042_failures < UINT8_MAX) {
        d042_failures++;
    }
    if (d042_failures == 1) {
        LOG_WRN("display-042inch: panel write failed (%d), retrying", err);
    }
    if (d042_failures <= D042_RETRIES) {
        d042_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_042INCH_RETRY_MS));
    } else if (d042_failures == D042_RETRIES + 1) {
        LOG_ERR("display-042inch: panel write failed (%d), waiting for the next change", err);
    }
}

/* Sends the contrast a setter asked for, if the panel is not at it already. */
static void d042_send_contrast(void) {
    const int want = (int)atomic_get(&d042_contrast_want);

    if (want < 0 || want == d042_contrast_sent) {
        return;
    }

    int err = display_set_contrast(d042_panel, (uint8_t)want);

    if (err == 0) {
        d042_contrast_sent = want;
    } else {
        /* Not retried: the value stays asked for, and the next change or a
         * client writing it again sends it. */
        LOG_WRN("display-042inch: contrast %d not taken (%d)", want, err);
    }
}

static void d042_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!atomic_test_bit(&d042_flags, D042_READY) || atomic_test_bit(&d042_flags, D042_SUSPENDED)) {
        return;
    }

    bool kicked = atomic_test_and_clear_bit(&d042_flags, D042_PEER_KICK);

    atomic_clear_bit(&d042_flags, D042_WAIT_PEER);

    if (atomic_test_and_clear_bit(&d042_flags, D042_FULL)) {
        d042_valid = 0;
    }

    if (atomic_test_and_clear_bit(&d042_flags, D042_REDRAW)) {
        struct d042_status st;

        d042_collect(&st);
        d042_render(&d042_canvas, &st);
#if D042_ROTATION != 0
        d042_project(&d042_canvas, d042_want, D042_WIDTH, D042_PAGES, D042_ROTATION);
#endif
    }

    /* A dark panel is not written: the frame goes out when it wakes. */
    bool awake = !atomic_test_bit(&d042_flags, D042_QUIET);
    bool stale = awake && d042_frame_differs();
    const bool contrast = atomic_test_bit(&d042_flags, D042_CONTRAST);

    if (!stale && awake == d042_panel_on && !contrast) {
        d042_failures = 0;
        return;
    }

    /* One bus operation per report while the sync device is reporting. */
    int budget = -1;

    if (d042_peer_busy()) {
        if (!kicked) {
            d042_wait_peer();
            return;
        }
        budget = 1;
    }

    /* A new contrast is one more bus operation, and counts as one. */
    if (contrast) {
        atomic_clear_bit(&d042_flags, D042_CONTRAST);
        d042_send_contrast();
        if (budget > 0) {
            budget--;
        }
        if (!stale && awake == d042_panel_on) {
            d042_failures = 0;
            return;
        }
    }

    struct d042_pass pass = {0};
    int err = 0;

    if (!awake) {
        if (budget == 0) {
            d042_wait_peer();
            return;
        }
        err = display_blanking_on(d042_panel);
        if (err == 0) {
            d042_panel_on = false;
            LOG_DBG("display-042inch: panel off");
        }
    } else {
        int left = d042_flush(&budget, &pass);

        if (left < 0) {
            err = left;
        } else if (left > 0) {
            LOG_DBG("display-042inch: sent %u run(s), %u byte(s), waiting for a report",
                    pass.runs, pass.bytes);
            d042_wait_peer();
            return;
        } else if (!d042_panel_on) {
            /* Switch on only once the new frame is in place. */
            if (budget == 0) {
                d042_wait_peer();
                return;
            }
            err = display_blanking_off(d042_panel);
            if (err == 0) {
                d042_panel_on = true;
            }
        }
        if (err == 0) {
            LOG_DBG("display-042inch: sent %u run(s), %u byte(s)%s", pass.runs, pass.bytes,
                    budget == 0 ? " after a report" : "");
        }
    }

    if (err < 0) {
        d042_retry(err);
        return;
    }
    if (d042_panel_on) {
        d042_arm_quiet();
    }
    d042_failures = 0;
}

int zmk_display_042inch_refresh(void) {
    if (!atomic_test_bit(&d042_flags, D042_READY)) {
        return -ENODEV;
    }
    atomic_set_bit(&d042_flags, D042_FULL);
    atomic_set_bit(&d042_flags, D042_REDRAW);
    d042_schedule_now();
    return 0;
}

static int d042_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(eh);

    if (layer != NULL) {
        if (layer->layer < 32 && (d042_hidden_layers & BIT(layer->layer))) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        atomic_set_bit(&d042_flags, D042_REDRAW);
        d042_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_042INCH_UPDATE_DELAY_MS));
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Keys and encoders, this half's and the other's alike, as ZMK's idle
     * counts them; pointers arrive through the input callback above. */
    if (as_zmk_position_state_changed(eh) != NULL || as_zmk_sensor_event(eh) != NULL) {
        d042_note_activity();
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER)
    const struct zmk_display_042inch_power_state_changed *power =
        as_zmk_display_042inch_power_state_changed(eh);

    if (power != NULL) {
        /* The relay numbers peripherals from one; the central itself is 0. */
        if (power->source != D042_PEER_SOURCE + 1) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        atomic_set(&d042_peer_powered, power->powered ? 1 : 0);
    }
#endif

    /* Every other subscribed event changes something the panel shows. */
    atomic_set_bit(&d042_flags, D042_REDRAW);
    d042_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_042INCH_UPDATE_DELAY_MS));
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(display_042inch, d042_listener);
ZMK_SUBSCRIPTION(display_042inch, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(display_042inch, zmk_position_state_changed);
ZMK_SUBSCRIPTION(display_042inch, zmk_sensor_event);
ZMK_SUBSCRIPTION(display_042inch, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
/* No endpoint change comes when the active profile connects or drops. */
ZMK_SUBSCRIPTION(display_042inch, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(display_042inch, zmk_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(display_042inch, zmk_peripheral_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_042INCH_PEER_POWER)
ZMK_SUBSCRIPTION(display_042inch, zmk_display_042inch_power_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(display_042inch, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(display_042inch, zmk_hid_indicators_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCKING)
ZMK_SUBSCRIPTION(display_042inch, zmk_studio_core_lock_state_changed);
#endif

/*
 * The controller powers up showing whatever its memory holds. Clear it as
 * soon as the driver is up, rather than when ZMK has started and the first
 * status is drawn, so that boot shows a dark panel instead of noise.
 */
static int d042_panel_init(void) {
    if (!device_is_ready(d042_panel)) {
        LOG_WRN("display-042inch: panel %s is not ready", d042_panel->name);
        return 0;
    }

#if DT_NODE_HAS_PROP(D042_NODE, contrast)
    if (display_set_contrast(d042_panel, (uint8_t)DT_PROP(D042_NODE, contrast)) == 0) {
        d042_contrast_sent = DT_PROP(D042_NODE, contrast);
    }
#endif

    unsigned int cleared = 0;

    memset(d042_shown, 0, sizeof(d042_shown));
    d042_valid = 0;
    for (uint16_t page = 0; page < D042_PAGES; page++) {
        const struct display_buffer_descriptor desc = {
            .buf_size = D042_WIDTH,
            .width = D042_WIDTH,
            .height = D042_PAGE_HEIGHT,
            .pitch = D042_WIDTH,
        };

        if (display_write(d042_panel, 0, (uint16_t)(page * D042_PAGE_HEIGHT), &desc,
                          &d042_shown[(size_t)page * D042_WIDTH]) == 0) {
            d042_valid |= BIT(page);
            cleared++;
        }
    }
    int on = display_blanking_off(d042_panel);

    d042_panel_on = on == 0;
    LOG_INF("display-042inch: %s %ux%u, cleared %u of %u page(s), switch-on %d", d042_panel->name,
            (unsigned int)D042_WIDTH, (unsigned int)D042_HEIGHT, cleared, (unsigned int)D042_PAGES, on);

    atomic_set_bit(&d042_flags, D042_READY);
    d042_schedule_now();
    return 0;
}

SYS_INIT(d042_panel_init, POST_KERNEL, CONFIG_ZMK_DISPLAY_042INCH_INIT_PRIORITY);

#if IS_ENABLED(CONFIG_PM_DEVICE)

#if DT_ON_BUS(D042_PANEL_NODE, i2c)
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_042INCH_PM_INIT_PRIORITY < CONFIG_I2C_INIT_PRIORITY,
             "the first power-off device must come before the I2C bus in init order");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_042INCH_PM_LATE_INIT_PRIORITY > CONFIG_I2C_INIT_PRIORITY,
             "the second power-off device must come after the I2C bus in init order");
#endif
#if DT_ON_BUS(D042_PANEL_NODE, spi)
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_042INCH_PM_INIT_PRIORITY < CONFIG_SPI_INIT_PRIORITY,
             "the first power-off device must come before the SPI bus in init order");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_042INCH_PM_LATE_INIT_PRIORITY > CONFIG_SPI_INIT_PRIORITY,
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
static int d042_pm_action(const struct device *dev, enum pm_device_action action) {
    ARG_UNUSED(dev);

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND: {
        struct k_work_sync sync;

        atomic_set_bit(&d042_flags, D042_SUSPENDED);
        k_work_cancel_delayable_sync(&d042_work, &sync);
        if (atomic_test_bit(&d042_flags, D042_READY) && d042_panel_on &&
            display_blanking_on(d042_panel) == 0) {
            d042_panel_on = false;
        }
        return 0;
    }
    case PM_DEVICE_ACTION_RESUME:
        atomic_clear_bit(&d042_flags, D042_SUSPENDED);
        d042_schedule_now();
        return 0;
    default:
        return -ENOTSUP;
    }
}

static int d042_pm_device_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

PM_DEVICE_DEFINE(display_042inch_pm, d042_pm_action);

DEVICE_DEFINE(display_042inch_pm, "display_042inch_pm", d042_pm_device_init,
              PM_DEVICE_GET(display_042inch_pm), NULL, NULL, POST_KERNEL,
              CONFIG_ZMK_DISPLAY_042INCH_PM_INIT_PRIORITY, NULL);

PM_DEVICE_DEFINE(display_042inch_pm_late, d042_pm_action);

DEVICE_DEFINE(display_042inch_pm_late, "display_042inch_pm_late", d042_pm_device_init,
              PM_DEVICE_GET(display_042inch_pm_late), NULL, NULL, POST_KERNEL,
              CONFIG_ZMK_DISPLAY_042INCH_PM_LATE_INIT_PRIORITY, NULL);

#endif /* IS_ENABLED(CONFIG_PM_DEVICE) */
