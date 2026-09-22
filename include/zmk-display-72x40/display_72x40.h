/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The module's runtime API. It draws by itself; this is only for the cases
 * where something outside it has touched the panel.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Redraw the whole panel.
 *
 * The module normally sends only what changed since its last write. Call this
 * after something else has written to the panel, or changed a setting that
 * affects what is already in the controller's memory, so that the next pass
 * sends the whole frame instead.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the panel is not ready.
 */
int zmk_display_72x40_refresh(void);

/*
 * Settings that can change while the keyboard runs. Each starts at its build
 * time value (the Kconfig options and the node's contrast property), takes
 * effect at once, and is safe to call from any thread: it records the value and
 * schedules the module's own work, which is the only thing that writes to the
 * panel. With CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS a Studio client sets them.
 */

/**
 * @brief Set the panel's blank timeout.
 *
 * The panel goes dark this long after the last key press, encoder turn or
 * pointer report, whatever ZMK makes of the keyboard; 0 switches the timeout
 * off, which lights a panel it had darkened and keeps it lit until the keyboard
 * powers off. Values above ten minutes are cut to ten minutes.
 */
void zmk_display_72x40_set_blank_after_ms(uint32_t ms);

/**
 * @brief Set the panel's contrast (brightness), 0 to 255.
 */
void zmk_display_72x40_set_contrast(uint8_t contrast);
