/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 * Generic column rendering is an independent reference for specialization.
 */
#include <zmk-display-72x40/display_72x40_core.h>

void d72x40_native_reference(uint8_t *out, uint16_t rotation, const struct d72x40_status *status);
void d72x40_native_led_reference(uint8_t *out, uint16_t rotation, uint8_t pattern);

static void turn_pixels(const uint8_t *logical, uint8_t *out, uint16_t rotation) {
    memset(out, 0, 360);
    for (unsigned y = 0; y < 72; y++) {
        for (unsigned x = 0; x < 40; x++) {
            if ((logical[(y / 8) * 40 + x] >> (y % 8)) & 1) {
                unsigned px = rotation == 90 ? y : 71 - y;
                unsigned py = rotation == 90 ? 39 - x : x;
                size_t at = (size_t)(py / 8) * 72 + px;
                out[at] = (uint8_t)(out[at] | (uint8_t)(1u << (py % 8)));
            }
        }
    }
}

void d72x40_native_reference(uint8_t *out, uint16_t rotation, const struct d72x40_status *status) {
    uint8_t logical[360];
    const struct d72x40_canvas canvas = {.buf = logical, .width = 40, .pages = 9};
    d72x40_render(&canvas, status);
    turn_pixels(logical, out, rotation);
}

void d72x40_native_led_reference(uint8_t *out, uint16_t rotation, uint8_t pattern) {
    uint8_t logical[360];
    const struct d72x40_canvas canvas = {.buf = logical, .width = 40, .pages = 9};
    d72x40_render_led_splash(&canvas, pattern, UINT8_MAX, UINT16_MAX);
    turn_pixels(logical, out, rotation);
}
