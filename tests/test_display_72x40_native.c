/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdio.h>
#include <zmk-display-72x40/display_72x40_core.h>
#include "approved_portrait_frames.h"

void d72x40_native_reference(uint8_t *out, uint16_t rotation, const struct d72x40_status *status);
void d72x40_native_led_reference(uint8_t *out, uint16_t rotation, uint8_t pattern);

_Static_assert(D72X40_NATIVE_STORAGE == 1, "exercise the production direct storage path");
_Static_assert(sizeof(d72x40_status_advance) == 95, "compact width table");
_Static_assert(sizeof(d72x40_status_rows) == 950, "unchanged row pixels");

int main(void) {
    uint8_t actual[360], expected[360];
    for (uint16_t rotation = 90; rotation <= 270; rotation = (uint16_t)(rotation + 180)) {
        const struct d72x40_canvas canvas = {.buf = actual, .width = 40, .pages = 9, .rotation = rotation};
        for (size_t i = 0; i < sizeof(d72x40_approved_frames) / sizeof(d72x40_approved_frames[0]); i++) {
            const struct d72x40_approved_frame *g = &d72x40_approved_frames[i];
            struct d72x40_status status = {.profile = 2, .ble_available = true, .usb_available = true,
                .ble_link = g->route == 0 ? D72X40_LINK_UP : g->route == 1 ? D72X40_LINK_OPEN : D72X40_LINK_DOWN,
                .usb_link = g->route == 1 ? D72X40_LINK_UP : D72X40_LINK_OPEN,
                .battery = g->battery, .peer_battery = g->peer_battery,
                .studio_lock_available = true, .messenger_lock_available = true,
                .unlocked = g->studio, .messenger_unlocked = g->messenger,
                .caps_lock = g->caps, .charging = g->charging};
            strcpy(status.layer, g->layer);
            d72x40_native_reference(expected, rotation, &status);
            d72x40_render(&canvas, &status);
            assert(memcmp(actual, expected, 360) == 0);
            status.profile = UINT8_MAX; status.peer_charging = true;
            d72x40_native_reference(expected, rotation, &status);
            d72x40_render(&canvas, &status);
            assert(memcmp(actual, expected, 360) == 0);
        }
        for (unsigned pattern = 0; pattern <= 18; pattern++) {
            d72x40_native_led_reference(expected, rotation, (uint8_t)pattern);
            d72x40_render_led_splash(&canvas, (uint8_t)pattern, UINT8_MAX, UINT16_MAX);
            assert(memcmp(actual, expected, 360) == 0);
        }
    }
    puts("display direct storage: all frames/LED variants passed");
    return 0;
}
