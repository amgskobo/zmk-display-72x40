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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zmk-display-72x40/display_72x40_font.h>

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
    /* A BLE profile with no bond, advertising to pair. */
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
};

struct d72x40_canvas {
    /* pages * width column bytes, page 0 first, column 0 first. */
    uint8_t *buf;
    uint16_t width;
    uint16_t pages;
};

/* A run of columns of one page; x1 is inclusive. */
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
    memset(cv->buf, 0, (size_t)cv->width * cv->pages);
}

/* ORs one column byte into the canvas; anything off the canvas is dropped. */
static inline void d72x40_plot(const struct d72x40_canvas *cv, int x, int page, uint8_t bits) {
    if (x < 0 || x >= (int)cv->width || page < 0 || page >= (int)cv->pages) {
        return;
    }
    size_t at = (size_t)page * cv->width + (size_t)x;
    cv->buf[at] = (uint8_t)(cv->buf[at] | bits);
}

/*
 * The pixel width of n glyphs laid out cell wide (D72X40_CELL or D72X40_WIDE_CELL),
 * not counting the blank column(s) after the last one.
 */
static inline int d72x40_text_width(size_t n, int cell) {
    if (n == 0) {
        return 0;
    }
    int scale = cell / D72X40_CELL;
    return (int)n * cell - (cell - scale * D72X40_FONT_WIDTH);
}

/* Draws at most max glyphs of s in the page at x; returns the x after them. */
static inline int d72x40_text_n(const struct d72x40_canvas *cv, int x, int page, const char *s,
                            size_t max) {
    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
        const uint8_t *g = d72x40_glyph((unsigned char)s[i]);
        for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
            d72x40_plot(cv, x + col, page, g[col]);
        }
        x += D72X40_CELL;
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
    int scale = wide ? 2 : 1;
    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
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
    char digits[4];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value = (uint8_t)(value / 10);
    } while (value != 0);
    char text[4];
    for (size_t i = 0; i < n; i++) {
        text[i] = digits[n - 1 - i];
    }
    text[n] = '\0';
    d72x40_append(buf, size, text);
}

static inline void d72x40_append_u16(char *buf, size_t size, uint16_t value) {
    char digits[6];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value = (uint16_t)(value / 10);
    } while (value != 0);
    char result[6];
    for (size_t i = 0; i < n; i++) {
        result[i] = digits[n - 1 - i];
    }
    result[n] = '\0';
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
    int page = row / D72X40_PAGE_HEIGHT;
    unsigned shift = (unsigned)(row % D72X40_PAGE_HEIGHT);

    for (size_t i = 0; i < max && s[i] != '\0'; i++) {
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
                  : st->link == D72X40_LINK_OPEN ? D72X40_SYM_OPEN
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
        d72x40_text_centred(cv, 0, "LED", 3);
        d72x40_text_centred(cv, 16, id, strlen(id));
        d72x40_text_centred(cv, 24, d72x40_led_pattern_name(pattern),
                            strlen(d72x40_led_pattern_name(pattern)));
        d72x40_text_centred(cv, 40, level, strlen(level));
        d72x40_text_centred(cv, 56, rate, strlen(rate));
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
