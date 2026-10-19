/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 * Fixed 72x40 panel: independent pixel expectations and pre-refactor frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <zmk-display-72x40/display_72x40_core.h>
#include "approved_portrait_frames.h"
#include "fixed_frame_hashes.h"

_Static_assert(D72X40_FRAME_BYTES == 360, "one physical frame");
_Static_assert(sizeof(struct d72x40_canvas) == sizeof(uint8_t *), "no orientation or size state");
_Static_assert(sizeof(d72x40_status_rows) + sizeof(d72x40_status_advance) == 1045, "one font format");
static int failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
struct frame { uint8_t before[16], buf[360], after[16]; struct d72x40_canvas cv; };
static void frame_init(struct frame *f) {
    memset(f, 0, sizeof(*f)); memset(f->before, 0xa5, sizeof(f->before)); memset(f->after, 0x5a, sizeof(f->after));
    f->cv = (struct d72x40_canvas){.buf = f->buf};
}
static void guarded(const struct frame *f) {
    for (size_t i=0; i<16; i++) { CHECK(f->before[i] == 0xa5); CHECK(f->after[i] == 0x5a); }
}
/* Fixed panel specification: logical (x,y) is controller (y,39-x). */
static bool pixel(const struct d72x40_canvas *cv, int x, int y) {
    unsigned py = (unsigned)(39-x);
    return (((unsigned)cv->buf[(py/8)*72+(unsigned)y] >> (py%8)) & 1u) != 0;
}
static void ink(uint8_t *out, int x, int y, bool set, bool erase) {
    if (x<0 || x>=40 || y<0 || y>=72 || !set) return;
    unsigned py=(unsigned)(39-x); size_t at=(py/8)*72+(unsigned)y; uint8_t mask=(uint8_t)(1u<<(py%8));
    out[at]=erase ? (uint8_t)(out[at] & (uint8_t)~mask) : (uint8_t)(out[at] | mask);
}
static uint64_t hash_frame(const uint8_t *frame) {
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<360;i++) h=(h^frame[i])*UINT64_C(1099511628211);
    return h;
}
static struct d72x40_status approved_status(const struct d72x40_approved_frame *g) {
    struct d72x40_status st={.profile=2,.ble_available=true,.usb_available=true,
        .ble_link=g->route==0?D72X40_LINK_UP:g->route==1?D72X40_LINK_OPEN:D72X40_LINK_DOWN,
        .usb_link=g->route==1?D72X40_LINK_UP:D72X40_LINK_OPEN,
        .battery=g->battery,.peer_battery=g->peer_battery,.studio_lock_available=true,.messenger_lock_available=true,
        .unlocked=g->studio,.messenger_unlocked=g->messenger,.caps_lock=g->caps,.charging=g->charging};
    strcpy(st.layer,g->layer); return st;
}
static void test_approved_frames(void) {
    struct frame f; size_t next=0;
    for(size_t i=0;i<24;i++) {
        frame_init(&f); struct d72x40_status st=approved_status(&d72x40_approved_frames[i]);
        d72x40_render(&f.cv,&st);
        for(int y=0;y<72;y++) for(int x=0;x<40;x++) {
            bool expected=(((unsigned)d72x40_approved_frames[i].pixels[(y/8)*40+x]>>(y%8))&1u)!=0;
            CHECK(pixel(&f.cv,x,y)==expected);
        }
        CHECK(hash_frame(f.buf)==fixed_frame_hashes[next++]); guarded(&f);
        st.profile=UINT8_MAX; st.peer_charging=true; d72x40_render(&f.cv,&st);
        CHECK(hash_frame(f.buf)==fixed_frame_hashes[next++]); guarded(&f);
    }
    for(unsigned id=0;id<=18;id++) {
        d72x40_render_led_splash(&f.cv,(uint8_t)id,UINT8_MAX,UINT16_MAX);
        CHECK(hash_frame(f.buf)==fixed_frame_hashes[next++]); guarded(&f);
    }
    CHECK(next==67);
    CHECK(strcmp(d72x40_led_pattern_name(255),"?")==0);
    for(unsigned id=0;id<18;id++) CHECK(strlen(d72x40_led_pattern_name((uint8_t)id))<=6);
    /* Regression control: every physical pixel is material to a full-frame comparison. */
    uint64_t before=hash_frame(f.buf); f.buf[359]^=1; CHECK(hash_frame(f.buf)!=before);
}
static void test_bitmap_edges(void) {
    const uint8_t rows[11]={0x81,0x42,0x24,0x18,0xff,0,0x81,0x5a,0xa5,0x7e,1};
    const unsigned heights[]={0,1,7,8,9,11}; struct frame f; uint8_t expected[360];
    for(int x=-9;x<=40;x++) for(int y=-1;y<=72;y++) for(size_t h=0;h<6;h++) for(int erase=0;erase<=1;erase++) {
        frame_init(&f); memset(f.buf,erase?0xff:0x24,360); memcpy(expected,f.buf,360);
        d72x40_native_bitmap(&f.cv,x,y,rows,heights[h],erase!=0);
        if(y>=0 && y<72) for(unsigned j=0;j<heights[h];j++) for(int bit=0;bit<8;bit++)
            ink(expected,x+bit,y+(int)j,((rows[j]>>(7-bit))&1u)!=0,erase!=0);
        CHECK(memcmp(expected,f.buf,360)==0); guarded(&f);
    }
    const int extreme[]={INT_MIN,INT_MAX};
    for(size_t i=0;i<2;i++) {
        frame_init(&f); d72x40_native_bitmap(&f.cv,extreme[i],0,rows,11,false);
        d72x40_native_bitmap(&f.cv,0,extreme[i],rows,11,false);
        CHECK(hash_frame(f.buf)==hash_frame((uint8_t[360]){0})); guarded(&f);
    }
}
static void test_status_font_and_locks(void) {
    struct frame f; uint8_t expected[360];
    for(unsigned code=0;code<=255;code++) {
        unsigned index=code>=32 && code<=126?code-32:(unsigned)'?'-32;
        CHECK(d72x40_status_glyph((unsigned char)code)==d72x40_status_rows[index]);
        if (code >= 32 && code <= 126) {
            unsigned any = 0;
            for (unsigned row = 0; row < 10; row++) any |= d72x40_status_rows[index][row];
            CHECK(code == ' ' ? any == 0 : any != 0);
        }
        if(code==0) continue;
        for(int x=-9;x<=40;x+=7) for(int y=-1;y<=72;y++) for(int erase=0;erase<=1;erase++) {
            frame_init(&f); memset(f.buf,erase?0xff:0,360); memcpy(expected,f.buf,360); char text[2]={(char)code,0};
            d72x40_status_text(&f.cv,x,y,text,1,erase!=0);
            if(y>=0 && y<72) for(int row=0;row<10;row++) for(int col=0;col<8;col++)
                ink(expected,x+col,y+row,((d72x40_status_rows[index][row]>>(7-col))&1u)!=0,erase!=0);
            CHECK(memcmp(expected,f.buf,360)==0); guarded(&f);
        }
    }
    static const uint16_t closed[11]={0x038,0x044,0x082,0x082,0x082,0x1ff,0x1ef,0x1c7,0x1ef,0x1ff,0x1ff};
    static const uint16_t opened[11]={0x0c0,0x120,0x110,0x008,0x008,0x1ff,0x1ef,0x1c7,0x1ef,0x1ff,0x1ff};
    for(int x=-10;x<=40;x++) for(int y=-1;y<=72;y++) for(int open=0;open<=1;open++) {
        frame_init(&f); memset(expected,0,360); d72x40_status_lock(&f.cv,x,y,open!=0);
        if(y>=0 && y<72) for(int row=0;row<11;row++) for(int col=0;col<9;col++)
            ink(expected,x+col,y+row,(((open?opened:closed)[row]>>col)&1u)!=0,false);
        CHECK(memcmp(expected,f.buf,360)==0); guarded(&f);
    }
    frame_init(&f); d72x40_status_lock(&f.cv,0,INT_MIN,false);
    d72x40_status_lock(&f.cv,0,INT_MAX,true);
    d72x40_status_lock(&f.cv,INT_MIN,0,false); d72x40_status_lock(&f.cv,INT_MAX,0,false); guarded(&f);
    CHECK(d72x40_text_advance(INT_MAX,6)==INT_MAX);
    CHECK(d72x40_text_advance(INT_MAX-6,6)==INT_MAX);
    CHECK(d72x40_text_advance(INT_MIN,6)==INT_MIN+6);
    CHECK(d72x40_status_width("",SIZE_MAX)==0); CHECK(d72x40_status_width("AB",0)==0);
    CHECK(d72x40_status_width("AB",1)==d72x40_status_advance['A'-32]);
    d72x40_status_text(&f.cv,40,0,"A",1,false); d72x40_status_text(&f.cv,0,0,"A",0,false);
    d72x40_status_text(&f.cv,0,0,"",1,false); d72x40_status_centred(&f.cv,0,"AAAAAAAAAAAAAAAAAAAAAA",false); guarded(&f);
}

