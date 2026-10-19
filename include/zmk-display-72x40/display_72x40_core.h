/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The drawing half of zmk-display-72x40: plain C with no Zephyr or ZMK
 * dependency, so that the host tests exercise exactly what the firmware runs.
 *
 * The canvas is the controller's own memory layout. A page-tiled monochrome
 * panel (SSD1306 and alike) is a stack of 8-pixel pages, each a row of column
 * bytes whose bit 0 is the top pixel. Keeping the frame in that shape means a
 * changed region goes to the panel byte for byte, with no conversion.
 */

#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zmk-display-72x40/display_72x40_font.h>
#include <zmk-display-72x40/display_72x40_status_font.h>

/* AKKB fixed panel storage and portrait drawing coordinates. */
#define D72X40_PANEL_WIDTH 72
#define D72X40_PANEL_HEIGHT 40
#define D72X40_PANEL_PAGES 5
#define D72X40_FRAME_BYTES 360
#define D72X40_CANVAS_WIDTH 40
#define D72X40_CANVAS_HEIGHT 72
#define D72X40_CANVAS_PAGES 9

/* One text line is one page: a 7-pixel glyph over a blank row. */
#define D72X40_PAGE_HEIGHT 8
/* A glyph and the blank column after it. */
#define D72X40_CELL 6

/* Room for a layer name as ZMK stores it; the panel shows what fits. */
#define D72X40_LAYER_TEXT_SIZE 24

/* The font's own symbols, as string literals for building lines. */
#define D72X40_SYM_CHECK "\x01"
#define D72X40_SYM_CROSS "\x02"
#define D72X40_SYM_OPEN "\x03"
#define D72X40_SYM_BLUETOOTH "\x04"
#define D72X40_SYM_BOLT "\x05"
#define D72X40_SYM_UNLOCKED "\x06"

/* A battery value that is not drawn at all, and one drawn as "--". */
#define D72X40_BATTERY_HIDDEN 0xff
#define D72X40_BATTERY_UNKNOWN 0xfe

/*
 * Merge two changed runs of a page into one write when at most this many
 * unchanged columns separate them. A write costs about a dozen bytes of
 * addressing on top of its data, so resending a shorter gap is cheaper than
 * addressing a second run.
 */
#define D72X40_SPAN_JOIN 12

enum d72x40_transport {
    D72X40_TRANSPORT_NONE = 0,
    D72X40_TRANSPORT_USB,
    D72X40_TRANSPORT_BLE,
};

enum d72x40_link {
    /* Not connected; for a BLE profile, bonded but not connected. */
    D72X40_LINK_DOWN = 0,
    D72X40_LINK_UP,
    /* Blank mark: an unpaired BLE profile or a connected inactive transport. */
    D72X40_LINK_OPEN,
};

/* Everything the panel shows. The renderer draws it and nothing else. */
struct d72x40_status {
    /* NUL-terminated; longer names are cut to the panel width. */
    char layer[D72X40_LAYER_TEXT_SIZE];
    /* enum d72x40_transport and enum d72x40_link. */
    uint8_t transport;
    uint8_t link;
    /* The BLE profile, counted from 1. */
    uint8_t profile;
    /* Percent, D72X40_BATTERY_UNKNOWN or D72X40_BATTERY_HIDDEN. */
    uint8_t battery;
    /* The split peripheral's; D72X40_BATTERY_HIDDEN on a unibody. */
    uint8_t peer_battery;
    /* On external power: this half, and the peripheral. */
    bool charging;
    bool peer_charging;
    bool caps_lock;
    bool studio_lock_available;
    bool unlocked;
    bool messenger_lock_available;
    bool messenger_unlocked;
    /* Availability and route marks; only the selected ready route is UP. */
    bool ble_available;
    bool usb_available;
    uint8_t ble_link;
    uint8_t usb_link;
};

/* A frame is always the AKKB controller's 360-byte page layout. */
struct d72x40_canvas {
    uint8_t *buf;
};

struct d72x40_span {
    uint16_t page;
    uint16_t x0;
    uint16_t x1;
};

static inline const uint8_t *d72x40_glyph(unsigned char c) {
    if (c < D72X40_FONT_SYMBOL_COUNT) {
        return d72x40_font_symbols[c];
    }
    if (c >= D72X40_FONT_ASCII_FIRST && c <= D72X40_FONT_ASCII_LAST) {
        return d72x40_font_ascii[c - D72X40_FONT_ASCII_FIRST];
    }
    return d72x40_font_ascii['?' - D72X40_FONT_ASCII_FIRST];
}

