/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdio.h>
#include <zmk-display-72x40/display_72x40.h>
#define ARG_UNUSED(value) (void)(value)
static bool open;
static unsigned reads, invalidations;
static bool zmk_ble_messenger_is_unlocked(void) { reads++; return open; }
void zmk_display_72x40_status_changed(void) { invalidations++; }

/* DRIVER_FUNCTIONS */

int main(void) {
    struct zmk_display_72x40_lock_status status = zmk_display_72x40_get_external_lock_status();
    assert(status.available && !status.unlocked && reads == 1);
    open = true;
    status = zmk_display_72x40_get_external_lock_status();
    assert(status.available && status.unlocked && reads == 2);
    zmk_ble_messenger_lock_state_changed(true);
    zmk_ble_messenger_lock_state_changed(false);
    assert(invalidations == 2 && open && reads == 2);
    open = false;
    status = zmk_display_72x40_get_external_lock_status();
    assert(status.available && !status.unlocked && reads == 3);
    puts("display Messenger adapter: PASS");
    return 0;
}