/* Independent glyph pixels at the approved normal-value group anchors. */
static void check_status_line(const struct d72x40_canvas *cv, int row, int x0,
                              const char *text) {
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 40; x++) {
            bool expected = false;
            int at = x0;
            for (size_t i = 0; text[i] != 0; i++) {
                unsigned index = (unsigned char)text[i] - 32u;
                if (x >= at && x < at + 8) {
                    expected |= (((unsigned)d72x40_status_rows[index][y] >>
                                  (7 - (x - at))) & 1u) != 0;
                }
                at += d72x40_status_advance[index];
            }
            CHECK(pixel(cv, x, row + y) == expected);
        }
    }
}
static void test_normal_led_content(void) {
    struct frame f;
    frame_init(&f);
    memset(f.buf, 0xff, sizeof(f.buf));
    d72x40_render_led_splash(&f.cv, 5, 60, 150);
    CHECK(pixel(&f.cv, 0, 0) && pixel(&f.cv, 39, 12));
    check_status_line(&f.cv, 16, 14, "P5");
    check_status_line(&f.cv, 27, 4, "TRIPLE");
    check_status_line(&f.cv, 40, 8, "B60%");
    check_status_line(&f.cv, 56, 7, "S150%");
    const int blank_rows[] = {13, 15, 37, 50, 66};
    for (size_t i = 0; i < sizeof(blank_rows)/sizeof(blank_rows[0]); i++) {
        for (int x = 0; x < 40; x++) CHECK(!pixel(&f.cv, x, blank_rows[i]));
    }
    guarded(&f);
}

