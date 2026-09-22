/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * A page-tiled monochrome display that keeps what it is sent, for the
 * runtime tests: the frame, every write in order, and the power commands.
 */

#define DT_DRV_COMPAT zmk_test_recording_display

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

#include "recording_display.h"

static struct recording_display recording;

static K_MUTEX_DEFINE(recording_lock);

struct recording_display *recording_display_lock(void) {
    k_mutex_lock(&recording_lock, K_FOREVER);
    return &recording;
}

void recording_display_unlock(void) { k_mutex_unlock(&recording_lock); }

void recording_display_clear_counts(void) {
    struct recording_display *rec = recording_display_lock();

    rec->writes = 0;
    rec->bytes = 0;
    rec->failed = 0;
    rec->blank_on = 0;
    rec->blank_off = 0;
    memset(rec->page_writes, 0, sizeof(rec->page_writes));
    rec->log_len = 0;
    recording_display_unlock();
}

static void note(struct recording_display *rec, enum recording_kind kind, uint16_t page,
                 uint16_t x0, uint16_t len) {
    rec->sequence++;
    if (rec->log_len < ARRAY_SIZE(rec->log)) {
        rec->log[rec->log_len++] = (struct recording_entry){
            .kind = kind,
            .sequence = rec->sequence,
            .at_ms = k_uptime_get_32(),
            .page = page,
            .x0 = x0,
            .len = len,
        };
    }
}

/* The module writes one page at a time, width and pitch equal. */
static bool well_formed(uint16_t x, uint16_t y, const struct display_buffer_descriptor *desc) {
    return y % 8 == 0 && y / 8 < RECORDING_PAGES && desc->height == 8 &&
           desc->pitch == desc->width && desc->buf_size >= desc->width && desc->width > 0 &&
           x + desc->width <= RECORDING_WIDTH;
}

static int rec_write(const struct device *dev, const uint16_t x, const uint16_t y,
                     const struct display_buffer_descriptor *desc, const void *buf) {
    ARG_UNUSED(dev);
    struct recording_display *rec = recording_display_lock();
    int ret = 0;

    if (rec->fail_writes > 0) {
        rec->fail_writes--;
        rec->failed++;
        ret = -EIO;
    } else if (!well_formed(x, y, desc)) {
        rec->malformed++;
        ret = -EINVAL;
    } else {
        uint16_t page = y / 8;

        memcpy(&rec->frame[page * RECORDING_WIDTH + x], buf, desc->width);
        rec->writes++;
        rec->bytes += desc->width;
        rec->page_writes[page]++;
        note(rec, RECORDING_WRITE, page, x, desc->width);
    }

    recording_display_unlock();
    return ret;
}

static int rec_blanking_on(const struct device *dev) {
    ARG_UNUSED(dev);
    struct recording_display *rec = recording_display_lock();

    rec->on = false;
    rec->blank_on++;
    note(rec, RECORDING_OFF, 0, 0, 0);
    recording_display_unlock();
    return 0;
}

static int rec_blanking_off(const struct device *dev) {
    ARG_UNUSED(dev);
    struct recording_display *rec = recording_display_lock();

    rec->on = true;
    rec->blank_off++;
    note(rec, RECORDING_ON, 0, 0, 0);
    recording_display_unlock();
    return 0;
}

static int rec_set_contrast(const struct device *dev, const uint8_t contrast) {
    ARG_UNUSED(dev);
    struct recording_display *rec = recording_display_lock();

    rec->contrast = contrast;
    rec->contrast_set = true;
    recording_display_unlock();
    return 0;
}

static void rec_get_capabilities(const struct device *dev, struct display_capabilities *caps) {
    ARG_UNUSED(dev);
    memset(caps, 0, sizeof(*caps));
    caps->x_resolution = RECORDING_WIDTH;
    caps->y_resolution = RECORDING_PAGES * 8;
    caps->supported_pixel_formats = PIXEL_FORMAT_MONO01;
    caps->current_pixel_format = PIXEL_FORMAT_MONO01;
    caps->screen_info = SCREEN_INFO_MONO_VTILED;
}

static int rec_set_pixel_format(const struct device *dev, const enum display_pixel_format pf) {
    ARG_UNUSED(dev);
    return pf == PIXEL_FORMAT_MONO01 ? 0 : -ENOTSUP;
}

static DEVICE_API(display, rec_api) = {
    .blanking_on = rec_blanking_on,
    .blanking_off = rec_blanking_off,
    .write = rec_write,
    .set_contrast = rec_set_contrast,
    .get_capabilities = rec_get_capabilities,
    .set_pixel_format = rec_set_pixel_format,
};

static int rec_init(const struct device *dev) {
    ARG_UNUSED(dev);
    /* A real controller powers up switched on, showing leftover memory. */
    memset(recording.frame, 0x5a, sizeof(recording.frame));
    recording.on = true;
    return 0;
}

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1, "one recording display");
BUILD_ASSERT(DT_INST_PROP(0, width) == RECORDING_WIDTH, "recording display width");
BUILD_ASSERT(DT_INST_PROP(0, height) == RECORDING_PAGES * 8, "recording display height");

DEVICE_DT_INST_DEFINE(0, rec_init, NULL, NULL, NULL, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,
                      &rec_api);
