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

/* One text line is one page: a 7-pixel glyph over a blank row. */
#define D72X40_PAGE_HEIGHT 8
/* A glyph and the blank column after it. */
#define D72X40_CELL 6
/* The same, drawn two columns per font column. */
#define D72X40_WIDE_CELL 12

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

struct d72x40_canvas {
    /* pages * width column bytes, page 0 first, column 0 first. */
    uint8_t *buf;
    uint16_t width;
    uint16_t pages;
    /* 0: logical page layout; 90/270: draw straight into panel page layout.
     * Turned storage requires width to be a multiple of eight. Coordinates
     * and dimensions always describe the logical drawing, not the storage. */
    uint16_t rotation;
};

/* A run of columns of one page; x1 is inclusive. */
struct d72x40_span {
    uint16_t page;
    uint16_t x0;
    uint16_t x1;
};

/* The driver knows the storage layout at build time. Keep this hint because
 * small-code builds otherwise retain unused logical-storage render paths. */
#ifndef D72X40_NATIVE_STORAGE
#define D72X40_NATIVE_STORAGE 0
#endif

static inline bool d72x40_native_storage(const struct d72x40_canvas *cv) {
    return D72X40_NATIVE_STORAGE || cv->rotation != 0;
}

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
    memset(cv->buf, 0, (size_t)cv->width * cv->pages);
}

static inline uint8_t d72x40_reverse_byte(uint8_t b) {
    b = (uint8_t)(((b & 0x55u) << 1) | ((b >> 1) & 0x55u));
    b = (uint8_t)(((b & 0x33u) << 2) | ((b >> 2) & 0x33u));
    return (uint8_t)((b << 4) | (b >> 4));
}

/* Apply a logical column to either storage layout. Small legacy symbols use
 * this path; native-size text writes complete row masks instead. */
static inline void d72x40_mask(const struct d72x40_canvas *cv, int x, int page,
                               uint8_t bits, bool erase) {
    if (x < 0 || x >= (int)cv->width || page < 0 || page >= (int)cv->pages) {
        return;
    }
    if (d72x40_native_storage(cv)) {
        size_t height = (size_t)cv->pages * D72X40_PAGE_HEIGHT;
        unsigned y = cv->rotation == 90 ? (unsigned)cv->width - 1u - (unsigned)x : (unsigned)x;
        uint8_t mask = (uint8_t)(1u << (y % 8));
        size_t base = (size_t)(y / 8) * height;
        for (unsigned bit = 0; bit < 8; bit++) {
            size_t row = (size_t)page * 8 + bit;
            size_t at = base + (cv->rotation == 90 ? row : height - 1 - row);
            uint8_t ink = (uint8_t)((0u - (((unsigned)bits >> bit) & 1u)) & mask);
            cv->buf[at] = erase ? (uint8_t)(cv->buf[at] & (uint8_t)~ink)
                                : (uint8_t)(cv->buf[at] | ink);
        }
        return;
    }
    size_t at = (size_t)page * cv->width + (size_t)x;
    cv->buf[at] = erase ? (uint8_t)(cv->buf[at] & (uint8_t)~bits)
                        : (uint8_t)(cv->buf[at] | bits);
}

/* ORs one column byte into the canvas; anything off the canvas is dropped. */
static inline void d72x40_plot(const struct d72x40_canvas *cv, int x, int page, uint8_t bits) {
    d72x40_mask(cv, x, page, bits, false);
}

/* Clip and calculate page addresses once per eight-pixel bitmap strip.
 * Native row addresses then advance by one byte, without per-row division. */
static inline void d72x40_native_bitmap(const struct d72x40_canvas *cv, int x, int row,
                                        const uint8_t *rows, unsigned height, bool erase) {
    int canvas_height = (int)cv->pages * 8;
    if (x <= -8 || x >= (int)cv->width || row < 0 || row >= canvas_height) {
        return;
    }
    bool left_to_right = cv->rotation == 90;
    int native_y = left_to_right ? (int)cv->width - x - 8 : x;
    unsigned skip = native_y < 0 ? (unsigned)-native_y : 0;
    if (native_y < 0) {
        native_y = 0;
    }
    unsigned shift = (unsigned)native_y % 8;
    unsigned page = (unsigned)native_y / 8;
    bool high = shift != 0 && page + 1 < cv->width / 8u;
    unsigned available = (unsigned)(canvas_height - row);
    unsigned count = height < available ? height : available;
    size_t at = (size_t)page * (size_t)canvas_height +
                (size_t)(left_to_right ? row : canvas_height - 1 - row);
    for (unsigned y = 0; y < count; y++) {
        uint8_t bits = left_to_right ? rows[y] : d72x40_reverse_byte(rows[y]);
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
        at = left_to_right ? at + 1 : at - 1;
    }
}

