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

#define DT_DRV_COMPAT zmk_display_lite

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

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_LITE_PEER_POWER)
#include <zmk-display-lite/power_state.h>
#endif

#include <zmk-display-lite/display_lite.h>
#include <zmk-display-lite/display_lite_core.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "declare exactly one zmk,display-lite node");

#define DL_NODE DT_DRV_INST(0)
#define DL_PANEL_NODE DT_PHANDLE(DL_NODE, display)

/* The longest panel timeout a setter takes, as the Kconfig range allows. */
#define DL_BLANK_AFTER_MAX_MS 600000U
#define DL_WIDTH DT_PROP(DL_PANEL_NODE, width)
#define DL_HEIGHT DT_PROP(DL_PANEL_NODE, height)
#define DL_PAGES (DL_HEIGHT / DL_PAGE_HEIGHT)

/* Turning the drawing swaps the canvas's sides; the panel keeps its own. */
#define DL_ROTATION DT_PROP_OR(DL_NODE, rotation, 0)
#define DL_DRAW_WIDTH DL_CANVAS_WIDTH(DL_WIDTH, DL_HEIGHT, DL_ROTATION)
#define DL_DRAW_PAGES (DL_CANVAS_HEIGHT(DL_WIDTH, DL_HEIGHT, DL_ROTATION) / DL_PAGE_HEIGHT)

BUILD_ASSERT(DL_HEIGHT % DL_PAGE_HEIGHT == 0,
             "zmk,display-lite: the panel height must be a multiple of 8");
BUILD_ASSERT(DL_PAGES >= 1 && DL_PAGES <= 32,
             "zmk,display-lite: the panel must be 8 to 256 rows tall");
BUILD_ASSERT(DL_WIDTH >= DL_CELL && DL_WIDTH <= 1024,
             "zmk,display-lite: unsupported panel width");

BUILD_ASSERT(DL_ROTATION == 0 || DL_ROTATION == 90 || DL_ROTATION == 180 || DL_ROTATION == 270,
             "zmk,display-lite: rotation must be 0, 90, 180 or 270");
BUILD_ASSERT(!DL_TURNED(DL_ROTATION) || DL_WIDTH % DL_PAGE_HEIGHT == 0,
             "zmk,display-lite: a turned panel must be a multiple of 8 columns wide");

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_LITE_INIT_PRIORITY > CONFIG_DISPLAY_INIT_PRIORITY,
             "the panel clear must run after the display driver has initialised");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_LITE_INIT_PRIORITY > CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
             "the panel clear must run after ZMK's low-priority work queue has started");

#define DL_RETRIES 3

#define DL_HIDDEN_BIT(node_id, prop, idx) | BIT(DT_PROP_BY_IDX(node_id, prop, idx))
#define DL_HIDDEN_CHECK(node_id, prop, idx)                                                        \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < 32,                                          \
                 "zmk,display-lite: hidden-layers takes layer ids below 32");

COND_CODE_1(DT_NODE_HAS_PROP(DL_NODE, hidden_layers),
            (DT_FOREACH_PROP_ELEM(DL_NODE, hidden_layers, DL_HIDDEN_CHECK)), ())

static const zmk_keymap_layers_state_t dl_hidden_layers =
    0 COND_CODE_1(DT_NODE_HAS_PROP(DL_NODE, hidden_layers),
                  (DT_FOREACH_PROP_ELEM(DL_NODE, hidden_layers, DL_HIDDEN_BIT)), ());

static const struct device *const dl_panel = DEVICE_DT_GET(DL_PANEL_NODE);

#if DT_NODE_HAS_PROP(DL_NODE, reset_gpios)

BUILD_ASSERT(CONFIG_ZMK_DISPLAY_LITE_RESET_INIT_PRIORITY < CONFIG_DISPLAY_INIT_PRIORITY,
             "the panel reset must be pulsed before the display driver initialises");

static const struct gpio_dt_spec dl_reset = GPIO_DT_SPEC_GET(DL_NODE, reset_gpios);

