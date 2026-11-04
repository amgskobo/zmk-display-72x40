/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool connected; } zmk_event_t;
#define ZMK_EVENT_IMPL(...)
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
#define ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(...)
#define ZMK_RELAY_EVENT_SOURCE_SELF 255
#define ZMK_EV_EVENT_BUBBLE 0
