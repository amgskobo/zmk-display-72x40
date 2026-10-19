/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * display_72x40.c with every option on: the fixed 72x40 panel, a reset
 * pin, a contrast, a sync input device, hidden layer 2, BLE, USB, battery
 * reporting, the peripheral's battery and power, HID indicators, Studio
 * locking and the LED splash.
 */
#define CONFIG_ZMK_DISPLAY_72X40_TEST 1
#define CONFIG_ZMK_BLE 1
#define CONFIG_ZMK_BATTERY_REPORTING 1
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING 1
#define CONFIG_ZMK_DISPLAY_72X40_PEER_POWER 1
#define CONFIG_ZMK_USB 1
#define CONFIG_ZMK_HID_INDICATORS 1
#define CONFIG_ZMK_STUDIO_LOCKING 1
#define HAS_external_lock 1
#define CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH 1
#define HAS_reset_gpios 1
#define HAS_sync_input_device 1
#define HAS_contrast 1
#define PROP_contrast 0x80
#define INITIAL_CONTRAST 0x80
#define HIDDEN_LAYERS BIT(2)
#include "display_stubs.h"

static int gpio_ready = 1;
static int configure_result;
static int set_result;
static int pin_level = -1;

static bool gpio_is_ready_dt(const struct gpio_dt_spec *spec) {
    assert(spec == &d72x40_reset);
    return gpio_ready;
}
static int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, int flags) {
    assert(spec == &d72x40_reset && flags == GPIO_OUTPUT_ACTIVE);
    if (configure_result == 0) {
        pin_level = 1;
    }
    return configure_result;
}
static int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value) {
    assert(spec == &d72x40_reset);
    if (set_result == 0) {
        pin_level = value;
    }
    return set_result;
}

static int preference_result;
static int __real_zmk_endpoint_set_preferred_transport(enum zmk_transport transport) {
    if (preference_result == 0) {
        preferred_transport = transport;
    }
    return preference_result;
}
int __wrap_zmk_endpoint_set_preferred_transport(enum zmk_transport transport);
int __wrap_zmk_endpoint_toggle_preferred_transport(void);
static int __real_zmk_endpoint_toggle_preferred_transport(void) {
    return __real_zmk_endpoint_set_preferred_transport(
        preferred_transport == ZMK_TRANSPORT_USB ? ZMK_TRANSPORT_BLE : ZMK_TRANSPORT_USB);
}
static void d72x40_schedule(k_timeout_t delay);

void zmk_display_72x40_test_quiet_candidate(struct k_work_delayable *quiet);
void zmk_display_72x40_test_splash_candidate(void);

/* DRIVER_FUNCTIONS */

static int splash_race_action;
void zmk_display_72x40_test_splash_candidate(void) {
    if (splash_race_action != 0) {
        splash_race_action = 0;
        zmk_display_72x40_set_led_splash_ms(200);
        zmk_display_72x40_show_led_settings(2, 20, 200);
    }
}

static int quiet_race_action;
static int quiet_race_hits;
void zmk_display_72x40_test_quiet_candidate(struct k_work_delayable *quiet) {
    ARG_UNUSED(quiet);
    const int action = quiet_race_action;
    quiet_race_action = 0;
    if (action != 0) {
        quiet_race_hits++;
        if (action == 1) {
            zmk_display_72x40_set_blank_after_ms(0);
        } else {
            d72x40_note_activity();
        }
    }
}

static const uint8_t *peer_sources;
static int peer_source_count;
static int list_sources(uint8_t *sources) {
    memcpy(sources, peer_sources, (size_t)peer_source_count);
    return peer_source_count;
}
static const struct zmk_split_transport_central_api no_list = {0};
static const struct zmk_split_transport_central_api listing = {list_sources};

static void event(enum event_kind kind) {
    const zmk_event_t eh = {kind};
    assert(d72x40_listener(&eh) == ZMK_EV_EVENT_BUBBLE);
}

static void layer(uint8_t id) {
    const struct zmk_layer_state_changed eh = {{EVENT_LAYER}, id, true};
    assert(d72x40_listener(&eh.header) == ZMK_EV_EVENT_BUBBLE);
}

static void power(uint8_t source, bool powered) {
    const struct zmk_display_72x40_power_state_changed eh = {{EVENT_POWER}, source, powered};
    assert(d72x40_listener(&eh.header) == ZMK_EV_EVENT_BUBBLE);
}

static void led(uint8_t pattern) {
    zmk_display_72x40_show_led_settings(pattern, 60, 150);
}

/* Runs the work until it stops rescheduling itself right away. */
static void settle(void) {
    for (int i = 0; i < 100 && d72x40_work.scheduled; i++) {
        run(&d72x40_work);
    }
}

static struct d72x40_status collected(void) {
    struct d72x40_status st;
    d72x40_collect(&st);
    return st;
}

static void test_reset_pin(void) {
    gpio_ready = 0;
    assert(d72x40_reset_panel() == 0 && errors == 1 && pin_level == -1);
    gpio_ready = 1;
    configure_result = -EIO;
    assert(d72x40_reset_panel() == 0 && errors == 2 && pin_level == -1);
    configure_result = 0;
    set_result = -EIO;
    assert(d72x40_reset_panel() == 0 && errors == 3 && pin_level == 1);
    set_result = 0;
    const uint32_t before = now_ms;
    assert(d72x40_reset_panel() == 0 && pin_level == 0);
    assert(now_ms == before + CONFIG_ZMK_DISPLAY_72X40_RESET_PULSE_MS +
                         CONFIG_ZMK_DISPLAY_72X40_RESET_SETTLE_MS);
}