/*
 * Start the panel from a known state at every firmware start, not only when
 * its own power-up circuit sees power come up, and give it time to answer
 * afterwards: an SSD1306 NACKs for a while after reset, and the display
 * driver, which sends its first command the moment it releases the line, has
 * no wait of its own and fails for good when that happens. So the line is
 * pulsed here, before the driver runs, and the driver is given no reset pin.
 */
static int dl_reset_panel(void) {
    if (!gpio_is_ready_dt(&dl_reset)) {
        LOG_ERR("display-lite: reset pin %s is not ready", dl_reset.port->name);
        return 0;
    }

    int err = gpio_pin_configure_dt(&dl_reset, GPIO_OUTPUT_ACTIVE);

    if (err < 0) {
        LOG_ERR("display-lite: cannot drive the reset pin (%d)", err);
        return 0;
    }

    k_sleep(K_MSEC(CONFIG_ZMK_DISPLAY_LITE_RESET_PULSE_MS));
    (void)gpio_pin_set_dt(&dl_reset, 0);
    k_sleep(K_MSEC(CONFIG_ZMK_DISPLAY_LITE_RESET_SETTLE_MS));
    LOG_DBG("display-lite: panel reset on %s pin %u", dl_reset.port->name, dl_reset.pin);
    return 0;
}

SYS_INIT(dl_reset_panel, POST_KERNEL, CONFIG_ZMK_DISPLAY_LITE_RESET_INIT_PRIORITY);

#endif /* DT_NODE_HAS_PROP(DL_NODE, reset_gpios) */

/*
 * The frame to show, and what the panel is known to show. Only the work
 * handler touches these once DL_READY is set; before that, only the boot-time
 * clear does. dl_valid has a bit per page whose dl_shown matches the panel.
 */
static uint8_t dl_want[DL_PAGES * DL_WIDTH];
static uint8_t dl_shown[DL_PAGES * DL_WIDTH];
static uint32_t dl_valid;
static bool dl_panel_on;
static uint8_t dl_failures;

#if DL_ROTATION == 0

/* Nothing to turn: the drawing is the frame. */
#define DL_DRAW_BUF dl_want

#else

static uint8_t dl_drawing[DL_DRAW_PAGES * DL_DRAW_WIDTH];

#define DL_DRAW_BUF dl_drawing

#endif

static const struct dl_canvas dl_canvas = {
    .buf = DL_DRAW_BUF,
    .width = DL_DRAW_WIDTH,
    .pages = DL_DRAW_PAGES,
};

enum {
    /* The boot-time clear has run; the work handler may use the panel. */
    DL_READY,
    /* Something the panel shows has changed. */
    DL_REDRAW,
    /* What the panel holds is unknown: send the whole frame. */
    DL_FULL,
    /* The keyboard is powering off; the panel stays dark. */
    DL_SUSPENDED,
    /* The panel's timeout has run out since the last key, encoder or pointer:
     * the only thing that darkens it while the keyboard runs. */
    DL_QUIET,
    /* A contrast a setter asked for is waiting to be sent. */
    DL_CONTRAST,
    /* A write is waiting for the sync device's next report. */
    DL_WAIT_PEER,
    /* That report has come. */
    DL_PEER_KICK,
};

/* The panel starts lit, and its timeout counts from boot. */
static atomic_t dl_flags = ATOMIC_INIT(BIT(DL_REDRAW));

static void dl_work_handler(struct k_work *work);

/* Defined at compile time: listeners may schedule it before any init runs. */
static K_WORK_DELAYABLE_DEFINE(dl_work, dl_work_handler);

static void dl_schedule(k_timeout_t delay) {
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &dl_work, delay);
}

static void dl_schedule_now(void) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &dl_work, K_NO_WAIT);
}

/*
 * The two values that can change while the keyboard runs: the panel's timeout
 * and its contrast. Each starts at its build-time default and is changed
 * through the setters in display_lite.h - display_lite_custom_settings.c calls
 * them for a Studio client - and read where it matters rather than latched.
 */
static atomic_t dl_blank_after_ms = ATOMIC_INIT(CONFIG_ZMK_DISPLAY_LITE_BLANK_AFTER_MS);
/* The contrast asked for, and the one the panel was last set to; -1 is
 * "never", which is where a node without a contrast property starts. */