static void test_state_and_bounds(void) {
    struct frame a,b; frame_init(&a); frame_init(&b);
    struct d72x40_status st={.battery=D72X40_BATTERY_HIDDEN,.peer_battery=D72X40_BATTERY_HIDDEN};
    strcpy(st.layer,"BASE"); d72x40_render(&a.cv,&st); memset(b.buf,0xff,360); d72x40_render(&b.cv,&st);
    CHECK(memcmp(a.buf,b.buf,360)==0);
    for(int y=13;y<72;y++) for(int x=0;x<40;x++) CHECK(!pixel(&a.cv,x,y));
    st.caps_lock=true; d72x40_render(&b.cv,&st); CHECK(memcmp(a.buf,b.buf,360)!=0);
    d72x40_render(&a.cv,&st); CHECK(memcmp(a.buf,b.buf,360)==0);
    const uint8_t levels[]={0,100,101,UINT8_MAX,D72X40_BATTERY_UNKNOWN,D72X40_BATTERY_HIDDEN};
    for(size_t i=0;i<6;i++) {st.battery=levels[i];st.peer_battery=levels[5-i];st.charging=true;st.peer_charging=true;st.profile=i%2?99:100;st.ble_available=true;d72x40_render(&a.cv,&st);guarded(&a);}
    st.layer[0]=0; d72x40_render(&a.cv,&st); guarded(&a);
    for(int x=-1;x<=40;x++) for(int y=-1;y<=72;y++) {
        bool expected=x>=0&&x<40&&y>=0&&y<72 ? pixel(&a.cv,x,y):false;
        CHECK(d72x40_canvas_pixel(&a.cv,x,y)==expected);
    }
    char text[4]=""; d72x40_append(text,sizeof(text),"ABCDE"); CHECK(strcmp(text,"ABC")==0);
    d72x40_append(text,sizeof(text),"Z"); CHECK(strcmp(text,"ABC")==0);
    char empty[1]=""; d72x40_append(empty,sizeof(empty),"A"); CHECK(empty[0]==0);
    char num[8]=""; d72x40_append_u8(num,sizeof(num),0); CHECK(strcmp(num,"0")==0);
    num[0]=0; d72x40_append_u16(num,sizeof(num),0); CHECK(strcmp(num,"0")==0);
    num[0]=0; d72x40_append_u16(num,sizeof(num),65535); CHECK(strcmp(num,"65535")==0);
    d72x40_render_led_splash(&a.cv,255,0,0); guarded(&a);
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

    /* A run beginning at column zero includes its adjacent change. */
    want[0] = 1;
    want[1] = 1;
    collect(want, shown, 72, 5, 0x1f, &list);
    CHECK(list.count == 1 && span_is(&list.spans[0], 0, 0, 1));
    memset(want, 0, sizeof(want));

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
    struct frame want,shown,before; frame_init(&want);frame_init(&shown);frame_init(&before);
    struct d72x40_status st=approved_status(&d72x40_approved_frames[0]);uint32_t valid=0;
    d72x40_render(&want.cv,&st);CHECK(flush(want.buf,shown.buf,72,5,&valid)==360);CHECK(valid==0x1f);
    d72x40_render(&want.cv,&st);CHECK(flush(want.buf,shown.buf,72,5,&valid)==0);
    st.peer_battery=71;d72x40_render(&want.cv,&st);size_t sent=flush(want.buf,shown.buf,72,5,&valid);CHECK(sent>0&&sent<360);
    memcpy(before.buf,shown.buf,360);strcpy(st.layer,"NUM");d72x40_render(&want.cv,&st);
    sent=flush(want.buf,shown.buf,72,5,&valid);CHECK(sent>0&&sent<=65);
    for(size_t p=0;p<5;p++) CHECK(memcmp(before.buf+p*72+13,shown.buf+p*72+13,59)==0);
    CHECK(memcmp(want.buf,shown.buf,360)==0);
}
static void test_cached_rendering(void) {
    struct frame a, b, reference;
    frame_init(&a); frame_init(&b); frame_init(&reference);
    struct d72x40_render_cache ca = {0}, cb = {0};
    struct d72x40_status st = approved_status(&d72x40_approved_frames[0]);
    CHECK(d72x40_render_cached(&a.cv, &ca, &st));
    CHECK(!d72x40_render_cached(&a.cv, &ca, &st));
    CHECK(d72x40_render_cached(&b.cv, &cb, &st));
    CHECK(memcmp(a.buf, b.buf, 360) == 0);
    /* Every status field must invalidate, even fields conservatively retained. */
#define CHANGE(field, value) do { \
    st.field = (value); \
    CHECK(d72x40_render_cached(&a.cv, &ca, &st)); \
    CHECK(!d72x40_render_cached(&a.cv, &ca, &st)); \
    d72x40_render(&reference.cv, &st); \
    CHECK(memcmp(a.buf, reference.buf, 360) == 0); \
} while (0)
    CHANGE(layer[0], 'X'); CHANGE(transport, 1); CHANGE(link, 1);
    CHANGE(profile, 255); CHANGE(battery, 0); CHANGE(peer_battery, 100);
    CHANGE(charging, !st.charging); CHANGE(peer_charging, !st.peer_charging);
    CHANGE(caps_lock, !st.caps_lock); CHANGE(studio_lock_available, !st.studio_lock_available);
    CHANGE(unlocked, !st.unlocked); CHANGE(messenger_lock_available, !st.messenger_lock_available);
    CHANGE(messenger_unlocked, !st.messenger_unlocked);
    CHANGE(ble_available, !st.ble_available); CHANGE(usb_available, !st.usb_available);
    CHANGE(ble_link, 255); CHANGE(usb_link, 255);
