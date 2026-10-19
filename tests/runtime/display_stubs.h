/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Stand-ins for Zephyr and ZMK shared by the display_72x40.c harnesses, plus
 * the driver's file-scope state. A harness defines the CONFIG_ options and
 * devicetree facts it builds as, then includes this before its
 * DRIVER_FUNCTIONS marker.
 */

#pragma once

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zmk-display-72x40/display_72x40.h>
#include <string.h>

#include <zmk-display-72x40/display_72x40.h>
#include <zmk-display-72x40/display_72x40_core.h>
#include <zmk-display-72x40/display_72x40_splash.h>

#define IS_ENABLED(option) option
/* Not every build uses every stand-in. */
#define STUB __attribute__((unused))
#define BIT(n) (1UL << (n))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ARG_UNUSED(x) ((void)(x))
#define ATOMIC_INIT(value) (value)
/* Unevaluated printf() keeps the format strings checked and the arguments used. */
#define LOG_DBG(fmt, ...) ((void)sizeof(printf(fmt, ##__VA_ARGS__)))
#define LOG_INF(fmt, ...) LOG_DBG(fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) ((void)warnings++, LOG_DBG(fmt, ##__VA_ARGS__))
#define LOG_ERR(fmt, ...) ((void)errors++, LOG_DBG(fmt, ##__VA_ARGS__))
#define K_MSEC(ms) ((k_timeout_t){(ms)})
#define K_NO_WAIT ((k_timeout_t){0})
#define K_FOREVER ((k_timeout_t){-1})
#define ZMK_EV_EVENT_BUBBLE 0

/* The devicetree node, as the harness describes it. */
#define D72X40_NODE display_node
#define DT_NODE_HAS_PROP(node, prop) HAS_##prop
#define DT_PROP(node, prop) PROP_##prop
#define D72X40_WIDTH 72
#define D72X40_HEIGHT 40
#define D72X40_PAGES (D72X40_HEIGHT / D72X40_PAGE_HEIGHT)
#define D72X40_DRAW_WIDTH D72X40_CANVAS_WIDTH(D72X40_WIDTH, D72X40_HEIGHT, D72X40_ROTATION)
#define D72X40_DRAW_PAGES                                                                          \
    (D72X40_CANVAS_HEIGHT(D72X40_WIDTH, D72X40_HEIGHT, D72X40_ROTATION) / D72X40_PAGE_HEIGHT)
#define D72X40_RETRIES 3
#define D72X40_PEER_SOURCE 0
#define D72X40_BLANK_AFTER_MAX_MS 600000U
#define CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS 30000
#define CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH_MS 1500
#define CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS 50
#define CONFIG_ZMK_DISPLAY_72X40_RETRY_MS 100
#define CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS 25
#define CONFIG_ZMK_DISPLAY_72X40_RESET_PULSE_MS 1
#define CONFIG_ZMK_DISPLAY_72X40_RESET_SETTLE_MS 5
#define ZMK_KEYMAP_LAYERS_LEN 6
#define ZMK_KEYMAP_LAYER_ID_INVAL UINT8_MAX
#define ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT 2
#define HID_USAGE_LED_NUM_LOCK 0x01
#define HID_USAGE_LED_CAPS_LOCK 0x02
#define GPIO_OUTPUT_ACTIVE 0x1
#define STRUCT_SECTION_FOREACH(type, var)                                                          \
    for (struct type *var = split_transports; var < split_transports + split_transport_count;     \
         var++)

typedef long atomic_t;
typedef long atomic_val_t;
typedef struct { int64_t ms; } k_timeout_t;
typedef uint32_t zmk_keymap_layers_state_t;
typedef uint8_t zmk_keymap_layer_id_t;
typedef uint8_t zmk_keymap_layer_index_t;
struct k_work { void (*handler)(struct k_work *work); };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct k_work_q { int unused; };
struct k_work_sync { int unused; };
struct k_spinlock { bool held; };
typedef int k_spinlock_key_t;
static STUB void (*after_spin_unlock)(struct k_spinlock *lock);
static STUB void (*after_atomic_store)(atomic_t *target, atomic_val_t value);
static STUB k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = true;
    return 0;
}
static STUB void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(key == 0 && lock->held);
    lock->held = false;
    if (after_spin_unlock != NULL) {
        after_spin_unlock(lock);
    }
}
struct device { const char *name; bool ready; };
struct gpio_dt_spec { const struct device *port; uint8_t pin; };
struct input_event { uint8_t sync; };
struct display_buffer_descriptor { uint32_t buf_size; uint16_t width; uint16_t height;
                                   uint16_t pitch; };