static inline void d72x40_clear(const struct d72x40_canvas *cv) {
    memset(cv->buf, 0, (size_t)D72X40_CANVAS_WIDTH * D72X40_CANVAS_PAGES);
}

static inline uint8_t d72x40_reverse_byte(uint8_t b) {
    b = (uint8_t)(((b & 0x55u) << 1) | ((b >> 1) & 0x55u));
    b = (uint8_t)(((b & 0x33u) << 2) | ((b >> 2) & 0x33u));
    return (uint8_t)((b << 4) | (b >> 4));
}

/* Logical column bits go straight to fixed controller pages. */
static inline void d72x40_mask(const struct d72x40_canvas *cv, int x, int page,
                               uint8_t bits, bool erase) {
    if (x < 0 || x >= D72X40_CANVAS_WIDTH || page < 0 || page >= D72X40_CANVAS_PAGES) {
        return;
    }
    unsigned y = (unsigned)(D72X40_CANVAS_WIDTH - 1 - x);
    uint8_t mask = (uint8_t)(1u << (y % 8));
    size_t base = (size_t)(y / 8) * D72X40_CANVAS_HEIGHT + (size_t)page * 8;
    for (unsigned bit = 0; bit < 8; bit++) {
        uint8_t ink = (uint8_t)((0u - (((unsigned)bits >> bit) & 1u)) & mask);
        cv->buf[base + bit] = erase ? (uint8_t)(cv->buf[base + bit] & (uint8_t)~ink)
                                    : (uint8_t)(cv->buf[base + bit] | ink);
    }
}

static inline void d72x40_plot(const struct d72x40_canvas *cv, int x, int page, uint8_t bits) {
    d72x40_mask(cv, x, page, bits, false);
}

/* Clip once per strip, then advance contiguous controller addresses. */
static inline void d72x40_native_bitmap(const struct d72x40_canvas *cv, int x, int row,
                                        const uint8_t *rows, unsigned height, bool erase) {
    int canvas_height = D72X40_CANVAS_PAGES * 8;
    if (x <= -8 || x >= D72X40_CANVAS_WIDTH || row < 0 || row >= canvas_height) {
        return;
    }
    int native_y = D72X40_CANVAS_WIDTH - x - 8;
    unsigned skip = native_y < 0 ? (unsigned)-native_y : 0;
    if (native_y < 0) {
        native_y = 0;
    }
    unsigned shift = (unsigned)native_y % 8;
    unsigned page = (unsigned)native_y / 8;
    bool high = shift != 0 && page + 1 < D72X40_CANVAS_WIDTH / 8u;
    unsigned available = (unsigned)(canvas_height - row);
    unsigned count = height < available ? height : available;
    size_t at = (size_t)page * (size_t)canvas_height +
                (size_t)row;
    for (unsigned y = 0; y < count; y++) {
        uint8_t bits = rows[y];
        uint16_t ink = (uint16_t)(((unsigned)bits >> skip) << shift);
        uint8_t low = (uint8_t)ink;
        cv->buf[at] = erase ? (uint8_t)(cv->buf[at] & (uint8_t)~low)
                            : (uint8_t)(cv->buf[at] | low);
        if (high) {
            size_t next = at + (size_t)canvas_height;
            uint8_t upper = (uint8_t)(ink >> 8);
            cv->buf[next] = erase ? (uint8_t)(cv->buf[next] & (uint8_t)~upper)
                                  : (uint8_t)(cv->buf[next] | upper);
        }
        at++;
    }
}

static inline int d72x40_text_advance(int x, int advance) {
    return x > INT_MAX - advance ? INT_MAX : x + advance;
}

static inline void d72x40_append(char *buf, size_t size, const char *s) {
    size_t len = strlen(buf);
    while (*s != '\0' && len + 1 < size) {
        buf[len++] = *s++;
    }
    buf[len] = '\0';
}

static inline void d72x40_append_u8(char *buf, size_t size, uint8_t value) {
    char digits[4] = {0};
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value = (uint8_t)(value / 10);
    } while (value != 0);
    char text[4] = {0}; /* at most three digits: always terminated */
    for (size_t i = 0; i < n; i++) {
        text[i] = digits[n - 1 - i];
    }
    d72x40_append(buf, size, text);
}

