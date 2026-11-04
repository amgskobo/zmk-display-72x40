/* SPDX-License-Identifier: MIT */
#pragma once
static inline struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &k_sys_work_q; }
