/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RECORDING_WIDTH 72
#define RECORDING_PAGES 5

enum recording_kind {
    RECORDING_WRITE,
    RECORDING_ON,
    RECORDING_OFF,
};

struct recording_entry {
    enum recording_kind kind;
    uint32_t sequence;
    uint32_t at_ms;
    uint16_t page;
    uint16_t x0;
    uint16_t len;
};

struct recording_display {
    uint8_t frame[RECORDING_PAGES * RECORDING_WIDTH];
    bool on;
    uint8_t contrast;
    bool contrast_set;
    /* Writes to fail with -EIO before accepting any. */
    int fail_writes;

    uint32_t writes;
    uint32_t bytes;
    uint32_t failed;
    uint32_t malformed;
    uint32_t blank_on;
    uint32_t blank_off;
    uint32_t page_writes[RECORDING_PAGES];

    uint32_t sequence;
    struct recording_entry log[64];
    uint32_t log_len;
};

/* Holds the recording's lock until recording_display_unlock(). */
struct recording_display *recording_display_lock(void);
void recording_display_unlock(void);

/* Zeroes the counters and the log; the frame and power state stay. */
void recording_display_clear_counts(void);