static atomic_t dl_contrast_want = ATOMIC_INIT(DT_PROP_OR(DL_NODE, contrast, -1));
static int dl_contrast_sent = -1;

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
static atomic_t dl_active_at_ms;

static void dl_quiet_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(dl_quiet_work, dl_quiet_handler);

static void dl_note_activity(void) {
    atomic_set(&dl_active_at_ms, (atomic_val_t)k_uptime_get_32());
    /* Only the first key after the panel went dark has anything to do. */
    if (atomic_test_and_clear_bit(&dl_flags, DL_QUIET)) {
        dl_schedule_now();
    }
}

static void dl_quiet_handler(struct k_work *work) {
    ARG_UNUSED(work);

    const uint32_t after_ms = (uint32_t)atomic_get(&dl_blank_after_ms);

    /* Switched off, or nothing lit to switch off. This runs on the work
     * handler's own queue, so reading dl_panel_on here is safe. */
    if (after_ms == 0 || !dl_panel_on) {
        return;
    }

    const uint32_t quiet_ms = k_uptime_get_32() - (uint32_t)atomic_get(&dl_active_at_ms);

    if (quiet_ms < after_ms) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &dl_quiet_work,
                                  K_MSEC(after_ms - quiet_ms));
        return;
    }
    atomic_set_bit(&dl_flags, DL_QUIET);
    dl_schedule_now();
}

/* Armed only while the panel is lit, so a dark panel runs no clock at all. */
static void dl_arm_quiet(void) {
    const uint32_t after_ms = (uint32_t)atomic_get(&dl_blank_after_ms);

    if (after_ms > 0) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &dl_quiet_work,
                                  K_MSEC(after_ms));
    }
}

static void dl_activity_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(evt);
    ARG_UNUSED(user_data);

    dl_note_activity();
}

/* Every pointing device, this half's and the other's alike. */
INPUT_CALLBACK_DEFINE(NULL, dl_activity_input_cb, NULL);

/*
 * The setters. Each may be called from any thread - a Studio client's arrive
 * on its RPC thread - so they only record the value and schedule; the bus is
 * touched by the work handler alone, which keeps a write clear of the sync
 * device's reads. Each ignores a value it already has, because a settings
 * change for anything at all sends all of them round again.
 */
void zmk_display_lite_set_blank_after_ms(uint32_t ms) {
    ms = MIN(ms, DL_BLANK_AFTER_MAX_MS);
    if ((uint32_t)atomic_set(&dl_blank_after_ms, (atomic_val_t)ms) == ms) {
        return;
    }
    if (ms == 0) {
        (void)k_work_cancel_delayable(&dl_quiet_work);
        /* A panel the old timeout darkened is lit again. */
        if (atomic_test_and_clear_bit(&dl_flags, DL_QUIET)) {
            dl_schedule_now();
        }
        return;
    }
    /* Measured again from the last key, against the new length. */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &dl_quiet_work, K_NO_WAIT);
}

void zmk_display_lite_set_contrast(uint8_t contrast) {
    if (atomic_set(&dl_contrast_want, (atomic_val_t)contrast) == (atomic_val_t)contrast) {
        return;
    }
    atomic_set_bit(&dl_flags, DL_CONTRAST);
    dl_schedule_now();
}

#if DT_NODE_HAS_PROP(DL_NODE, sync_input_device)

#define DL_PEER_NODE DT_PHANDLE(DL_NODE, sync_input_device)

/* k_uptime_get_32() of the sync device's last completed report. */
static atomic_t dl_peer_last_ms;

static void dl_peer_input_cb(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);

    if (!evt->sync) {
        return;
    }
    atomic_set(&dl_peer_last_ms, (atomic_val_t)k_uptime_get_32());
    /* The device's read is over and its next sample is a report period
     * away: the one moment a write cannot run into it. */
    if (atomic_test_bit(&dl_flags, DL_WAIT_PEER)) {
        atomic_set_bit(&dl_flags, DL_PEER_KICK);
        dl_schedule_now();
    }
}

COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DL_PEER_NODE),
            (INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DL_PEER_NODE), dl_peer_input_cb, NULL);), ())

