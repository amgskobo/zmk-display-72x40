/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Host tests for the drawing core. Set D72X40_TEST_DUMP=1 to print every frame
 * the tests draw as text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <zmk-display-72x40/display_72x40_core.h>
#include "approved_portrait_frames.h"

_Static_assert(sizeof(d72x40_status_rows) + sizeof(d72x40_status_advance) == 1045, "one compact pixel format and spacing metadata");

static int failures;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define MAX_WIDTH 128
#define MAX_PAGES 9

struct frame {
    uint8_t buf[MAX_WIDTH * MAX_PAGES];
    struct d72x40_canvas cv;
};

static void frame_init(struct frame *f, uint16_t width, uint16_t pages) {
    memset(f->buf, 0, sizeof(f->buf));
    f->cv = (struct d72x40_canvas){.buf = f->buf, .width = width, .pages = pages};
}

static bool pixel(const struct d72x40_canvas *cv, int x, int y) {
    uint8_t column = cv->buf[(size_t)(y / D72X40_PAGE_HEIGHT) * cv->width + (size_t)x];
    return (column >> (y % D72X40_PAGE_HEIGHT)) & 1;
}

static void dump(const struct d72x40_canvas *cv, const char *title) {
    if (getenv("D72X40_TEST_DUMP") == NULL) {
        return;
    }
    printf("%s (%ux%u)\n", title, (unsigned)cv->width,
           (unsigned)(cv->pages * D72X40_PAGE_HEIGHT));
    for (int y = 0; y < cv->pages * D72X40_PAGE_HEIGHT; y++) {
        for (int x = 0; x < cv->width; x++) {
            putchar(pixel(cv, x, y) ? '#' : '.');
        }
        putchar('\n');
    }
    putchar('\n');
}

/* The leftmost and rightmost lit columns within rows y0..y1. */
static bool lit_extent(const struct d72x40_canvas *cv, int y0, int y1, int *first, int *last) {
    *first = -1;
    *last = -1;
    for (int x = 0; x < cv->width; x++) {
        for (int y = y0; y <= y1; y++) {
            if (pixel(cv, x, y)) {
                if (*first < 0) {
                    *first = x;
                }
                *last = x;
                break;
            }
        }
    }
    return *first >= 0;
}

static bool row_blank(const struct d72x40_canvas *cv, int y) {
    for (int x = 0; x < cv->width; x++) {
        if (pixel(cv, x, y)) {
            return false;
        }
    }
    return true;
}

/* True when page holds exactly the given pieces of text and nothing else. */
static bool page_is(const struct d72x40_canvas *cv, int page, int x1, const char *s1, int x2,
                    const char *s2) {
    struct frame expect;
    frame_init(&expect, cv->width, cv->pages);
    if (s1 != NULL) {
        d72x40_text(&expect.cv, x1, page, s1);
    }
    if (s2 != NULL) {
        d72x40_text(&expect.cv, x2, page, s2);
    }
    return memcmp(&cv->buf[(size_t)page * cv->width], &expect.buf[(size_t)page * cv->width],
                  cv->width) == 0;
}

static bool studio_mark_is(const struct d72x40_canvas *cv, int page, bool unlocked) {
    static const uint8_t locked_icon[14] = {
        0x00, 0x00, 0xf0, 0xfe, 0x91, 0x91, 0xd1, 0xd1,
        0x91, 0x91, 0xfe, 0xf0, 0x00, 0x00,
    };
    static const uint8_t unlocked_icon[14] = {
        0x00, 0x00, 0xe0, 0xe0, 0xa0, 0xae, 0xe1, 0xe1,
        0xa1, 0xa1, 0xe1, 0xe1, 0x0e, 0x10,
    };
    const uint8_t *row = cv->buf + (size_t)page * cv->width;
    const uint8_t *icon = unlocked ? unlocked_icon : locked_icon;
    struct frame label;
    frame_init(&label, cv->width, cv->pages);
    d72x40_text(&label.cv, (int)cv->width - 39, page,
                unlocked ? "OPEN" : "LOCK");
    return memcmp(row + cv->width - 39,
                  label.buf + (size_t)page * cv->width + cv->width - 39,
                  (size_t)d72x40_text_width(4, D72X40_CELL)) == 0 &&
           memcmp(row + cv->width - 15, icon, 14) == 0;
}

static struct d72x40_status split_status(void) {
    struct d72x40_status st = {
        .transport = D72X40_TRANSPORT_BLE,
        .link = D72X40_LINK_UP,
        .profile = 1,
        .ble_available = true,
        .usb_available = true,
        .ble_link = D72X40_LINK_UP,
        .usb_link = D72X40_LINK_DOWN,
        .battery = 85,
        .peer_battery = 72,
    };
    strcpy(st.layer, "BASE");
    return st;
}

static void test_font(void) {
    for (int c = 0; c < 0x80; c++) {
        const uint8_t *g = d72x40_glyph((unsigned char)c);
        for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
            /* Seven rows: the eighth row of a page stays the line gap. */
            CHECK((g[col] & 0x80) == 0);
        }
    }
    int lit = 0;
    for (int c = D72X40_FONT_ASCII_FIRST + 1; c <= D72X40_FONT_ASCII_LAST; c++) {
        const uint8_t *g = d72x40_glyph((unsigned char)c);
        int any = 0;
        for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
            any |= g[col];
        }
        lit += any != 0;
    }
    CHECK(lit == D72X40_FONT_ASCII_LAST - D72X40_FONT_ASCII_FIRST);
    for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
        CHECK(d72x40_glyph(' ')[col] == 0);
    }
    CHECK(d72x40_glyph(0x80) == d72x40_glyph('?'));
    CHECK(d72x40_glyph(0xff) == d72x40_glyph('?'));
    CHECK(d72x40_glyph(0x1f) == d72x40_glyph('?'));
    CHECK(d72x40_glyph(0x01) != d72x40_glyph('?'));
}

static void test_text_width(void) {
    CHECK(d72x40_text_width(0, D72X40_CELL) == 0);
    CHECK(d72x40_text_width(1, D72X40_CELL) == 5);
    CHECK(d72x40_text_width(12, D72X40_CELL) == 71);
    CHECK(d72x40_text_width(1, D72X40_WIDE_CELL) == 10);
    CHECK(d72x40_text_width(6, D72X40_WIDE_CELL) == 70);
    CHECK(d72x40_text_width(1, 0) == 0);
    CHECK(d72x40_text_width(1, -D72X40_CELL) == 0);
}

static void test_tall_column(void) {
    CHECK(d72x40_tall_column(0) == 0);
    /* The top font row becomes rows 1-2; row 0 stays blank. */
    CHECK(d72x40_tall_column(0x01) == 0x0006);
    /* The bottom font row becomes rows 13-14; row 15 stays blank. */
    CHECK(d72x40_tall_column(0x40) == 0x6000);
    CHECK(d72x40_tall_column(0x7f) == 0x7ffe);
}

static void test_layer_wide_and_centred(void) {
    struct frame f;
    frame_init(&f, 72, 5);
    struct d72x40_status st = split_status();
    strcpy(st.layer, "NUM");
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "NUM");

    int first, last;
    CHECK(lit_extent(&f.cv, 0, 15, &first, &last));
    /* Three wide glyphs are 34 columns, centred in 72. */
    CHECK(first == 19);
    CHECK(last == 52);
    CHECK(row_blank(&f.cv, 0));
    CHECK(row_blank(&f.cv, 15));
    /* N's left stroke fills the band at double width. */
    for (int y = 1; y <= 14; y++) {
        CHECK(pixel(&f.cv, 19, y));
        CHECK(pixel(&f.cv, 20, y));
        CHECK(!pixel(&f.cv, 18, y));
    }
}

static void test_layer_narrow_when_wide_does_not_fit(void) {
    struct frame f;
    frame_init(&f, 72, 5);
    struct d72x40_status st = split_status();

    strcpy(st.layer, "USER-A");
    d72x40_render(&f.cv, &st);
    int first, last;
    CHECK(lit_extent(&f.cv, 0, 15, &first, &last));
    /* Six wide glyphs still fit: 70 columns. */
    CHECK(first == 1);
    CHECK(last == 70);

    /* Too long for double size: one ordinary line, centred in the block. */
    strcpy(st.layer, "TEMP-SCROLL");
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "TEMP-SCROLL");
    CHECK(lit_extent(&f.cv, 0, 15, &first, &last));
    /* Eleven ordinary glyphs are 65 columns. */
    CHECK(first == 3);
    CHECK(last == 67);
    /* Rows 4 to 10 of the sixteen-row block, and nothing outside them. */
    for (int y = 0; y < 16; y++) {
        CHECK(row_blank(&f.cv, y) == (y < 4 || y > 10));
    }

    /* Too long for one line: two ordinary lines, split at the hyphen. */
    strcpy(st.layer, "TEMP-SCROLLING");
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "wrapped at the hyphen");
    CHECK(page_is(&f.cv, 0, 21, "TEMP-", 0, NULL));
    CHECK(page_is(&f.cv, 1, 9, "SCROLLING", 0, NULL));

    /* With no hyphen to split at, the break comes where the line ends. */
    strcpy(st.layer, "ABCDEFGHIJKLMNOPQRST");
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "wrapped and cut");
    CHECK(page_is(&f.cv, 0, 0, "ABCDEFGHIJKL", 0, NULL));
    CHECK(page_is(&f.cv, 1, 12, "MNOPQRST", 0, NULL));
}

