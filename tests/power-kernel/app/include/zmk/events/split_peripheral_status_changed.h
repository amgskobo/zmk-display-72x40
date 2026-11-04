/* SPDX-License-Identifier: MIT */
#pragma once
struct zmk_split_peripheral_status_changed { bool connected; };
static inline const struct zmk_split_peripheral_status_changed *as_zmk_split_peripheral_status_changed(const zmk_event_t *event) { return (const void *)event; }
