/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 * Host CPU comparison only; not MCU timing, current or I2C evidence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <zmk-display-72x40/display_72x40_core.h>

static uint8_t pixels[D72X40_FRAME_BYTES];
static struct d72x40_canvas canvas = {.buf = pixels};
static struct d72x40_status status = {
    .layer = "BASE", .profile = 2, .battery = 80, .peer_battery = 70,
    .ble_available = true, .usb_available = true, .ble_link = D72X40_LINK_UP,
    .usb_link = D72X40_LINK_OPEN, .studio_lock_available = true,
    .messenger_lock_available = true, .charging = true, .peer_charging = true,
};
#ifdef D72X40_RENDER_CACHE
static struct d72x40_render_cache cache;
#endif
/* Keep the complete production draw observable across compiler optimizations. */
__attribute__((noinline)) static unsigned draw(unsigned mode, unsigned phase) {
    if (mode < 2 || (mode >= 4 && mode <= 7)) {
        if (mode < 2) {
            status.profile = (uint8_t)(phase % 255u);
            status.caps_lock = (phase & 1u) != 0;
            status.unlocked = (phase & 2u) != 0;
            status.messenger_unlocked = (phase & 4u) != 0;
        } else if (mode == 4) {
            strcpy(status.layer, (phase & 1u) ? "NAV" : "BASE");
        } else if (mode == 5) {
            status.battery = (uint8_t)(phase % 101u);
        } else if (mode == 6) {
            status.messenger_unlocked = (phase & 1u) != 0;
        } else {
            status.profile = (uint8_t)(phase % 255u);
        }
#ifdef D72X40_RENDER_CACHE
        return d72x40_render_cached(&canvas, &cache, &status) ? 1u : 0u;
#else
        d72x40_render(&canvas, &status);
        return 1;
#endif
    }
    uint8_t id = mode == 9 || mode == 10 ? 5 : (uint8_t)(phase % 19u);
    uint8_t brightness = mode == 8 || mode == 10 ? 60 : (uint8_t)(phase % 101u);
    uint16_t speed = mode == 8 || mode == 9 ? 150 : (uint16_t)phase;
#ifdef D72X40_RENDER_CACHE
    return d72x40_render_led_cached(&canvas, &cache, id, brightness, speed) ? 1u : 0u;
#else
    d72x40_render_led_splash(&canvas, id, brightness, speed);
    return 1;
#endif
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    unsigned mode = (unsigned)strtoul(argv[1], NULL, 10);
    unsigned count = (unsigned)strtoul(argv[2], NULL, 10);
    if (mode >= 11 || count == 0) return 2;
    uint64_t sum = 0;
    unsigned draws = 0;
    clock_t start = clock();
    for (unsigned i = 0; i < count; i++) {
        unsigned phase = mode == 0 || mode == 2 ? i / 100u : i;
        draws += draw(mode, phase);
        sum += pixels[i % D72X40_FRAME_BYTES];
    }
    clock_t end = clock();
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < sizeof(pixels); i++) hash = (hash ^ pixels[i]) * UINT64_C(1099511628211);
    printf("{\"mode\":%u,\"iterations\":%u,\"draws\":%u,\"cpu_seconds\":%.9f,"
           "\"checksum\":\"%" PRIu64 "\",\"frame_hash\":\"%" PRIu64 "\",\"status_bytes\":%zu,\"cache_bytes\":%zu}\n",
           mode, count, draws, (double)(end - start) / CLOCKS_PER_SEC, sum, hash, sizeof(status),
#ifdef D72X40_RENDER_CACHE
           sizeof(cache)
#else
           (size_t)0
#endif
    );
    return 0;
}