static void test_layer_on_other_panels(void) {
    struct frame f;
    struct d72x40_status st = split_status();
    int first, last;

    strcpy(st.layer, "ABCDEFGHIJ");
    frame_init(&f, 128, 4);
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "128x32");
    CHECK(lit_extent(&f.cv, 0, 15, &first, &last));
    /* Ten wide glyphs fit in 128: 118 columns. */
    CHECK(first == 5);
    CHECK(last == 122);

    strcpy(st.layer, "NUM");
    frame_init(&f, 72, 1);
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 0, 27, "NUM", 0, NULL));

    st.layer[0] = '\0';
    frame_init(&f, 72, 5);
    d72x40_render(&f.cv, &st);
    CHECK(row_blank(&f.cv, 5));
    CHECK(!lit_extent(&f.cv, 0, 15, &first, &last));
}

static void test_output_line(void) {
    struct frame f;
    frame_init(&f, 72, 5);
    struct d72x40_status st = split_status();

    d72x40_render(&f.cv, &st);
    dump(&f.cv, "BASE, BLE 1 connected");
    CHECK(page_is(&f.cv, 2, 0, D72X40_SYM_BLUETOOTH "1 " D72X40_SYM_CHECK, 0, NULL));

    st.link = D72X40_LINK_DOWN;
    st.profile = 3;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 2, 0, D72X40_SYM_BLUETOOTH "3 " D72X40_SYM_CROSS, 0, NULL));

    st.link = D72X40_LINK_OPEN;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 2, 0, D72X40_SYM_BLUETOOTH "3 ", 0, NULL));

    st.transport = D72X40_TRANSPORT_USB;
    st.link = D72X40_LINK_UP;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 2, 0, "USB", 0, NULL));

    st.link = D72X40_LINK_DOWN;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 2, 0, "USB " D72X40_SYM_CROSS, 0, NULL));

    st.transport = D72X40_TRANSPORT_NONE;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 2, 0, D72X40_SYM_CROSS, 0, NULL));

    st.transport = D72X40_TRANSPORT_BLE;
    st.link = D72X40_LINK_UP;
    st.profile = 1;
    st.studio_lock_available = true;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "Studio locked");
    CHECK(studio_mark_is(&f.cv, 2, false));

    st.unlocked = true;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "Studio unlocked");
    CHECK(studio_mark_is(&f.cv, 2, true));
}

static void test_battery_line(void) {
    struct frame f;
    frame_init(&f, 72, 5);
    struct d72x40_status st = split_status();

    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, "L 85%", 43, "R 72%"));

    st.peer_battery = D72X40_BATTERY_UNKNOWN;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, "L 85%", 49, "R --"));

    st.charging = true;
    st.battery = 100;
    st.peer_battery = 72;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "charging");
    CHECK(page_is(&f.cv, 3, 0, "L100%" D72X40_SYM_BOLT, 43, "R 72%"));

    /* Both full: the spaces go so a cell still separates the halves. */
    st.peer_battery = 100;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, "L100%" D72X40_SYM_BOLT, 43, "R100%"));
    st.charging = false;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "both full");
    CHECK(page_is(&f.cv, 3, 0, "L100%", 37, "R 100%"));

    /* Values above 100 are clamped. */
    st.battery = 180;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, "L100%", 37, "R 100%"));

    /* The peripheral on external power gets a bolt of its own. */
    st.charging = false;
    st.battery = 85;
    st.peer_battery = 72;
    st.peer_charging = true;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "peripheral charging");
    CHECK(page_is(&f.cv, 3, 0, "L 85%", 37, "R 72%" D72X40_SYM_BOLT));

    /* Both charging and both full: no space left to drop, so a line each. */
    st.charging = true;
    st.battery = 100;
    st.peer_battery = 100;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "both charging and full");
    CHECK(page_is(&f.cv, 3, 0, "L 100%" D72X40_SYM_BOLT, 0, NULL));
    CHECK(page_is(&f.cv, 4, 0, "R 100%" D72X40_SYM_BOLT, 0, NULL));

    /* One space is enough to keep them side by side. */
    st.peer_charging = false;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, "L100%" D72X40_SYM_BOLT, 43, "R100%"));
    st.charging = false;
    st.peer_charging = false;
    st.battery = 85;
    st.peer_battery = 72;

    /* A central without a battery of its own shows only the peripheral's. */
    st.battery = D72X40_BATTERY_HIDDEN;
    st.peer_battery = 40;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, NULL, 43, "R 40%"));

    /* A unibody. */
    st.battery = 9;
    st.peer_battery = D72X40_BATTERY_HIDDEN;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, "BAT 9%", 0, NULL));
    st.charging = true;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, "BAT 9%" D72X40_SYM_BOLT, 0, NULL));
    st.battery = D72X40_BATTERY_HIDDEN;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 3, 0, NULL, 0, NULL));
}

static void test_indicator_line(void) {
    struct frame f;
    frame_init(&f, 72, 5);
    struct d72x40_status st = split_status();

    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 4, 0, NULL, 0, NULL));
    st.caps_lock = true;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "caps lock");
    CHECK(page_is(&f.cv, 4, 0, "CAPS", 0, NULL));

    /* A four-page panel has no indicator line and draws nothing past it. */
    frame_init(&f, 72, 4);
    memset(f.buf, 0, sizeof(f.buf));
    d72x40_render(&f.cv, &st);
    for (size_t i = 4u * 72u; i < sizeof(f.buf); i++) {
        CHECK(f.buf[i] == 0);
    }
}

/* A 72x40 panel turned on its side: 40 columns, nine pages. */
/* The portrait groups have fixed anchors; state changes never reflow them. */
static bool row_text_is(const struct d72x40_canvas *cv, int row, int x, const char *text) {
    struct frame expected;
    frame_init(&expected, cv->width, cv->pages);
    bool symbol = (unsigned char)text[0] < 32;
    int width = symbol ? d72x40_text_width(strlen(text), D72X40_CELL)
                       : d72x40_status_width(text, SIZE_MAX);
    int height = symbol ? D72X40_FONT_HEIGHT : D72X40_STATUS_FONT_HEIGHT;
    if (symbol) {
        d72x40_text_row(&expected.cv, x, row, text, strlen(text));
    } else {
        d72x40_status_text(&expected.cv, x, row, text, SIZE_MAX, false);
    }
    for (int y = row; y < row + height; y++) {
        for (int col = x; col < x + width; col++) {
            if (pixel(cv, col, y) != pixel(&expected.cv, col, y)) {
                return false;
            }
        }
    }
    return true;
}

static void test_portrait_layout(void) {
    struct frame f;
    frame_init(&f, 40, 9);
    struct d72x40_status st = split_status();
    st.caps_lock = true;
    st.studio_lock_available = true;
    st.messenger_lock_available = true;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "portrait: two locks, independent BLE/USB, aligned batteries");

    int first, last;
    CHECK(lit_extent(&f.cv, 0, 15, &first, &last));
    CHECK(first == 0 && last == 39);
    CHECK(!row_blank(&f.cv, 1) && row_blank(&f.cv, 14));
    CHECK(!row_blank(&f.cv, 0) && row_blank(&f.cv, 15));
    CHECK(row_text_is(&f.cv, 61, 1, "S"));
    CHECK(row_text_is(&f.cv, 61, 21, "M"));
    CHECK(row_text_is(&f.cv, 16, 7, "1"));
    CHECK(row_text_is(&f.cv, 17, 11, D72X40_SYM_CHECK));
    CHECK(row_text_is(&f.cv, 17, 34, D72X40_SYM_CROSS));
    CHECK(row_text_is(&f.cv, 27, 7, "CAPS"));
    CHECK(row_text_is(&f.cv, 38, 1, "L"));
    CHECK(row_text_is(&f.cv, 38, 15, "85%"));
    CHECK(row_text_is(&f.cv, 49, 1, "R"));
    CHECK(row_text_is(&f.cv, 49, 15, "72%"));
    CHECK(row_blank(&f.cv, 71)); /* outer margin around both locks */

    /* OPEN has no mark, clearing the preceding check; USB stays independent. */
    for (uint8_t ble = D72X40_LINK_DOWN; ble <= D72X40_LINK_OPEN; ble++) {
        for (uint8_t usb = D72X40_LINK_DOWN; usb <= D72X40_LINK_UP; usb++) {
            st.ble_link = ble;
            st.usb_link = usb;
            d72x40_render(&f.cv, &st);
            const char *mark = ble == D72X40_LINK_UP ? D72X40_SYM_CHECK
                               : ble == D72X40_LINK_OPEN ? " " : D72X40_SYM_CROSS;
            CHECK(row_text_is(&f.cv, 17, 11, mark));
            CHECK(row_text_is(&f.cv, 17, 34,
                              usb == D72X40_LINK_UP ? D72X40_SYM_CHECK : D72X40_SYM_CROSS));
            CHECK(row_text_is(&f.cv, 38, 15, "85%"));
        }
    }

    st.battery = 100;
    st.peer_battery = 100;
    st.charging = true;
    st.peer_charging = true;
    d72x40_render(&f.cv, &st);
    CHECK(row_text_is(&f.cv, 38, 12, "100%"));
    CHECK(row_text_is(&f.cv, 39, 34, D72X40_SYM_BOLT));
    CHECK(row_text_is(&f.cv, 49, 12, "100%"));
    CHECK(row_text_is(&f.cv, 50, 34, D72X40_SYM_BOLT));
    st.battery = 255; /* hidden rather than clamped */
    st.peer_battery = D72X40_BATTERY_UNKNOWN;
    d72x40_render(&f.cv, &st);
    CHECK(row_blank(&f.cv, 38));
    CHECK(row_text_is(&f.cv, 49, 20, "--"));
    st.battery = 180; /* genuine out-of-range values still clamp */
    st.peer_battery = D72X40_BATTERY_HIDDEN;
    d72x40_render(&f.cv, &st);
    CHECK(row_text_is(&f.cv, 38, 1, "B"));
    CHECK(row_text_is(&f.cv, 38, 12, "100%"));
    CHECK(row_blank(&f.cv, 49));

    strcpy(st.layer, "TEMP-SCROLL");
    d72x40_render(&f.cv, &st);
    CHECK(row_blank(&f.cv, 13) && row_blank(&f.cv, 14)); /* whole-glyph header truncation */

    /* Features omitted by the build leave their own fixed column empty. */
    st.ble_available = false;
    st.usb_available = false;
    st.studio_lock_available = false;
    st.messenger_lock_available = false;
    st.caps_lock = false;
    d72x40_render(&f.cv, &st);
    CHECK(row_blank(&f.cv, 61) && row_blank(&f.cv, 16) && row_blank(&f.cv, 27));
}

