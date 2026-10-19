/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdio.h>
#include <zmk-display-72x40/display_72x40.h>
#define __weak

/* DRIVER_FUNCTIONS */

int main(void) {
    struct zmk_display_72x40_lock_status status = zmk_display_72x40_get_external_lock_status();
    assert(!status.available && !status.unlocked);
    status.available = true;
    status.unlocked = true;
    status = zmk_display_72x40_get_external_lock_status();
    assert(!status.available && !status.unlocked);
    puts("display default provider: PASS");
    return 0;
}