/* All owned bitmap strips are at most eleven rows tall. Assemble columns
 * directly for logical storage; quarter turns never need this conversion. */
static inline void d72x40_logical_bitmap(const struct d72x40_canvas *cv, int x, int row,
                                         const uint8_t *rows, unsigned height, bool erase) {
    if (x <= -8 || x >= (int)cv->width || row < 0 || row >= (int)cv->pages * 8) {
        return;
    }
    unsigned shift = (unsigned)row % 8;
    int page = row / 8;
    for (unsigned col = 0; col < 8; col++) {
        uint32_t bits = 0;
        for (unsigned y = 0; y < height; y++) {
            bits |= (((uint32_t)rows[y] >> (7 - col)) & 1u) << y;
        }
        bits <<= shift;
        d72x40_mask(cv, x + (int)col, page, (uint8_t)bits, erase);
        d72x40_mask(cv, x + (int)col, page + 1, (uint8_t)(bits >> 8), erase);
        if (height + shift > 16) {
            d72x40_mask(cv, x + (int)col, page + 2, (uint8_t)(bits >> 16), erase);
        }
    }
}

/*
 * The pixel width of n glyphs laid out cell wide (D72X40_CELL or D72X40_WIDE_CELL),
 * not counting the blank column(s) after the last one. Unrepresentable
 * widths saturate at INT_MAX.
 */
static inline int d72x40_text_width(size_t n, int cell) {
    if (n == 0 || cell <= 0) {
        return 0;
    }
    int scale = cell / D72X40_CELL;
    int gap = cell - scale * D72X40_FONT_WIDTH;
    if (n > ((size_t)INT_MAX + (size_t)gap) / (size_t)cell) {
        return INT_MAX;
    }
    return (int)(n * (size_t)cell - (size_t)gap);
}

/* Font advances are positive; retain exact coordinates until int is full. */
static inline int d72x40_text_advance(int x, int advance) {
    return x > INT_MAX - advance ? INT_MAX : x + advance;
}

/* Draws at most max glyphs of s in the page at x; returns the x after them,
 * saturated at INT_MAX only when that coordinate is not representable. */
static inline int d72x40_text_n(const struct d72x40_canvas *cv, int x, int page, const char *s,
                            size_t max) {
    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
        if (x < (int)cv->width) {
            const uint8_t *g = d72x40_glyph((unsigned char)s[i]);
            for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
                d72x40_plot(cv, x + col, page, g[col]);
            }
        }
        x = d72x40_text_advance(x, D72X40_CELL);
    }
    return x;
}

static inline int d72x40_text(const struct d72x40_canvas *cv, int x, int page, const char *s) {
    return d72x40_text_n(cv, x, page, s, SIZE_MAX);
}

/* A font column stretched to two rows per row, below one blank row. */
static inline uint16_t d72x40_tall_column(uint8_t column) {
    uint16_t tall = 0;
    for (unsigned row = 0; row < D72X40_FONT_HEIGHT; row++) {
        if (column & (1u << row)) {
            tall = (uint16_t)(tall | (3u << (2u * row)));
        }
    }
    return (uint16_t)(tall << 1);
}

/*
 * Draws at most max glyphs of s across the page and the one below it, twice
 * the font's height and, when wide, twice its width.
 */
static inline void d72x40_text_tall(const struct d72x40_canvas *cv, int x, int page, const char *s,
                                size_t max, bool wide) {
    if (page >= (int)cv->pages) {
        return;
    }
    int scale = wide ? 2 : 1;
    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
        if (x >= (int)cv->width) {
            return;
        }
        const uint8_t *g = d72x40_glyph((unsigned char)s[i]);
        for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
            uint16_t tall = d72x40_tall_column(g[col]);
            for (int copy = 0; copy < scale; copy++) {
                d72x40_plot(cv, x + col * scale + copy, page, (uint8_t)(tall & 0xff));
                d72x40_plot(cv, x + col * scale + copy, page + 1, (uint8_t)(tall >> 8));
            }
        }
        x += D72X40_CELL * scale;
    }
}

/* Appends s to a NUL-terminated buffer of size bytes, cutting it to fit. */
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