static void test_portrait_studio_lock(void) {
    struct frame f;
    frame_init(&f, 40, 9);
    struct d72x40_status st = split_status();
    st.studio_lock_available = true;
    st.messenger_lock_available = true;
    for (int studio = 0; studio <= 1; studio++) {
        for (int messenger = 0; messenger <= 1; messenger++) {
            st.unlocked = studio != 0;
            st.messenger_unlocked = messenger != 0;
            d72x40_render(&f.cv, &st);
            CHECK(pixel(&f.cv, 10, 62) == !st.unlocked);
            CHECK(pixel(&f.cv, 17, 62) == st.unlocked);
            CHECK(pixel(&f.cv, 9, 65)); /* Studio body never moves */
            CHECK(pixel(&f.cv, 31, 62) == !st.messenger_unlocked);
            CHECK(pixel(&f.cv, 38, 62) == st.messenger_unlocked);
            CHECK(pixel(&f.cv, 30, 65)); /* Messenger body never moves */
            CHECK(row_text_is(&f.cv, 38, 15, "85%"));
            CHECK(row_text_is(&f.cv, 49, 15, "72%"));
        }
    }
}

static void test_portrait_boundaries(void) {
    struct frame f;
    frame_init(&f, 40, 9);
    /* A nine-pixel USB column crosses a page even at an aligned row. */
    d72x40_column_row(&f.cv, 0, 0, 0x1ff);
    CHECK(f.buf[0] == 0xff && f.buf[40] == 1);
    d72x40_column_row(&f.cv, 1, 7, 0x1ff);
    CHECK(f.buf[1] == 0x80 && f.buf[41] == 0xff);
    d72x40_column_row(&f.cv, 39, 71, 0x1ff);
    CHECK(f.buf[8 * 40 + 39] == 0x80);
    d72x40_column_row(&f.cv, 40, 0, 0x1ff);
    d72x40_column_row(&f.cv, 2, -1, 0x1ff);
    d72x40_column_row(&f.cv, 2, 72, 0x1ff);
    CHECK(f.buf[2] == 0); /* off-screen columns do not wrap */

    struct d72x40_status st = split_status();
    st.profile = 99;
    strcpy(st.layer, "NUM");
    d72x40_render(&f.cv, &st);
    CHECK(row_text_is(&f.cv, 16, 7, "99"));
    CHECK(row_text_is(&f.cv, 17, 18, D72X40_SYM_CHECK));
    st.profile = 255;
    d72x40_render(&f.cv, &st);
    CHECK(row_text_is(&f.cv, 16, 1, "255"));
    CHECK(row_text_is(&f.cv, 17, 17, D72X40_SYM_CHECK));
    CHECK(row_text_is(&f.cv, 17, 34, D72X40_SYM_CROSS));

    /* A short portrait uses the compact page layout and stays in bounds. */
    frame_init(&f, 40, 8);
    st.battery = 100;
    st.peer_battery = D72X40_BATTERY_HIDDEN;
    st.charging = true;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 5, 0, "B100%" D72X40_SYM_BOLT, 0, NULL));
    for (size_t i = 40u * 8u; i < sizeof(f.buf); i++) {
        CHECK(f.buf[i] == 0);
    }
    frame_init(&f, 39, 9);
    d72x40_render(&f.cv, &st);
    for (size_t i = 39u * 9u; i < sizeof(f.buf); i++) {
        CHECK(f.buf[i] == 0);
    }
}

static int count_lit(const struct d72x40_canvas *cv) {
    int lit = 0;

    for (int y = 0; y < cv->pages * D72X40_PAGE_HEIGHT; y++) {
        for (int x = 0; x < cv->width; x++) {
            lit += pixel(cv, x, y);
        }
    }
    return lit;
}

static void test_rotation(void) {
    struct frame canvas;
    struct frame panel;

    /* The drawing for a 72x40 panel turned on its side. */
    frame_init(&canvas, 40, 9);
    frame_init(&panel, 72, 5);
    canvas.buf[0] = 1; /* canvas pixel (0, 0) */

    d72x40_project(&canvas.cv, panel.buf, 72, 5, 90);
    /* Counter-clockwise: the canvas's top-left corner goes bottom-left. */
    CHECK(pixel(&panel.cv, 0, 39));
    CHECK(count_lit(&panel.cv) == 1);

    d72x40_project(&canvas.cv, panel.buf, 72, 5, 270);
    /* Clockwise: it goes top-right. */
    CHECK(pixel(&panel.cv, 71, 0));
    CHECK(count_lit(&panel.cv) == 1);

    /* The canvas's bottom-right corner is the other corner each way. */
    memset(canvas.buf, 0, sizeof(canvas.buf));
    canvas.buf[8 * 40 + 39] = 1u << 7; /* canvas pixel (39, 71) */
    d72x40_project(&canvas.cv, panel.buf, 72, 5, 90);
    CHECK(pixel(&panel.cv, 71, 0));
    d72x40_project(&canvas.cv, panel.buf, 72, 5, 270);
    CHECK(pixel(&panel.cv, 0, 39));

    /* Turning keeps every pixel, and a whole status survives the turn. */
    struct d72x40_status st = split_status();

    strcpy(st.layer, "NUM");
    d72x40_render(&canvas.cv, &st);

    int drawn = count_lit(&canvas.cv);

    d72x40_project(&canvas.cv, panel.buf, 72, 5, 90);
    dump(&panel.cv, "portrait status turned onto the panel");
    CHECK(count_lit(&panel.cv) == drawn);
    for (int y = 0; y < 72; y++) {
        for (int x = 0; x < 40; x++) {
            /* (x, y) of the canvas is (y, 39 - x) of the panel. */
            CHECK(pixel(&canvas.cv, x, y) == pixel(&panel.cv, y, 39 - x));
        }
    }

    /* Half a turn, on a canvas the panel's own way round. */
    struct frame flat;

    frame_init(&flat, 72, 5);
    d72x40_render(&flat.cv, &st);
    drawn = count_lit(&flat.cv);
    d72x40_project(&flat.cv, panel.buf, 72, 5, 180);
    CHECK(count_lit(&panel.cv) == drawn);
    CHECK(pixel(&flat.cv, 3, 4) == pixel(&panel.cv, 68, 35));

    /* No turn at all is a copy. */
    d72x40_project(&flat.cv, panel.buf, 72, 5, 0);
    CHECK(memcmp(flat.buf, panel.buf, 72 * 5) == 0);
}

/* Independent pixel coordinates specify the panel output, without calling
 * the production projector, row writer or logical pixel getter. */
static void reference_turn(const struct d72x40_canvas *logical, uint8_t *out, uint16_t rotation) {
    int height = (int)logical->pages * 8;
    memset(out, 0, (size_t)logical->width * logical->pages);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < (int)logical->width; x++) {
            if (!pixel(logical, x, y)) {
                continue;
            }
            int px = rotation == 90 ? y : height - 1 - y;
            int py = rotation == 90 ? (int)logical->width - 1 - x : x;
            size_t at = (size_t)(py / 8) * (size_t)height + (size_t)px;
            out[at] = (uint8_t)(out[at] | (uint8_t)(1u << (py % 8)));
        }
    }
}

static void check_native(const struct frame *logical, const struct frame *native) {
    uint8_t expected[MAX_WIDTH * MAX_PAGES];
    reference_turn(&logical->cv, expected, native->cv.rotation);
    CHECK(memcmp(expected, native->buf, (size_t)logical->cv.width * logical->cv.pages) == 0);
}