static void test_before_ready(void) {
    /* Nothing touches the panel before the boot-time clear. */
    assert(zmk_display_72x40_refresh() == -ENODEV);
    led(3); /* not ready: not recorded */
    assert(d72x40_led_splash_seen == 0);
    d72x40_schedule_now();
    run(&d72x40_work);
    assert(panel_writes == 0);

    /* A panel that never came up is left alone. */
    panel_device.ready = false;
    assert(d72x40_panel_init() == 0 && warnings == 1);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_READY));
    panel_device.ready = true;
}

static void test_boot(void) {
    /* The clear: the contrast first, then every page, then light it. One page
     * that fails is left unknown, and one refused contrast is retried later. */
    contrast_result = -EIO;
    write_fail_after = 2;
    blanking_result = -EIO;
    d72x40_valid = 0x1f;
    assert(d72x40_panel_init() == 0);
    assert(d72x40_contrast_sent == -1 && d72x40_valid == 0x1b && !d72x40_panel_on);
    contrast_result = 0;
    blanking_result = 0;
    memset(panel_memory, 0xff, sizeof(panel_memory));
    memset(d72x40_shown, 0xff, sizeof(d72x40_shown));
    assert(d72x40_panel_init() == 0);
    assert(d72x40_contrast_sent == 0x80 && d72x40_valid == 0x1f && d72x40_panel_on);
    for (size_t i = 0; i < sizeof(panel_memory); i++) {
        assert(panel_memory[i] == 0 && d72x40_shown[i] == 0);
    }
    assert(atomic_test_bit(&d72x40_flags, D72X40_READY) && d72x40_work.scheduled);

    /* The first frame: the whole status, turned, and nothing else to send. */
    settle();
    assert(panel_matches() && d72x40_panel_on && d72x40_quiet_work.scheduled);
    const int writes = panel_writes;
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    d72x40_failures = 3;
    run(&d72x40_work);
    assert(panel_writes == writes && d72x40_failures == 0);
    const unsigned draws = render_calls;
    for (unsigned repeat = 0; repeat < 100; repeat++) {
        zmk_display_72x40_status_changed(); settle();
    }
    assert(render_calls == draws && panel_writes == writes);
    /* Unknown panel pages need transmission even when all bytes match. */
    d72x40_valid &= ~BIT(4);
    assert(d72x40_frame_differs());
    settle();
    d72x40_schedule_now(); settle();
    assert(panel_matches() && d72x40_valid == 0x1f);
    d72x40_shown[0] ^= 1;
    assert(d72x40_frame_differs());
    d72x40_shown[0] ^= 1;
    assert(!d72x40_frame_differs());

    /* A refresh sends the whole frame again. */
    assert(zmk_display_72x40_refresh() == 0);
    settle();
    assert(panel_writes == writes + 1 + D72X40_PAGES && panel_matches());
    /* ...drawn from the state as it is now, even a change nobody announced. */
    uint8_t before[sizeof(d72x40_want)];
    memcpy(before, d72x40_want, sizeof(before));
    active_layers = BIT(0) | BIT(1);
    assert(zmk_display_72x40_refresh() == 0);
    settle();
    assert(memcmp(before, d72x40_want, sizeof(before)) != 0 && panel_matches());
    active_layers = BIT(0);
    assert(zmk_display_72x40_refresh() == 0);
    settle();
}

