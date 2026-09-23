/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/* Unsigned subtraction also handles the 32-bit uptime counter wrapping. */
static inline uint32_t d72x40_splash_remaining_ms(uint32_t now_ms, uint32_t changed_ms,
                                                   uint32_t duration_ms) {
    if (duration_ms == 0U) {
        return 0U;
    }
    const uint32_t elapsed = now_ms - changed_ms;
    return elapsed < duration_ms ? duration_ms - elapsed : 0U;
}