static inline void d72x40_append_u16(char *buf, size_t size, uint16_t value) {
    char digits[6] = {0};
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value = (uint16_t)(value / 10);
    } while (value != 0);
    char result[6] = {0}; /* at most five digits: always terminated */
    for (size_t i = 0; i < n; i++) {
        result[i] = digits[n - 1 - i];
    }
    d72x40_append(buf, size, result);
}

static inline void d72x40_append_battery(char *buf, size_t size, uint8_t level) {
    if (level == D72X40_BATTERY_UNKNOWN) {
        d72x40_append(buf, size, "--");
        return;
    }
    d72x40_append_u8(buf, size, level > 100 ? 100 : level);
    d72x40_append(buf, size, "%");
}

static inline void d72x40_text_row(const struct d72x40_canvas *cv, int x, int row, const char *s,
                               size_t max) {
    if (row < 0 || row >= D72X40_CANVAS_PAGES * D72X40_PAGE_HEIGHT) {
        return;
    }
    int page = row / D72X40_PAGE_HEIGHT;
    unsigned shift = (unsigned)(row % D72X40_PAGE_HEIGHT);

    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
        if (x >= D72X40_CANVAS_WIDTH) {
            return;
        }
        unsigned char code = (unsigned char)s[i];
        if (code < D72X40_FONT_SYMBOL_COUNT) {
            d72x40_native_bitmap(cv, x, row, d72x40_symbol_rows[code], D72X40_FONT_HEIGHT, false);
            x += D72X40_CELL;
            continue;
        }
        const uint8_t *g = d72x40_glyph((unsigned char)s[i]);

        for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
            uint16_t shifted = (uint16_t)((uint16_t)g[col] << shift);

            d72x40_plot(cv, x + col, page, (uint8_t)(shifted & 0xff));
            if (shift != 0) {
                d72x40_plot(cv, x + col, page + 1, (uint8_t)(shifted >> 8));
            }
        }
        x += D72X40_CELL;
    }
}

static inline const char *d72x40_link_mark(uint8_t link) {
    return link == D72X40_LINK_UP ? D72X40_SYM_CHECK
           : link == D72X40_LINK_OPEN ? " " : D72X40_SYM_CROSS;
}

static inline unsigned d72x40_status_index(unsigned char c) {
    unsigned char code = c >= 32 && c <= 126 ? c : (unsigned char)'?';
    return (unsigned)code - 32u;
}

static inline const uint8_t *d72x40_status_glyph(unsigned char c) {
    return d72x40_status_rows[d72x40_status_index(c)];
}

static inline int d72x40_status_width(const char *s, size_t n) {
    int width = 0;
    for (size_t i = 0; i < n && s[i] != '\0'; i++) {
        width = d72x40_text_advance(width,
            d72x40_status_advance[d72x40_status_index((unsigned char)s[i])]);
    }
    return width;
}

static inline void d72x40_status_text(const struct d72x40_canvas *cv, int x, int row,
                                       const char *s, size_t n, bool erase) {
    for (size_t i = 0; i < n && s[i] != '\0'; i++) {
        if (x >= D72X40_CANVAS_WIDTH) {
            return;
        }
        unsigned index = d72x40_status_index((unsigned char)s[i]);
        const uint8_t *rows = d72x40_status_rows[index];
        d72x40_native_bitmap(cv, x, row, rows, D72X40_STATUS_FONT_HEIGHT, erase);
        x += (int)d72x40_status_advance[index];
    }
}

static inline void d72x40_status_centred(const struct d72x40_canvas *cv, int row,
                                          const char *s, bool erase) {
    size_t n = 0;
    int width = 0;
    /* Truncate at a whole glyph; long names never spill into another group. */
    while (s[n] != '\0') {
        int next = d72x40_status_advance[d72x40_status_index((unsigned char)s[n])];
        if (width + next > D72X40_CANVAS_WIDTH - 2) {
            break;
        }
        width += next;
        n++;
    }
    d72x40_status_text(cv, (D72X40_CANVAS_WIDTH - width) / 2, row, s, n, erase);
}

