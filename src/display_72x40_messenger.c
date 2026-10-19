/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/sys/util.h>
#include <zmk-display-72x40/display_72x40.h>
#include <zmk_ble_messenger/service.h>

struct zmk_display_72x40_lock_status zmk_display_72x40_get_external_lock_status(void) {
    return (struct zmk_display_72x40_lock_status){
        .available = true,
        .unlocked = zmk_ble_messenger_is_unlocked(),
    };
}

void zmk_ble_messenger_lock_state_changed(bool open) {
    ARG_UNUSED(open);
    zmk_display_72x40_status_changed();
}