/*
 * Draws at most max glyphs of s with its top at row, which need not be a page
 * boundary; a glyph then spans two pages.
 */
static inline void d72x40_text_row(const struct d72x40_canvas *cv, int x, int row, const char *s,
                               size_t max) {
    if (row < 0 || row >= (int)cv->pages * D72X40_PAGE_HEIGHT) {
        return;
    }
    int page = row / D72X40_PAGE_HEIGHT;
    unsigned shift = (unsigned)(row % D72X40_PAGE_HEIGHT);

    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
        if (x >= (int)cv->width) {
            return;
        }
        unsigned char code = (unsigned char)s[i];
        if (d72x40_native_storage(cv) && code < D72X40_FONT_SYMBOL_COUNT) {
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

/* Draws at most n glyphs of s centred across the canvas, with its top at row. */
static inline void d72x40_text_centred(const struct d72x40_canvas *cv, int row, const char *s,
                                   size_t n) {
    d72x40_text_row(cv, ((int)cv->width - d72x40_text_width(n, D72X40_CELL)) / 2, row, s, n);
}

/*
 * Where a name too long for one line breaks: after the last hyphen that
 * leaves both halves within max, else at max.
 */
static inline size_t d72x40_wrap_at(const char *s, size_t len, size_t max) {
    for (size_t i = max; i > 0; i--) {
        if (s[i - 1] == '-' && len - i <= max) {
            return i;
        }
    }
    return max;
}

/*
 * The layer, centred across the first two pages. It is drawn at double size,
 * both ways - the only enlargement that keeps the letters' shape - when the
 * whole name fits that way, at ordinary size on one centred line when it
 * does not, and over two lines when it is too long even for that.
 *
 * The block is always two pages tall, so that the lines below it never move
 * when the layer changes.
 */
static inline int d72x40_render_layer(const struct d72x40_canvas *cv, const char *name) {
    size_t len = strlen(name);
    size_t max_narrow = (size_t)(cv->width / D72X40_CELL);

    if (cv->pages < 2) {
        d72x40_text_centred(cv, 0, name, len < max_narrow ? len : max_narrow);
        return 1;
    }

    /* n double-size glyphs take 12n - 2 columns. */
    size_t max_wide = (size_t)((cv->width + 2) / D72X40_WIDE_CELL);

    if (len <= max_wide) {
        int width = d72x40_text_width(len, D72X40_WIDE_CELL);

        d72x40_text_tall(cv, ((int)cv->width - width) / 2, 0, name, len, true);
        return 2;
    }

    if (len <= max_narrow) {
        /* One ordinary line, centred in the two-page block. */
        d72x40_text_centred(cv, (2 * D72X40_PAGE_HEIGHT - D72X40_FONT_HEIGHT) / 2, name, len);
        return 2;
    }

    size_t split = d72x40_wrap_at(name, len, max_narrow);
    size_t rest = len - split;

    d72x40_text_centred(cv, 0, name, split);
    d72x40_text_centred(cv, D72X40_PAGE_HEIGHT, name + split, rest < max_narrow ? rest : max_narrow);
    return 2;
}

/* Fourteen columns give the shackle enough room to visibly swing up and away
 * from the body. The accompanying word carries the state without relying on
 * a tiny monochrome silhouette alone. */
static inline void d72x40_render_studio_lock(const struct d72x40_canvas *cv, int page,
                                             bool unlocked, int host_width) {
    if (cv->width < 40) {
        if (unlocked) {
            d72x40_text(cv, (int)cv->width - D72X40_FONT_WIDTH, page, D72X40_SYM_UNLOCKED);
        }
        return;
    }
    if (cv->width == 40) {
        static const uint8_t closed[12] = {
            0xf0, 0x90, 0x9e, 0x91, 0x91, 0xb1,
            0xb1, 0x91, 0x91, 0x9e, 0x90, 0xf0,
        };
        static const uint8_t open[12] = {
            0xf0, 0x90, 0x90, 0x90, 0x9c, 0x92,
            0x91, 0x91, 0x92, 0x9c, 0x91, 0x06,
        };
        const uint8_t *icon = unlocked ? open : closed;
        for (int col = 0; col < 12; col++) {
            d72x40_plot(cv, 28 + col, page, icon[col]);
        }
        return;
    }
    static const uint8_t locked_icon[14] = {
        0x00, 0x00, 0xf0, 0xfe, 0x91, 0x91, 0xd1, 0xd1,
        0x91, 0x91, 0xfe, 0xf0, 0x00, 0x00,
    };
    static const uint8_t unlocked_icon[14] = {
        0x00, 0x00, 0xe0, 0xe0, 0xa0, 0xae, 0xe1, 0xe1,
        0xa1, 0xa1, 0xe1, 0xe1, 0x0e, 0x10,
    };
    const uint8_t *icon = unlocked ? unlocked_icon : locked_icon;
    const int x = (int)cv->width - 15;
    if (cv->width >= 54 && host_width < (int)cv->width - 39) {
        d72x40_text(cv, (int)cv->width - 39, page, unlocked ? "OPEN" : "LOCK");
    }
    for (int col = 0; col < 14; col++) {
        d72x40_plot(cv, x + col, page, icon[col]);
    }
}

/* The host connection on the left, Studio's lock on the right. */
static inline int d72x40_render_output(const struct d72x40_canvas *cv, int page,
                                   const struct d72x40_status *st) {
    char line[16] = "";
    switch (st->transport) {
    case D72X40_TRANSPORT_USB:
        d72x40_append(line, sizeof(line), "USB");
        if (st->link != D72X40_LINK_UP) {
            d72x40_append(line, sizeof(line),
                          cv->width == 40 && st->studio_lock_available
                              ? D72X40_SYM_CROSS : " " D72X40_SYM_CROSS);
        }
        break;
    case D72X40_TRANSPORT_BLE:
        d72x40_append(line, sizeof(line), D72X40_SYM_BLUETOOTH);
        d72x40_append_u8(line, sizeof(line), st->profile);
        d72x40_append(line, sizeof(line), " ");
        d72x40_append(line, sizeof(line),
                  st->link == D72X40_LINK_UP     ? D72X40_SYM_CHECK
                  : st->link == D72X40_LINK_OPEN ? " "
                                             : D72X40_SYM_CROSS);
        break;
    default:
        d72x40_append(line, sizeof(line), D72X40_SYM_CROSS);
        break;
    }
    d72x40_text(cv, 0, page, line);
    if (st->studio_lock_available) {
        d72x40_render_studio_lock(cv, page, st->unlocked,
                                 d72x40_text_width(strlen(line), D72X40_CELL));
    }
    return page + 1;
}

/* The battery line(s) as text, with how they are placed. */
struct d72x40_battery_text {
    char left[12];
    char right[12];
    /* One below the other rather than side by side. */
    bool stacked;
    /* Nothing to show at all. */
    bool empty;
};

/*
 * A split shows "L" on the left and "R" right-aligned on one line, or one
 * below the other when the panel is too narrow for both; a unibody shows one
 * "BAT". A bolt after the percentage means external power.
 */
static inline void d72x40_battery_text(const struct d72x40_canvas *cv, const struct d72x40_status *st,
                                   struct d72x40_battery_text *out) {
    memset(out, 0, sizeof(*out));

    char own[6] = "";
    if (st->battery != D72X40_BATTERY_HIDDEN) {
        d72x40_append_battery(own, sizeof(own), st->battery);
    }

    if (st->peer_battery == D72X40_BATTERY_HIDDEN) {
        if (st->battery == D72X40_BATTERY_HIDDEN) {
            out->empty = true;
            return;
        }
        size_t full_len = 4 + strlen(own) + (st->charging ? 1 : 0);
        const char *label = d72x40_text_width(full_len, D72X40_CELL) <= (int)cv->width
                                ? "BAT" : "B";
        d72x40_append(out->left, sizeof(out->left), label);
        if (d72x40_text_width(strlen(label) + 1 + strlen(own) + (st->charging ? 1 : 0),
                              D72X40_CELL) <= (int)cv->width) {
            d72x40_append(out->left, sizeof(out->left), " ");
        }
        d72x40_append(out->left, sizeof(out->left), own);
        if (st->charging) {
            d72x40_append(out->left, sizeof(out->left), D72X40_SYM_BOLT);
        }
        return;
    }

    char peer[6] = "";
    d72x40_append_battery(peer, sizeof(peer), st->peer_battery);

    /*
     * Keep the bolt after its percentage. First remove the left label's space,
     * then the right label's space if needed. If the values still do not fit
     * side by side, stack them and restore each space where that line fits.
     */
    bool has_own = own[0] != '\0';
    size_t left_len = has_own ? 2 + strlen(own) + (st->charging ? 1 : 0) : 0;
    size_t right_len = 2 + strlen(peer) + (st->peer_charging ? 1 : 0);
    bool left_space = true;
    bool right_space = true;
    int total = d72x40_text_width(left_len + right_len + (has_own ? 1 : 0), D72X40_CELL);
    if (has_own && total > (int)cv->width) {
        left_space = false;
        total -= D72X40_CELL;
    }
    if (has_own && total > (int)cv->width) {
        right_space = false;
        total -= D72X40_CELL;
    }
    out->stacked = has_own && total > (int)cv->width;
    if (out->stacked) {
        left_space = d72x40_text_width(left_len, D72X40_CELL) <= (int)cv->width;
        right_space = d72x40_text_width(right_len, D72X40_CELL) <= (int)cv->width;
    } else if (!has_own && d72x40_text_width(right_len, D72X40_CELL) > (int)cv->width) {
        right_space = false;
    }

    if (has_own) {
        d72x40_append(out->left, sizeof(out->left), "L");
        if (left_space) {
            d72x40_append(out->left, sizeof(out->left), " ");
        }
        d72x40_append(out->left, sizeof(out->left), own);
        if (st->charging) {
            d72x40_append(out->left, sizeof(out->left), D72X40_SYM_BOLT);
        }
    }
    d72x40_append(out->right, sizeof(out->right), "R");
    if (right_space) {
        d72x40_append(out->right, sizeof(out->right), " ");
    }
    d72x40_append(out->right, sizeof(out->right), peer);
    if (st->peer_charging) {
        d72x40_append(out->right, sizeof(out->right), D72X40_SYM_BOLT);
    }
}

static inline int d72x40_battery_pages(const struct d72x40_canvas *cv, const struct d72x40_status *st) {
    struct d72x40_battery_text text;

    d72x40_battery_text(cv, st, &text);
    return text.empty ? 0 : (text.stacked ? 2 : 1);
}

static inline int d72x40_render_battery(const struct d72x40_canvas *cv, int page,
                                    const struct d72x40_status *st) {
    struct d72x40_battery_text text;

    d72x40_battery_text(cv, st, &text);
    if (text.empty) {
        return page;
    }

    d72x40_text(cv, 0, page, text.left);
    if (text.right[0] == '\0') {
        return page + 1;
    }
    if (text.stacked) {
        if (page + 1 < (int)cv->pages) {
            d72x40_text(cv, 0, page + 1, text.right);
        }
        return page + 2;
    }
    d72x40_text(cv, (int)cv->width - d72x40_text_width(strlen(text.right), D72X40_CELL), page, text.right);
    return page + 1;
}

/* Host lock indicators. */
static inline int d72x40_render_indicators(const struct d72x40_canvas *cv, int page,
                                       const struct d72x40_status *st) {
    if (st->caps_lock) {
        d72x40_text(cv, 0, page, "CAPS");
    }
    return page + 1;
}

/* A bitmap column up to nine pixels tall, including a page-aligned ninth row. */
static inline void d72x40_column_row(const struct d72x40_canvas *cv, int x, int row, uint16_t bits) {
    if (row < 0 || row >= (int)cv->pages * D72X40_PAGE_HEIGHT) {
        return;
    }
    unsigned shift = (unsigned)(row % D72X40_PAGE_HEIGHT);
    uint16_t shifted = (uint16_t)((uint16_t)bits << shift);
    d72x40_plot(cv, x, row / D72X40_PAGE_HEIGHT, (uint8_t)(shifted & 0xff));
    d72x40_plot(cv, x, row / D72X40_PAGE_HEIGHT + 1, (uint8_t)(shifted >> 8));
}

/* The same compact padlock for both services; opening moves the shackle. */
static inline void d72x40_lock_row(const struct d72x40_canvas *cv, int x, int row, bool open) {
    static const uint8_t closed_icon[12] = {
        0xf0, 0x90, 0x9e, 0x91, 0x91, 0xb1,
        0xb1, 0x91, 0x91, 0x9e, 0x90, 0xf0,
    };
    static const uint8_t open_icon[12] = {
        0xf0, 0x90, 0x90, 0x90, 0x9c, 0x92,
        0x91, 0x91, 0x92, 0x9c, 0x91, 0x06,
    };
    const uint8_t *icon = open ? open_icon : closed_icon;
    for (int col = 0; col < 12; col++) {
        d72x40_column_row(cv, x + col, row, icon[col]);
    }
}

static inline const char *d72x40_link_mark(uint8_t link) {
    return link == D72X40_LINK_UP ? D72X40_SYM_CHECK
           : link == D72X40_LINK_OPEN ? " " : D72X40_SYM_CROSS;
}

/* Two fixed columns; the collector checks only the selected HID route. */
static inline void d72x40_connections_row(const struct d72x40_canvas *cv, int row,
                                           const struct d72x40_status *st) {
    if (st->ble_available) {
        char profile[4] = "";
        d72x40_append_u8(profile, sizeof(profile), st->profile);
        int x = st->profile >= 100 ? 0 : 6;
        if (x != 0) {
            d72x40_text_row(cv, 0, row, D72X40_SYM_BLUETOOTH, 1);
        }
        d72x40_text_row(cv, x, row, profile, 3);
        d72x40_text_row(cv, x + d72x40_text_width(strlen(profile), D72X40_CELL) + 1,
                         row, d72x40_link_mark(st->ble_link), 1);
    }
    if (st->usb_available) {
        static const uint16_t usb_icon[7] = {0x010, 0x038, 0x1b2, 0x1ef, 0x192, 0x00c, 0x00c};
        int x = (int)cv->width - 15;
        for (int col = 0; col < 7; col++) {
            d72x40_column_row(cv, x + col, row, usb_icon[col]);
        }
        d72x40_text_row(cv, (int)cv->width - D72X40_FONT_WIDTH - 1, row,
                         d72x40_link_mark(st->usb_link), 1);
    }
}

/* Stable labels/value columns; reserve the bolt even while not charging. */
static inline void d72x40_battery_row(const struct d72x40_canvas *cv, int row, const char *label,
                                       uint8_t level, bool charging) {
    if (level == D72X40_BATTERY_HIDDEN) {
        return;
    }
    char value[6] = "";
    d72x40_append_battery(value, sizeof(value), level);
    d72x40_text_row(cv, 0, row, label, 1);
    d72x40_text_row(cv, (int)cv->width - 7 - d72x40_text_width(strlen(value), D72X40_CELL),
                     row, value, strlen(value));
    if (charging) {
        d72x40_text_row(cv, (int)cv->width - D72X40_FONT_WIDTH, row, D72X40_SYM_BOLT, 1);
    }
}

/* One ten-pixel row format with compact spacing metadata for all layouts. */
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
        if (x >= (int)cv->width) {
            return;
        }
        unsigned index = d72x40_status_index((unsigned char)s[i]);
        const uint8_t *rows = d72x40_status_rows[index];
        if (d72x40_native_storage(cv)) {
            d72x40_native_bitmap(cv, x, row, rows, D72X40_STATUS_FONT_HEIGHT, erase);
        } else {
            d72x40_logical_bitmap(cv, x, row, rows, D72X40_STATUS_FONT_HEIGHT, erase);
        }
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
        if (width + next > (int)cv->width - 2) {
            break;
        }
        width += next;
        n++;
    }
    d72x40_status_text(cv, ((int)cv->width - width) / 2, row, s, n, erase);
}