static void test_status(void) {
    /* The layer shown: the highest active one that is not hidden, falling
     * back to the default, named if it has a name. */
    active_layers = BIT(0) | BIT(1);
    assert(strcmp(collected().layer, "NAV") == 0);
    active_layers = BIT(0) | BIT(2); /* hidden */
    assert(strcmp(collected().layer, "BASE") == 0);
    active_layers = BIT(0) | BIT(3); /* no name */
    assert(strcmp(collected().layer, "LAYER 3") == 0);
    layer_names[3] = "";
    assert(strcmp(collected().layer, "LAYER 3") == 0);
    layer_names[3] = NULL;
    layer_ids[5] = ZMK_KEYMAP_LAYER_ID_INVAL; /* a slot with no layer */
    layer_ids[4] = 40;                        /* past the hidden-layer mask */
    active_layers = BIT(0);
    assert(strcmp(collected().layer, "BASE") == 0);
    layer_ids[4] = 4;
    layer_ids[5] = 5;
    /* A default layer that is not at the bottom still stops the search. */
    default_layer = 1;
    active_layers = BIT(0);
    assert(strcmp(collected().layer, "NAV") == 0);
    /* A default with no name is named by its slot. */
    const char *nav = layer_names[1];
    layer_names[1] = NULL;
    default_layer = 1;
    active_layers = 0;
    assert(strcmp(collected().layer, "LAYER 1") == 0);
    layer_names[1] = nav;
    /* A default that no slot holds: the search runs out and names it. */
    default_layer = 5;
    layer_ids[5] = ZMK_KEYMAP_LAYER_ID_INVAL;
    active_layers = 0;
    assert(strcmp(collected().layer, "FN") == 0);
    active_layers = BIT(0);
    assert(strcmp(collected().layer, "BASE") == 0);
    layer_ids[5] = 5;
    active_layers = BIT(0);
    default_layer = 0;

    /* Every route/readiness/preference combination: at most one HID check.
     * Preference never overrides the route selected by ZMK, including fallback. */
    struct d72x40_status st;
    selected_transport = ZMK_TRANSPORT_BLE;
    preferred_transport = ZMK_TRANSPORT_BLE;
    ble_connected = true;
    usb_state = ZMK_USB_CONN_HID;
    st = collected();
    assert(st.ble_link == D72X40_LINK_UP && st.usb_link == D72X40_LINK_OPEN);
    for (int route = ZMK_TRANSPORT_NONE; route <= ZMK_TRANSPORT_BLE; route++) {
        for (int preference = ZMK_TRANSPORT_USB; preference <= ZMK_TRANSPORT_BLE; preference++) {
            for (int connected = 0; connected < 2; connected++) {
                for (int open = 0; open < 2; open++) {
                    for (int usb = ZMK_USB_CONN_NONE; usb <= ZMK_USB_CONN_HID; usb++) {
                        selected_transport = (enum zmk_transport)route;
                        preferred_transport = (enum zmk_transport)preference;
                        ble_connected = connected;
                        ble_open = open;
                        usb_state = (enum zmk_usb_conn_state)usb;
                        ble_profile = 2;
                        st = collected();
                        assert(st.profile == 3 && st.ble_available && st.usb_available);
                        assert((st.ble_link == D72X40_LINK_UP) ==
                               (connected && route == ZMK_TRANSPORT_BLE));
                        assert((st.usb_link == D72X40_LINK_UP) ==
                               (usb == ZMK_USB_CONN_HID && route == ZMK_TRANSPORT_USB));
                        assert(!(st.ble_link == D72X40_LINK_UP && st.usb_link == D72X40_LINK_UP));
                        if (route == ZMK_TRANSPORT_NONE) {
                            assert(st.transport == D72X40_TRANSPORT_NONE && st.link == D72X40_LINK_DOWN);
                        } else if (route == ZMK_TRANSPORT_USB) {
                            assert(st.transport == D72X40_TRANSPORT_USB && st.link == st.usb_link);
                        } else {
                            assert(st.transport == D72X40_TRANSPORT_BLE && st.link == st.ble_link);
                        }
                    }
                }
            }
        }
    }
    selected_transport = ZMK_TRANSPORT_USB;
    preferred_transport = ZMK_TRANSPORT_BLE;
    usb_state = ZMK_USB_CONN_HID;
    ble_connected = false;
    ble_open = false;
    st = collected();
    assert(st.transport == D72X40_TRANSPORT_USB && st.usb_link == D72X40_LINK_UP);
    assert(st.ble_link == D72X40_LINK_DOWN);

    /* Preference updates redraw even if ZMK's actual USB endpoint stays put. */
    atomic_clear_bit(&d72x40_flags, D72X40_REDRAW);
    assert(__wrap_zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_USB) == 0);
    assert(atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    assert(collected().transport == D72X40_TRANSPORT_USB);
    atomic_clear_bit(&d72x40_flags, D72X40_REDRAW);
    assert(__wrap_zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_USB) == 0);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    preference_result = -EIO;
    assert(__wrap_zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE) == -EIO);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    preference_result = 0;
    assert(__wrap_zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE) == 0);
    assert(atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    atomic_clear_bit(&d72x40_flags, D72X40_REDRAW);
    preference_result = -EIO;
    assert(__wrap_zmk_endpoint_toggle_preferred_transport() == -EIO);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    preference_result = 0;
    assert(__wrap_zmk_endpoint_toggle_preferred_transport() == 0);
    assert(atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    assert(preferred_transport == ZMK_TRANSPORT_USB);
    assert(__wrap_zmk_endpoint_toggle_preferred_transport() == 0);
    assert(preferred_transport == ZMK_TRANSPORT_BLE);
    selected_transport = ZMK_TRANSPORT_BLE;

    /* Battery, charging, caps lock and the Studio lock. */
    usb_powered = true;
    indicators = BIT(HID_USAGE_LED_CAPS_LOCK - HID_USAGE_LED_NUM_LOCK);
    unlocked = true;
    st = collected();
    assert(st.battery == 87 && st.charging && st.caps_lock);
    assert(st.studio_lock_available && st.unlocked);

    /* Connected inactive links have no check; switching the route moves it. */
    usb_state = ZMK_USB_CONN_HID;
    ble_connected = true;
    st = collected();
    assert(st.ble_available && st.usb_available);
    assert(st.ble_link == D72X40_LINK_UP && st.usb_link == D72X40_LINK_OPEN);
    selected_transport = ZMK_TRANSPORT_USB;
    st = collected();
    assert(st.ble_link == D72X40_LINK_OPEN && st.usb_link == D72X40_LINK_UP);
    selected_transport = ZMK_TRANSPORT_BLE;
    assert(collected().usb_link == D72X40_LINK_OPEN);
    usb_state = ZMK_USB_CONN_POWERED;
    st = collected();
    assert(st.usb_link == D72X40_LINK_DOWN && st.charging);
    ble_connected = false;
    ble_open = true;
    assert(collected().ble_link == D72X40_LINK_OPEN);
    ble_open = false;
    assert(collected().ble_link == D72X40_LINK_DOWN);
    ble_connected = true;
    usb_state = ZMK_USB_CONN_NONE;
    assert(collected().usb_link == D72X40_LINK_DOWN);

    external_lock_unlocked = false;
    assert(collected().messenger_lock_available && !collected().messenger_unlocked);
    external_lock_unlocked = true;
    assert(collected().messenger_unlocked && collected().unlocked);
    external_lock_available = false;
    assert(!collected().messenger_lock_available);
    external_lock_available = true;
    unlocked = false;
    assert(collected().messenger_unlocked && !collected().unlocked);
    unlocked = true;

    d72x40_work.scheduled = false;
    atomic_clear_bit(&d72x40_flags, D72X40_REDRAW);
    const atomic_t activity_before = d72x40_active_at_ms;
    zmk_display_72x40_status_changed();
    assert(atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    assert(d72x40_work.scheduled && d72x40_work.delay_ms == CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS);
    zmk_display_72x40_status_changed();
    assert(d72x40_active_at_ms == activity_before);

    /* Built from nothing: whatever the caller's buffer held is gone. */
    struct d72x40_status dirty_st, clean_st;
    memset(&dirty_st, 0xab, sizeof(dirty_st));
    memset(&clean_st, 0, sizeof(clean_st));
    d72x40_collect(&dirty_st);
    d72x40_collect(&clean_st);
    assert(memcmp(&dirty_st, &clean_st, sizeof(dirty_st)) == 0);
    /* No caps lock is no caps lock. */
    indicators = 0;
    assert(!collected().caps_lock);
    indicators = BIT(HID_USAGE_LED_CAPS_LOCK - HID_USAGE_LED_NUM_LOCK);

    /* The peripheral: "--" unless a transport lists it and it has a level. */
    assert(collected().peer_battery == D72X40_BATTERY_UNKNOWN);
    split_transports[0].api = NULL;
    split_transports[1].api = &no_list;
    split_transport_count = 2;
    assert(collected().peer_battery == D72X40_BATTERY_UNKNOWN);
    static const uint8_t others[] = {1};
    peer_sources = others;
    peer_source_count = 1;
    split_transports[1].api = &listing;
    assert(collected().peer_battery == D72X40_BATTERY_UNKNOWN);
    static const uint8_t ours[] = {1, D72X40_PEER_SOURCE};
    peer_sources = ours;
    peer_source_count = 2;
    peer_level_result = -ENODEV;
    assert(collected().peer_battery == D72X40_BATTERY_UNKNOWN);
    peer_level_result = 0;
    st = collected();
    assert(st.peer_battery == 42 && !st.peer_charging);
    static const uint8_t first[] = {D72X40_PEER_SOURCE, 1};
    peer_sources = first;
    assert(collected().peer_battery == 42);
    peer_sources = ours;

    /* Its power state comes from its own relay; others are ignored. */
    power(D72X40_PEER_SOURCE + 2, true);
    assert(!collected().peer_charging);
    atomic_clear_bit(&d72x40_flags, D72X40_REDRAW);
    power(D72X40_PEER_SOURCE + 1, true);
    assert(collected().peer_charging && atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    power(D72X40_PEER_SOURCE + 1, false);
    assert(!collected().peer_charging);
    power(D72X40_PEER_SOURCE + 1, true);
    settle();
}

static void test_events(void) {
    /* Hidden layers cost nothing; others redraw after the update delay. */
    d72x40_work.scheduled = false;
    layer(2);
    assert(!d72x40_work.scheduled);
    layer(1);
    assert(d72x40_work.scheduled && d72x40_work.delay_ms == CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS);
    d72x40_work.scheduled = false;
    layer(40);
    assert(d72x40_work.scheduled);
    d72x40_work.scheduled = false;
    event(EVENT_OTHER);
    assert(d72x40_work.scheduled);

    /* Keys and encoders only count as activity. */
    d72x40_work.scheduled = false;
    now_ms += 7;
    event(EVENT_POSITION);
    assert(d72x40_active_at_ms == now_ms && !d72x40_work.scheduled);
    event(EVENT_SENSOR);
    now_ms += 5;
    d72x40_activity_input_cb(NULL, NULL);
    assert(!d72x40_work.scheduled && d72x40_active_at_ms == now_ms);
    settle();
}

static void test_quiet(void) {
    /* The timeout counts from the last activity, re-arms while there is
     * some, and darkens the panel once it runs out. */
    now_ms += 100;
    d72x40_note_activity();
    run(&d72x40_quiet_work);
    assert(d72x40_quiet_work.scheduled &&
           d72x40_quiet_work.delay_ms == CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS);
    now_ms += 40;
    run(&d72x40_quiet_work);
    assert(d72x40_quiet_work.delay_ms == CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS - 40);
    now_ms -= 40;
    now_ms += CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS;
    run(&d72x40_quiet_work);
    assert(atomic_test_bit(&d72x40_flags, D72X40_QUIET));
    settle();
    assert(!d72x40_panel_on && !panel_lit);
    run(&d72x40_quiet_work); /* nothing lit: nothing to do */
    assert(!d72x40_quiet_work.scheduled);

    /* A change while dark is not written until it wakes. */
    layer(1);
    const int writes = panel_writes;
    const int blanks = blanking_calls;
    settle();
    assert(panel_writes == writes && blanking_calls == blanks);

    /* The next key lights it with the current frame. */
    d72x40_note_activity();
    settle();
    assert(d72x40_panel_on && panel_matches());

    /* A panel that will not go dark is retried. */
    atomic_set_bit(&d72x40_flags, D72X40_QUIET);
    blanking_result = -EIO;
    d72x40_schedule_now();
    run(&d72x40_work);
    assert(d72x40_panel_on && d72x40_failures == 1);
    blanking_result = 0;
    settle();
    assert(!d72x40_panel_on);
    d72x40_note_activity();
    /* ...and one that will not light again likewise. */
    blanking_result = -EIO;
    run(&d72x40_work);
    assert(!d72x40_panel_on && d72x40_failures == 1);
    blanking_result = 0;
    settle();
    assert(d72x40_panel_on);

    /* Setting the timeout: unchanged, off (lighting a dark panel), on. */
    zmk_display_72x40_set_blank_after_ms(CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS);
    zmk_display_72x40_set_blank_after_ms(0);
    assert(!d72x40_quiet_work.scheduled);
    d72x40_arm_quiet();
    assert(!d72x40_quiet_work.scheduled);
    run(&d72x40_quiet_work); /* switched off */
    assert(!atomic_test_bit(&d72x40_flags, D72X40_QUIET));
    zmk_display_72x40_set_blank_after_ms(1000);
    atomic_set_bit(&d72x40_flags, D72X40_QUIET);
    zmk_display_72x40_set_blank_after_ms(0);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_QUIET) && d72x40_work.scheduled);
    d72x40_quiet_work.scheduled = false;
    zmk_display_72x40_set_blank_after_ms(1);
    assert(d72x40_quiet_work.scheduled && d72x40_quiet_work.delay_ms == 0);
    d72x40_quiet_work.scheduled = false;
    d72x40_arm_quiet();
    assert(d72x40_quiet_work.scheduled && d72x40_quiet_work.delay_ms == 1);
    zmk_display_72x40_set_blank_after_ms(UINT32_MAX);
    assert(d72x40_blank_after_ms == D72X40_BLANK_AFTER_MAX_MS && d72x40_quiet_work.scheduled);
    zmk_display_72x40_set_blank_after_ms(CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS);
    settle();
}

static void test_quiet_race(bool disable) {
    zmk_display_72x40_set_blank_after_ms(0);
    d72x40_note_activity();
    settle();
    assert(d72x40_panel_on && panel_lit);
    const int blanks = blanking_calls;
    const int hits = quiet_race_hits;
    zmk_display_72x40_set_blank_after_ms(1000);
    now_ms += 1000;
    quiet_race_action = disable ? 1 : 2;
    run(&d72x40_quiet_work);
    settle();
    assert(quiet_race_hits == hits + 1);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_QUIET));
    assert(d72x40_panel_on && panel_lit && blanking_calls == blanks);
    if (disable) {
        assert(d72x40_blank_after_ms == 0 && !d72x40_quiet_work.scheduled);
    } else {
        assert(d72x40_active_at_ms == now_ms);
        assert(d72x40_quiet_work.scheduled && d72x40_quiet_work.delay_ms == 1000);
        now_ms += 1000;
        run(&d72x40_quiet_work);
        settle();
        assert(!d72x40_panel_on && !panel_lit && blanking_calls == blanks + 1);
    }
    zmk_display_72x40_set_blank_after_ms(0);
    settle();
    zmk_display_72x40_set_blank_after_ms(CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS);
}