#undef CHANGE
    CHECK(memcmp(a.buf, b.buf, 360) != 0);
    CHECK(!d72x40_render_cached(&b.cv, &cb, &cb.last.status));
    for (unsigned i = 0; i < 8; i++) {
        uint8_t pattern = i & 1u ? UINT8_MAX : 0;
        uint8_t brightness = i & 2u ? UINT8_MAX : 0;
        uint16_t speed = i & 4u ? UINT16_MAX : 0;
        CHECK(d72x40_render_led_cached(&a.cv, &ca, pattern, brightness, speed));
        CHECK(!d72x40_render_led_cached(&a.cv, &ca, pattern, brightness, speed));
        d72x40_render_led_splash(&reference.cv, pattern, brightness, speed);
        CHECK(memcmp(a.buf, reference.buf, 360) == 0);
    }
    CHECK(d72x40_render_cached(&a.cv, &ca, &st)); /* LED expiry restores status. */
    CHECK(!d72x40_render_cached(&a.cv, &ca, &st));
    CHECK(d72x40_render_led_cached(&a.cv, &ca, 255, 255, 65535)); /* Same tuple, new mode. */
    memset(&ca, 0, sizeof(ca)); memset(a.buf, 0, sizeof(a.buf));
    CHECK(d72x40_render_led_cached(&a.cv, &ca, 255, 255, 65535));
    CHECK(memcmp(a.buf, reference.buf, 360) == 0);
    memset(&ca, 0, sizeof(ca));
    CHECK(d72x40_render_cached(&a.cv, &ca, &st));
    guarded(&a); guarded(&b); guarded(&reference);
}

