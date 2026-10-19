/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * display_72x40_custom_settings.c with the LED splash: three published values
 * applied, clamped, through the driver's setters, and only on this module's
 * own setting changes.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IS_ENABLED(option) option
#define CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH 1
#define ARG_UNUSED(x) ((void)(x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) MIN(MAX((v), (lo)), (hi))
#define ZMK_EV_EVENT_BUBBLE 0
#define D72X40_BLANK_AFTER_MAX_S 600

typedef struct { int kind; } zmk_event_t;
typedef struct { int unused; } zmk_custom_CallRequest;
typedef struct { int unused; } pb_callback_t;
enum zmk_custom_setting_value_type {
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 = 1,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,
};
struct zmk_custom_setting_value {
    enum zmk_custom_setting_value_type type;
    int32_t int32_value;
};
struct zmk_custom_setting { struct zmk_custom_setting_value value; int read_result; };
struct zmk_custom_setting_changed { zmk_event_t header; const struct zmk_custom_setting *setting; };

static struct zmk_custom_setting d72x40_cs_blank_after = {{ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 30}, 0};
static struct zmk_custom_setting d72x40_cs_contrast = {{ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 128}, 0};
static struct zmk_custom_setting d72x40_cs_led_splash = {{ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1500}, 0};
static uint32_t blank_after_ms = UINT32_MAX;
static int contrast = -1;
static uint32_t splash_ms = UINT32_MAX;
static int applies;

/* Controlled registry read/callback ordering; no display policy is copied. */
static pthread_mutex_t control_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t control_changed = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t d72x40_settings_apply_mutex __attribute__((unused)) = PTHREAD_MUTEX_INITIALIZER;
static bool pause_old_read, old_copied, newer_done, newer_contended, release_old;
#define K_FOREVER (-1)

static void wait_locked(const bool *first, const bool *second) {
    struct timespec deadline;
    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    deadline.tv_sec += 5;
    while (!*first && (second == NULL || !*second)) {
        int rc = pthread_cond_timedwait(&control_changed, &control_mutex, &deadline);
        if (rc == ETIMEDOUT)
            fputs("settings callback interleaving watchdog expired\n", stderr);
        assert(rc == 0);
    }
}
static __attribute__((unused)) int k_mutex_lock(pthread_mutex_t *mutex, int timeout) {
    assert(timeout == K_FOREVER);
    int rc = pthread_mutex_trylock(mutex);
    if (rc == EBUSY) {
        assert(pthread_mutex_lock(&control_mutex) == 0);
        newer_contended = true;
        assert(pthread_cond_broadcast(&control_changed) == 0);
        assert(pthread_mutex_unlock(&control_mutex) == 0);
        rc = pthread_mutex_lock(mutex);
    }
    assert(rc == 0);
    return 0;
}
static __attribute__((unused)) int k_mutex_unlock(pthread_mutex_t *mutex) {
    assert(pthread_mutex_unlock(mutex) == 0);
    return 0;
}


static int zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                   struct zmk_custom_setting_value *value) {
    assert(pthread_mutex_lock(&control_mutex) == 0);
    *value = setting->value;
    int result = setting->read_result;
    if (pause_old_read && setting == &d72x40_cs_blank_after && value->int32_value == 30 && !old_copied) {
        old_copied = true;
        assert(pthread_cond_broadcast(&control_changed) == 0);
        wait_locked(&release_old, NULL);
    }
    assert(pthread_mutex_unlock(&control_mutex) == 0);
    return result;
}
static const struct zmk_custom_setting_changed *
as_zmk_custom_setting_changed(const zmk_event_t *eh) {
    return eh->kind == 1 ? (const void *)eh : NULL;
}
void zmk_display_72x40_set_blank_after_ms(uint32_t ms) {
    blank_after_ms = ms;
    applies++;
}
void zmk_display_72x40_set_contrast(uint8_t value) { contrast = value; }
void zmk_display_72x40_set_led_splash_ms(uint32_t ms) { splash_ms = ms; }

/* DRIVER_FUNCTIONS */

static void changed(const struct zmk_custom_setting *setting) {
    const struct zmk_custom_setting_changed event = {{1}, setting};
    assert(d72x40_settings_event_cb(&event.header) == ZMK_EV_EVENT_BUBBLE);
}