/* The API hooks model preemption at the actual policy store. A competing
 * setter runs immediately unless the production spinlock excludes it; in that
 * case it runs on unlock. Neither hook blocks inside a critical section. */
static bool quiet_setter_inject;
static bool quiet_setter_waiting;
static int quiet_setter_runs;
static void quiet_setter_service(void) {
    quiet_setter_waiting = false;
    quiet_setter_runs++;
    zmk_display_72x40_set_blank_after_ms(2000);
    run(&d72x40_quiet_work);
    assert(d72x40_quiet_work.scheduled && d72x40_quiet_work.delay_ms == 2000);
}
static void quiet_setter_store(atomic_t *target, atomic_val_t value) {
    if (quiet_setter_inject && target == &d72x40_blank_after_ms && value == 0) {
        quiet_setter_inject = false;
        quiet_setter_waiting = true;
        if (!d72x40_quiet_policy_lock.held) {
            quiet_setter_service();
        }
    }
}
static void quiet_setter_unlock(struct k_spinlock *lock) {
    if (lock == &d72x40_quiet_policy_lock && quiet_setter_waiting) {
        quiet_setter_service();
    }
}
static void test_quiet_setter_ordering(void) {
    zmk_display_72x40_set_blank_after_ms(1000);
    d72x40_note_activity();
    settle();
    assert(d72x40_panel_on);
    const int before = quiet_setter_runs;
    after_atomic_store = quiet_setter_store;
    after_spin_unlock = quiet_setter_unlock;
    quiet_setter_inject = true;
    zmk_display_72x40_set_blank_after_ms(0);
    after_atomic_store = NULL;
    after_spin_unlock = NULL;
    printf("QUIET setters policy %ld scheduled %d delay %lld\n",
           (long)d72x40_blank_after_ms, d72x40_quiet_work.scheduled,
           (long long)d72x40_quiet_work.delay_ms);
    assert(!quiet_setter_inject && !quiet_setter_waiting);
    assert(quiet_setter_runs == before + 1 && d72x40_blank_after_ms == 2000);
    assert(d72x40_quiet_work.scheduled && d72x40_quiet_work.delay_ms == 2000);
    now_ms += 2000;
    run(&d72x40_quiet_work);
    settle();
    assert(!d72x40_panel_on && !panel_lit);
    zmk_display_72x40_set_blank_after_ms(0);
    settle();
    zmk_display_72x40_set_blank_after_ms(CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS);
}

