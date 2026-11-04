/* SPDX-License-Identifier: MIT */
#pragma once
struct zmk_display_72x40_power_state_changed { uint8_t source; bool powered; };
static int raise_zmk_display_72x40_power_state_changed(struct zmk_display_72x40_power_state_changed event);