/* Pixel coordinates independently specify packed rows and all storage layouts. */
static void test_bitmap_edges(void) {
    const uint8_t rows[11] = {0xa7, 0x59, 0x80, 0x01, 0xff, 0x00, 0x18, 0x42, 0x81, 0x7e, 0x24};
    const int positions[] = {INT_MIN, -17, -9, -8, -1, 0, 7, 31, 39, 40};
    const uint16_t rotations[] = {0, 90, 270};
    struct frame actual, expected;
    for (size_t r = 0; r < 3; r++) {
        for (size_t p = 0; p < sizeof(positions) / sizeof(positions[0]); p++) {
            for (int row = -1; row <= 72; row++) {
                for (unsigned erasing = 0; erasing < 2; erasing++) {
                    frame_init(&actual, 40, 9); frame_init(&expected, 40, 9);
                    actual.cv.rotation = rotations[r];
                    memset(actual.buf, erasing ? 0xff : 0, sizeof(actual.buf));
                    memset(expected.buf, erasing ? 0xff : 0, sizeof(expected.buf));
                    if (rotations[r] == 0) {
                        d72x40_logical_bitmap(&actual.cv, positions[p], row, rows, 11, erasing != 0);
                    } else {
                        d72x40_native_bitmap(&actual.cv, positions[p], row, rows, 11, erasing != 0);
                    }
                    if (row >= 0 && row < 72) {
                        for (unsigned y = 0; y < 11; y++) {
                            for (unsigned col = 0; col < 8; col++) {
                                int x = positions[p] + (int)col;
                                int py = row + (int)y;
                                if (x >= 0 && x < 40 && py < 72 && (rows[y] & (0x80u >> col))) {
                                    size_t at = (size_t)(py / 8) * 40 + (size_t)x;
                                    uint8_t ink = (uint8_t)(1u << (py % 8));
                                    expected.buf[at] = erasing
                                        ? (uint8_t)(expected.buf[at] & (uint8_t)~ink)
                                        : (uint8_t)(expected.buf[at] | ink);
                                }
                            }
                        }
                    }
                    if (rotations[r] == 0) {
                        CHECK(memcmp(actual.buf, expected.buf, sizeof(actual.buf)) == 0);
                    } else {
                        check_native(&expected, &actual);
                        CHECK(memcmp(actual.buf + 360, expected.buf + 360, sizeof(actual.buf) - 360) == 0);
                    }
                }
            }
        }
    }
}

static void test_direct_primitives(void) {
    for (uint16_t rotation = 90; rotation <= 270; rotation = (uint16_t)(rotation + 180)) {
        struct frame logical, native;
        frame_init(&logical, 40, 9); frame_init(&native, 40, 9);
        native.cv.rotation = rotation;
        unsigned seed = 7351;
        for (unsigned i = 0; i < 300; i++) {
            seed = seed * 1664525u + 1013904223u;
            int x = (int)(seed % 44) - 2;
            int page = (int)((seed >> 8) % 12) - 1;
            uint8_t bits = (uint8_t)(seed >> 16);
            bool erase = (i & 1u) != 0;
            d72x40_mask(&logical.cv, x, page, bits, erase);
            d72x40_mask(&native.cv, x, page, bits, erase);
        }
        check_native(&logical, &native);
        for (int y = -1; y <= 72; y++) {
            for (int x = -1; x <= 40; x++) {
                bool expected = x >= 0 && x < 40 && y >= 0 && y < 72 && pixel(&logical.cv, x, y);
                CHECK(d72x40_canvas_pixel(&native.cv, x, y) == expected);
            }
        }
        /* Nine-pixel lock masks retain clipping and zero-row behavior. */
        const int positions[] = {INT_MIN, -9, -1, 0, 7, 31, 39, 40};
        for (size_t p = 0; p < sizeof(positions) / sizeof(positions[0]); p++) {
            for (int row = -1; row <= 72; row++) {
                d72x40_clear(&logical.cv); d72x40_clear(&native.cv);
                uint16_t bits = row % 3 == 0 ? 0 : UINT16_C(0x195);
                d72x40_native_lock_row(&native.cv, positions[p], row, bits);
                if (row >= 0 && row < 72) {
                    for (unsigned bit = 0; bit < 9; bit++) {
                        int x = positions[p] + (int)bit;
                        if (((unsigned)bits >> bit) & 1u) {
                            d72x40_mask(&logical.cv, x, row / 8, (uint8_t)(1u << (row % 8)), false);
                        }
                    }
                }
                check_native(&logical, &native);
            }
        }
        for (unsigned open = 0; open < 2; open++) {
            d72x40_status_lock(&native.cv, -9, 0, open != 0);
            d72x40_status_lock(&native.cv, 40, 0, open != 0);
        }
        for (unsigned code = 31; code <= 127; code++) {
            const char text[2] = {(char)code, '\0'};
            for (int x = -2; x <= 40; x += 7) {
                for (int row = 0; row < 72; row += 7) {
                    d72x40_clear(&logical.cv); d72x40_clear(&native.cv);
                    d72x40_status_text(&logical.cv, x, row, text, 1, false);
                    d72x40_status_text(&native.cv, x, row, text, 1, false);
                    check_native(&logical, &native);
                    d72x40_status_text(&logical.cv, x, row, text, 1, true);
                    d72x40_status_text(&native.cv, x, row, text, 1, true);
                    check_native(&logical, &native);
                }
            }
        }
        /* Independent old column symbols specify the new native row path. */
        const int symbol_x[] = {-9, -8, -1, 0, 33, 39, 40};
        for (unsigned code = 1; code < D72X40_FONT_SYMBOL_COUNT; code++) {
            const char text[2] = {(char)code, '\0'};
            for (size_t p = 0; p < sizeof(symbol_x) / sizeof(symbol_x[0]); p++) {
                for (int row = 0; row <= 72; row++) {
                    d72x40_clear(&logical.cv);d72x40_clear(&native.cv);
                    d72x40_text_row(&logical.cv, symbol_x[p], row, text, 1);
                    d72x40_text_row(&native.cv, symbol_x[p], row, text, 1);
                    check_native(&logical, &native);
                }
            }
        }
        uint8_t unchanged[sizeof(native.buf)];memcpy(unchanged, native.buf, sizeof(unchanged));
        d72x40_native_bitmap(&native.cv, 0, -1, d72x40_symbol_rows[1], 7, false);
        CHECK(memcmp(unchanged, native.buf, sizeof(unchanged)) == 0);
        /* Native row masks clip all four edges without a glyph conversion. */
        for (int x = -1; x <= 72; x += 1) {
            for (int y = -9; y <= 40; y += 1) {
                for (unsigned erasing = 0; erasing < 2; erasing++) {
                    memset(native.buf, erasing ? 0xff : 0, sizeof(native.buf));
                    uint8_t expected[MAX_WIDTH * MAX_PAGES];
                    memset(expected, erasing ? 0xff : 0, sizeof(expected));
                    /* Same physical single-row specification, through the
                     * retained bitmap entry point after row-helper removal. */
                    const uint8_t rows[1] = {rotation == 90 ? 0xa9 : d72x40_reverse_byte(0xa9)};
                    int logical_x = rotation == 90 ? 40 - y - 8 : y;
                    int logical_row = rotation == 90 ? x : 71 - x;
                    d72x40_native_bitmap(&native.cv, logical_x, logical_row, rows, 1, erasing != 0);
                    for (int bit = 0; bit < 8; bit++) {
                        int py = y + bit;
                        if (x >= 0 && x < 72 && py >= 0 && py < 40 && (0xa9u & (1u << bit))) {
                            size_t at = (size_t)(py / 8) * 72 + (size_t)x;
                            uint8_t ink = (uint8_t)(1u << (py % 8));
                            expected[at] = erasing ? (uint8_t)(expected[at] & (uint8_t)~ink)
                                                    : (uint8_t)(expected[at] | ink);
                        }
                    }
                    CHECK(memcmp(native.buf, expected, 360) == 0);
                }
            }
        }
        for (unsigned open = 0; open < 2; open++) {
            d72x40_clear(&logical.cv); d72x40_clear(&native.cv);
            d72x40_status_lock(&logical.cv, -1, 68, open != 0);
            d72x40_status_lock(&native.cv, -1, 68, open != 0);
            check_native(&logical, &native);
        }
        frame_init(&logical, 16, 1); frame_init(&native, 16, 1);
        native.cv.rotation = rotation;
        d72x40_status_header(&logical.cv); d72x40_status_header(&native.cv);
        check_native(&logical, &native);
        const uint16_t widths[] = {40, 56, 72};
        const uint16_t pages[] = {9, 7, 5};
        for (size_t shape = 0; shape < 3; shape++) {
            frame_init(&logical, widths[shape], pages[shape]);
            frame_init(&native, widths[shape], pages[shape]);native.cv.rotation = rotation;
            for (unsigned pattern = 0; pattern <= 18; pattern++) {
                d72x40_render_led_splash(&logical.cv, (uint8_t)pattern, UINT8_MAX, UINT16_MAX);
                d72x40_render_led_splash(&native.cv, (uint8_t)pattern, UINT8_MAX, UINT16_MAX);
                check_native(&logical, &native);
            }
        }
    }
}