static void test_contrast(void) {
    /* A new contrast is sent once, an unchanged one not at all. */
    zmk_display_72x40_set_contrast(0x80);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_CONTRAST));
    d72x40_work.scheduled = false;
    zmk_display_72x40_set_contrast(0x20);
    assert(d72x40_work.scheduled && d72x40_work.delay_ms == 0);
    run(&d72x40_work);
    assert(contrast_set == 0x20 && d72x40_contrast_sent == 0x20);
    zmk_display_72x40_set_contrast(0);
    run(&d72x40_work);
    assert(contrast_set == 0 && d72x40_contrast_sent == 0);

    /* A refused one is kept and retried; after too many, it waits. */
    zmk_display_72x40_set_contrast(0x30);
    contrast_result = -EIO;
    const int warned = warnings, erred = errors;
    for (int i = 1; i <= D72X40_RETRIES + 2; i++) {
        d72x40_work.scheduled = false;
        run(&d72x40_work);
        /* One warning per refusal, one for the first retry, one error
         * when the retries run out. */
        assert(d72x40_failures == i && warnings == warned + i + 1);
        assert(errors == erred + (i > D72X40_RETRIES ? 1 : 0));
        assert(d72x40_work.scheduled == (i <= D72X40_RETRIES));
    }
    d72x40_failures = UINT8_MAX;
    run(&d72x40_work);
    assert(d72x40_failures == UINT8_MAX);
    contrast_result = 0;
    run(&d72x40_work);
    assert(d72x40_contrast_sent == 0x30 && d72x40_failures == 0);

    /* Asked for with nothing else to send; or with no contrast known. */
    atomic_set_bit(&d72x40_flags, D72X40_CONTRAST);
    contrast_set = -1;
    d72x40_failures = 3;
    d72x40_quiet_work.scheduled = false;
    run(&d72x40_work);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_CONTRAST));
    assert(contrast_set == -1 && !d72x40_quiet_work.scheduled && d72x40_failures == 0);
    d72x40_contrast_want = -1;
    assert(d72x40_send_contrast() == 0);
    d72x40_contrast_want = 0x30;
}