/* A nine-pixel lock row uses one mask, avoiding two padded strip writes. */
static inline void d72x40_native_lock_row(const struct d72x40_canvas *cv, int x, int row,
                                     uint16_t bits) {
    unsigned count = 9;
    if (row < 0 || row >= (int)cv->pages * 8 ||
        x >= (int)cv->width || x <= -(int)count) {
        return;
    }
    if (x < 0) {
        unsigned skip = (unsigned)-x;
        bits = (uint16_t)(bits >> skip);
        count -= skip;
        x = 0;
    }
    unsigned available = (unsigned)cv->width - (unsigned)x;
    if (count > available) {
        count = available;
    }
    bits = (uint16_t)(bits & ((1ul << count) - 1));
    if (bits == 0) {
        return;
    }
    unsigned y;
    size_t height = (size_t)cv->pages * 8;
    size_t native_x;
    if (cv->rotation == 90) {
        y = (unsigned)cv->width - (unsigned)x - count;
        bits = (uint16_t)(((uint16_t)d72x40_reverse_byte((uint8_t)bits) << 8) |
                         d72x40_reverse_byte((uint8_t)(bits >> 8)));
        bits = (uint16_t)(bits >> (16 - count));
        native_x = (size_t)row;
    } else {
        y = (unsigned)x;
        native_x = height - 1 - (size_t)row;
    }
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
    if (x <= -9 || x >= (int)cv->width || row < 0 || row >= (int)cv->pages * 8) {
        return;
    }
    static const uint16_t closed[11] = {
        0x038, 0x044, 0x082, 0x082, 0x082, 0x1ff, 0x1ef, 0x1c7, 0x1ef, 0x1ff, 0x1ff,
    };
    static const uint16_t opened[11] = {
        0x0c0, 0x120, 0x110, 0x008, 0x008, 0x1ff, 0x1ef, 0x1c7, 0x1ef, 0x1ff, 0x1ff,
    };
    const uint16_t *rows = open ? opened : closed;
    if (d72x40_native_storage(cv)) {
        for (unsigned y = 0; y < 11; y++) {
            d72x40_native_lock_row(cv, x, row + (int)y, rows[y]);
        }
    } else {
        for (unsigned strip = 0; strip < 2; strip++) {
            uint8_t packed[11];
            for (unsigned y = 0; y < 11; y++) {
                packed[y] = d72x40_reverse_byte((uint8_t)(rows[y] >> (strip * 8)));
            }
            d72x40_logical_bitmap(cv, x + (int)(strip * 8), row, packed, 11, false);
        }
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
    d72x40_status_text(cv, (int)cv->width - 8 - d72x40_status_width(value, SIZE_MAX),
                         row, value, SIZE_MAX, false);
    if (charging) {
        d72x40_text_row(cv, (int)cv->width - 6, row + 1, D72X40_SYM_BOLT, 1);
    }
}

/* Fixed portrait groups: inverted layer, selected HID route, Caps, batteries,
 * then independent Studio/Messenger locks. Missing features never reflow. */
static inline void d72x40_status_header(const struct d72x40_canvas *cv) {
    if (d72x40_native_storage(cv)) {
        size_t height = (size_t)cv->pages * 8;
        size_t rows = height < 13 ? height : 13;
        size_t x = cv->rotation == 90 ? 0 : height - rows;
        for (unsigned page = 0; page < cv->width / 8u; page++) {
            memset(&cv->buf[(size_t)page * height + x], 0xff, rows);
        }
        return;
    }
    for (int x = 0; x < (int)cv->width; x++) {
        d72x40_plot(cv, x, 0, 0xff);
        d72x40_plot(cv, x, 1, 0x1f);
    }
}

static inline void d72x40_render_portrait(const struct d72x40_canvas *cv,
                                          const struct d72x40_status *st) {
    int height = (int)cv->pages * D72X40_PAGE_HEIGHT;
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
        if (d72x40_native_storage(cv)) {
            d72x40_native_bitmap(cv, (int)cv->width - 15, 16, usb_rows, 9, false);
        } else {
            d72x40_logical_bitmap(cv, (int)cv->width - 15, 16, usb_rows, 9, false);
        }
        d72x40_text_row(cv, (int)cv->width - 6, 17, d72x40_link_mark(st->usb_link), 1);
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
        d72x40_status_text(cv, (int)cv->width - 19, height - 11, "M", 1, false);
        d72x40_status_lock(cv, (int)cv->width - 10, height - 12, st->messenger_unlocked);
    }
}