static void test_approved_portrait_frames(void) {
    for (size_t i = 0; i < sizeof(d72x40_approved_frames) / sizeof(d72x40_approved_frames[0]); i++) {
        const struct d72x40_approved_frame *g = &d72x40_approved_frames[i];
        struct frame f;
        frame_init(&f, 40, 9);
        struct d72x40_status st = {.profile = 2, .ble_available = true, .usb_available = true,
            .ble_link = g->route == 0 ? D72X40_LINK_UP : g->route == 1 ? D72X40_LINK_OPEN : D72X40_LINK_DOWN,
            .usb_link = g->route == 1 ? D72X40_LINK_UP : D72X40_LINK_OPEN,
            .battery = g->battery, .peer_battery = g->peer_battery,
            .studio_lock_available = true, .messenger_lock_available = true,
            .unlocked = g->studio, .messenger_unlocked = g->messenger,
            .caps_lock = g->caps, .charging = g->charging};
        strcpy(st.layer, g->layer);
        d72x40_render(&f.cv, &st);
        for (uint16_t rotation = 90; rotation <= 270; rotation = (uint16_t)(rotation + 180)) {
            struct frame native;frame_init(&native, 40, 9);native.cv.rotation = rotation;
            d72x40_render(&native.cv, &st);
            check_native(&f, &native);
        }
        if (memcmp(f.buf, g->pixels, sizeof(g->pixels)) != 0) {
            fprintf(stderr, "approved portrait frame %zu differs\n", i);
            failures++;
        }
        /* Migrated from the retired private-font-mode fixture. */
        st.profile = UINT8_MAX;
        st.peer_charging = true;
        d72x40_render(&f.cv, &st);
        for (uint16_t rotation = 90; rotation <= 270; rotation = (uint16_t)(rotation + 180)) {
            struct frame native; frame_init(&native, 40, 9); native.cv.rotation = rotation;
            d72x40_render(&native.cv, &st);
            check_native(&f, &native);
        }
    }
}

static void test_projection_coordinates(void) {
    /* Independent coordinates check every bit, including dense frames. */
    struct frame canvas, panel;
    frame_init(&canvas, 40, 9);
    frame_init(&panel, 72, 5);
    uint32_t random = 0x1248abcd;
    for (unsigned run = 0; run < 100; run++) {
        for (size_t i = 0; i < 360; i++) {
            random = random * 1664525u + 1013904223u;
            canvas.buf[i] = (uint8_t)(random >> 24);
        }
        const uint16_t rotations[2] = {90, 270};
        for (unsigned turn = 0; turn < 2; turn++) {
            d72x40_project(&canvas.cv, panel.buf, 72, 5, rotations[turn]);
            for (int y = 0; y < 72; y++) {
                for (int x = 0; x < 40; x++) {
                    int px = turn == 0 ? y : 71 - y;
                    int py = turn == 0 ? 39 - x : x;
                    CHECK(pixel(&canvas.cv, x, y) == pixel(&panel.cv, px, py));
                }
            }
        }
    }

}

static void test_status_font_boundaries(void) {
    struct frame f;
    frame_init(&f, 40, 4);
    CHECK(d72x40_status_glyph(0) == d72x40_status_glyph('?'));
    CHECK(d72x40_status_glyph(255) == d72x40_status_glyph('?'));
    CHECK(d72x40_status_width("AB", 0) == 0);
    CHECK(d72x40_status_width("AB", 1) == 6);
    d72x40_mask(&f.cv, -1, 0, 0xff, false);
    d72x40_mask(&f.cv, 40, 0, 0xff, false);
    d72x40_mask(&f.cv, 0, -1, 0xff, false);
    d72x40_mask(&f.cv, 0, 4, 0xff, false);
    d72x40_status_text(&f.cv, 0, -1, "A", 1, false);
    d72x40_status_text(&f.cv, 0, 32, "A", 1, false);
    d72x40_status_lock(&f.cv, 0, -1, true);
    d72x40_status_lock(&f.cv, 0, 32, false);
    for (size_t i = 0; i < sizeof(f.buf); i++) {
        CHECK(f.buf[i] == 0);
    }
    /* A descender at row seven crosses a third page, in both ink modes. */
    d72x40_status_text(&f.cv, 0, 7, "gA", 1, false);
    CHECK(pixel(&f.cv, 1, 16));
    CHECK(!pixel(&f.cv, 0, 16));
    memset(f.buf, 0xff, 160);
    d72x40_status_text(&f.cv, 0, 7, "g", 2, true);
    CHECK(!pixel(&f.cv, 1, 16));
    CHECK(pixel(&f.cv, 0, 16));
    d72x40_status_text(&f.cv, -3, 31, "AB", 2, false);
    d72x40_status_centred(&f.cv, 0, "", false);

    /* Nonmatching shapes still use the bounded coordinate implementation. */
    struct frame panel;
    frame_init(&panel, 72, 5);
    for (unsigned shape = 0; shape < 2; shape++) {
        frame_init(&f, shape == 0 ? 41 : 40, shape == 0 ? 9 : 8);
        memset(f.buf, 0x96, (size_t)f.cv.width * f.cv.pages);
        for (unsigned turn = 0; turn < 2; turn++) {
            d72x40_project(&f.cv, panel.buf, 72, 5, turn == 0 ? 90 : 270);
            for (int y = 0; y < 40; y++) {
                for (int x = 0; x < 72; x++) {
                    int cx = turn == 0 ? (int)f.cv.width - 1 - y : y;
                    int cy = turn == 0 ? x : (int)f.cv.pages * 8 - 1 - x;
                    bool expected = cx >= 0 && cx < (int)f.cv.width && cy >= 0 &&
                                    cy < (int)f.cv.pages * 8 && pixel(&f.cv, cx, cy);
                    CHECK(pixel(&panel.cv, x, y) == expected);
                }
            }
        }
    }
}

static void test_render_is_a_pure_function(void) {
    struct frame a, b;
    frame_init(&a, 72, 5);
    frame_init(&b, 72, 5);
    memset(b.buf, 0xa5, 72 * 5);
    struct d72x40_status st = split_status();
    st.unlocked = true;
    st.caps_lock = true;
    d72x40_render(&a.cv, &st);
    d72x40_render(&b.cv, &st);
    CHECK(memcmp(a.buf, b.buf, sizeof(a.buf)) == 0);
}

struct span_list {
    struct d72x40_span spans[64];
    size_t count;
};

static void collect(const uint8_t *want, const uint8_t *shown, uint16_t width, uint16_t pages,
                    uint32_t valid, struct span_list *out) {
    uint16_t page = 0;
    uint16_t x = 0;
    out->count = 0;
    while (out->count < 64 &&
           d72x40_next_span(want, shown, width, pages, valid, &page, &x, &out->spans[out->count])) {
        out->count++;
    }
}

static bool span_is(const struct d72x40_span *s, uint16_t page, uint16_t x0, uint16_t x1) {
    return s->page == page && s->x0 == x0 && s->x1 == x1;
}

static void test_spans(void) {
    uint8_t want[72 * 5] = {0};
    uint8_t shown[72 * 5] = {0};
    struct span_list list;

    collect(want, shown, 72, 5, 0x1f, &list);
    CHECK(list.count == 0);

    /* Unknown pages go whole, in order. */
    collect(want, shown, 72, 5, 0x00, &list);
    CHECK(list.count == 5);
    for (uint16_t p = 0; p < 5 && p < list.count; p++) {
        CHECK(span_is(&list.spans[p], p, 0, 71));
    }

    want[2 * 72 + 10] = 1;
    collect(want, shown, 72, 5, 0x1f, &list);
    CHECK(list.count == 1);
    CHECK(span_is(&list.spans[0], 2, 10, 10));

    /* Twelve unchanged columns between two changes: one write. */
    want[2 * 72 + 23] = 1;
    collect(want, shown, 72, 5, 0x1f, &list);
    CHECK(list.count == 1);
    CHECK(span_is(&list.spans[0], 2, 10, 23));

    /* Thirteen: two. */
    want[2 * 72 + 23] = 0;
    want[2 * 72 + 24] = 1;
    collect(want, shown, 72, 5, 0x1f, &list);
    CHECK(list.count == 2);
    CHECK(span_is(&list.spans[0], 2, 10, 10));
    CHECK(span_is(&list.spans[1], 2, 24, 24));

    /* Runs reach the page edges, and pages come in order. */
    want[0] = 1;
    want[4 * 72 + 71] = 1;
    collect(want, shown, 72, 5, 0x1f, &list);
    CHECK(list.count == 4);
    CHECK(span_is(&list.spans[0], 0, 0, 0));
    CHECK(span_is(&list.spans[3], 4, 71, 71));

    /* An unknown page is one write however little changed in it. */
    collect(want, shown, 72, 5, 0x1f & ~(1u << 2), &list);
    CHECK(list.count == 3);
    CHECK(span_is(&list.spans[1], 2, 0, 71));
}

/* What the firmware does with the spans, counting the bytes it would send. */
static size_t flush(const uint8_t *want, uint8_t *shown, uint16_t width, uint16_t pages,
                    uint32_t *valid) {
    struct d72x40_span s;
    uint16_t page = 0;
    uint16_t x = 0;
    size_t bytes = 0;
    while (d72x40_next_span(want, shown, width, pages, *valid, &page, &x, &s)) {
        size_t len = (size_t)(s.x1 - s.x0 + 1);
        memcpy(&shown[(size_t)s.page * width + s.x0], &want[(size_t)s.page * width + s.x0], len);
        if (len == width) {
            *valid |= (uint32_t)(1ul << s.page);
        }
        bytes += len;
    }
    return bytes;
}