static void test_failures(void) {
    /* A failed write leaves its page unknown and is retried. */
    layer(1);
    active_layers = BIT(0) | BIT(4);
    write_fail_after = 0;
    d72x40_schedule_now();
    run(&d72x40_work);
    assert(d72x40_failures == 1 && d72x40_work.scheduled);
    assert(d72x40_valid < 0x1f && __builtin_popcount(d72x40_valid) == 4);
    settle();
    assert(panel_matches() && d72x40_failures == 0);
}

static void peer_report(bool sync) {
    struct input_event evt = {.sync = sync};
    d72x40_peer_input_cb(&evt, NULL);
}

static void test_peer_clock_boundaries(void) {
    const uint32_t saved_now = now_ms;
    const atomic_val_t saved_flags = atomic_get(&d72x40_flags);
    const atomic_val_t saved_last = atomic_get(&d72x40_peer_last_ms);
    const struct k_work_delayable saved_work = d72x40_work;
    const uint32_t saved_valid = d72x40_valid;

    atomic_clear_bit(&d72x40_flags, D72X40_PEER_SEEN);
    atomic_set(&d72x40_peer_last_ms, 0);
    now_ms = 0;
    assert(!d72x40_peer_busy());
    peer_report(false);
    assert(!d72x40_peer_busy());
    peer_report(true);
    assert(d72x40_peer_busy() && "a sync report at timestamp zero must be recent");

    const int before = panel_writes;
    atomic_set_bit(&d72x40_flags, D72X40_FULL);
    run(&d72x40_work);
    assert(panel_writes == before && atomic_test_bit(&d72x40_flags, D72X40_WAIT_PEER));
    now_ms = CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS - 1;
    assert(d72x40_peer_busy());
    now_ms++;
    assert(!d72x40_peer_busy());

    now_ms = UINT32_MAX;
    peer_report(true);
    now_ms = 0;
    assert(d72x40_peer_busy());
    peer_report(true); /* A fresh report exactly at the wrap also has timestamp0. */
    assert(d72x40_peer_busy());
    now_ms = CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS;
    assert(!d72x40_peer_busy());

    now_ms = saved_now;
    atomic_set(&d72x40_flags, saved_flags);
    atomic_set(&d72x40_peer_last_ms, saved_last);
    d72x40_work = saved_work;
    d72x40_valid = saved_valid;
    puts("PASS: sync timestamps0/max/0 retain ordered window and bus admission");
}

