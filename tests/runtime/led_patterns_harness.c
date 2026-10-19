/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdio.h>
#include <zmk-display-72x40/display_72x40.h>
typedef struct { unsigned kind; } zmk_event_t;
struct zmk_led_pattern_state_changed {
    zmk_event_t header;
    uint8_t pattern;
    uint8_t brightness;
    uint16_t speed;
};
#define ZMK_EV_EVENT_BUBBLE 0
static unsigned calls;
static uint8_t pattern, brightness;
static uint16_t speed;
static const struct zmk_led_pattern_state_changed *as_zmk_led_pattern_state_changed(const zmk_event_t *eh) {
    return eh->kind == 1 ? (const struct zmk_led_pattern_state_changed *)eh : NULL;
}
void zmk_display_72x40_show_led_settings(uint8_t p, uint8_t b, uint16_t s) {
    calls++; pattern = p; brightness = b; speed = s;
}

/* DRIVER_FUNCTIONS */

int main(void) {
    const zmk_event_t unrelated = {0};
    assert(d72x40_led_patterns_listener(&unrelated) == ZMK_EV_EVENT_BUBBLE && calls == 0);
    const struct zmk_led_pattern_state_changed normal = {{1}, 5, 60, 150};
    assert(d72x40_led_patterns_listener(&normal.header) == ZMK_EV_EVENT_BUBBLE);
    assert(calls == 1 && pattern == 5 && brightness == 60 && speed == 150);
    const struct zmk_led_pattern_state_changed limits = {{1}, UINT8_MAX, UINT8_MAX, UINT16_MAX};
    assert(d72x40_led_patterns_listener(&limits.header) == ZMK_EV_EVENT_BUBBLE);
    assert(calls == 2 && pattern == UINT8_MAX && brightness == UINT8_MAX && speed == UINT16_MAX);
    assert(d72x40_led_patterns_listener(&unrelated) == ZMK_EV_EVENT_BUBBLE && calls == 2);
    puts("display LED adapter: PASS");
    return 0;
}
