/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <zmk/event_manager.h>
#include <zmk-display-72x40/display_72x40.h>
#include <zmk-led-patterns/events/state_changed.h>

static int d72x40_led_patterns_listener(const zmk_event_t *eh) {
    const struct zmk_led_pattern_state_changed *led = as_zmk_led_pattern_state_changed(eh);
    if (led != NULL) {
        zmk_display_72x40_show_led_settings(led->pattern, led->brightness, led->speed);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(display_72x40_led_patterns, d72x40_led_patterns_listener);
ZMK_SUBSCRIPTION(display_72x40_led_patterns, zmk_led_pattern_state_changed);