enum pm_device_action { PM_DEVICE_ACTION_SUSPEND, PM_DEVICE_ACTION_RESUME,
                        PM_DEVICE_ACTION_TURN_OFF };
enum zmk_transport { ZMK_TRANSPORT_NONE, ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
struct zmk_endpoint_instance { enum zmk_transport transport; };
enum zmk_studio_core_lock_state { ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
                                  ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED };
enum zmk_usb_conn_state { ZMK_USB_CONN_NONE, ZMK_USB_CONN_POWERED, ZMK_USB_CONN_HID };
struct zmk_split_transport_central_api { int (*get_available_source_ids)(uint8_t *sources); };
struct zmk_split_transport_central { const struct zmk_split_transport_central_api *api; };
enum event_kind { EVENT_LED = 1, EVENT_LAYER, EVENT_POSITION, EVENT_SENSOR, EVENT_POWER,
                  EVENT_OTHER };
typedef struct { enum event_kind kind; } zmk_event_t;
struct zmk_led_pattern_state_changed { zmk_event_t header; uint8_t pattern; uint8_t brightness;
                                       uint16_t speed; };
struct zmk_layer_state_changed { zmk_event_t header; uint8_t layer; bool state; };
struct zmk_display_72x40_power_state_changed { zmk_event_t header; uint8_t source;
                                               bool powered; };

static STUB int warnings;
static STUB int errors;
static STUB uint32_t now_ms = 1000;
static STUB struct k_work_q lowprio_queue;

/* The panel: every write lands in a model of its memory. */
static STUB struct device panel_device = {.name = "panel", .ready = true};
static STUB uint8_t panel_memory[D72X40_PAGES * D72X40_WIDTH];
static STUB bool panel_lit;
static STUB int panel_writes;
static STUB int write_result;
static STUB int write_fail_after = -1; /* writes that succeed before the next failure */
static STUB int contrast_result;
static STUB int contrast_set = -1;
static STUB int blanking_result;
static STUB int blanking_calls;

/* The keymap and the host state. */
static STUB zmk_keymap_layer_id_t layer_ids[ZMK_KEYMAP_LAYERS_LEN] = {0, 1, 2, 3, 4, 5};
static STUB uint32_t active_layers = BIT(0);
static STUB const char *layer_names[ZMK_KEYMAP_LAYERS_LEN] = {"BASE", "NAV", "", NULL, "SYM",
                                                              "FN"};
static STUB zmk_keymap_layer_id_t default_layer;
static STUB enum zmk_transport selected_transport = ZMK_TRANSPORT_NONE;
static STUB enum zmk_transport preferred_transport = ZMK_TRANSPORT_BLE;
static STUB int ble_profile;
static STUB bool ble_connected;
static STUB bool ble_open;
static STUB uint8_t battery = 87;
static STUB bool usb_powered;
static STUB uint8_t indicators;
static STUB bool unlocked;
#ifndef HAS_external_lock
#define HAS_external_lock 0
#endif
static STUB bool external_lock_available = HAS_external_lock;
static STUB bool external_lock_unlocked;
static STUB enum zmk_usb_conn_state usb_state = ZMK_USB_CONN_NONE;
static STUB struct zmk_split_transport_central split_transports[2];
static STUB size_t split_transport_count;
static STUB int peer_level_result;
static STUB uint8_t peer_level = 42;

static STUB uint32_t k_uptime_get_32(void) { return now_ms; }
static STUB void k_sleep(k_timeout_t delay) { now_ms += (uint32_t)delay.ms; }
static STUB atomic_val_t atomic_get(const atomic_t *target) { return *target; }
static STUB atomic_val_t atomic_set(atomic_t *target, atomic_val_t value) {
    const atomic_val_t old = *target;
    *target = value;
    if (after_atomic_store != NULL) {
        after_atomic_store(target, value);
    }
    return old;
}
static STUB bool atomic_test_bit(const atomic_t *target, int bit) { return (*target >> bit) & 1; }
static STUB void atomic_set_bit(atomic_t *target, int bit) { *target |= 1L << bit; }
static STUB void atomic_clear_bit(atomic_t *target, int bit) { *target &= ~(1L << bit); }
static STUB bool atomic_test_and_clear_bit(atomic_t *target, int bit) {
    const bool was = atomic_test_bit(target, bit);
    atomic_clear_bit(target, bit);
    return was;
}
static STUB struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio_queue; }
/* Schedule leaves a pending item alone; reschedule moves it. */
static STUB int k_work_schedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                                          k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    if (work->scheduled) {
        return 0;
    }
    work->scheduled = true;
    work->delay_ms = delay.ms;
    return 1;
}
static STUB int k_work_reschedule_for_queue(struct k_work_q *queue,
                                            struct k_work_delayable *work, k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    work->scheduled = delay.ms >= 0;
    work->delay_ms = delay.ms;
    return 1;
}
static STUB int k_work_cancel_delayable(struct k_work_delayable *work) {
    work->scheduled = false;
    return 0;
}
static STUB bool k_work_cancel_delayable_sync(struct k_work_delayable *work,
                                              struct k_work_sync *sync) {
    ARG_UNUSED(sync);
    work->scheduled = false;
    return false;
}
static STUB bool device_is_ready(const struct device *dev) { return dev->ready; }
static STUB int display_write(const struct device *dev, uint16_t x, uint16_t y,
                              const struct display_buffer_descriptor *desc, const void *buf) {
    assert(dev == &panel_device && y % D72X40_PAGE_HEIGHT == 0);
    assert(desc->height == D72X40_PAGE_HEIGHT && desc->width == desc->buf_size);
    assert(x + desc->width <= D72X40_WIDTH && y / D72X40_PAGE_HEIGHT < D72X40_PAGES);
    if (write_fail_after == 0) {
        write_fail_after = -1;
        return -EIO;
    }
    if (write_fail_after > 0) {
        write_fail_after--;
    }
    if (write_result != 0) {
        return write_result;
    }
    memcpy(&panel_memory[(size_t)(y / D72X40_PAGE_HEIGHT) * D72X40_WIDTH + x], buf,
           desc->width);
    panel_writes++;
    return 0;
}
static STUB int display_set_contrast(const struct device *dev, uint8_t contrast) {
    assert(dev == &panel_device);
    if (contrast_result == 0) {
        contrast_set = contrast;
    }
    return contrast_result;
}
static STUB int display_blanking_on(const struct device *dev) {
    assert(dev == &panel_device);
    blanking_calls++;
    if (blanking_result == 0) {
        panel_lit = false;
    }
    return blanking_result;
}
static STUB int display_blanking_off(const struct device *dev) {
    assert(dev == &panel_device);
    blanking_calls++;
    if (blanking_result == 0) {
        panel_lit = true;
    }
    return blanking_result;
}
static STUB zmk_keymap_layer_id_t zmk_keymap_layer_default(void) { return default_layer; }
static STUB zmk_keymap_layer_id_t zmk_keymap_layer_index_to_id(zmk_keymap_layer_index_t index) {
    return layer_ids[index];
}
static STUB bool zmk_keymap_layer_active(zmk_keymap_layer_id_t id) {
    return (active_layers & BIT(id)) != 0;
}
static STUB const char *zmk_keymap_layer_name(zmk_keymap_layer_id_t id) {
    return layer_names[id];
}
static STUB struct zmk_endpoint_instance zmk_endpoint_get_selected(void) {
    return (struct zmk_endpoint_instance){.transport = selected_transport};
}
static STUB enum zmk_transport zmk_endpoint_get_preferred_transport(void) {
    return preferred_transport;
}
static STUB int zmk_ble_active_profile_index(void) { return ble_profile; }
static STUB bool zmk_ble_active_profile_is_connected(void) { return ble_connected; }
static STUB bool zmk_ble_active_profile_is_open(void) { return ble_open; }
static STUB uint8_t zmk_battery_state_of_charge(void) { return battery; }
static STUB bool zmk_usb_is_powered(void) { return usb_powered; }
static STUB enum zmk_usb_conn_state zmk_usb_get_conn_state(void) { return usb_state; }
struct zmk_display_72x40_lock_status zmk_display_72x40_get_external_lock_status(void) {
    return (struct zmk_display_72x40_lock_status){external_lock_available, external_lock_unlocked};
}
static STUB uint8_t zmk_hid_indicators_get_current_profile(void) { return indicators; }
static STUB enum zmk_studio_core_lock_state zmk_studio_core_get_lock_state(void) {
    return unlocked ? ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED : ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
}
static STUB int zmk_split_central_get_peripheral_battery_level(uint8_t source, uint8_t *level) {
    assert(source == D72X40_PEER_SOURCE);
    *level = peer_level;
    return peer_level_result;
}
static STUB const void *as_kind(const zmk_event_t *eh, enum event_kind kind) {
    return eh->kind == kind ? eh : NULL;
}
static STUB const struct zmk_led_pattern_state_changed *
as_zmk_led_pattern_state_changed(const zmk_event_t *eh) {
    return as_kind(eh, EVENT_LED);
}
static STUB const struct zmk_layer_state_changed *as_zmk_layer_state_changed(const zmk_event_t *eh) {
    return as_kind(eh, EVENT_LAYER);
}
static STUB const void *as_zmk_position_state_changed(const zmk_event_t *eh) {
    return as_kind(eh, EVENT_POSITION);
}
static STUB const void *as_zmk_sensor_event(const zmk_event_t *eh) {
    return as_kind(eh, EVENT_SENSOR);
}
static STUB const struct zmk_display_72x40_power_state_changed *
as_zmk_display_72x40_power_state_changed(const zmk_event_t *eh) {
    return as_kind(eh, EVENT_POWER);
}