/* Independent logical-column expectations preserve every original symbol. */
static const uint8_t symbol_columns[D72X40_FONT_SYMBOL_COUNT][D72X40_FONT_WIDTH] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* 0x00 blank */
    {0x10, 0x20, 0x10, 0x08, 0x06}, /* 0x01 check */
    {0x22, 0x14, 0x08, 0x14, 0x22}, /* 0x02 cross */
    {0x1c, 0x22, 0x22, 0x22, 0x1c}, /* 0x03 open (a profile with no bond) */
    {0x22, 0x14, 0x7f, 0x2a, 0x14}, /* 0x04 bluetooth */
    {0x08, 0x6c, 0x3e, 0x1b, 0x08}, /* 0x05 bolt (charging) */
    {0x7e, 0x79, 0x49, 0x79, 0x7a}, /* 0x06 unlocked padlock */
};

static void test_direct_symbols_and_numbers(void) {
    struct frame f; uint8_t expected[360];
    for (unsigned code = 0; code <= 255; code++) {
        for (int x = -9; x <= 40; x++) for (int y = -1; y <= 72; y++) {
            frame_init(&f); memset(expected, 0, sizeof(expected));
            d72x40_symbol(&f.cv, x, y, code);
            if (code < 7 && y >= 0 && y < 72)
                for (int col = 0; col < 5; col++) for (int row = 0; row < 7; row++)
                    ink(expected, x + col, y + row,
                        ((symbol_columns[code][col] >> row) & 1u) != 0, false);
            CHECK(memcmp(f.buf, expected, 360) == 0); guarded(&f);
        }
    }
    d72x40_symbol(&f.cv, INT_MIN, INT_MAX, UINT_MAX); guarded(&f);
    CHECK(d72x40_link_symbol(D72X40_LINK_UP) == 1);
    CHECK(d72x40_link_symbol(D72X40_LINK_OPEN) == 0);
    CHECK(d72x40_link_symbol(D72X40_LINK_DOWN) == 2);
    CHECK(d72x40_link_symbol(255) == 2);
    for (unsigned value = 0; value <= UINT16_MAX; value++) {
        char expected_num[8];
        (void)snprintf(expected_num, sizeof(expected_num), "%u", value);
        for (size_t cap = 1; cap <= sizeof(expected_num); cap++) {
            char actual[8] = "";
            d72x40_append_u16(actual, cap, (uint16_t)value);
            size_t length = strlen(expected_num);
            size_t kept = length < cap - 1 ? length : cap - 1;
            CHECK(strlen(actual) == kept && memcmp(actual, expected_num, kept) == 0);
            if (value <= UINT8_MAX) {
                actual[0] = 0; d72x40_append_u8(actual, cap, (uint8_t)value);
                CHECK(strlen(actual) == kept && memcmp(actual, expected_num, kept) == 0);
            }
        }
    }
}

