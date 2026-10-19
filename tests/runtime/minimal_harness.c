/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * display_72x40.c with every option off: an unturned panel with no reset pin,
 * contrast or sync device, and no BLE, USB, battery, split, HID indicators,
 * Studio locking or LED splash. What differs from the full build is the #else
 * side of each option.
 */
#define HAS_reset_gpios 0
#define HAS_sync_input_device 0
#define HAS_contrast 0
#define INITIAL_CONTRAST -1
#define D72X40_ROTATION 0
#define HIDDEN_LAYERS 0
#include "display_stubs.h"

/* DRIVER_FUNCTIONS */

int main(void) {
    /* No sync device: never busy. */
    assert(!d72x40_peer_busy());

    /* No splash to time: a duration is accepted and ignored. */
    zmk_display_72x40_set_led_splash_ms(1500);
    zmk_display_72x40_show_led_settings(3, 60, 150);
    assert(!d72x40_work.scheduled);

    /* Batteries the build cannot know are hidden, not unknown. */
    struct d72x40_status st;
    d72x40_collect(&st);
    assert(st.battery == D72X40_BATTERY_HIDDEN && st.peer_battery == D72X40_BATTERY_HIDDEN);
    assert(!st.charging && !st.caps_lock && !st.studio_lock_available);
    assert(!st.messenger_lock_available && !st.messenger_unlocked);
    assert(strcmp(st.layer, "BASE") == 0 && st.transport == D72X40_TRANSPORT_NONE);

    /* The unturned frame is drawn straight into what the panel is sent. */
    assert(d72x40_panel_init() == 0 && contrast_set == -1);
    for (int i = 0; i < 10 && d72x40_work.scheduled; i++) {
        run(&d72x40_work);
    }
    assert(panel_matches() && d72x40_canvas.buf == d72x40_want);
    /* Status invalidation retains the existing activity/blank policy. */
    d72x40_work.scheduled = false;
    atomic_clear_bit(&d72x40_flags, D72X40_REDRAW);
    atomic_set_bit(&d72x40_flags, D72X40_QUIET);
    const atomic_t active_before = d72x40_active_at_ms;
    zmk_display_72x40_status_changed();
    assert(atomic_test_bit(&d72x40_flags, D72X40_REDRAW));
    assert(atomic_test_bit(&d72x40_flags, D72X40_QUIET));
    assert(d72x40_active_at_ms == active_before);
    assert(d72x40_work.scheduled && d72x40_work.delay_ms == CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS);
    puts("display (every option off): PASS");
    return 0;
}