static void test_redraw_costs(void) {
    struct frame want, shown;
    frame_init(&want, 72, 5);
    frame_init(&shown, 72, 5);
    uint32_t valid = 0;
    struct d72x40_status st = split_status();

    d72x40_render(&want.cv, &st);
    CHECK(flush(want.buf, shown.buf, 72, 5, &valid) == 72u * 5u);
    CHECK(valid == 0x1f);
    CHECK(memcmp(want.buf, shown.buf, 72 * 5) == 0);

    /* Nothing changed: nothing sent. */
    d72x40_render(&want.cv, &st);
    CHECK(flush(want.buf, shown.buf, 72, 5, &valid) == 0);

    /* One battery digit: a glyph's worth of columns. */
    st.peer_battery = 71;
    d72x40_render(&want.cv, &st);
    size_t digit = flush(want.buf, shown.buf, 72, 5, &valid);
    CHECK(digit > 0 && digit <= (size_t)D72X40_FONT_WIDTH);
    CHECK(memcmp(want.buf, shown.buf, 72 * 5) == 0);

    /* A layer change stays within the two layer pages. */
    struct frame before;
    frame_init(&before, 72, 5);
    memcpy(before.buf, shown.buf, sizeof(before.buf));
    strcpy(st.layer, "NUM");
    d72x40_render(&want.cv, &st);
    size_t layer = flush(want.buf, shown.buf, 72, 5, &valid);
    CHECK(layer > 0 && layer <= 2u * 72u);
    CHECK(memcmp(&before.buf[2 * 72], &shown.buf[2 * 72], 3 * 72) == 0);
}

static void test_led_splash(void) {
    struct frame portrait;
    frame_init(&portrait, 40, 9);
    for (uint8_t pattern = 0; pattern < 18; pattern++) {
        CHECK(strlen(d72x40_led_pattern_name(pattern)) <= 6);
        d72x40_render_led_splash(&portrait.cv, pattern, 100, 400);
        int first;
        int last;
        CHECK(lit_extent(&portrait.cv, 0, 7, &first, &last));
        CHECK(lit_extent(&portrait.cv, 16, 31, &first, &last));
        CHECK(lit_extent(&portrait.cv, 40, 47, &first, &last));
        CHECK(lit_extent(&portrait.cv, 56, 63, &first, &last));
        CHECK(last < 40);
    }
    CHECK(strcmp(d72x40_led_pattern_name(255), "?") == 0);

    uint8_t before[MAX_WIDTH * MAX_PAGES];
    memcpy(before, portrait.buf, sizeof(before));
    d72x40_render_led_splash(&portrait.cv, 17, 0, 10);
    CHECK(memcmp(before, portrait.buf, sizeof(before)) != 0);

    struct frame landscape;
    frame_init(&landscape, 72, 5);
    d72x40_render_led_splash(&landscape.cv, 12, 80, 120);
    for (int page = 0; page < 5; page++) {
        int first;
        int last;
        CHECK(lit_extent(&landscape.cv, page * 8, page * 8 + 7, &first, &last));
        CHECK(last < 72);
    }
}

static void test_small_canvas_and_offscreen_guards(void) {
    struct frame f;
    /* Invalid top coordinates drop the whole glyph, in every storage layout. */
    const uint16_t rotations[] = {0, 90, 270};
    const int invalid_rows[] = {-1, INT_MIN, 72};
    const char *const glyphs[] = {"A", "\x80", D72X40_SYM_CHECK};
    uint8_t untouched[sizeof(f.buf)];
    for (size_t r = 0; r < sizeof(rotations) / sizeof(rotations[0]); r++) {
        for (size_t y = 0; y < sizeof(invalid_rows) / sizeof(invalid_rows[0]); y++) {
            for (size_t g = 0; g < sizeof(glyphs) / sizeof(glyphs[0]); g++) {
                frame_init(&f, 40, 9);
                f.cv.rotation = rotations[r];
                memset(f.buf, 0x5a, sizeof(f.buf));
                memcpy(untouched, f.buf, sizeof(untouched));
                d72x40_text_row(&f.cv, 0, invalid_rows[y], glyphs[g], 1);
                CHECK(memcmp(f.buf, untouched, sizeof(f.buf)) == 0);
            }
        }
    }


    /* Public text coordinates may be anywhere in int; off-right glyphs cannot
     * re-enter the canvas. Only an unrepresentable next-x saturates. */
    for (size_t r = 0; r < sizeof(rotations) / sizeof(rotations[0]); r++) {
        frame_init(&f, 40, 9);
        f.cv.rotation = rotations[r];
        memset(f.buf, 0x5a, sizeof(f.buf));
        memcpy(untouched, f.buf, sizeof(untouched));
        CHECK(d72x40_text_n(&f.cv, INT_MAX, 0, "A", 1) == INT_MAX);
        CHECK(d72x40_text_n(&f.cv, INT_MAX - 6, 0, "A", 1) == INT_MAX);
        CHECK(d72x40_text_n(&f.cv, INT_MAX - 5, 0, "A", 1) == INT_MAX);
        CHECK(d72x40_text_n(&f.cv, 40, 0, "AB", 2) == 52);
        CHECK(d72x40_text_n(&f.cv, 100, 0, "AB", 2) == 112);
        CHECK(d72x40_text_n(&f.cv, INT_MIN, 0, "AB", 2) == INT_MIN + 12);
        const int offscreen_x[] = {INT_MIN, 40, INT_MAX};
        for (size_t x = 0; x < sizeof(offscreen_x) / sizeof(offscreen_x[0]); x++) {
            for (size_t g = 0; g < sizeof(glyphs) / sizeof(glyphs[0]); g++) {
                d72x40_text_row(&f.cv, offscreen_x[x], 0, glyphs[g], 1);
                d72x40_text_tall(&f.cv, offscreen_x[x], 0, glyphs[g], 1, false);
                d72x40_text_tall(&f.cv, offscreen_x[x], 0, glyphs[g], 1, true);
                d72x40_status_text(&f.cv, offscreen_x[x], 0, glyphs[g], 1, false);
                d72x40_status_text(&f.cv, offscreen_x[x], 0, glyphs[g], 1, true);
            }
        }
        d72x40_text_tall(&f.cv, 0, INT_MAX, "A", 1, false);
        CHECK(memcmp(f.buf, untouched, sizeof(f.buf)) == 0);

        struct frame expected;
        frame_init(&f, 40, 9);
        frame_init(&expected, 40, 9);
        f.cv.rotation = expected.cv.rotation = rotations[r];
        CHECK(d72x40_text_n(&f.cv, -6, 0, "BA", 2) == 6);
        d72x40_text_n(&expected.cv, 0, 0, "A", 1);
        CHECK(memcmp(f.buf, expected.buf, sizeof(f.buf)) == 0);
        d72x40_clear(&f.cv);
        d72x40_clear(&expected.cv);
        d72x40_text_tall(&f.cv, -12, 0, "BA", 2, true);
        d72x40_text_tall(&expected.cv, 0, 0, "A", 1, true);
        CHECK(memcmp(f.buf, expected.buf, sizeof(f.buf)) == 0);
        d72x40_clear(&f.cv);
        d72x40_clear(&expected.cv);
        d72x40_text_row(&f.cv, -6, 1, "BA", 2);
        d72x40_text_row(&expected.cv, 0, 1, "A", 1);
        CHECK(memcmp(f.buf, expected.buf, sizeof(f.buf)) == 0);
    }

    /* Width arithmetic stays exact through the last representable result. */
    const size_t narrow_limit = ((size_t)INT_MAX + 1u) / 6u;
    const size_t wide_limit = ((size_t)INT_MAX + 2u) / 12u;
    CHECK(d72x40_text_width(narrow_limit, D72X40_CELL) == (int)(narrow_limit * 6u - 1u));
    CHECK(d72x40_text_width(narrow_limit + 1u, D72X40_CELL) == INT_MAX);
    CHECK(d72x40_text_width(wide_limit, D72X40_WIDE_CELL) == (int)(wide_limit * 12u - 2u));
    CHECK(d72x40_text_width(wide_limit + 1u, D72X40_WIDE_CELL) == INT_MAX);
    CHECK(d72x40_text_width(SIZE_MAX, D72X40_CELL) == INT_MAX);
    CHECK(d72x40_text_width(SIZE_MAX, D72X40_WIDE_CELL) == INT_MAX);

    frame_init(&f, 24, 2);
    d72x40_plot(&f.cv, -1, 0, 0xff);
    d72x40_plot(&f.cv, 24, 0, 0xff);
    d72x40_plot(&f.cv, 0, -1, 0xff);
    d72x40_plot(&f.cv, 0, 2, 0xff);
    CHECK(f.buf[0] == 0);

    d72x40_render_studio_lock(&f.cv, 0, false, 0);
    CHECK(f.buf[23] == 0);
    d72x40_render_studio_lock(&f.cv, 0, true, 0);
    CHECK(f.buf[23] != 0);

    CHECK(!d72x40_canvas_pixel(&f.cv, -1, 0));
    CHECK(!d72x40_canvas_pixel(&f.cv, 24, 0));
    CHECK(!d72x40_canvas_pixel(&f.cv, 0, -1));
    CHECK(!d72x40_canvas_pixel(&f.cv, 0, 16));

    struct d72x40_status st = split_status();
    st.battery = D72X40_BATTERY_HIDDEN;
    struct d72x40_battery_text battery;
    d72x40_battery_text(&f.cv, &st, &battery);
    CHECK(!battery.stacked);
    CHECK(strcmp(battery.right, "R72%") == 0);

    d72x40_text_n(&f.cv, 0, 0, "AB", 1);
    d72x40_text_tall(&f.cv, 0, 0, "AB", 1, false);
    d72x40_text_tall(&f.cv, 0, 0, "A", 2, false);
    d72x40_text_row(&f.cv, 0, 1, "AB", 1);
    d72x40_text_row(&f.cv, 0, 1, "A", 2);
    char clipped[2] = "";
    d72x40_append(clipped, sizeof(clipped), "AB");
    CHECK(strcmp(clipped, "A") == 0);
    CHECK(d72x40_wrap_at("A-123456789", 11, 4) == 4);

    frame_init(&f, 52, 2);
    d72x40_render_studio_lock(&f.cv, 0, false, 0);
    frame_init(&f, 72, 2);
    d72x40_render_studio_lock(&f.cv, 0, false, 72);
    st.transport = D72X40_TRANSPORT_USB;
    st.link = D72X40_LINK_DOWN;
    d72x40_render_output(&f.cv, 0, &st);
    st.link = D72X40_LINK_UP;
    d72x40_render_output(&f.cv, 0, &st);
    frame_init(&f, 40, 2);
    st.link = D72X40_LINK_DOWN;
    st.studio_lock_available = true;
    d72x40_render_output(&f.cv, 0, &st);
    st.studio_lock_available = false;
    d72x40_render_output(&f.cv, 0, &st);

    frame_init(&f, 24, 1);
    st = split_status();
    CHECK(d72x40_render_battery(&f.cv, 0, &st) == 2);

    frame_init(&f, 40, 5);
    d72x40_render_led_splash(&f.cv, 1, 100, 100);
}