/*
 * Redraws the whole canvas from st: the layer over two pages, then the host
 * connection, the batteries and the indicators. A panel with pages to spare -
 * a tall one, or one turned on its side - gets a blank line between them.
 * Lines the panel has no page left for are left out.
 *
 * The indicator line is counted whether or not anything is on it, so that
 * Caps Lock coming and going never moves the lines above it.
 */
static inline void d72x40_render(const struct d72x40_canvas *cv, const struct d72x40_status *st) {
    d72x40_clear(cv);

    if (cv->width >= 40 && cv->width < 66 && cv->pages >= 9) {
        d72x40_render_portrait(cv, st);
        return;
    }

    int lines = (cv->pages >= 2 ? 2 : 1) + 1 + d72x40_battery_pages(cv, st) + 1;
    int spare = (int)cv->pages - lines;
    int gap = spare >= 3 ? 1 : 0;

    int page = d72x40_render_layer(cv, st->layer) + gap;

    if (page < (int)cv->pages) {
        page = d72x40_render_output(cv, page, st) + gap;
    }
    if (page < (int)cv->pages) {
        page = d72x40_render_battery(cv, page, st) + gap;
    }
    if (page < (int)cv->pages) {
        (void)d72x40_render_indicators(cv, page, st);
    }
}

/* Short names fit the 40-column portrait canvas without hiding the pattern ID. */
static inline const char *d72x40_led_pattern_name(uint8_t pattern) {
    static const char *const names[] = {
        "STEADY", "BREATH", "HEART", "BLINK", "FAST", "TRIPLE",
        "SOS", "CANDLE", "SAW", "BEACON", "SLOW", "STROBE",
        "DBLBCN", "LONG", "SPARK", "COUNT", "RIPPLE", "FADE",
    };
    return pattern < sizeof(names) / sizeof(names[0]) ? names[pattern] : "?";
}

