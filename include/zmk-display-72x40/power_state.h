/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Whether a half of a split is on external power. ZMK tells the central a
 * peripheral's battery level but not where its power comes from, so this
 * event carries that over the split relay: a peripheral raises it for itself,
 * and the central sees it raised again with that peripheral's source id.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zmk/event_manager.h>

struct zmk_display_72x40_power_state_changed {
    /*
     * ZMK_RELAY_EVENT_SOURCE_SELF on the half that raises it, and the
     * peripheral's source id plus one (0 would be the central) once the
     * relay has carried it across.
     */
    uint8_t source;
    bool powered;
};

ZMK_EVENT_DECLARE(zmk_display_72x40_power_state_changed);