static void test_sync_device(void) {
    test_peer_clock_boundaries();
    /* While the touch device reports, each bus operation waits for the end
     * of a report and goes out right after it, one per report. */
    assert(!d72x40_peer_busy()); /* never reported */
    peer_report(false);
    assert(!d72x40_peer_busy());
    peer_report(true);
    assert(d72x40_peer_busy());

    active_layers = BIT(0) | BIT(1);
    zmk_display_72x40_set_contrast(0x31);
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    run(&d72x40_work);
    assert(atomic_test_bit(&d72x40_flags, D72X40_WAIT_PEER) &&
           d72x40_work.delay_ms == CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS);

    /* Each report lets one operation through: the contrast, then runs. */
    int reports = 0;
    while (!panel_matches() || atomic_test_bit(&d72x40_flags, D72X40_CONTRAST)) {
        now_ms += 10;
        peer_report(true);
        assert(atomic_test_bit(&d72x40_flags, D72X40_PEER_KICK));
        const int before = panel_writes;
        run(&d72x40_work);
        assert(panel_writes - before <= 1);
        assert(++reports < 20);
    }
    assert(reports >= 2 && contrast_set == 0x31);

    atomic_set_bit(&d72x40_flags, D72X40_WAIT_PEER);
    d72x40_work.scheduled = false;
    peer_report(true);
    assert(d72x40_work.scheduled && d72x40_work.delay_ms == 0);
    atomic_clear_bit(&d72x40_flags, D72X40_WAIT_PEER);
    atomic_clear_bit(&d72x40_flags, D72X40_PEER_KICK);

    /* A report with nothing waiting kicks nothing. */
    peer_report(true);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_PEER_KICK));

    /* Going dark, and lighting again, also wait their turn. */
    atomic_set_bit(&d72x40_flags, D72X40_QUIET);
    run(&d72x40_work);
    assert(d72x40_panel_on && atomic_test_bit(&d72x40_flags, D72X40_WAIT_PEER));
    atomic_set_bit(&d72x40_flags, D72X40_PEER_KICK);
    zmk_display_72x40_set_contrast(0x32);
    run(&d72x40_work); /* the kick spends its one operation on the contrast */
    assert(d72x40_panel_on && contrast_set == 0x32);
    assert(atomic_test_bit(&d72x40_flags, D72X40_WAIT_PEER));
    atomic_set_bit(&d72x40_flags, D72X40_PEER_KICK);
    run(&d72x40_work);
    assert(!d72x40_panel_on);
    atomic_clear_bit(&d72x40_flags, D72X40_QUIET);
    atomic_set_bit(&d72x40_flags, D72X40_PEER_KICK);
    zmk_display_72x40_set_contrast(0x33);
    run(&d72x40_work); /* contrast again; the frame is already in place */
    assert(!d72x40_panel_on && atomic_test_bit(&d72x40_flags, D72X40_WAIT_PEER));
    atomic_set_bit(&d72x40_flags, D72X40_PEER_KICK);
    run(&d72x40_work);
    assert(d72x40_panel_on);

    /* The device goes quiet: the window closes and writes flow freely. */
    now_ms += CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS;
    assert(!d72x40_peer_busy());
    atomic_set_bit(&d72x40_flags, D72X40_WAIT_PEER);
    run(&d72x40_work);
    assert(!atomic_test_bit(&d72x40_flags, D72X40_WAIT_PEER));
    settle();
}

static void test_splash(void) {
    /* An LED change shows the splash for its duration, then the status. */
    uint8_t status[sizeof(d72x40_want)];
    memcpy(status, d72x40_want, sizeof(status));
    now_ms += 3;
    led(5);
    assert(d72x40_led_splash_seen && d72x40_led_splash_work.scheduled);
    assert((uint32_t)d72x40_led_settings == UINT32_C(0x00963c05));
    assert(d72x40_active_at_ms == now_ms);
    settle();
    uint8_t splash[sizeof(d72x40_want)];
    memcpy(splash, d72x40_want, sizeof(splash));
    assert(memcmp(splash, status, sizeof(splash)) != 0);
    /* The public input keeps all bits, including a high-bit 16-bit speed. */
    zmk_display_72x40_show_led_settings(UINT8_MAX, UINT8_MAX, UINT16_MAX);
    assert((uint32_t)d72x40_led_settings == UINT32_MAX);
    settle();
    uint8_t limits[sizeof(d72x40_want)];
    struct d72x40_canvas limits_cv = {.buf = limits};
    d72x40_render_led_splash(&limits_cv, UINT8_MAX, UINT8_MAX, UINT16_MAX);
    uint8_t limits_panel[sizeof(limits)];
    memcpy(limits_panel, limits_cv.buf, sizeof(limits_panel));
    assert(memcmp(d72x40_want, limits_panel, sizeof(limits_panel)) == 0);
    led(5);
    settle();
    now_ms += 500;
    run(&d72x40_led_splash_work);
    assert(d72x40_led_splash_work.scheduled && d72x40_led_splash_work.delay_ms == 1000);
    now_ms += 1000;
    run(&d72x40_led_splash_work);
    settle();
    assert(memcmp(status, d72x40_want, sizeof(status)) == 0);

    /* The duration: clamped, unchanged, off (dropping a pending splash), on. */
    atomic_clear_bit(&d72x40_flags, D72X40_REDRAW);
    d72x40_work.scheduled = false;
    zmk_display_72x40_set_led_splash_ms(20000);
    assert(d72x40_led_splash_ms == 10000);
    assert(atomic_test_bit(&d72x40_flags, D72X40_REDRAW) && d72x40_work.scheduled);
    zmk_display_72x40_set_led_splash_ms(10000);
    led(1);
    zmk_display_72x40_set_led_splash_ms(0);
    assert(!d72x40_led_splash_seen && !d72x40_led_splash_work.scheduled);
    led(2); /* switched off: ignored */
    assert(!d72x40_led_splash_seen);
    zmk_display_72x40_set_led_splash_ms(1500);
    led(3);
    d72x40_led_splash_ms = 0; /* switched off with a splash already seen */
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    settle();
    d72x40_led_splash_ms = 1500;
    now_ms += 5000; /* seen, on, and long over */
    atomic_set_bit(&d72x40_flags, D72X40_REDRAW);
    settle();
    assert(memcmp(status, d72x40_want, sizeof(status)) == 0);
}