/* The nine-pixel lock uses one bounded mask across controller pages. */
static inline void d72x40_native_lock_row(const struct d72x40_canvas *cv, int x, int row,
                                     uint16_t bits) {
    unsigned count = 9;
    if (row < 0 || row >= D72X40_CANVAS_PAGES * 8 ||
        x >= D72X40_CANVAS_WIDTH || x <= -(int)count) {
        return;
    }
    if (x < 0) {
        unsigned skip = (unsigned)-x;
        bits = (uint16_t)(bits >> skip);
        count -= skip;
        x = 0;
    }
    unsigned available = (unsigned)D72X40_CANVAS_WIDTH - (unsigned)x;
    if (count > available) {
        count = available;
    }
    bits = (uint16_t)(bits & ((1ul << count) - 1));
    if (bits == 0) {
        return;
    }
    unsigned y = (unsigned)D72X40_CANVAS_WIDTH - (unsigned)x - count;
    bits = (uint16_t)(((uint16_t)d72x40_reverse_byte((uint8_t)bits) << 8) |
                     d72x40_reverse_byte((uint8_t)(bits >> 8)));
    bits = (uint16_t)(bits >> (16 - count));
    size_t height = (size_t)D72X40_CANVAS_PAGES * 8;
    size_t native_x = (size_t)row;
    unsigned shift = y % 8;
    uint32_t ink = (uint32_t)bits << shift;
    size_t at = (size_t)(y / 8) * height + native_x;
    for (unsigned offset = 0; offset < count + shift; offset += 8, at += height) {
        uint8_t chunk = (uint8_t)(ink >> offset);
        cv->buf[at] = (uint8_t)(cv->buf[at] | chunk);
    }
}

static inline void d72x40_status_lock(const struct d72x40_canvas *cv, int x, int row,
                                       bool open) {
    if (x <= -9 || x >= D72X40_CANVAS_WIDTH || row < 0 || row >= D72X40_CANVAS_PAGES * 8) {
        return;
    }
    static const uint16_t closed[11] = {
        0x038, 0x044, 0x082, 0x082, 0x082, 0x1ff, 0x1ef, 0x1c7, 0x1ef, 0x1ff, 0x1ff,
    };
    static const uint16_t opened[11] = {
        0x0c0, 0x120, 0x110, 0x008, 0x008, 0x1ff, 0x1ef, 0x1c7, 0x1ef, 0x1ff, 0x1ff,
    };
    const uint16_t *rows = open ? opened : closed;
    for (unsigned y = 0; y < 11; y++) {
        d72x40_native_lock_row(cv, x, row + (int)y, rows[y]);
    }
}

static inline void d72x40_status_battery(const struct d72x40_canvas *cv, int row,
                                          const char *label, uint8_t level, bool charging) {
    if (level == D72X40_BATTERY_HIDDEN) {
        return;
    }
    char value[6] = "";
    d72x40_append_battery(value, sizeof(value), level);
    d72x40_status_text(cv, 1, row, label, 1, false);
    d72x40_status_text(cv, D72X40_CANVAS_WIDTH - 8 - d72x40_status_width(value, SIZE_MAX),
                         row, value, SIZE_MAX, false);
    if (charging) {
        d72x40_text_row(cv, D72X40_CANVAS_WIDTH - 6, row + 1, D72X40_SYM_BOLT, 1);
    }
}

static inline void d72x40_status_header(const struct d72x40_canvas *cv) {
    for (unsigned page = 0; page < D72X40_PANEL_PAGES; page++) {
        memset(&cv->buf[(size_t)page * D72X40_PANEL_WIDTH], 0xff, 13);
    }
}

static inline void d72x40_render_portrait(const struct d72x40_canvas *cv,
                                          const struct d72x40_status *st) {
    int height = D72X40_CANVAS_PAGES * D72X40_PAGE_HEIGHT;
    d72x40_status_header(cv);
    d72x40_status_centred(cv, 2, st->layer, true);
    if (st->ble_available) {
        char profile[4] = "";
        d72x40_append_u8(profile, sizeof(profile), st->profile);
        int x = st->profile >= 100 ? 1 : 7;
        if (st->profile < 100) {
            d72x40_text_row(cv, 1, 17, D72X40_SYM_BLUETOOTH, 1);
        }
        d72x40_status_text(cv, x, 16, profile, SIZE_MAX, false);
        d72x40_text_row(cv, x + d72x40_status_width(profile, SIZE_MAX) + 1, 17,
                          d72x40_link_mark(st->ble_link), 1);
    }
    if (st->usb_available) {
        static const uint8_t usb_rows[9] = {0x10, 0x38, 0x16, 0x56, 0xe8, 0x70, 0x10, 0x38, 0x38};
        d72x40_native_bitmap(cv, D72X40_CANVAS_WIDTH - 15, 16, usb_rows, 9, false);
        d72x40_text_row(cv, D72X40_CANVAS_WIDTH - 6, 17, d72x40_link_mark(st->usb_link), 1);
    }
    if (st->caps_lock) {
        d72x40_status_centred(cv, 27, "CAPS", false);
    }
    d72x40_status_battery(cv, 38,
                            st->peer_battery == D72X40_BATTERY_HIDDEN ? "B" : "L",
                            st->battery, st->charging);
    d72x40_status_battery(cv, 49, "R", st->peer_battery, st->peer_charging);
    if (st->studio_lock_available) {
        d72x40_status_text(cv, 1, height - 11, "S", 1, false);
        d72x40_status_lock(cv, 9, height - 12, st->unlocked);
    }
    if (st->messenger_lock_available) {
        d72x40_status_text(cv, D72X40_CANVAS_WIDTH - 19, height - 11, "M", 1, false);
        d72x40_status_lock(cv, D72X40_CANVAS_WIDTH - 10, height - 12, st->messenger_unlocked);
    }
}