static bool dl_peer_busy(void) {
    uint32_t last = (uint32_t)atomic_get(&dl_peer_last_ms);

    return last != 0 &&
           (uint32_t)(k_uptime_get_32() - last) < CONFIG_ZMK_DISPLAY_LITE_SYNC_WINDOW_MS;
}

#else

static bool dl_peer_busy(void) { return false; }

#endif

static void dl_wait_peer(void) {
    atomic_set_bit(&dl_flags, DL_WAIT_PEER);
    /* If no report comes, the device is no longer busy when this fires. */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &dl_work,
                                K_MSEC(CONFIG_ZMK_DISPLAY_LITE_SYNC_WINDOW_MS));
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

/* The peripheral whose battery the panel shows. */
#define DL_PEER_SOURCE 0

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_LITE_PEER_POWER)

/* What that peripheral last said about its own power; see power_state.h. */
static atomic_t dl_peer_powered;

#endif

/*
 * Whether that peripheral is connected. ZMK raises no event for it and keeps
 * no public flag, but every split transport can list the sources it is
 * carrying, and the transports are an iterable section.
 */
static bool dl_peer_connected(void) {
    uint8_t sources[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];

    STRUCT_SECTION_FOREACH(zmk_split_transport_central, transport) {
        if (transport->api == NULL || transport->api->get_available_source_ids == NULL) {
            continue;
        }

        int count = transport->api->get_available_source_ids(sources);

        for (int i = 0; i < count; i++) {
            if (sources[i] == DL_PEER_SOURCE) {
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
static void dl_collect_layer(struct dl_status *st) {
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
        if (id < 32 && (dl_hidden_layers & BIT(id))) {
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
        dl_append(st->layer, sizeof(st->layer), name);
    } else {
        dl_append(st->layer, sizeof(st->layer), "LAYER ");
        dl_append_u8(st->layer, sizeof(st->layer), (uint8_t)shown_index);
    }
}

/* As ZMK's output widget: with no host selected, the preferred transport. */
static void dl_collect_output(struct dl_status *st) {
    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    enum zmk_transport transport = selected.transport;
    bool connected = transport != ZMK_TRANSPORT_NONE;

    if (!connected) {
        transport = zmk_endpoint_get_preferred_transport();
    }

    st->transport = DL_TRANSPORT_NONE;
    st->link = DL_LINK_DOWN;
    switch (transport) {
    case ZMK_TRANSPORT_USB:
        st->transport = DL_TRANSPORT_USB;
        st->link = connected ? DL_LINK_UP : DL_LINK_DOWN;
        break;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        st->transport = DL_TRANSPORT_BLE;
        st->profile = (uint8_t)(zmk_ble_active_profile_index() + 1);
        if (zmk_ble_active_profile_is_connected()) {
            st->link = DL_LINK_UP;
        } else if (zmk_ble_active_profile_is_open()) {
            st->link = DL_LINK_OPEN;
        }
        break;
#endif
    default:
        break;
    }
}

static void dl_collect(struct dl_status *st) {
    memset(st, 0, sizeof(*st));
    dl_collect_layer(st);
    dl_collect_output(st);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    st->battery = zmk_battery_state_of_charge();
#else
    st->battery = DL_BATTERY_HIDDEN;
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    /*
     * ZMK reports 0 both for a peripheral that has just disconnected and for
     * one whose battery really is flat (its own reading is 0% below 3.45 V),
     * so ask the transport which peripherals are connected: "--" means no
     * peripheral, and a connected one shows its level, 0% included.
     */
    if (!dl_peer_connected() ||
        zmk_split_central_get_peripheral_battery_level(DL_PEER_SOURCE, &st->peer_battery) < 0) {
        st->peer_battery = DL_BATTERY_UNKNOWN;
    }
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_LITE_PEER_POWER)
    else {
        st->peer_charging = atomic_get(&dl_peer_powered) != 0;
    }
#endif
#else
    st->peer_battery = DL_BATTERY_HIDDEN;
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

static bool dl_frame_differs(void) {
    struct dl_span span;
    uint16_t page = 0;
    uint16_t x = 0;

    return dl_next_span(dl_want, dl_shown, DL_WIDTH, DL_PAGES, dl_valid, &page, &x, &span);
}

/* What one pass sent, for the debug log. */
struct dl_pass {
    unsigned int runs;
    unsigned int bytes;
};

/*
 * Sends changed runs until none is left or *budget (when not negative) runs
 * out. Returns 0 when the panel matches the frame, 1 when runs remain, or a
 * negative errno.
 */
static int dl_flush(int *budget, struct dl_pass *pass) {
    struct dl_span span;
    uint16_t page = 0;
    uint16_t x = 0;

    while (dl_next_span(dl_want, dl_shown, DL_WIDTH, DL_PAGES, dl_valid, &page, &x, &span)) {
        if (*budget == 0) {
            return 1;
        }

        uint16_t len = (uint16_t)(span.x1 - span.x0 + 1);
        size_t at = (size_t)span.page * DL_WIDTH + span.x0;
        const struct display_buffer_descriptor desc = {
            .buf_size = len,
            .width = len,
            .height = DL_PAGE_HEIGHT,
            .pitch = len,
        };
        int err = display_write(dl_panel, span.x0, (uint16_t)(span.page * DL_PAGE_HEIGHT), &desc,
                                &dl_want[at]);

        if (err < 0) {
            /* Part of the page may have landed: its content is unknown. */
            dl_valid &= ~BIT(span.page);
            return err;
        }
        memcpy(&dl_shown[at], &dl_want[at], len);
        if (len == DL_WIDTH) {
            dl_valid |= BIT(span.page);
        }
        pass->runs++;
        pass->bytes += len;
        if (*budget > 0) {
            (*budget)--;
        }
    }

    return 0;
}

static void dl_retry(int err) {
    if (dl_failures < UINT8_MAX) {
        dl_failures++;
    }
    if (dl_failures == 1) {
        LOG_WRN("display-lite: panel write failed (%d), retrying", err);
    }
    if (dl_failures <= DL_RETRIES) {
        dl_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_LITE_RETRY_MS));
    } else if (dl_failures == DL_RETRIES + 1) {
        LOG_ERR("display-lite: panel write failed (%d), waiting for the next change", err);
    }
}

/* Sends the contrast a setter asked for, if the panel is not at it already. */
static void dl_send_contrast(void) {
    const int want = (int)atomic_get(&dl_contrast_want);

    if (want < 0 || want == dl_contrast_sent) {
        return;
    }

    int err = display_set_contrast(dl_panel, (uint8_t)want);

    if (err == 0) {
        dl_contrast_sent = want;
    } else {
        /* Not retried: the value stays asked for, and the next change or a
         * client writing it again sends it. */
        LOG_WRN("display-lite: contrast %d not taken (%d)", want, err);
    }
}

static void dl_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!atomic_test_bit(&dl_flags, DL_READY) || atomic_test_bit(&dl_flags, DL_SUSPENDED)) {
        return;
    }

    bool kicked = atomic_test_and_clear_bit(&dl_flags, DL_PEER_KICK);

    atomic_clear_bit(&dl_flags, DL_WAIT_PEER);

    if (atomic_test_and_clear_bit(&dl_flags, DL_FULL)) {
        dl_valid = 0;
    }

    if (atomic_test_and_clear_bit(&dl_flags, DL_REDRAW)) {
        struct dl_status st;

        dl_collect(&st);
        dl_render(&dl_canvas, &st);
#if DL_ROTATION != 0
        dl_project(&dl_canvas, dl_want, DL_WIDTH, DL_PAGES, DL_ROTATION);
#endif
    }

    /* A dark panel is not written: the frame goes out when it wakes. */
    bool awake = !atomic_test_bit(&dl_flags, DL_QUIET);
    bool stale = awake && dl_frame_differs();
    const bool contrast = atomic_test_bit(&dl_flags, DL_CONTRAST);

    if (!stale && awake == dl_panel_on && !contrast) {
        dl_failures = 0;
        return;
    }

    /* One bus operation per report while the sync device is reporting. */
    int budget = -1;

    if (dl_peer_busy()) {
        if (!kicked) {
            dl_wait_peer();
            return;
        }
        budget = 1;
    }

    /* A new contrast is one more bus operation, and counts as one. */
    if (contrast) {
        atomic_clear_bit(&dl_flags, DL_CONTRAST);
        dl_send_contrast();
        if (budget > 0) {
            budget--;
        }
        if (!stale && awake == dl_panel_on) {
            dl_failures = 0;
            return;
        }
    }

    struct dl_pass pass = {0};
    int err = 0;

    if (!awake) {
        if (budget == 0) {
            dl_wait_peer();
            return;
        }
        err = display_blanking_on(dl_panel);
        if (err == 0) {
            dl_panel_on = false;
            LOG_DBG("display-lite: panel off");
        }
    } else {
        int left = dl_flush(&budget, &pass);

        if (left < 0) {
            err = left;
        } else if (left > 0) {
            LOG_DBG("display-lite: sent %u run(s), %u byte(s), waiting for a report",
                    pass.runs, pass.bytes);
            dl_wait_peer();
            return;
        } else if (!dl_panel_on) {
            /* Switch on only once the new frame is in place. */
            if (budget == 0) {
                dl_wait_peer();
                return;
            }
            err = display_blanking_off(dl_panel);
            if (err == 0) {
                dl_panel_on = true;
            }
        }
        if (err == 0) {
            LOG_DBG("display-lite: sent %u run(s), %u byte(s)%s", pass.runs, pass.bytes,
                    budget == 0 ? " after a report" : "");
        }
    }

    if (err < 0) {
        dl_retry(err);
        return;
    }
    if (dl_panel_on) {
        dl_arm_quiet();
    }
    dl_failures = 0;
}

