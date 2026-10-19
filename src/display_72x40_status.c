/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/toolchain.h>
#include <zmk-display-72x40/display_72x40.h>

/* No producer is a supported configuration, without stored or polled state. */
__weak struct zmk_display_72x40_lock_status zmk_display_72x40_get_external_lock_status(void) {
    return (struct zmk_display_72x40_lock_status){0};
}
