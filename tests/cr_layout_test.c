/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the compiled layouts (firmware/src/cr_layout.h, build/gen/cr_layouts.h by tools/gen_layout.py;
 * docs/LAYOUT-TOOL.md "Step 4, the firmware: a plan"): the header compiles as C, P5's panel has its counts (8 screens,
 * 73 elements, 19 lanes), every bound element is an address of P5's 19 pages and in exactly one lane, every string
 * is NUL-terminated and reachable, and the flags say what the design says (REV's two LEDs, KEYBOARD's three positions):
 *   python3 tools/gen_layout.py build/gen/cr_layouts.h
 *   cc -std=c11 -Wall -Wextra -Werror -pedantic -Ibuild/gen -Ifirmware/src -o build/host/cr_layout_test tests/cr_layout_test.c
 * (tests/run_cr_tests.sh runs it. Nothing of the firmware uses the tables yet: stage 2.) */
#include <stdio.h>
#include <string.h>
#include "cr_layouts.h"

#define P5_ENGINE 16u
#define P5_PAGES 19u            /* design/layouts/p5.json */

static int passed, failed;
static void ok(int c, const char *n)
{
    if (c) passed++;
    else { failed++; printf("FAIL %s\n", n); }
}

/* the string at off: NUL-terminated inside the pool? -> its length + 1, 0 if not */
static unsigned str_at(unsigned off)
{
    const char *z;
    if (off >= CR_LAY_POOL_SIZE) return 0;
    z = memchr(CR_LAY_POOL + off, 0, CR_LAY_POOL_SIZE - off);
    return z ? (unsigned)(z - (CR_LAY_POOL + off)) + 1u : 0;
}
static unsigned char seen[CR_LAY_POOL_SIZE];
/* mark a label (and its value names up to the empty string); -> how many strings, 0 if one runs off the pool */
static unsigned mark(unsigned off, int names)
{
    unsigned n = 0, len;
    do {
        if (!(len = str_at(off))) return 0;
        memset(seen + off, 1, len);
        off += len;
        n++;
    } while (names && (n == 1 || len > 1));
    return n;
}
static const cr_lay_elem_t *find(unsigned type, const char *label)
{
    unsigned i;
    for (i = 0; i < CR_LAY_NELEMS; i++)
        if (CR_LAY_ELEMS[i].type == type && CR_LAY_ELEMS[i].label != CR_LAY_NOSTR && str_at(CR_LAY_ELEMS[i].label) &&
            !strcmp(CR_LAY_POOL + CR_LAY_ELEMS[i].label, label)) return &CR_LAY_ELEMS[i];
    return 0;
}