int zmk_display_lite_refresh(void) {
    if (!atomic_test_bit(&dl_flags, DL_READY)) {
        return -ENODEV;
    }
    atomic_set_bit(&dl_flags, DL_FULL);
    atomic_set_bit(&dl_flags, DL_REDRAW);
    dl_schedule_now();
    return 0;
}

static int dl_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(eh);

    if (layer != NULL) {
        if (layer->layer < 32 && (dl_hidden_layers & BIT(layer->layer))) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        atomic_set_bit(&dl_flags, DL_REDRAW);
        dl_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_LITE_UPDATE_DELAY_MS));
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Keys and encoders, this half's and the other's alike, as ZMK's idle
     * counts them; pointers arrive through the input callback above. */
    if (as_zmk_position_state_changed(eh) != NULL || as_zmk_sensor_event(eh) != NULL) {
        dl_note_activity();
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_DISPLAY_LITE_PEER_POWER)
    const struct zmk_display_lite_power_state_changed *power =
        as_zmk_display_lite_power_state_changed(eh);

    if (power != NULL) {
        /* The relay numbers peripherals from one; the central itself is 0. */
        if (power->source != DL_PEER_SOURCE + 1) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        atomic_set(&dl_peer_powered, power->powered ? 1 : 0);
    }
#endif

    /* Every other subscribed event changes something the panel shows. */
    atomic_set_bit(&dl_flags, DL_REDRAW);
    dl_schedule(K_MSEC(CONFIG_ZMK_DISPLAY_LITE_UPDATE_DELAY_MS));
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(display_lite, dl_listener);
ZMK_SUBSCRIPTION(display_lite, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(display_lite, zmk_position_state_changed);
ZMK_SUBSCRIPTION(display_lite, zmk_sensor_event);
ZMK_SUBSCRIPTION(display_lite, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
/* No endpoint change comes when the active profile connects or drops. */
ZMK_SUBSCRIPTION(display_lite, zmk_ble_active_profile_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_SUBSCRIPTION(display_lite, zmk_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(display_lite, zmk_peripheral_battery_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_LITE_PEER_POWER)
ZMK_SUBSCRIPTION(display_lite, zmk_display_lite_power_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(display_lite, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(display_lite, zmk_hid_indicators_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_STUDIO_LOCKING)
ZMK_SUBSCRIPTION(display_lite, zmk_studio_core_lock_state_changed);
#endif

/*
 * The controller powers up showing whatever its memory holds. Clear it as
 * soon as the driver is up, rather than when ZMK has started and the first
 * status is drawn, so that boot shows a dark panel instead of noise.
 */
static int dl_panel_init(void) {
    if (!device_is_ready(dl_panel)) {
        LOG_WRN("display-lite: panel %s is not ready", dl_panel->name);
        return 0;
    }

#if DT_NODE_HAS_PROP(DL_NODE, contrast)
    if (display_set_contrast(dl_panel, (uint8_t)DT_PROP(DL_NODE, contrast)) == 0) {
        dl_contrast_sent = DT_PROP(DL_NODE, contrast);
    }
#endif

    unsigned int cleared = 0;

    memset(dl_shown, 0, sizeof(dl_shown));
    dl_valid = 0;
    for (uint16_t page = 0; page < DL_PAGES; page++) {
        const struct display_buffer_descriptor desc = {
            .buf_size = DL_WIDTH,
            .width = DL_WIDTH,
            .height = DL_PAGE_HEIGHT,
            .pitch = DL_WIDTH,
        };

        if (display_write(dl_panel, 0, (uint16_t)(page * DL_PAGE_HEIGHT), &desc,
                          &dl_shown[(size_t)page * DL_WIDTH]) == 0) {
            dl_valid |= BIT(page);
            cleared++;
        }
    }
    int on = display_blanking_off(dl_panel);

    dl_panel_on = on == 0;
    LOG_INF("display-lite: %s %ux%u, cleared %u of %u page(s), switch-on %d", dl_panel->name,
            (unsigned int)DL_WIDTH, (unsigned int)DL_HEIGHT, cleared, (unsigned int)DL_PAGES, on);

    atomic_set_bit(&dl_flags, DL_READY);
    dl_schedule_now();
    return 0;
}

SYS_INIT(dl_panel_init, POST_KERNEL, CONFIG_ZMK_DISPLAY_LITE_INIT_PRIORITY);

#if IS_ENABLED(CONFIG_PM_DEVICE)

#if DT_ON_BUS(DL_PANEL_NODE, i2c)
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_LITE_PM_INIT_PRIORITY < CONFIG_I2C_INIT_PRIORITY,
             "the first power-off device must come before the I2C bus in init order");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_LITE_PM_LATE_INIT_PRIORITY > CONFIG_I2C_INIT_PRIORITY,
             "the second power-off device must come after the I2C bus in init order");
#endif
#if DT_ON_BUS(DL_PANEL_NODE, spi)
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_LITE_PM_INIT_PRIORITY < CONFIG_SPI_INIT_PRIORITY,
             "the first power-off device must come before the SPI bus in init order");
BUILD_ASSERT(CONFIG_ZMK_DISPLAY_LITE_PM_LATE_INIT_PRIORITY > CONFIG_SPI_INIT_PRIORITY,
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
static int dl_pm_action(const struct device *dev, enum pm_device_action action) {
    ARG_UNUSED(dev);

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND: {
        struct k_work_sync sync;

        atomic_set_bit(&dl_flags, DL_SUSPENDED);
        k_work_cancel_delayable_sync(&dl_work, &sync);
        if (atomic_test_bit(&dl_flags, DL_READY) && dl_panel_on &&
            display_blanking_on(dl_panel) == 0) {
            dl_panel_on = false;
        }
        return 0;
    }
    case PM_DEVICE_ACTION_RESUME:
        atomic_clear_bit(&dl_flags, DL_SUSPENDED);
        dl_schedule_now();
        return 0;
    default:
        return -ENOTSUP;
    }
}

static int dl_pm_device_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

PM_DEVICE_DEFINE(display_lite_pm, dl_pm_action);

DEVICE_DEFINE(display_lite_pm, "display_lite_pm", dl_pm_device_init,
              PM_DEVICE_GET(display_lite_pm), NULL, NULL, POST_KERNEL,
              CONFIG_ZMK_DISPLAY_LITE_PM_INIT_PRIORITY, NULL);

PM_DEVICE_DEFINE(display_lite_pm_late, dl_pm_action);

DEVICE_DEFINE(display_lite_pm_late, "display_lite_pm_late", dl_pm_device_init,
              PM_DEVICE_GET(display_lite_pm_late), NULL, NULL, POST_KERNEL,
              CONFIG_ZMK_DISPLAY_LITE_PM_LATE_INIT_PRIORITY, NULL);

#endif /* IS_ENABLED(CONFIG_PM_DEVICE) */