/* True when columns x0..x1-1 of page are what text at text_x (or nothing) draws there. */
static bool columns_are(const struct d72x40_canvas *cv, int page, int x0, int x1, int text_x,
                        const char *text) {
    struct frame expect;
    frame_init(&expect, cv->width, cv->pages);
    if (text != NULL) {
        d72x40_text(&expect.cv, text_x, page, text);
    }
    return memcmp(&cv->buf[(size_t)page * cv->width + (size_t)x0],
                  &expect.buf[(size_t)page * cv->width + (size_t)x0], (size_t)(x1 - x0)) == 0;
}

static bool battery_is(uint16_t width, uint8_t own, uint8_t peer, const char *left,
                       const char *right, bool stacked) {
    struct frame f;
    struct d72x40_battery_text text;
    struct d72x40_status st = {.battery = own, .peer_battery = peer};

    frame_init(&f, width, 5);
    d72x40_battery_text(&f.cv, &st, &text);
    return strcmp(text.left, left) == 0 && strcmp(text.right, right) == 0 &&
           text.stacked == stacked && !text.empty;
}

/*
 * Every limit exactly: the first value past a bound and the last one inside
 * it, found missing by mutating each comparison of the core.
 */
static void test_exact_limits(void) {
    /* Numbers, all of their digits. */
    char text[8] = "S";
    d72x40_append_u16(text, sizeof(text), 150);
    CHECK(strcmp(text, "S150") == 0);
    text[0] = '\0';
    d72x40_append_u16(text, sizeof(text), 0);
    CHECK(strcmp(text, "0") == 0);
    text[0] = '\0';
    d72x40_append_u16(text, sizeof(text), 65535);
    CHECK(strcmp(text, "65535") == 0);

    /* The last character of the font is its own glyph, not the fallback. */
    CHECK(d72x40_glyph(D72X40_FONT_ASCII_LAST) ==
          d72x40_font_ascii[D72X40_FONT_ASCII_LAST - D72X40_FONT_ASCII_FIRST]);
    /* A font column has seven rows; an eighth bit is not one of them. */
    CHECK(d72x40_tall_column(0xff) == 0x7ffe);

    /* Plotting and reading stop exactly at the canvas edges: the byte after a
     * page's last column is the next page's first, and after the last page
     * comes whatever follows the buffer. */
    uint8_t exact[8 * 2 + 8];
    memset(exact, 0, sizeof(exact));
    struct d72x40_canvas small = {.buf = exact, .width = 8, .pages = 2};
    d72x40_plot(&small, 0, 0, 0x01);
    d72x40_plot(&small, 8, 0, 0xff);
    d72x40_plot(&small, 0, 2, 0xff);
    CHECK(exact[0] == 0x01 && exact[8] == 0 && exact[16] == 0);
    memset(exact, 0xff, sizeof(exact));
    memset(exact, 0, 8);
    CHECK(!d72x40_canvas_pixel(&small, 8, 0) && !d72x40_canvas_pixel(&small, 0, 16));
    CHECK(d72x40_canvas_pixel(&small, 7, 15));

    /* A turned copy fills the frame and not a byte past it. */
    struct frame src;
    frame_init(&src, 16, 2);
    memset(src.buf, 0x5a, 32);
    for (uint16_t rotation = 90; rotation <= 270; rotation += 90) {
        uint8_t turned[16 * 2 + 8];
        memset(turned, 0xaa, sizeof(turned));
        d72x40_project(&src.cv, turned, 16, 2, rotation);
        for (size_t i = 32; i < sizeof(turned); i++) {
            CHECK(turned[i] == 0xaa);
        }
    }

    /* At most max glyphs, in either size. */
    struct frame f;
    frame_init(&f, 72, 2);
    CHECK(d72x40_text_n(&f.cv, 0, 0, "AB", 1) == D72X40_CELL);
    CHECK(columns_are(&f.cv, 0, 0, 72, 0, "A"));
    frame_init(&f, 72, 2);
    d72x40_text_tall(&f.cv, 0, 0, "AB", 1, false);
    for (int y = 0; y < 16; y++) {
        for (int x = D72X40_CELL; x < 72; x++) {
            CHECK(!pixel(&f.cv, x, y));
        }
    }

    /* Two pages are enough for the large layer name. */
    frame_init(&f, 72, 2);
    CHECK(d72x40_render_layer(&f.cv, "NAV") == 2);
    /* A name exactly one narrow line long stays one line, centred in the
     * block rather than wrapped from its top. */
    frame_init(&f, 72, 5);
    CHECK(d72x40_render_layer(&f.cv, "ABCDEFGHIJKL") == 2);
    CHECK(row_blank(&f.cv, 0) && !row_blank(&f.cv, 4));

    /* The Studio word: from 54 columns, and only clear of the host text. */
    frame_init(&f, 54, 2);
    d72x40_render_studio_lock(&f.cv, 0, true, 0);
    CHECK(columns_are(&f.cv, 0, 15, 39, 15, "OPEN"));
    frame_init(&f, 54, 2);
    d72x40_render_studio_lock(&f.cv, 0, true, 15);
    CHECK(columns_are(&f.cv, 0, 0, 39, 0, NULL));
    frame_init(&f, 40, 2);
    d72x40_render_studio_lock(&f.cv, 0, true, 0);
    CHECK(columns_are(&f.cv, 0, 0, 25, 0, NULL));

    /* Only the 40-column panel with a Studio mark drops the space before the
     * USB cross. */
    struct d72x40_status st = {.transport = D72X40_TRANSPORT_USB, .link = D72X40_LINK_DOWN,
                               .studio_lock_available = true};
    frame_init(&f, 72, 5);
    d72x40_render_output(&f.cv, 0, &st);
    CHECK(columns_are(&f.cv, 0, 0, 30, 0, "USB " D72X40_SYM_CROSS));
    st.studio_lock_available = false;
    frame_init(&f, 40, 9);
    d72x40_render_output(&f.cv, 0, &st);
    CHECK(columns_are(&f.cv, 0, 0, 30, 0, "USB " D72X40_SYM_CROSS));

    /* Battery text at widths where it fits exactly (n glyphs are 6n - 1
     * columns), and one space narrower at a time. */
    struct d72x40_status none = {.battery = D72X40_BATTERY_HIDDEN,
                                 .peer_battery = D72X40_BATTERY_HIDDEN};
    struct d72x40_battery_text bt;
    frame_init(&f, 72, 5);
    d72x40_battery_text(&f.cv, &none, &bt);
    CHECK(bt.empty && d72x40_battery_pages(&f.cv, &none) == 0);
    CHECK(battery_is(41, 87, D72X40_BATTERY_HIDDEN, "BAT 87%", "", false));
    CHECK(battery_is(29, 87, D72X40_BATTERY_HIDDEN, "B 87%", "", false));
    CHECK(battery_is(65, 87, 42, "L 87%", "R 42%", false));
    CHECK(battery_is(59, 87, 42, "L87%", "R 42%", false));
    CHECK(battery_is(53, 87, 42, "L87%", "R42%", false));
    CHECK(battery_is(29, 87, 42, "L 87%", "R 42%", true));
    CHECK(battery_is(29, D72X40_BATTERY_HIDDEN, 42, "", "R 42%", false));

    /* Side by side the batteries take one page. */
    struct d72x40_status split = {.battery = 87, .peer_battery = 42};
    frame_init(&f, 72, 5);
    CHECK(d72x40_render_battery(&f.cv, 2, &split) == 3);

    /* The first number past the pattern names has none. */
    CHECK(strcmp(d72x40_led_pattern_name(18), "?") == 0);

    /* The splash stands up only on a narrow, tall canvas. */
    static const struct {
        uint16_t width, pages;
        bool landscape;
    } splash[] = {{66, 9, true}, {72, 9, true}, {40, 7, false}, {40, 6, true}};
    for (size_t i = 0; i < sizeof(splash) / sizeof(splash[0]); i++) {
        frame_init(&f, splash[i].width, splash[i].pages);
        d72x40_render_led_splash(&f.cv, 5, 60, 150);
        CHECK(pixel(&f.cv, 0, 8)); /* normal text or the new inverted portrait header */
        CHECK(pixel(&f.cv, 0, 0) != splash[i].landscape);
    }

    /* A run that starts in the first column still takes its neighbours. */
    uint8_t want[8] = {1, 1};
    uint8_t shown[8] = {0};
    uint16_t span_page = 0;
    uint16_t span_x = 0;
    struct d72x40_span span;
    CHECK(d72x40_next_span(want, shown, 8, 1, 1, &span_page, &span_x, &span));
    CHECK(span.x0 == 0 && span.x1 == 1);
}