static inline void d72x40_render(const struct d72x40_canvas *cv, const struct d72x40_status *st) {
    d72x40_clear(cv);
    d72x40_render_portrait(cv, st);
}

static inline const char *d72x40_led_pattern_name(uint8_t pattern) {
    static const char *const names[] = {
        "STEADY", "BREATH", "HEART", "BLINK", "FAST", "TRIPLE",
        "SOS", "CANDLE", "SAW", "BEACON", "SLOW", "STROBE",
        "DBLBCN", "LONG", "SPARK", "COUNT", "RIPPLE", "FADE",
    };
    return pattern < sizeof(names) / sizeof(names[0]) ? names[pattern] : "?";
}

static inline void d72x40_render_led_splash(const struct d72x40_canvas *cv, uint8_t pattern,
                                           uint8_t brightness, uint16_t speed) {
    d72x40_clear(cv);
    char id[8] = "P";
    d72x40_append_u8(id, sizeof(id), pattern);
    char level[8] = "B";
    d72x40_append_u8(level, sizeof(level), brightness);
    d72x40_append(level, sizeof(level), "%");
    char rate[8] = "S";
    d72x40_append_u16(rate, sizeof(rate), speed);
    d72x40_append(rate, sizeof(rate), "%");

    d72x40_status_header(cv);
    d72x40_status_centred(cv, 2, "LED", true);
    d72x40_status_centred(cv, 16, id, false);
    d72x40_status_centred(cv, 27, d72x40_led_pattern_name(pattern), false);
    d72x40_status_centred(cv, 40, level, false);
    d72x40_status_centred(cv, 56, rate, false);
}

static inline bool d72x40_canvas_pixel(const struct d72x40_canvas *cv, int x, int y) {
    if (x < 0 || x >= D72X40_CANVAS_WIDTH || y < 0 || y >= D72X40_CANVAS_HEIGHT) {
        return false;
    }
    unsigned bit = (unsigned)(D72X40_CANVAS_WIDTH - 1 - x);
    uint8_t column = cv->buf[(size_t)(bit / 8) * D72X40_PANEL_WIDTH + (size_t)y];
    return (((unsigned)column >> (bit % 8)) & 1u) != 0;
}

/* Ordered changed spans; joining gaps costs less than a second address. */
static inline bool d72x40_next_span(const uint8_t *want, const uint8_t *shown, uint16_t width,
                                uint16_t pages, uint32_t valid, uint16_t *page, uint16_t *x,
                                struct d72x40_span *span) {
    for (; *page < pages; (*page)++, *x = 0) {
        const uint8_t *w = &want[(size_t)*page * width];
        const uint8_t *s = &shown[(size_t)*page * width];

        if (!(valid & (1ul << *page)) && *x == 0) {
            *span = (struct d72x40_span){.page = *page, .x0 = 0, .x1 = (uint16_t)(width - 1)};
            *x = width;
            return true;
        }

        uint16_t col = *x;
        while (col < width && w[col] == s[col]) {
            col++;
        }
        if (col >= width) {
            continue;
        }

        uint16_t start = col;
        uint16_t end = col;
        for (col = (uint16_t)(start + 1); col < width; col++) {
            if (w[col] == s[col]) {
                continue;
            }
            if (col - end - 1 > D72X40_SPAN_JOIN) {
                break;
            }
            end = col;
        }
        *span = (struct d72x40_span){.page = *page, .x0 = start, .x1 = end};
        *x = (uint16_t)(end + 1);
        return true;
    }
    return false;
}
