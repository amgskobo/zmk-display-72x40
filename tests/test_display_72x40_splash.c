/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <zmk-display-72x40/display_72x40_splash.h>

int main(void) {
    assert(d72x40_splash_remaining_ms(100U, 100U, 1800U) == 1800U);
    assert(d72x40_splash_remaining_ms(1899U, 100U, 1800U) == 1U);
    assert(d72x40_splash_remaining_ms(1900U, 100U, 1800U) == 0U);
    assert(d72x40_splash_remaining_ms(101U, 100U, 0U) == 0U);
    assert(d72x40_splash_remaining_ms(1000U, 100U, 500U) == 0U);
    assert(d72x40_splash_remaining_ms(1000U, 100U, 1800U) == 900U);
    assert(d72x40_splash_remaining_ms(3U, UINT32_MAX - 2U, 10U) == 4U);
    puts("display_72x40_splash: all checks passed");
    return 0;
}