/* Centred as d72x40_text_centred() places n glyphs. */
static int centred_x(const struct d72x40_canvas *cv, const char *s) {
    return ((int)cv->width - d72x40_text_width(strlen(s), D72X40_CELL)) / 2;
}

/*
 * What the extended mutation pass (deleted statements, swapped arithmetic,
 * 0 <-> 1) found no test looking at.
 */
static void test_exact_content(void) {
    /* The LED splash says what it shows: the number, the name, the level and
     * the rate, each where it belongs, on a canvas cleared first. */
    struct frame f;
    frame_init(&f, 72, 5);
    memset(f.buf, 0xff, sizeof(f.buf));
    d72x40_render_led_splash(&f.cv, 5, 60, 150);
    CHECK(page_is(&f.cv, 0, centred_x(&f.cv, "LED"), "LED", 0, NULL));
    CHECK(page_is(&f.cv, 1, 0, "P5", 0, NULL));
    CHECK(page_is(&f.cv, 2, 0, "TRIPLE", 0, NULL));
    CHECK(page_is(&f.cv, 3, 0, "B60%", 0, NULL));
    CHECK(page_is(&f.cv, 4, 0, "S150%", 0, NULL));
    frame_init(&f, 40, 9);
    memset(f.buf, 0xff, sizeof(f.buf));
    d72x40_render_led_splash(&f.cv, 5, 60, 150);
    CHECK(pixel(&f.cv, 0, 0) && pixel(&f.cv, 39, 12));
    CHECK(row_blank(&f.cv, 13) && row_blank(&f.cv, 15));
    CHECK(row_text_is(&f.cv, 16, 14, "P5"));
    CHECK(row_text_is(&f.cv, 27, 4, "TRIPLE"));
    CHECK(row_blank(&f.cv, 37));
    CHECK(row_text_is(&f.cv, 40, 8, "B60%"));
    CHECK(row_blank(&f.cv, 50));
    CHECK(row_text_is(&f.cv, 56, 7, "S150%"));
    CHECK(row_blank(&f.cv, 66));

    /* Double height: every font row becomes two rows, one row down; double
     * width repeats each column. */
    const uint8_t *a = d72x40_glyph('A');
    for (int wide = 0; wide <= 1; wide++) {
        int scale = wide ? 2 : 1;
        frame_init(&f, 72, 2);
        d72x40_text_tall(&f.cv, 3, 0, "A", 1, wide);
        for (int x = 0; x < 72; x++) {
            int col = (x - 3) / scale;
            uint16_t tall = x >= 3 && x < 3 + D72X40_FONT_WIDTH * scale
                                ? d72x40_tall_column(a[col]) : 0;
            CHECK(f.buf[x] == (uint8_t)(tall & 0xff));
            CHECK(f.buf[72 + x] == (uint8_t)(tall >> 8));
        }
    }

    /* A line whose top is one row below a page boundary spills one row into
     * the next page. */
    frame_init(&f, 72, 2);
    d72x40_text_row(&f.cv, 0, 1, "A", 1);
    for (int col = 0; col < D72X40_FONT_WIDTH; col++) {
        uint16_t shifted = (uint16_t)(a[col] << 1);
        CHECK(f.buf[col] == (uint8_t)(shifted & 0xff));
        CHECK(f.buf[72 + col] == (uint8_t)(shifted >> 8));
    }

    /* A hyphen in first place is still a place to break. */
    CHECK(d72x40_wrap_at("-ABCDEFGHIJKL", 13, 12) == 1);

    /* The battery text starts from nothing, whatever the caller left in it. */
    struct d72x40_battery_text bt;
    struct d72x40_status none = {.battery = D72X40_BATTERY_HIDDEN,
                                 .peer_battery = D72X40_BATTERY_HIDDEN};
    memset(&bt, 0x55, sizeof(bt));
    frame_init(&f, 72, 5);
    d72x40_battery_text(&f.cv, &none, &bt);
    CHECK(bt.left[0] == '\0' && bt.right[0] == '\0' && !bt.stacked && bt.empty);

    /* The bolt counts toward the fit: at 6n - 1 columns exactly. */
    struct d72x40_status charging = {.battery = 87, .peer_battery = D72X40_BATTERY_HIDDEN,
                                     .charging = true};
    frame_init(&f, 47, 5);
    d72x40_battery_text(&f.cv, &charging, &bt);
    CHECK(strcmp(bt.left, "BAT 87%" D72X40_SYM_BOLT) == 0);
    frame_init(&f, 41, 5);
    d72x40_battery_text(&f.cv, &charging, &bt);
    CHECK(strcmp(bt.left, "B 87%" D72X40_SYM_BOLT) == 0);
    charging.peer_battery = 42;
    frame_init(&f, 71, 5);
    d72x40_battery_text(&f.cv, &charging, &bt);
    CHECK(strcmp(bt.left, "L 87%" D72X40_SYM_BOLT) == 0 && strcmp(bt.right, "R 42%") == 0 &&
          !bt.stacked);
    frame_init(&f, 65, 5);
    d72x40_battery_text(&f.cv, &charging, &bt);
    CHECK(strcmp(bt.left, "L87%" D72X40_SYM_BOLT) == 0 && strcmp(bt.right, "R 42%") == 0);

    /* How many pages each arrangement takes, and where drawing goes on. */
    struct d72x40_status split = {.battery = 87, .peer_battery = 42};
    struct d72x40_status unibody = {.battery = 87, .peer_battery = D72X40_BATTERY_HIDDEN};
    frame_init(&f, 72, 5);
    CHECK(d72x40_battery_pages(&f.cv, &split) == 1);
    CHECK(d72x40_render_battery(&f.cv, 1, &unibody) == 2);
    frame_init(&f, 29, 5);
    CHECK(d72x40_battery_pages(&f.cv, &split) == 2);
    CHECK(d72x40_render_battery(&f.cv, 1, &split) == 3);

    /* A blank line between blocks only when three pages are to spare: seven
     * pages for five lines leaves two, so the lines stay packed. */
    struct d72x40_status packed = {.layer = "BASE", .transport = D72X40_TRANSPORT_USB,
                                   .link = D72X40_LINK_UP, .battery = 87,
                                   .peer_battery = D72X40_BATTERY_HIDDEN};
    frame_init(&f, 72, 7);
    d72x40_render(&f.cv, &packed);
    CHECK(page_is(&f.cv, 2, 0, "USB", 0, NULL));
    CHECK(page_is(&f.cv, 3, 0, "BAT 87%", 0, NULL));
    frame_init(&f, 72, 8);
    d72x40_render(&f.cv, &packed);
    CHECK(page_is(&f.cv, 3, 0, "USB", 0, NULL));
    CHECK(page_is(&f.cv, 5, 0, "BAT 87%", 0, NULL));

    /* Half a turn puts the top-left pixel at the bottom right. */
    frame_init(&f, 16, 2);
    d72x40_plot(&f.cv, 0, 0, 0x01);
    uint8_t turned[16 * 2];
    d72x40_project(&f.cv, turned, 16, 2, 180);
    for (size_t i = 0; i < sizeof(turned); i++) {
        CHECK(turned[i] == (i == 16 + 15 ? 0x80 : 0));
    }
}

int main(void) {
    test_font();
    test_text_width();
    test_tall_column();
    test_layer_wide_and_centred();
    test_layer_narrow_when_wide_does_not_fit();
    test_layer_on_other_panels();
    test_portrait_layout();
    test_portrait_studio_lock();
    test_portrait_boundaries();
    test_approved_portrait_frames();
    test_direct_primitives();
    test_bitmap_edges();
    test_projection_coordinates();
    test_status_font_boundaries();
    test_rotation();
    test_output_line();
    test_battery_line();
    test_indicator_line();
    test_render_is_a_pure_function();
    test_spans();
    test_redraw_costs();
    test_led_splash();
    test_small_canvas_and_offscreen_guards();
    test_exact_limits();
    test_exact_content();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    printf("display_72x40_core: all checks passed\n");

    return EXIT_SUCCESS;
}
