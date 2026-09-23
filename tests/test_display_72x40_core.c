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

#include <zmk-display-72x40/display_72x40_core.h>

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
    CHECK(page_is(&f.cv, 2, 0, D72X40_SYM_BLUETOOTH "3 " D72X40_SYM_OPEN, 0, NULL));

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
static void test_portrait_layout(void) {
    struct frame f;
    frame_init(&f, 40, 9);
    struct d72x40_status st = split_status();

    st.caps_lock = true;
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "portrait 40x72");

    int first, last;

    /* Four narrow-tall glyphs are 23 columns, centred in 40. */
    CHECK(lit_extent(&f.cv, 0, 15, &first, &last));
    CHECK(first == 8);
    CHECK(last == 30);
    /* Nine pages for six lines: a blank line between the groups. */
    CHECK(page_is(&f.cv, 2, 0, NULL, 0, NULL));
    CHECK(page_is(&f.cv, 3, 0, D72X40_SYM_BLUETOOTH "1 " D72X40_SYM_CHECK, 0, NULL));
    CHECK(page_is(&f.cv, 4, 0, NULL, 0, NULL));
    /* Too narrow for both halves side by side: one line each. */
    CHECK(page_is(&f.cv, 5, 0, "L 85%", 0, NULL));
    CHECK(page_is(&f.cv, 6, 0, "R 72%", 0, NULL));
    CHECK(page_is(&f.cv, 7, 0, NULL, 0, NULL));
    CHECK(page_is(&f.cv, 8, 0, "CAPS", 0, NULL));

    /* At 100%, the bolt stays after the complete number and still fits. */
    st.battery = 100;
    st.peer_battery = 100;
    st.charging = true;
    st.peer_charging = true;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 5, 0, "L100%" D72X40_SYM_BOLT, 0, NULL));
    CHECK(page_is(&f.cv, 6, 0, "R100%" D72X40_SYM_BOLT, 0, NULL));

    st.peer_battery = D72X40_BATTERY_HIDDEN;
    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 5, 0, "B100%" D72X40_SYM_BOLT, 0, NULL));

    /* A name too long for 40 columns takes two lines. */
    strcpy(st.layer, "TEMP-SCROLL");
    d72x40_render(&f.cv, &st);
    dump(&f.cv, "portrait, wrapped");
    CHECK(page_is(&f.cv, 0, 5, "TEMP-", 0, NULL));
    CHECK(page_is(&f.cv, 1, 2, "SCROLL", 0, NULL));
}

static void test_portrait_studio_lock(void) {
    struct frame f;
    frame_init(&f, 40, 9);
    struct d72x40_status st = split_status();
    st.studio_lock_available = true;
    st.caps_lock = true;

    d72x40_render(&f.cv, &st);
    CHECK(page_is(&f.cv, 2, 0, NULL, 0, NULL));
    CHECK(pixel(&f.cv, 30, 25)); /* closed shackle, left side */
    CHECK(!pixel(&f.cv, 39, 25));
    CHECK(pixel(&f.cv, 28, 28)); /* body remains in place */
    CHECK(page_is(&f.cv, 5, 0, "L 85%", 0, NULL));
    CHECK(page_is(&f.cv, 6, 0, "R 72%", 0, NULL));
    CHECK(page_is(&f.cv, 8, 0, "CAPS", 0, NULL));

    st.unlocked = true;
    d72x40_render(&f.cv, &st);
    CHECK(!pixel(&f.cv, 30, 25));
    CHECK(pixel(&f.cv, 39, 25)); /* shackle swings right */
    CHECK(!pixel(&f.cv, 39, 27)); /* visible gap above body */
    CHECK(pixel(&f.cv, 28, 28));
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

int main(void) {
    test_font();
    test_text_width();
    test_tall_column();
    test_layer_wide_and_centred();
    test_layer_narrow_when_wide_does_not_fit();
    test_layer_on_other_panels();
    test_portrait_layout();
    test_portrait_studio_lock();
    test_rotation();
    test_output_line();
    test_battery_line();
    test_indicator_line();
    test_render_is_a_pure_function();
    test_spans();
    test_redraw_costs();
    test_led_splash();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    printf("display_72x40_core: all checks passed\n");

    return EXIT_SUCCESS;
}