/* Temporary LED settings view; all three live values remain visible together. */
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

    if (cv->width < 66 && cv->pages >= 7) {
        int height = (int)cv->pages * D72X40_PAGE_HEIGHT;
        d72x40_status_header(cv);
        d72x40_status_centred(cv, 2, "LED", true);
        d72x40_status_centred(cv, 16, id, false);
        d72x40_status_centred(cv, 27, d72x40_led_pattern_name(pattern), false);
        d72x40_status_centred(cv, height >= 66 ? 40 : height - 22, level, false);
        d72x40_status_centred(cv, height >= 66 ? 56 : height - 10, rate, false);
    } else {
        d72x40_text_centred(cv, 0, "LED", 3);
        d72x40_text(cv, 0, 1, id);
        d72x40_text(cv, 0, 2, d72x40_led_pattern_name(pattern));
        d72x40_text(cv, 0, 3, level);
        d72x40_text(cv, 0, 4, rate);
    }
}

/* True when the canvas pixel is lit; anything off the canvas is dark. */
static inline bool d72x40_canvas_pixel(const struct d72x40_canvas *cv, int x, int y) {
    if (x < 0 || x >= (int)cv->width || y < 0 || y >= (int)cv->pages * D72X40_PAGE_HEIGHT) {
        return false;
    }
    if (d72x40_native_storage(cv)) {
        size_t height = (size_t)cv->pages * 8;
        unsigned native_y = cv->rotation == 90 ? (unsigned)cv->width - 1u - (unsigned)x : (unsigned)x;
        size_t native_x = cv->rotation == 90 ? (size_t)y : height - 1 - (size_t)y;
        uint8_t column = cv->buf[(size_t)(native_y / 8) * height + native_x];
        return ((unsigned)column >> (native_y % 8)) & 1u;
    }
    uint8_t column = cv->buf[(size_t)(y / D72X40_PAGE_HEIGHT) * cv->width + (size_t)x];

    return (column >> (y % D72X40_PAGE_HEIGHT)) & 1;
}