static void *older_callback(void *unused) {
    (void)unused;
    changed(&d72x40_cs_blank_after);
    return NULL;
}
static void *newer_callback(void *unused) {
    (void)unused;
    assert(pthread_mutex_lock(&control_mutex) == 0);
    d72x40_cs_blank_after.value.int32_value = 10;
    assert(pthread_mutex_unlock(&control_mutex) == 0);
    changed(&d72x40_cs_blank_after);
    assert(pthread_mutex_lock(&control_mutex) == 0);
    newer_done = true;
    assert(pthread_cond_broadcast(&control_changed) == 0);
    assert(pthread_mutex_unlock(&control_mutex) == 0);
    return NULL;
}
static void test_newer_registry_callback_wins(void) {
    d72x40_cs_blank_after = (struct zmk_custom_setting){{ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 30}, 0};
    d72x40_cs_contrast = (struct zmk_custom_setting){{ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 128}, 0};
    d72x40_cs_led_splash = (struct zmk_custom_setting){{ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1500}, 0};
    pause_old_read = true;
    old_copied = newer_done = newer_contended = release_old = false;
    pthread_t older, newer;
    assert(pthread_create(&older, NULL, older_callback, NULL) == 0);
    assert(pthread_mutex_lock(&control_mutex) == 0);
    wait_locked(&old_copied, NULL);
    assert(pthread_mutex_unlock(&control_mutex) == 0);
    assert(pthread_create(&newer, NULL, newer_callback, NULL) == 0);
    assert(pthread_mutex_lock(&control_mutex) == 0);
    wait_locked(&newer_done, &newer_contended);
    release_old = true;
    assert(pthread_cond_broadcast(&control_changed) == 0);
    assert(pthread_mutex_unlock(&control_mutex) == 0);
    assert(pthread_join(older, NULL) == 0);
    assert(pthread_join(newer, NULL) == 0);
    pause_old_read = false;
    fprintf(stderr, "settings callback registry=%d runtime_ms=%u contended=%u\n",
            d72x40_cs_blank_after.value.int32_value, blank_after_ms, newer_contended);
    assert(d72x40_cs_blank_after.value.int32_value == 10 && blank_after_ms == 10000);
    assert(contrast == 128 && splash_ms == 1500);
    puts("newer registry callback wins across an older paused read: PASS");
}

int main(void) {
    assert(!d72x40_namespace_handler(NULL, NULL));

    /* Boot (not a setting change): everything is applied, the timeout in ms. */
    const zmk_event_t initialized = {2};
    assert(d72x40_settings_event_cb(&initialized) == ZMK_EV_EVENT_BUBBLE);
    assert(blank_after_ms == 30000 && contrast == 128 && splash_ms == 1500);

    /* Another module's setting is not ours. */
    static const struct zmk_custom_setting foreign = {{ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1}, 0};
    changed(&foreign);
    assert(applies == 1);

    /* Each of ours applies, clamped into range. */
    d72x40_cs_blank_after.value.int32_value = 100000;
    changed(&d72x40_cs_blank_after);
    assert(blank_after_ms == D72X40_BLANK_AFTER_MAX_S * 1000U);
    d72x40_cs_blank_after.value.int32_value = -1;
    d72x40_cs_contrast.value.int32_value = 300;
    changed(&d72x40_cs_contrast);
    assert(blank_after_ms == 0 && contrast == 255);
    d72x40_cs_contrast.value.int32_value = -5;
    d72x40_cs_led_splash.value.int32_value = 20000;
    changed(&d72x40_cs_led_splash);
    assert(contrast == 0 && splash_ms == 10000);
    d72x40_cs_led_splash.value.int32_value = -1;
    changed(&d72x40_cs_led_splash);
    assert(splash_ms == 0);

    /* A value that cannot be read, or of another type, is left alone. */
    d72x40_cs_blank_after.read_result = -5;
    d72x40_cs_contrast.value.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL;
    d72x40_cs_led_splash.read_result = -5;
    const int before = applies;
    changed(&d72x40_cs_contrast);
    assert(applies == before && contrast == 0 && splash_ms == 0);
    test_newer_registry_callback_wins();
    puts("display settings bridge: PASS");
    return 0;
}
