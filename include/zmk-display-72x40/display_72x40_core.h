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

/* Fixed panel storage and portrait drawing coordinates. */
#define D72X40_PANEL_WIDTH 72
#define D72X40_PANEL_HEIGHT 40
#define D72X40_PANEL_PAGES 5
#define D72X40_FRAME_BYTES 360
#define D72X40_CANVAS_WIDTH 40
#define D72X40_CANVAS_HEIGHT 72
#define D72X40_CANVAS_PAGES 9

/* The controller page height. */
#define D72X40_PAGE_HEIGHT 8
/* Room for a layer name as ZMK stores it; the panel shows what fits. */
#define D72X40_LAYER_TEXT_SIZE 24

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

/* A frame is always the fixed controller's 360-byte page layout. */
struct d72x40_canvas {
    uint8_t *buf;
};

struct d72x40_span {
    uint16_t page;
    uint16_t x0;
    uint16_t x1;
};

static inline void d72x40_clear(const struct d72x40_canvas *cv) {
    memset(cv->buf, 0, (size_t)D72X40_CANVAS_WIDTH * D72X40_CANVAS_PAGES);
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

static inline void d72x40_append_u16(char *buf, size_t size, uint16_t value) {
    char digits[6];
    size_t at = sizeof(digits) - 1;
    digits[at] = '\0';
    do {
        digits[--at] = (char)('0' + value % 10);
        value = (uint16_t)(value / 10);
    } while (value != 0);
    d72x40_append(buf, size, &digits[at]);
}

static inline void d72x40_append_u8(char *buf, size_t size, uint8_t value) {
    d72x40_append_u16(buf, size, value);
}

static inline void d72x40_append_battery(char *buf, size_t size, uint8_t level) {
    if (level == D72X40_BATTERY_UNKNOWN) {
        d72x40_append(buf, size, "--");
        return;
    }
    d72x40_append_u8(buf, size, level > 100 ? 100 : level);
    d72x40_append(buf, size, "%");
}

enum d72x40_symbol_code {
    D72X40_SYMBOL_BLANK = 0,
    D72X40_SYMBOL_CHECK,
    D72X40_SYMBOL_CROSS,
    D72X40_SYMBOL_OPEN,
    D72X40_SYMBOL_BLUETOOTH,
    D72X40_SYMBOL_BOLT,
    D72X40_SYMBOL_UNLOCKED,
};

/* Status marks use native symbol rows, including the empty mark (code zero). */
static inline void d72x40_symbol(const struct d72x40_canvas *cv, int x, int row,
                                  unsigned code) {
    if (code < D72X40_FONT_SYMBOL_COUNT) {
        d72x40_native_bitmap(cv, x, row, d72x40_symbol_rows[code], D72X40_FONT_HEIGHT, false);
    }
}

static inline unsigned d72x40_link_symbol(uint8_t link) {
    return link == D72X40_LINK_UP ? D72X40_SYMBOL_CHECK
           : link == D72X40_LINK_OPEN ? D72X40_SYMBOL_BLANK : D72X40_SYMBOL_CROSS;
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

/* Native nine-bit rows: clip and locate controller pages once per lock. */
static inline void d72x40_status_lock(const struct d72x40_canvas *cv, int x, int row,
                                       bool open) {
    if (x <= -9 || x >= D72X40_CANVAS_WIDTH || row < 0 || row >= D72X40_CANVAS_HEIGHT) {
        return;
    }
    static const uint16_t closed[11] = {
        0x038, 0x044, 0x082, 0x082, 0x082, 0x1ff, 0x1ef, 0x1c7, 0x1ef, 0x1ff, 0x1ff,
    };
    static const uint16_t opened[11] = {
        0x006, 0x009, 0x011, 0x020, 0x020, 0x1ff, 0x1ef, 0x1c7, 0x1ef, 0x1ff, 0x1ff,
    };
    unsigned skip = x < 0 ? (unsigned)-x : 0;
    if (x < 0) {
        x = 0;
    }
    unsigned width = 9u - skip;
    unsigned available = (unsigned)(D72X40_CANVAS_WIDTH - x);
    if (width > available) {
        width = available;
    }
    unsigned source_shift = 9u - skip - width;
    uint16_t mask = (uint16_t)((1u << width) - 1u);
    unsigned native_y = (unsigned)(D72X40_CANVAS_WIDTH - x) - width;
    unsigned shift = native_y % 8u;
    bool high = width + shift > 8u;
    size_t at = (size_t)(native_y / 8u) * D72X40_PANEL_WIDTH + (size_t)row;
    unsigned rows_available = (unsigned)(D72X40_CANVAS_HEIGHT - row);
    unsigned height = rows_available < 11 ? rows_available : 11;
    const uint16_t *rows = open ? opened : closed;
    for (unsigned y = 0; y < height; y++, at++) {
        uint16_t ink = (uint16_t)((((unsigned)rows[y] >> source_shift) & mask) << shift);
        cv->buf[at] |= (uint8_t)ink;
        if (high) {
            cv->buf[at + D72X40_PANEL_WIDTH] |= (uint8_t)(ink >> 8);
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
    d72x40_status_text(cv, D72X40_CANVAS_WIDTH - 8 - d72x40_status_width(value, SIZE_MAX),
                         row, value, SIZE_MAX, false);
    if (charging) {
        d72x40_symbol(cv, D72X40_CANVAS_WIDTH - 6, row + 1, D72X40_SYMBOL_BOLT);
    }
}

static inline void d72x40_status_header(const struct d72x40_canvas *cv) {
    for (unsigned page = 0; page < D72X40_PANEL_PAGES; page++) {
        memset(&cv->buf[(size_t)page * D72X40_PANEL_WIDTH], 0xff, 13);
    }
}

enum d72x40_status_group {
    D72X40_GROUP_HEADER = 1u << 0,
    D72X40_GROUP_CONNECTIONS = 1u << 1,
    D72X40_GROUP_CAPS = 1u << 2,
    D72X40_GROUP_BATTERY = 1u << 3,
    D72X40_GROUP_PEER_BATTERY = 1u << 4,
    D72X40_GROUP_LOCKS = 1u << 5,
    D72X40_GROUP_ALL = (1u << 6) - 1u,
};

/* Every field affecting a group is compared before reusing its pixels. */
static inline unsigned d72x40_status_changes(const struct d72x40_status *before,
                                              const struct d72x40_status *after) {
    unsigned groups = 0;
    if (memcmp(before->layer, after->layer, sizeof(before->layer)) != 0) {
        groups |= D72X40_GROUP_HEADER;
    }
    if (before->profile != after->profile || before->ble_available != after->ble_available ||
        before->usb_available != after->usb_available || before->ble_link != after->ble_link ||
        before->usb_link != after->usb_link) {
        groups |= D72X40_GROUP_CONNECTIONS;
    }
    if (before->caps_lock != after->caps_lock) {
        groups |= D72X40_GROUP_CAPS;
    }
    if (before->battery != after->battery || before->charging != after->charging ||
        (before->peer_battery == D72X40_BATTERY_HIDDEN) !=
            (after->peer_battery == D72X40_BATTERY_HIDDEN)) {
        groups |= D72X40_GROUP_BATTERY;
    }
    if (before->peer_battery != after->peer_battery || before->peer_charging != after->peer_charging) {
        groups |= D72X40_GROUP_PEER_BATTERY;
    }
    if (before->studio_lock_available != after->studio_lock_available ||
        before->unlocked != after->unlocked ||
        before->messenger_lock_available != after->messenger_lock_available ||
        before->messenger_unlocked != after->messenger_unlocked) {
        groups |= D72X40_GROUP_LOCKS;
    }
    return groups;
}

/* Logical rows are contiguous controller columns on every physical page.
 * The header initializes its own inverted background; the other groups clear
 * only their reserved columns. Blank margins between groups remain untouched. */
static inline void d72x40_clear_columns(const struct d72x40_canvas *cv,
                                         unsigned start, unsigned count) {
    for (unsigned page = 0; page < D72X40_PANEL_PAGES; page++) {
        memset(&cv->buf[(size_t)page * D72X40_PANEL_WIDTH + start], 0, count);
    }
}

static inline void d72x40_clear_groups(const struct d72x40_canvas *cv, unsigned groups) {
    static const uint8_t starts[] = {16, 27, 38, 49, 60};
    static const uint8_t heights[] = {10, 10, 10, 10, 11};
    for (unsigned i = 0; i < sizeof(starts); i++) {
        if (groups & (D72X40_GROUP_CONNECTIONS << i)) {
            d72x40_clear_columns(cv, starts[i], heights[i]);
        }
    }
}

static inline void d72x40_render_groups(const struct d72x40_canvas *cv,
                                         const struct d72x40_status *st, unsigned groups) {
    if (groups & D72X40_GROUP_HEADER) {
        d72x40_status_header(cv);
        d72x40_status_centred(cv, 2, st->layer, true);
    }
    if (groups & D72X40_GROUP_CONNECTIONS) {
        if (st->ble_available) {
            char profile[4] = "";
            d72x40_append_u8(profile, sizeof(profile), st->profile);
            int x = st->profile >= 100 ? 1 : 7;
            if (st->profile < 100) {
                d72x40_symbol(cv, 1, 17, D72X40_SYMBOL_BLUETOOTH);
            }
            d72x40_status_text(cv, x, 16, profile, SIZE_MAX, false);
            d72x40_symbol(cv, x + d72x40_status_width(profile, SIZE_MAX) + 1, 17,
                           d72x40_link_symbol(st->ble_link));
        }
        if (st->usb_available) {
            static const uint8_t usb_rows[9] = {0x10, 0x38, 0x16, 0x56, 0xe8, 0x70, 0x10, 0x38, 0x38};
            d72x40_native_bitmap(cv, D72X40_CANVAS_WIDTH - 15, 16, usb_rows, 9, false);
            d72x40_symbol(cv, D72X40_CANVAS_WIDTH - 6, 17, d72x40_link_symbol(st->usb_link));
        }
    }
    if ((groups & D72X40_GROUP_CAPS) && st->caps_lock) {
        d72x40_status_centred(cv, 27, "CAPS", false);
    }
    if (groups & D72X40_GROUP_BATTERY) {
        d72x40_status_battery(cv, 38,
                                st->peer_battery == D72X40_BATTERY_HIDDEN ? "B" : "L",
                                st->battery, st->charging);
    }
    if (groups & D72X40_GROUP_PEER_BATTERY) {
        d72x40_status_battery(cv, 49, "R", st->peer_battery, st->peer_charging);
    }
    if (groups & D72X40_GROUP_LOCKS) {
        if (st->studio_lock_available) {
            d72x40_status_text(cv, 1, 61, "S", 1, false);
            d72x40_status_lock(cv, 9, 60, st->unlocked);
        }
        if (st->messenger_lock_available) {
            d72x40_status_text(cv, D72X40_CANVAS_WIDTH - 19, 61, "M", 1, false);
            d72x40_status_lock(cv, D72X40_CANVAS_WIDTH - 10, 60, st->messenger_unlocked);
        }
    }
}

static inline void d72x40_render_portrait(const struct d72x40_canvas *cv,
                                          const struct d72x40_status *st) {
    d72x40_render_groups(cv, st, D72X40_GROUP_ALL);
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

enum d72x40_led_group {
    D72X40_LED_PATTERN = 1u << 0,
    D72X40_LED_BRIGHTNESS = 1u << 1,
    D72X40_LED_SPEED = 1u << 2,
    D72X40_LED_ALL = (1u << 3) - 1u,
};

static inline void d72x40_render_led_groups(const struct d72x40_canvas *cv, uint8_t pattern,
                                             uint8_t brightness, uint16_t speed, unsigned groups) {
    if (groups & D72X40_LED_PATTERN) {
        char id[8] = "P";
        d72x40_append_u8(id, sizeof(id), pattern);
        d72x40_status_centred(cv, 16, id, false);
        d72x40_status_centred(cv, 27, d72x40_led_pattern_name(pattern), false);
    }
    if (groups & D72X40_LED_BRIGHTNESS) {
        char level[8] = "B";
        d72x40_append_u8(level, sizeof(level), brightness);
        d72x40_append(level, sizeof(level), "%");
        d72x40_status_centred(cv, 40, level, false);
    }
    if (groups & D72X40_LED_SPEED) {
        char rate[8] = "S";
        d72x40_append_u16(rate, sizeof(rate), speed);
        d72x40_append(rate, sizeof(rate), "%");
        d72x40_status_centred(cv, 56, rate, false);
    }
}

static inline void d72x40_render_led_splash(const struct d72x40_canvas *cv, uint8_t pattern,
                                           uint8_t brightness, uint16_t speed) {
    d72x40_clear(cv);
    d72x40_status_header(cv);
    d72x40_status_centred(cv, 2, "LED", true);
    d72x40_render_led_groups(cv, pattern, brightness, speed, D72X40_LED_ALL);
}

/* One cache belongs to one canvas; zero it whenever that canvas is reset.
 * Exact state comparison is conservative: padding can only cause an extra draw.
 * Status providers still run on every invalidation, including while dark. */
#define D72X40_RENDER_CACHE 1
struct d72x40_render_cache {
    union {
        struct d72x40_status status;
        uint32_t led;
    } last;
    enum { D72X40_FRAME_EMPTY, D72X40_FRAME_STATUS, D72X40_FRAME_LED } kind;
};

static inline bool d72x40_render_cached(const struct d72x40_canvas *cv,
                                         struct d72x40_render_cache *cache,
                                         const struct d72x40_status *st) {
    if (cache->kind == D72X40_FRAME_STATUS &&
        memcmp(&cache->last.status, st, sizeof(*st)) == 0) {
        return false;
    }
    if (cache->kind == D72X40_FRAME_STATUS) {
        unsigned groups = d72x40_status_changes(&cache->last.status, st);
        if (groups != 0 && groups != D72X40_GROUP_ALL) {
            d72x40_clear_groups(cv, groups);
            d72x40_render_groups(cv, st, groups);
        } else {
            /* Conservatively preserve redraws for retained nonvisual fields. */
            d72x40_render(cv, st);
        }
    } else {
        d72x40_render(cv, st);
    }
    cache->last.status = *st;
    cache->kind = D72X40_FRAME_STATUS;
    return true;
}

static inline bool d72x40_render_led_cached(const struct d72x40_canvas *cv,
                                             struct d72x40_render_cache *cache,
                                             uint8_t pattern, uint8_t brightness, uint16_t speed) {
    uint32_t settings = (uint32_t)pattern | ((uint32_t)brightness << 8) | ((uint32_t)speed << 16);
    if (cache->kind == D72X40_FRAME_LED && cache->last.led == settings) {
        return false;
    }
    if (cache->kind == D72X40_FRAME_LED) {
        uint32_t changed = cache->last.led ^ settings;
        unsigned groups = ((changed & 0xffu) != 0 ? D72X40_LED_PATTERN : 0u) |
                          ((changed & 0xff00u) != 0 ? D72X40_LED_BRIGHTNESS : 0u) |
                          ((changed >> 16) != 0 ? D72X40_LED_SPEED : 0u);
        unsigned first = (groups & D72X40_LED_PATTERN) ? 16u
                         : (groups & D72X40_LED_BRIGHTNESS) ? 40u : 56u;
        unsigned end = (groups & D72X40_LED_SPEED) ? 66u
                       : (groups & D72X40_LED_BRIGHTNESS) ? 50u : 37u;
        d72x40_clear_columns(cv, first, end - first);
        /* A pattern/speed range includes brightness: restore its pixels too. */
        if (groups == (D72X40_LED_PATTERN | D72X40_LED_SPEED)) {
            groups = D72X40_LED_ALL;
        }
        d72x40_render_led_groups(cv, pattern, brightness, speed, groups);
    } else {
        d72x40_render_led_splash(cv, pattern, brightness, speed);
    }
    cache->last.led = settings;
    cache->kind = D72X40_FRAME_LED;
    return true;
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
