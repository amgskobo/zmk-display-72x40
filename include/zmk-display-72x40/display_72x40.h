/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The module's runtime API. It draws by itself; this is only for the cases
 * where something outside it has touched the panel.
 */

#pragma once

#include <stdbool.h>
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

/** Set LED-change splash duration (0 disables it; maximum 10000 ms). */
void zmk_display_72x40_set_led_splash_ms(uint32_t ms);

/** Plain state for the second service lock; meaningful only when available. */
struct zmk_display_72x40_lock_status {
    bool available;
    bool unlocked;
};

/**
 * Optional source of the second lock. The default reports unavailable.
 * A provider supplies one strong definition and must return without blocking.
 * The display reads it when drawing; it stores no copy of the producer's state.
 */
struct zmk_display_72x40_lock_status zmk_display_72x40_get_external_lock_status(void);

/**
 * Invalidate status from any thread. Queues the existing redraw without
 * resetting activity, waking a dark panel or writing to the bus in the caller.
 */
void zmk_display_72x40_status_changed(void);

/**
 * Show numeric LED settings using the existing overlay/wake/expiry policy.
 * Pattern IDs follow the display's documented names; speed is a 16-bit percent.
 * Numeric values are published and read as one atomic tuple. No producer
 * header is needed. Ignored before init, while disabled, or when
 * CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH is off.
 */
void zmk_display_72x40_show_led_settings(uint8_t pattern, uint8_t brightness, uint16_t speed);
