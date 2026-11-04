/* SPDX-License-Identifier: MIT */
#pragma once
#define NRF_POWER_HAS_USBREG 1
#define NRF_POWER ((void *)0)
static inline bool nrf_power_usbregstatus_vbusdet_get(void *unused) { (void)unused; return true; }