/* The file-scope state of display_72x40.c, with its initial values. */
static STUB const zmk_keymap_layers_state_t d72x40_hidden_layers = HIDDEN_LAYERS;
static STUB const struct device *const d72x40_panel = &panel_device;
static STUB struct device reset_port = {.name = "gpio0", .ready = true};
static STUB const struct gpio_dt_spec d72x40_reset = {&reset_port, 9};
static STUB uint8_t d72x40_want[D72X40_PAGES * D72X40_WIDTH];
static STUB uint8_t d72x40_shown[D72X40_PAGES * D72X40_WIDTH];
static STUB uint32_t d72x40_valid;
static STUB bool d72x40_panel_on;
static STUB uint8_t d72x40_failures;
#if D72X40_ROTATION != 180
#define D72X40_DRAW_BUF d72x40_want
#else
static STUB uint8_t d72x40_drawing[D72X40_DRAW_PAGES * D72X40_DRAW_WIDTH];
#define D72X40_DRAW_BUF d72x40_drawing
#endif
static STUB const struct d72x40_canvas d72x40_canvas = {
    .buf = D72X40_DRAW_BUF,
    .width = D72X40_DRAW_WIDTH,
    .pages = D72X40_DRAW_PAGES,
    .rotation = D72X40_TURNED(D72X40_ROTATION) ? D72X40_ROTATION : 0,
};
enum {
    D72X40_READY,
    D72X40_REDRAW,
    D72X40_FULL,
    D72X40_SUSPENDED,
    D72X40_QUIET,
    D72X40_CONTRAST,
    D72X40_WAIT_PEER,
    D72X40_PEER_KICK,
};
static STUB atomic_t d72x40_flags = ATOMIC_INIT(BIT(D72X40_REDRAW));
struct d72x40_pass {
    unsigned int runs;
    unsigned int bytes;
};
static STUB void d72x40_work_handler(struct k_work *work);
static STUB void d72x40_schedule_now(void);
/* Gated in one build and so lifted after the functions that call them. */
static STUB bool d72x40_peer_busy(void);
static STUB void d72x40_collect(struct d72x40_status *st);
static STUB struct k_work_delayable d72x40_work = {.work = {.handler = d72x40_work_handler}};
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH)
static STUB struct k_spinlock d72x40_splash_lock;
static STUB atomic_t d72x40_led_settings;
static STUB atomic_t d72x40_led_changed_at_ms;
static STUB atomic_t d72x40_led_splash_seen;
static STUB atomic_t d72x40_led_splash_ms = ATOMIC_INIT(CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH_MS);
static STUB void d72x40_led_splash_handler(struct k_work *work);
static STUB struct k_work_delayable d72x40_led_splash_work = {
    .work = {.handler = d72x40_led_splash_handler}};
#endif
static STUB struct k_spinlock d72x40_quiet_policy_lock;
static STUB atomic_t d72x40_blank_after_ms = ATOMIC_INIT(CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS);
static STUB atomic_t d72x40_contrast_want = ATOMIC_INIT(INITIAL_CONTRAST);
static STUB int d72x40_contrast_sent = -1;
static STUB atomic_t d72x40_active_at_ms;
static STUB void d72x40_quiet_handler(struct k_work *work);
static STUB struct k_work_delayable d72x40_quiet_work = {.work = {.handler = d72x40_quiet_handler}};
#if HAS_sync_input_device
static STUB atomic_t d72x40_peer_last_ms;
#endif
#if IS_ENABLED(CONFIG_ZMK_DISPLAY_72X40_PEER_POWER)
static STUB atomic_t d72x40_peer_powered;
#endif

static STUB void run(struct k_work_delayable *work) {
    work->scheduled = false;
    work->work.handler(&work->work);
}

/* The panel shows exactly the frame the driver wants. */
static STUB bool panel_matches(void) {
    return memcmp(panel_memory, d72x40_want, sizeof(panel_memory)) == 0;
}

static STUB const zmk_event_t other_event = {EVENT_OTHER};