static void test_splash_wrap(void) {
    zmk_display_72x40_set_blank_after_ms(0);
    zmk_display_72x40_set_led_splash_ms(500);
    led(5);
    const uint32_t changed = now_ms;
    settle();
    now_ms += 500;
    /* A redraw can reach the deadline before the expiry work itself runs. */
    zmk_display_72x40_status_changed();
    settle();
    run(&d72x40_led_splash_work);
    settle();
    uint8_t expired[sizeof(d72x40_want)];
    memcpy(expired, d72x40_want, sizeof(expired));
    const uint64_t later = (uint64_t)changed + (UINT64_C(1) << 32);
    now_ms = (uint32_t)later;
    zmk_display_72x40_status_changed();
    settle();
    printf("SPLASH wrap logical uptime %llu\n", (unsigned long long)later);
    assert(memcmp(expired, d72x40_want, sizeof(expired)) == 0);
    assert(!d72x40_led_splash_seen);
    run(&d72x40_led_splash_work); /* already retired, with a positive duration */
    assert(!d72x40_led_splash_seen);
    zmk_display_72x40_set_led_splash_ms(0);
    settle();
    run(&d72x40_led_splash_work); /* disabled callback retained from a running item */
    assert(!d72x40_led_splash_seen);
    zmk_display_72x40_set_led_splash_ms(1500);
}

static void test_splash_republish(void) {
    zmk_display_72x40_set_blank_after_ms(0);
    zmk_display_72x40_set_led_splash_ms(1000);
    led(5);
    settle();
    now_ms += 500;
    splash_race_action = 1;
    run(&d72x40_led_splash_work);
    assert(!splash_race_action);
    assert((uint32_t)d72x40_led_settings == UINT32_C(0x00c81402));
    assert(d72x40_led_splash_work.scheduled && d72x40_led_splash_work.delay_ms == 200);
    settle();
    now_ms += 200;
    run(&d72x40_led_splash_work);
    settle();
    assert(!d72x40_led_splash_seen);
    zmk_display_72x40_set_led_splash_ms(1500);
}

static void test_power_off(void) {
    /* Suspend darkens the panel and stops the work; resume picks up. */
    assert(d72x40_panel_on);
    blanking_result = -EIO;
    assert(d72x40_pm_action(NULL, PM_DEVICE_ACTION_SUSPEND) == 0 && d72x40_panel_on);
    blanking_result = 0;
    d72x40_work.scheduled = true;
    assert(d72x40_pm_action(NULL, PM_DEVICE_ACTION_SUSPEND) == 0 && !d72x40_panel_on);
    assert(!d72x40_work.scheduled);
    assert(d72x40_pm_action(NULL, PM_DEVICE_ACTION_SUSPEND) == 0); /* already dark */
    d72x40_schedule_now();
    run(&d72x40_work);
    assert(!d72x40_panel_on);
    assert(d72x40_pm_action(NULL, PM_DEVICE_ACTION_RESUME) == 0 && d72x40_work.scheduled);
    settle();
    assert(d72x40_panel_on);
    assert(d72x40_pm_action(NULL, PM_DEVICE_ACTION_TURN_OFF) == -ENOTSUP);
    assert(d72x40_pm_device_init(NULL) == 0);

    /* Before the clear, a suspend has no panel to darken. */
    atomic_clear_bit(&d72x40_flags, D72X40_READY);
    assert(d72x40_pm_action(NULL, PM_DEVICE_ACTION_SUSPEND) == 0 && d72x40_panel_on);
}

int main(int argc, char **argv) {
    if (argc == 2) {
        test_boot();
        if (strcmp(argv[1], "peer-zero") == 0) {
            test_peer_clock_boundaries();
        } else if (strcmp(argv[1], "quiet-setters") == 0) {
            test_quiet_setter_ordering();
        } else if (strcmp(argv[1], "splash-wrap") == 0) {
            test_splash_wrap();
        } else if (strcmp(argv[1], "splash-republish") == 0) {
            test_splash_republish();
        } else {
            assert(strcmp(argv[1], "quiet-disable") == 0 || strcmp(argv[1], "quiet-activity") == 0);
            test_quiet_race(strcmp(argv[1], "quiet-disable") == 0);
        }
        puts("display quiet interleaving: PASS");
        return 0;
    }
    assert(argc == 1);
    test_reset_pin();
    test_before_ready();
    test_boot();
    test_status();
    test_events();
    test_quiet();
    test_quiet_race(true);
    test_quiet_race(false);
    test_quiet_setter_ordering();
    test_contrast();
    test_failures();
    test_sync_device();
    test_splash();
    test_splash_wrap();
    test_splash_republish();
    test_power_off();
    puts("display (every option): PASS");
    return 0;
}