/* The canvas size a panel of this size needs when the drawing is turned. */
#define D72X40_TURNED(rotation) ((rotation) == 90 || (rotation) == 270)
#define D72X40_CANVAS_WIDTH(panel_width, panel_height, rotation)                                       \
    (D72X40_TURNED(rotation) ? (panel_height) : (panel_width))
#define D72X40_CANVAS_HEIGHT(panel_width, panel_height, rotation)                                      \
    (D72X40_TURNED(rotation) ? (panel_width) : (panel_height))

/*
 * Copies the canvas into a frame in the panel's own layout, turning it
 * counter-clockwise by rotation degrees (0, 90, 180 or 270) on the way. The
 * canvas of a turned panel is as wide as the panel is tall, and vice versa.
 */
static inline void d72x40_project(const struct d72x40_canvas *cv, uint8_t *frame, uint16_t width,
                              uint16_t pages, uint16_t rotation) {
    if (rotation == 0) {
        memcpy(frame, cv->buf, (size_t)width * pages);
        return;
    }

    int canvas_height = (int)cv->pages * D72X40_PAGE_HEIGHT;

    for (uint16_t page = 0; page < pages; page++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t column = 0;

            for (int bit = 0; bit < D72X40_PAGE_HEIGHT; bit++) {
                int y = page * D72X40_PAGE_HEIGHT + bit;
                int cx;
                int cy;

                if (rotation == 90) {
                    /* The canvas's top edge becomes the panel's left edge. */
                    cx = (int)cv->width - 1 - y;
                    cy = x;
                } else if (rotation == 180) {
                    cx = (int)width - 1 - x;
                    cy = canvas_height - 1 - y;
                } else {
                    cx = y;
                    cy = canvas_height - 1 - x;
                }
                if (d72x40_canvas_pixel(cv, cx, cy)) {
                    column = (uint8_t)(column | (1u << bit));
                }
            }
            frame[(size_t)page * width + x] = column;
        }
    }
}

/*
 * Finds the next run of columns at or after the cursor (*page, *x) where want
 * differs from shown, and moves the cursor past it. A page whose bit is clear
 * in valid has unknown content on the panel, so it is one run from edge to
 * edge. Runs of a page at most D72X40_SPAN_JOIN unchanged columns apart become
 * one. Returns false once no page differs.
 */
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
