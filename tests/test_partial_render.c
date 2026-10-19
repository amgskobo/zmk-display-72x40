/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 * Sentinel probes prove that partial updates leave unrelated groups untouched.
 * The probe intentionally alters RAM only in this owner fixture; restoring it
 * must then yield the exact complete frame. Firmware has no such probe.
 */
#include <assert.h>
#include <stdio.h>
#include <zmk-display-72x40/display_72x40_core.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    uint8_t pixels[D72X40_FRAME_BYTES], expected[D72X40_FRAME_BYTES];
    struct d72x40_canvas canvas = {.buf = pixels}, reference = {.buf = expected};
    struct d72x40_render_cache cache = {0};
    struct d72x40_status status = {.layer = "BASE", .battery = 80, .peer_battery = 70};
    bool led = strcmp(argv[1], "led") == 0;
    assert(led || strcmp(argv[1], "status") == 0);
    if (led) {
        (void)d72x40_render_led_cached(&canvas, &cache, 5, 60, 150);
    } else {
        (void)d72x40_render_cached(&canvas, &cache, &status);
    }
    /* This inverted-header byte is outside the changed battery/value group. */
    pixels[0] ^= 1;
    uint8_t sentinel = pixels[0];
    if (led) {
        (void)d72x40_render_led_cached(&canvas, &cache, 5, 80, 150);
        d72x40_render_led_splash(&reference, 5, 80, 150);
    } else {
        status.battery = 50;
        (void)d72x40_render_cached(&canvas, &cache, &status);
        d72x40_render(&reference, &status);
    }
    assert(pixels[0] == sentinel && "unchanged groups must not be redrawn");
    pixels[0] ^= 1;
    assert(memcmp(pixels, expected, sizeof(pixels)) == 0);
    puts("partial rendering: untouched header and complete changed frame PASS");
    return 0;
}