int main(void)
{
    const cr_layout_t *L = &CR_LAYOUTS[0];
    unsigned s, i, k, lanes = 0, bound = 0, inlane = 0, bad_addr = 0, bad_lane = 0, bad_str = 0, frames_first = 1;
    const cr_lay_elem_t *e;

    ok(sizeof(cr_lay_elem_t) == 14u && sizeof(cr_lay_screen_t) == 38u && sizeof(cr_layout_t) == 8u, "the records: 14, 38, 8 B");
    ok(CR_LAY_NENGINES == 1u && L->engine == P5_ENGINE, "one layout: P5's");
    ok(L->nscreens == 8u && CR_LAY_NSCREENS == 8u && L->screen0 == 0u, "P5: 8 screens");
    ok(L->nelems == 73u && CR_LAY_NELEMS == 73u && L->elem0 == 0u, "P5: 73 elements");
    for (s = 0; s < L->nscreens; s++) {
        const cr_lay_screen_t *c = &CR_LAY_SCREENS[L->screen0 + s];
        unsigned char count[256] = {0};
        lanes += c->nlanes;
        if (s && (c->first != CR_LAY_SCREENS[L->screen0 + s - 1].first + CR_LAY_SCREENS[L->screen0 + s - 1].count ||
                  c->group < CR_LAY_SCREENS[L->screen0 + s - 1].group)) bad_lane++;
        if (c->group > 4u || c->nlanes < 1u || c->nlanes > CR_LAY_LANES) bad_lane++;
        if (!mark(c->title, 0)) bad_str++;
        for (k = 0; k < CR_LAY_LANES * CR_LAY_SLOTS; k++) {
            unsigned x = c->lane[k / CR_LAY_SLOTS][k % CR_LAY_SLOTS];
            if (x == CR_LAY_EMPTY) continue;
            if (k / CR_LAY_SLOTS >= c->nlanes || x >= c->count) { bad_lane++; continue; }
            count[x]++;
            inlane++;
        }
        for (i = 0; i < c->count; i++) {
            e = &CR_LAY_ELEMS[L->elem0 + c->first + i];
            if (i && e->type == CR_E_FRAME && e[-1].type != CR_E_FRAME) frames_first = 0;
            if (e->type >= CR_E_N || e->y < 28u || e->x + e->w > 240u || e->y + e->h > 240u) bad_addr++;
            if (e->page == CR_LAY_NONE) { if (count[i]) bad_lane++; }
            else {
                bound++;
                if (e->page >= P5_PAGES || e->col >= 4u) bad_addr++;
                if (count[i] != 1u) bad_lane++;
            }
            if (e->label != CR_LAY_NOSTR && !mark(e->label, (e->flags & CR_LF_NAMES) != 0)) bad_str++;
        }
    }
    ok(lanes == 19u, "P5: 19 lanes");
    ok(bound == 56u && inlane == 56u, "56 bound elements (all of P5's parameters), 56 lane slots");
    ok(!bad_addr, "every element inside rows 28 .. 239, every (page, col) within P5's 19 pages");
    ok(!bad_lane, "screens in group order, every bound element in exactly one lane, nothing else in one");
    ok(frames_first, "a screen's frames first");
    ok(!bad_str, "every string NUL-terminated inside the pool");
    for (i = 0; i < CR_LAY_POOL_SIZE && seen[i]; i++) {}
    ok(i == CR_LAY_POOL_SIZE, "every byte of the pool reachable from a label or a title");
    /* the flags as the design says */
    e = find(CR_E_LED, "REV");
    ok(e && (e->flags & CR_LF_LEDS2) && (e->flags & CR_LF_BELOW) && (e->flags & CR_LF_NAMES) &&
       !strcmp(CR_LAY_POOL + e->label + 4, "1/2") && !strcmp(CR_LAY_POOL + e->label + 8, "3"),
       "FILTER's REV: two LEDs, the label below, the names 1/2 and 3");
    e = find(CR_E_SWITCH, "KEYBOARD");
    ok(e && CR_LF_POS(e->flags) == 3u && (e->flags & CR_LF_NAMES) && !strcmp(CR_LAY_POOL + e->label + 9, "OFF"),
       "FILTER's KEYBOARD: a switch of 3 positions, OFF HALF FULL");
    e = find(CR_E_LED, "SYNC");
    ok(e && (e->flags & CR_LF_BELOW) && !CR_LF_ICON(e->flags) && e->page == 1u && e->col == 0u,
       "OSC A's SYNC: an led at OSC A+ col 0, its label below");
    ok(CR_LAY_ELEMS[3].type == CR_E_LED && CR_LF_ICON(CR_LAY_ELEMS[3].flags) == CR_LI_SAW && CR_LAY_ELEMS[3].label == CR_LAY_NOSTR,
       "OSC A's saw: the saw icon, no label");
    ok(CR_LAY_ELEMS[0].type == CR_E_FRAME && CR_LAY_ELEMS[0].r == 8u && !strcmp(CR_LAY_POOL + CR_LAY_ELEMS[0].label, "OSCILLATOR A"),
       "OSC A's frame: r 8, its title in capitals");
    printf("cr_layout: %u screens, %u elements, %u lanes, a pool of %u B: %u B; %d passed, %d failed\n", CR_LAY_NSCREENS,
           CR_LAY_NELEMS, lanes, CR_LAY_POOL_SIZE, (unsigned)(CR_LAY_NELEMS * 14u + CR_LAY_NSCREENS * 38u + 8u + CR_LAY_POOL_SIZE),
           passed, failed);
    return failed != 0;
}