static void test_incremental_transitions(void) {
    struct frame actual, reference;
    for (size_t from = 0; from < 24; from++) for (size_t to = 0; to < 24; to++) {
        frame_init(&actual); frame_init(&reference);
        struct d72x40_render_cache cache = {0};
        struct d72x40_status before = approved_status(&d72x40_approved_frames[from]);
        struct d72x40_status after = approved_status(&d72x40_approved_frames[to]);
        CHECK(d72x40_render_cached(&actual.cv, &cache, &before));
        (void)d72x40_render_cached(&actual.cv, &cache, &after);
        d72x40_render(&reference.cv, &after);
        CHECK(memcmp(actual.buf, reference.buf, 360) == 0); guarded(&actual);
    }
    struct d72x40_status before = approved_status(&d72x40_approved_frames[0]);
    CHECK(d72x40_status_changes(&before, &before) == 0);
#define GROUP(field, value, expected) do { \
    struct d72x40_status after = before; after.field = (value); \
    CHECK(d72x40_status_changes(&before, &after) == (unsigned)(expected)); \
} while (0)
    GROUP(layer[0], 'X', D72X40_GROUP_HEADER);
    GROUP(profile, 255, D72X40_GROUP_CONNECTIONS);
    GROUP(ble_available, !before.ble_available, D72X40_GROUP_CONNECTIONS);
    GROUP(usb_available, !before.usb_available, D72X40_GROUP_CONNECTIONS);
    GROUP(ble_link, 255, D72X40_GROUP_CONNECTIONS);
    GROUP(usb_link, 255, D72X40_GROUP_CONNECTIONS);
    GROUP(caps_lock, !before.caps_lock, D72X40_GROUP_CAPS);
    GROUP(battery, 0, D72X40_GROUP_BATTERY);
    GROUP(charging, !before.charging, D72X40_GROUP_BATTERY);
    GROUP(peer_battery, 100, D72X40_GROUP_PEER_BATTERY);
    GROUP(peer_charging, !before.peer_charging, D72X40_GROUP_PEER_BATTERY);
    GROUP(peer_battery, D72X40_BATTERY_HIDDEN, D72X40_GROUP_BATTERY | D72X40_GROUP_PEER_BATTERY);
    GROUP(studio_lock_available, !before.studio_lock_available, D72X40_GROUP_LOCKS);
    GROUP(unlocked, !before.unlocked, D72X40_GROUP_LOCKS);
    GROUP(messenger_lock_available, !before.messenger_lock_available, D72X40_GROUP_LOCKS);
    GROUP(messenger_unlocked, !before.messenger_unlocked, D72X40_GROUP_LOCKS);
    GROUP(transport, 1, 0); GROUP(link, 1, 0);
#undef GROUP
    for (unsigned mask = 0; mask <= D72X40_GROUP_ALL; mask++) {
        frame_init(&actual); frame_init(&reference);
        struct d72x40_render_cache cache = {0};
        struct d72x40_status after = before;
        if (mask & D72X40_GROUP_HEADER) strcpy(after.layer, "NUM");
        if (mask & D72X40_GROUP_CONNECTIONS) {after.profile = 255; after.ble_available = false; after.usb_available = false;}
        if (mask & D72X40_GROUP_CAPS) after.caps_lock = !before.caps_lock;
        if (mask & D72X40_GROUP_BATTERY) after.battery = D72X40_BATTERY_HIDDEN;
        if (mask & D72X40_GROUP_PEER_BATTERY) after.peer_battery = D72X40_BATTERY_HIDDEN;
        if (mask & D72X40_GROUP_LOCKS) {after.studio_lock_available = false; after.messenger_lock_available = false;}
        (void)d72x40_render_cached(&actual.cv, &cache, &before);
        (void)d72x40_render_cached(&actual.cv, &cache, &after);
        d72x40_render(&reference.cv, &after);
        CHECK(memcmp(actual.buf, reference.buf, 360) == 0);
        (void)d72x40_render_cached(&actual.cv, &cache, &before);
        d72x40_render(&reference.cv, &before);
        CHECK(memcmp(actual.buf, reference.buf, 360) == 0); guarded(&actual);
    }
    for (unsigned from = 0; from < 8; from++) for (unsigned to = 0; to < 8; to++) {
        frame_init(&actual); frame_init(&reference);
        struct d72x40_render_cache cache = {0};
        (void)d72x40_render_led_cached(&actual.cv, &cache, from & 1u ? 255 : 5,
                                     from & 2u ? 255 : 60, from & 4u ? 65535 : 150);
        uint8_t pattern = to & 1u ? 255 : 5;
        uint8_t brightness = to & 2u ? 255 : 60;
        uint16_t speed = to & 4u ? 65535 : 150;
        (void)d72x40_render_led_cached(&actual.cv, &cache, pattern, brightness, speed);
        d72x40_render_led_splash(&reference.cv, pattern, brightness, speed);
        CHECK(memcmp(actual.buf, reference.buf, 360) == 0); guarded(&actual);
    }
}

int main(void) {
    test_approved_frames();test_bitmap_edges();test_status_font_and_locks();
    test_normal_led_content();test_state_and_bounds();test_spans();test_redraw_costs();
    test_cached_rendering();test_direct_symbols_and_numbers();test_incremental_transitions();
    if(failures){fprintf(stderr,"%d checks failed\n",failures);return EXIT_FAILURE;}
    puts("display_72x40_core: all fixed-panel checks passed");return EXIT_SUCCESS;
}
