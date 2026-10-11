/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the ChoralRoot screen renderer (firmware/src/cr_draw.c on gfx.c, the LCD a 240 x 240 array as in
 * tests/ui_test.c), run by tests/run_cr_draw.sh:
 *   cr_draw_test OUTDIR
 * Renders every mock-up state of tests/cr_screens_gen.h (tests/gen_cr_screens.py from
 * design/choralroot-fm1-mockups.json) in the MOD palette, settled (anim cleared, anim_ms 0) and mid-animation (the
 * state's anim at CR_TEST_MID ms), to OUTDIR/ppm/<nn>_<slug>.ppm and <nn>_<slug>_mid.ppm (tests/run_cr_draw.sh
 * makes the PNGs and the contact sheet).
 * Layout lint (gfx.c GFX_HOOK_TEXT, as tests/ui_render.c): the ink box of every text as it lands on the screen; a
 * finding: a box off the screen, two texts overlapping, a text on the ring's band. Ellipsised texts are listed.
 * Checks: every glyph of the cr faces fits cr_gfx.c's buffer; the draw cache (unchanged: nothing drawn; one line
 * changed: one strip blitted; cr_draw_invalidate: all six); animations are pure (the same (state, ms) twice gives the
 * same pixels, whatever was drawn between) and settle (past their duration they equal the settled screen).
 * Exit 1 on a finding or a failed check. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)

static uint16_t host_screen[240 * 240];
static uint32_t host_blits;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_blits++;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_screen[(y + j) * 240u + x + i] = p[j * w + i];
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)   /* (cr_draw_black: the screen off) */
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
}

/* the layout lint's hook: ink boxes in canvas rows; the strip's top row makes them screen rows */
typedef struct { int16_t x0, y0, x1, y1; uint8_t flags; char s[40]; } tbox_t;
static tbox_t boxes[256];
static uint32_t nbox;
static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hk_text(x0, y0, x1, y1, s, flags)
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)

#define FELUCCA_KEYCAPS 0                        /* as choralroot.c: no Felucca keycaps */
#include "../firmware/src/gfx.c"
#include "../firmware/src/cr_gfx.c"
#include "../firmware/src/cr_draw.c"
#include "cr_screens_gen.h"

#define CR_TEST_MID 80u                   /* ms into the animations: mid-squeeze, mid-slide, 2 stripes filled */

static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    uint32_t i;
    tbox_t b;
    b.x0 = (int16_t)x0; b.x1 = (int16_t)x1;
    b.y0 = (int16_t)(y0 - cv_oy); b.y1 = (int16_t)(y1 - cv_oy);     /* canvas rows -> screen rows */
    b.flags = (uint8_t)(flags & ~2u);                                 /* (cut by a strip: every strip draws its part) */
    snprintf(b.s, sizeof b.s, "%s", s);
    for (i = 0; i < nbox; i++)
        if (!memcmp(&boxes[i], &b, 8) && !strcmp(boxes[i].s, b.s)) return;   /* the same text from another strip */
    if (nbox < 256u) boxes[nbox++] = b;
}

static FILE *rep;
static uint32_t nfind, nell, nfail;
static void finding(const char *scr, const char *what, const tbox_t *a, const tbox_t *b)
{
    nfind++;
    fprintf(rep, "LINT %-34s %-22s '%s' (%d,%d)-(%d,%d)", scr, what, a->s, a->x0, a->y0, a->x1, a->y1);
    if (b) fprintf(rep, " / '%s' (%d,%d)-(%d,%d)", b->s, b->x0, b->y0, b->x1, b->y1);
    fputc('\n', rep);
}
static int ring_hits(const tbox_t *b)
{
    int32_t x, y;
    for (y = b->y0; y < b->y1; y++)
        for (x = b->x0; x < b->x1; x++) {
            int32_t dx = 2 * x + 1 - 240, dy = 2 * y + 1 - 240, d2 = dx * dx + dy * dy;   /* (half px) */
            if (d2 >= 2 * 110 * 2 * 110 && d2 <= 2 * 116 * 2 * 116) return 1;
        }
    return 0;
}
static void lint(const char *scr, const cr_screen_t *s)
{
    uint32_t i, j;
    for (i = 0; i < nbox; i++) {
        const tbox_t *b = &boxes[i];
        if ((b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) && !(b->flags & 32u))   /* (sliding: clipped) */
            finding(scr, "off the screen", b, 0);
        if (b->flags & 1u) {
            nell++;
            fprintf(rep, "note %-34s ellipsised '%s'\n", scr, b->s);
        }
        if (s->ring_on && !(b->flags & 32u) && ring_hits(b)) finding(scr, "on the ring", b, 0);
        if (s->dial_on && s->header && b->x1 > 219 && b->y0 < 23) finding(scr, "on the dial", b, 0);
        if (s->message[0] && b->x0 < 120 + 114 && b->x1 > 120 - 114 && b->y0 < 140 && b->y1 > 100) continue;
        for (j = i + 1u; j < nbox; j++) {
            const tbox_t *o = &boxes[j];
            if ((b->flags | o->flags) & 32u) continue;                /* (a sliding text: clipped on purpose) */
            if (b->x0 < o->x1 && o->x0 < b->x1 && b->y0 < o->y1 && o->y0 < b->y1) finding(scr, "overlaps", b, o);
        }
    }
}

static void write_ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/ppm/%s.ppm", dir, name);
    if (!(f = fopen(path, "wb"))) { fprintf(stderr, "cr_draw_test: cannot write %s\n", path); nfail++; return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t c = swap16(host_screen[i]);
        uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u),
                          (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void check(const char *what, int ok)
{
    printf("cr_draw: %-78s %s\n", what, ok ? "ok" : "FAIL");
    fprintf(rep, "check %-78s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) nfail++;
}

/* draw s at t from scratch: the lint sees every text */
static void render(const cr_screen_t *s, uint32_t t)
{
    nbox = 0;
    cr_draw_invalidate();
    cr_draw(s, t);
}

/* the meter on screen: its stripes (the runs of ink on the row through their middle) -> *all, the lit ones (in the
 * panel's colour) returned */
static uint32_t meter_stripes(const cr_screen_t *s, uint32_t *all)
{
    uint32_t y = (uint32_t)(CR_PY0 + cr_ph(s) - 46 + 7), x, lit = 0, n = 0, in = 0, hot = 0;
    uint16_t bg = swap16(T_BG), on = swap16(cr_rgb(s->col, CR_RED));
    for (x = 0; x <= 240u; x++) {
        uint16_t p = x < 240u ? host_screen[y * 240u + x] : bg;
        if (p != bg) {
            in = 1;
            hot |= p == on;
        } else if (in) {
            n++;
            lit += hot;
            in = hot = 0;
        }
    }
    *all = n;
    return lit;
}

static uint32_t mod_index(void)
{
    uint32_t i;
    for (i = 0; i < NPALETTES; i++)
        if (!strcmp(UI_PALETTES[i].name, "MOD")) return i;
    return NPALETTES;
}

/* the ring's table (cr_draw.c cr_ring, firmware/src/cr_ring_table.h): built here from cr_arc_px, checked against
 * the header byte for byte; CR_RING_GEN=1 rewrites the header */
static int cr_ring_build(cr_ring_t *r)
{
    cr_arcg_t gs, gd;
    uint32_t k = 0, q = 0, s, d;
    int32_t i, j;
    memset(r, 0, sizeof *r);
    cr_arc_geom(&gs, CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, 0, 65536u, 0, 0, 0);
    cr_arc_geom(&gd, CR_RING_C, CR_RING_C, CR_RING_R, CR_RING_W, 0, 65536u, 0, 2 * 16, 6 * 16);
    for (s = 0; s <= 16u; s++)
        for (d = 0; d <= s; d++) {
            r->ps[q] = (uint8_t)s;
            r->pd[q++] = (uint8_t)d;
        }
    for (j = 0; j < 240; j++) {
        int runs = 0, open = 0;
        r->off[j] = (uint16_t)k;
        for (i = 0; i < 240; i++) {
            s = cr_arc_px(&gs, i, j);
            if (!s) {
                open = 0;
                continue;
            }
            if (!open) {
                if (runs == 2 || k >= CR_RING_MAX) return 0;
                r->x0[j][runs++] = (uint8_t)i;
                open = 1;
            }
            if (k >= CR_RING_MAX) return 0;
            d = cr_arc_px(&gd, i, j);
            r->cov[k++] = (uint8_t)(s * (s + 1u) / 2u + d);
            r->n[j][runs - 1]++;
        }
    }
    r->off[240] = (uint16_t)k;
    return 1;
}

static void ring_arr(FILE *f, const char *name, const uint8_t *p, uint32_t n, int u16)
{
    uint32_t i;
    fprintf(f, "    /* %s */ {", name);
    for (i = 0; i < n; i++)
        fprintf(f, "%s%u,", i % 20u ? "" : "\n        ", u16 ? ((const uint16_t *)(const void *)p)[i] : p[i]);
    fprintf(f, "\n    },\n");
}

static int ring_check(void)
{
    static cr_ring_t r;
    const char *gen = getenv("CR_RING_GEN");
    FILE *f;
    if (!cr_ring_build(&r)) { fprintf(stderr, "cr_draw_test: the ring's table does not fit CR_RING_MAX\n"); return 0; }
    if (gen && *gen == '1') {
        if (!(f = fopen("firmware/src/cr_ring_table.h", "w"))) return 0;
        fprintf(f, "/* SPDX-License-Identifier: GPL-3.0-only */\n/* generated by tests/cr_draw_test.c (CR_RING_GEN=1 sh "
                   "tests/run_cr_draw.sh): cr_draw.c's cr_ring, the loop ring's pixels (%u) */\n", r.off[240]);
        ring_arr(f, "off", (const uint8_t *)r.off, 241, 1);
        fprintf(f, "    /* x0 */ {");
        { uint32_t j; for (j = 0; j < 240u; j++) fprintf(f, "%s{%u,%u},", j % 10u ? "" : "\n        ", r.x0[j][0], r.x0[j][1]); }
        fprintf(f, "\n    },\n    /* n */ {");
        { uint32_t j; for (j = 0; j < 240u; j++) fprintf(f, "%s{%u,%u},", j % 10u ? "" : "\n        ", r.n[j][0], r.n[j][1]); }
        fprintf(f, "\n    },\n");
        ring_arr(f, "ps", r.ps, 153, 0);
        ring_arr(f, "pd", r.pd, 153, 0);
        ring_arr(f, "cov", r.cov, CR_RING_MAX, 0);
        fclose(f);
        printf("cr_draw_test: wrote firmware/src/cr_ring_table.h (rebuild to check it)\n");
        return 1;
    }
    return !memcmp(&r, &cr_ring, sizeof r);
}

int main(int argc, char **argv)
{
    static uint16_t a[240 * 240], b[240 * 240];
    const char *dir = argc > 1 ? argv[1] : "build/cr_screens";
    char path[512], name[160];
    uint32_t i, k, pal = mod_index(), worst = 0;

    snprintf(path, sizeof path, "%s/report.txt", dir);
    if (!(rep = fopen(path, "w"))) { fprintf(stderr, "cr_draw_test: cannot write %s (make %s/ppm first)\n", path, dir); return 1; }
    check("the MOD palette is one of the UI palettes", pal < NPALETTES);
    check("the ring's table (firmware/src/cr_ring_table.h) is cr_ring_build's (CR_RING_GEN=1 rewrites it)", ring_check());
    palette_set(pal < NPALETTES ? pal : 0u);

    {   /* every glyph of the big faces fits the decode buffer */
        const aafont_t *F[2] = {&AF_CRX, &AF_CRB};
        uint32_t n[2] = {(uint32_t)(AF_CRX.last - AF_CRX.first + 1u + AF_CRX.nex), (uint32_t)(AF_CRB.last - AF_CRB.first + 1u + AF_CRB.nex)};
        int ok = 1;
        for (k = 0; k < 2u; k++)
            for (i = 0; i < n[k]; i++) {
                uint32_t area = (uint32_t)F[k]->g[i].bw * F[k]->g[i].bh;
                if (area > worst) worst = area;
                if (area > CR_GLYPH_MAX) ok = 0;
            }
        snprintf(name, sizeof name, "every CRX / CRB glyph fits cr_gfx.c's buffer (largest %u of %u px)", worst, CR_GLYPH_MAX);
        check(name, ok);
        check("CRX has the chord charset (roots, # b, m dim sus +, M7 9 JAZZ, figures, PANIC)",
              cr_covers(&AF_CRX, "ABCDEFG#b") && cr_covers(&AF_CRX, "mdimsus+") && cr_covers(&AF_CRX, "0123456789") &&
              cr_covers(&AF_CRX, "MJZWTF?PANIC"));
    }

    for (i = 0; i < CR_NSCREENS; i++) {
        cr_screen_t s = CR_SCREENS[i];
        s.anim = 0;
        render(&s, 0);
        lint(CR_SCREEN_SLUGS[i], &s);
        write_ppm(dir, CR_SCREEN_SLUGS[i]);
        fprintf(rep, "screen %-34s %u texts\n", CR_SCREEN_SLUGS[i], nbox);
        render(&CR_SCREENS[i], CR_TEST_MID);
        snprintf(name, sizeof name, "%s_mid", CR_SCREEN_SLUGS[i]);
        lint(name, &CR_SCREENS[i]);
        write_ppm(dir, name);
    }

    {   /* the cache */
        cr_screen_t s = CR_SCREENS[3];  /* Em in Key Mode */
        s.anim = 0;
        render(&s, 0);
        check("a forced draw blits all six strips", cr_dc.blits == CR_NSTRIP);
        host_blits = 0;
        cr_draw(&s, 0);
        check("the same screen again: nothing drawn, nothing blitted", cr_dc.blits == 0 && host_blits == 0);
        cr_draw(&s, 5000);
        check("a later clock with no animation: nothing drawn", cr_dc.blits == 0);
        snprintf(s.mid, sizeof s.mid, "Key: G");
        cr_draw(&s, 0);
        check("the top line changes: only its strip is blitted", cr_dc.blits == 1);
        s.ring_on = 1;
        cr_draw(&s, 0);
        check("the ring appears: every strip it crosses (all six)", cr_dc.blits == CR_NSTRIP);
        cr_draw_invalidate();
        cr_draw(&s, 0);
        check("cr_draw_invalidate: all six again", cr_dc.blits == CR_NSTRIP);
        {   /* the screen off (cr_ui.c): the whole panel black; the wake's draw, of the very same screen, puts every
             * pixel back (a strip or a tile left to the cache would stay black) */
            static uint16_t before[240 * 240];
            uint32_t i, ink = 0, left = 0;
            memcpy(before, host_screen, sizeof before);
            cr_draw_black();
            for (i = 0; i < 240u * 240u; i++) ink += host_screen[i] != 0;
            check("cr_draw_black: every pixel of the panel black", ink == 0);
            cr_draw(&s, 0);
            for (i = 0; i < 240u * 240u; i++) left += host_screen[i] == 0;
            check("the draw after it: all six strips whole (115200 bytes), the screen as before, no pixel left black",
                  cr_dc.blits == CR_NSTRIP && cr_dc.drawn == CR_NSTRIP && cr_dc.bytes == 240u * 240u * 2u &&
                  !memcmp(before, host_screen, sizeof before) && left == 0);
        }
    }

    {   /* the meters settled: every fixture's stripes are its `segments`, `pct` of them lit. The sound read-out's
         * (cr_ui.c cu_pool_popup: a stripe per preset; INIT, position 00, is not one): 5 of 25 on FM6's 05 (the preset
         * sheet's 1), none on INIT, every one on the last preset (pool_init, pool_last: device renders, no mock-up),
         * one unlit stripe for a pool of INIT alone (not cr_segs' 12 of `segments` 0) */
        cr_screen_t s;
        uint32_t all, lit, nm = 0, nbad = 0;
        int32_t ip = -1;
        int ok;
        for (k = 0; k < CR_NSCREENS; k++) {
            if (CR_SCREENS[k].kind != CR_K_METER || CR_SCREENS[k].ring_on) continue;
            s = CR_SCREENS[k];
            s.anim = 0;
            render(&s, 0);
            lit = meter_stripes(&s, &all);
            nm++;
            if (all != cr_segs(&s) || lit != cr_filled(&s, s.pct)) {
                nbad++;
                fprintf(rep, "meter %s: %u of %u stripes lit, not %u of %u\n", CR_SCREEN_SLUGS[k], lit, all,
                        cr_filled(&s, s.pct), cr_segs(&s));
            }
            if (ip < 0 && !strcmp(s.label, "FM6 \267 05/25")) ip = (int32_t)k;
        }
        snprintf(name, sizeof name, "the %u meters: as many stripes as segments, pct of them lit", nm);
        check(name, nm > 0u && nbad == 0u);
        check("the sound read-out's fixture (FM6 05/25) is in the table", ip >= 0);
        if (ip >= 0) {
            s = CR_SCREENS[ip];
            s.anim = 0;
            render(&s, 0);
            lit = meter_stripes(&s, &all);
            ok = lit == 5u && all == 25u;
            snprintf(s.value, sizeof s.value, "00");
            snprintf(s.sub, sizeof s.sub, "INIT");
            snprintf(s.label, sizeof s.label, "FM6 \267 00/25");
            s.pct = 0;
            render(&s, 0);
            write_ppm(dir, "pool_init");
            lit = meter_stripes(&s, &all);
            ok = ok && lit == 0u && all == 25u;
            snprintf(s.value, sizeof s.value, "25");
            snprintf(s.sub, sizeof s.sub, "PIANO");
            snprintf(s.label, sizeof s.label, "FM6 \267 25/25");
            s.pct = 256;
            render(&s, 0);
            write_ppm(dir, "pool_last");
            lit = meter_stripes(&s, &all);
            snprintf(name, sizeof name, "a pool's meter: 5 of 25 lit on 05, none on INIT, every one on the last (%u of %u)", lit, all);
            check(name, ok && lit == 25u && all == 25u);
            snprintf(s.value, sizeof s.value, "00");
            snprintf(s.sub, sizeof s.sub, "INIT");
            snprintf(s.label, sizeof s.label, "FM6 \267 00/00");
            s.pct = 0;
            s.segments = 1;
            render(&s, 0);
            lit = meter_stripes(&s, &all);
            check("a pool of INIT alone: one stripe, not lit", lit == 0u && all == 1u);
        }
    }

    {   /* a meter's fill (CR_A_FILL): a stripe every CR_FILL_MS; more than CR_FILL_MAX to light: all of them in the
         * time of CR_FILL_MAX (a pool's meter, at most 32 stripes, is full inside the popup's 900 ms) */
        cr_screen_t s;
        uint32_t all, lit[5];
        cr_screen_clear(&s);
        s.kind = CR_K_METER;
        s.batt = 255;
        s.col = CR_COL_WHITE;
        s.thick = 14;
        s.pos = -1;
        snprintf(s.value, sizeof s.value, "64");
        s.anim = CR_A_FILL;
        s.pct = 256;
        s.segments = 12;                /* a knob's meter: 30 ms a stripe, as ever */
        render(&s, 12u * CR_FILL_MS - 1u);
        lit[0] = meter_stripes(&s, &all);
        render(&s, 12u * CR_FILL_MS);
        lit[1] = meter_stripes(&s, &all);
        snprintf(name, sizeof name, "a meter's fill: 12 stripes, one every %u ms (%u lit at %u ms, %u at %u)", CR_FILL_MS,
                 lit[0], 12u * CR_FILL_MS - 1u, lit[1], 12u * CR_FILL_MS);
        check(name, lit[0] == 11u && lit[1] == 12u && all == 12u);
        s.segments = 32;                /* a long pool's last preset */
        render(&s, CR_FILL_MAX * CR_FILL_MS / 2u);
        lit[0] = meter_stripes(&s, &all);
        render(&s, CR_FILL_MAX * CR_FILL_MS - 1u);
        lit[1] = meter_stripes(&s, &all);
        render(&s, CR_FILL_MAX * CR_FILL_MS);
        lit[2] = meter_stripes(&s, &all);
        s.pct_from = 248;               /* a step from the place before: one stripe, at once */
        render(&s, CR_FILL_MS);
        lit[3] = meter_stripes(&s, &all);
        s.pct_from = 256;               /* back to INIT: emptied in the same time */
        s.pct = 0;
        render(&s, CR_FILL_MAX * CR_FILL_MS);
        lit[4] = meter_stripes(&s, &all);
        snprintf(name, sizeof name, "32 stripes fill in the %u ms of %u (%u lit halfway, %u at %u ms, %u at %u)",
                 CR_FILL_MAX * CR_FILL_MS, CR_FILL_MAX, lit[0], lit[1], CR_FILL_MAX * CR_FILL_MS - 1u, lit[2],
                 CR_FILL_MAX * CR_FILL_MS);
        check(name, lit[0] == 16u && lit[1] == 31u && lit[2] == 32u && lit[3] == 32u && lit[4] == 0u && all == 32u);
        {   /* a step of the sound read-out (cr_ui.c cu_animate: pct_from is the place shown before): only the stripes
             * between the two places move, never fewer than the lower place lit nor more than the higher; down: they
             * go dark one by one; a wrap (25 -> none): every frame between, done in the time of CR_FILL_MAX */
            uint32_t t, lo = 99u, hi = 0u, ok = 1u, f;
            s.segments = 25;
            s.pct_from = 5u * 256u / 25u;   /* 05 -> 06 */
            s.pct = 6u * 256u / 25u;
            for (t = 0; t <= 4u * CR_FILL_MS; t += 5u) {
                render(&s, t);
                f = meter_stripes(&s, &all);
                lo = f < lo ? f : lo;
                hi = f > hi ? f : hi;
                ok &= f == (t < CR_FILL_MS ? 5u : 6u) && cr_anim_busy(&s, t) == (t < CR_FILL_MS);
            }
            snprintf(name, sizeof name, "a step up (05 -> 06 of 25): %u..%u stripes lit, the sixth after %u ms", lo, hi,
                     CR_FILL_MS);
            check(name, ok && lo == 5u && hi == 6u && all == 25u);
            s.pct_from = 6u * 256u / 25u;   /* 06 -> 03: 6, 5, 4, 3 */
            s.pct = 3u * 256u / 25u;
            for (t = 0, ok = 1u; t <= 5u * CR_FILL_MS; t += 5u) {
                render(&s, t);
                f = meter_stripes(&s, &all);
                ok &= f == (t >= 3u * CR_FILL_MS ? 3u : 6u - t / CR_FILL_MS);
            }
            check("a step down (06 -> 03): the stripes go dark one every 30 ms, never more than 6 lit", ok);
            s.pct_from = 256;               /* the last -> INIT */
            s.pct = 0;
            for (t = 0, ok = 1u, hi = 25u; t <= CR_FILL_MAX * CR_FILL_MS; t += 15u) {
                render(&s, t);
                f = meter_stripes(&s, &all);
                ok &= f <= hi;
                hi = f;
            }
            ok &= cr_fill_at(25u, 0u, CR_FILL_MAX * CR_FILL_MS - 1u) == 1u && cr_fill_at(25u, 0u, CR_FILL_MAX * CR_FILL_MS) == 0u &&
                  cr_fill_at(7u, 7u, 0u) == 7u && cr_fill_at(0u, 14u, 14u * CR_FILL_MS - 1u) == 13u;
            snprintf(name, sizeof name, "a wrap (25 -> none): darker frame by frame, none lit at %u ms", CR_FILL_MAX * CR_FILL_MS);
            check(name, ok && hi == 0u);
        }
    }

    {   /* the corner dial (a loop playing): red in its box in the top line, none in the panel's centre; the downbeat's
         * frame (5 px) redder than a plain one; the right text clear of it; its fraction alone: the top strip only */
        cr_screen_t s = CR_SCREENS[3];  /* Em in Key Mode */
        uint32_t box0, box1, mid0, mid1, x, y, rx1 = 0, i;
        uint16_t red = swap16(CR_RED);
#define DIAL_RED(x0, y0, x1, y1, n) do { n = 0; for (y = (y0); y < (y1); y++) for (x = (x0); x < (x1); x++) \
                                                n += host_screen[y * 240u + x] == red; } while (0)
        s.anim = 0;
        snprintf(s.right, sizeof s.right, "Dub 3.2");
        s.dial_on = 1;
        s.dial = 158;                   /* 62 % */
        render(&s, 0);
        write_ppm(dir, "loop_dial");
        DIAL_RED(218, 0, 240, 26, box0);
        DIAL_RED(30, 90, 210, 150, mid0);
        for (i = 0; i < nbox; i++)
            if (!strcmp(boxes[i].s, "Dub 3.2")) rx1 = (uint32_t)boxes[i].x1;
        check("the corner dial: red in its box, none in the panel's centre", box0 > 20u && mid0 == 0u);
        check("the corner dial: the right text moves left of it", rx1 && rx1 <= 219u);
        s.dial_pulse = 1;
        render(&s, 0);
        write_ppm(dir, "loop_dial_pulse");
        DIAL_RED(218, 0, 240, 26, box1);
        DIAL_RED(30, 90, 210, 150, mid1);
        check("the corner dial: the downbeat's frame has more red than a plain one", box1 > box0 && mid1 == 0u);
        s.dial_pulse = 0;
        cr_draw(&s, 0);
        s.dial = 170;
        cr_draw(&s, 0);
        check("the corner dial's fraction moves: only the top strip is blitted", cr_dc.drawn == 1 && cr_dc.blits == 1);
        {   /* the REC looks (docs/LOOPER.md): armed, no loop yet: the grey track and the REC dot, no arc; the first take:
             * the track red too; an overdub: the playing arc and the dot, which blinks (the top strip alone redrawn) */
            uint32_t armed, play, rec, od0, od1, c0, c1;
            s.dial_mode = CR_DIAL_ARMED;
            s.dial = 0;
            s.dial_dot = 1;
            render(&s, 0);
            write_ppm(dir, "loop_dial_armed");
            DIAL_RED(218, 0, 240, 26, armed);
            DIAL_RED(30, 90, 210, 150, mid0);
            c0 = host_screen[12u * 240u + 229u] == red;
            check("the dial armed: the REC dot red in its middle, no arc, the track grey", c0 && armed > 12u && armed < 40u && mid0 == 0u);
            s.dial_mode = CR_DIAL_PLAY;
            s.dial = 100;
            render(&s, 0);
            DIAL_RED(218, 0, 240, 26, play);
            s.dial_mode = CR_DIAL_REC;
            render(&s, 0);
            write_ppm(dir, "loop_dial_rec");
            DIAL_RED(218, 0, 240, 26, rec);
            check("the dial recording the first take: the track red as well (more red than the arc and the dot)", rec > play + 10u);
            s.dial_mode = CR_DIAL_OD;
            s.dial = 158;
            render(&s, 0);
            write_ppm(dir, "loop_dial_od");
            DIAL_RED(218, 0, 240, 26, od1);
            c1 = host_screen[12u * 240u + 229u] == red;
            s.dial_dot = 0;
            cr_draw(&s, 0);
            DIAL_RED(218, 0, 240, 26, od0);
            c0 = host_screen[12u * 240u + 229u] == red;
            check("the dial overdubbing: the dot blinks, only the top strip is drawn", c1 && !c0 && od1 > od0 + 12u &&
                  cr_dc.drawn == 1 && cr_dc.blits == 1);
            s.dial_mode = CR_DIAL_PLAY;
        }
        s.dial_on = 0;
        s.dial = 0;
        render(&s, 0);
        DIAL_RED(218, 0, 240, 26, box1);
        check("no dial: no red in its box", box1 == 0u);
#undef DIAL_RED
    }

    {   /* the ring's table (cr_draw.c cr_ring_draw): pixel for pixel the two cr_arc calls it replaces, over a pattern,
         * at every fraction and both colours */
        static uint16_t ref[240 * 40];
        uint32_t f, k, p, diffs = 0, maxd = 0;
        cr_screen_t s;
        cr_screen_clear(&s);
        s.ring_on = 1;
        for (f = 0; f <= 257u; f++)
            for (k = 0; k < CR_NSTRIP; k++) {
                s.ring = (uint16_t)(f > 256u ? 300u : f);
                s.ring_rec = (uint8_t)(f & 1u);
                s.ring_col = (uint8_t)(f % 3u ? CR_COL_BLUE : CR_COL_NONE);
                cv_begin(240u, CR_STRIP_H, T_BG);
                cv_oy = -(int32_t)(k * CR_STRIP_H);
                cr_clip_all();
                for (p = 0; p < 240u * CR_STRIP_H; p++) cv_px[p] = (uint16_t)(p * 2654435761u >> 16);
                cr_arc(120 * 16, 120 * 16, 113 * 16, 5 * 16, 0, 65536u, 0, 2 * 16, 6 * 16, T_LINE);
                if (s.ring)
                    cr_arc(120 * 16, 120 * 16, 113 * 16, 5 * 16, 49152u, s.ring >= 256u ? 65536u : (uint32_t)s.ring << 8,
                           0, 0, 0, s.ring_rec ? T_REC : cr_rgb(s.ring_col, CR_RED));
                memcpy(ref, cv_px, sizeof ref);
                for (p = 0; p < 240u * CR_STRIP_H; p++) cv_px[p] = (uint16_t)(p * 2654435761u >> 16);
                cr_ring_draw(&s);
                for (p = 0; p < 240u * CR_STRIP_H; p++)
                    if (cv_px[p] != ref[p]) {
                        uint32_t a = swap16(cv_px[p]), b = swap16(ref[p]), d;
                        d = (uint32_t)abs((int)(a >> 11) - (int)(b >> 11));
                        if ((uint32_t)abs((int)((a >> 5) & 63u) - (int)((b >> 5) & 63u)) > d)
                            d = (uint32_t)abs((int)((a >> 5) & 63u) - (int)((b >> 5) & 63u));
                        if ((uint32_t)abs((int)(a & 31u) - (int)(b & 31u)) > d) d = (uint32_t)abs((int)(a & 31u) - (int)(b & 31u));
                        diffs++;
                        if (d > maxd) maxd = d;
                    }
            }
        cv_oy = 0;
        snprintf(name, sizeof name, "the ring's table: %u band pixels (of %u), as cr_arc at 258 fractions (%u px differ, max %u)",
                 cr_ring.off[240], CR_RING_MAX, diffs, maxd);
        check(name, diffs == 0);
    }

    {   /* the ring's fraction alone moves: only the strips its tip crossed are drawn, and the screen is the full draw's */
        uint32_t k, f, same = 1, found = 0, most = 0, sum = 0, steps = 0;
        for (k = 0; k < CR_NSCREENS && !found; k++)
            if (CR_SCREENS[k].ring_on) {
                cr_screen_t s = CR_SCREENS[k];
                found = 1;
                s.anim = 0;
                s.ring = 0;
                render(&s, 0);
                for (f = 3; f <= 270u; f += 3) {
                    s.ring = (uint16_t)(f == 270u ? 0u : f);   /* (and back to 0: the loop starts again) */
                    cr_draw(&s, 0);
                    memcpy(a, host_screen, sizeof a);
                    if (f < 270u) {                            /* (the jump back to 0 clears the whole ring) */
                        if (cr_dc.blits > most) most = cr_dc.blits;
                        sum += cr_dc.blits;
                        steps++;
                    }
                    cr_draw_invalidate();
                    cr_draw(&s, 0);
                    same &= !memcmp(a, host_screen, sizeof a);
                }
            }
        snprintf(name, sizeof name, "the ring's fraction moves: the strips its tip crosses (avg %.1f, max %u), as a full draw%s",
                 (double)sum / (double)(steps ? steps : 1u), most, same ? "" : " (DIFFERS)");
        check(name, found && same && most <= 3u);
    }

    {   /* the scope: its samples are in the signature; one moved sample redraws, the same trace costs nothing */
        uint32_t k, found = 0;
        for (k = 0; k < CR_NSCREENS; k++)
            if (CR_SCREENS[k].kind == CR_K_SCOPE && CR_SCREENS[k].wave[60]) {
                cr_screen_t s = CR_SCREENS[k];
                found = 1;
                render(&s, 0);
                cr_draw(&s, 0);
                check("scope: the same trace again: nothing drawn", cr_dc.blits == 0);
                s.wave[120] = (int8_t)(s.wave[120] > 0 ? -100 : 100);
                cr_draw(&s, 0);
                check("scope: a sample moved: redrawn, only the strips it crosses blitted",
                      cr_dc.blits > 0 && cr_dc.blits < CR_NSTRIP);
                break;
            }
        check("scope: a sounding scope state is in the table", found);
    }

    {   /* the pictograms (FORMAT.md "cell glyphs"): ink inside their box and none outside it, and each changes with its
         * value the way its picture says */
        static const struct { uint8_t g; const char *name; } G[] = {
            {CR_G_ROOM, "room"}, {CR_G_MOON, "moon"}, {CR_G_ECHOES, "echoes"}, {CR_G_LFO, "lfo"}, {CR_G_CLIP, "clip"},
            {CR_G_MIX, "mix"}, {CR_G_GATE, "gate"}, {CR_G_RANGE, "range"},
            {CR_G_ARROW, "arrow"}, {CR_G_SHIFT, "shift"},
            /* the FX layer's (0.18, docs/FX.md) */
            {CR_G_TAIL, "tail"}, {CR_G_PRE, "pre"}, {CR_G_CUT, "cut"}, {CR_G_WIDTH, "width"}, {CR_G_SPREAD, "spread"},
            {CR_G_SYNC, "sync"}, {CR_G_PING, "ping"}};
        enum { BX = 20, BY = 2, BW = 48, BH = 36 };
        uint32_t gi, inside_all = 1;
        uint16_t ink = swap16(CR_NAMED[CR_COL_GREEN]), bg = swap16(T_BG);
#define PIC(gl_, p1, p2) do { cr_cell_t c_; memset(&c_, 0, sizeof c_); c_.flags = CR_CF_ON; c_.glyph = (gl_); \
        c_.pct = (uint8_t)(p1); c_.pct2 = (uint8_t)(p2); cr_cv_begin(); cv_oy = 0; cr_clip_all(); \
        cr_cglyph(&c_, BX * 16, BY * 16, BW * 16, BH * 16, CR_NAMED[CR_COL_GREEN]); } while (0)
#define PX(x, y) cv_px[(y) * 240u + (x)]
        for (gi = 0; gi < sizeof G / sizeof G[0]; gi++) {
            uint32_t in = 0, out = 0, x, y;
            PIC(G[gi].g, 128, 128);
            for (y = 0; y < CR_STRIP_H; y++)
                for (x = 0; x < 240u; x++) {
                    int inb = x >= BX && x < BX + BW && y >= BY && y < BY + BH;
                    if (PX(x, y) != bg) { if (inb) in++; else out++; }
                }
            if (!(in > 20u && out == 0u)) {
                inside_all = 0;
                fprintf(rep, "pictogram %s: %u px in its box, %u outside\n", G[gi].name, in, out);
            }
        }
        check("pictograms: each draws ink inside its 48 x 36 box and none outside it", inside_all);
        {   /* room: it opens up with pct (the owner 2026-10-11): on the middle row both the far wall's half width (from
             * the centre to its left edge) and the mouth's (from the box's left edge inwards to the first ink) grow */
            int32_t w[3], m[3], k, x;
            for (k = 0; k < 3; k++) {
                PIC(CR_G_ROOM, k * 127, 128);
                for (x = BX + BW / 2; x > BX && PX(x, BY + BH / 2) == bg; x--) {}
                w[k] = BX + BW / 2 - x;
                for (x = BX; x < BX + BW / 2 && PX(x, BY + BH / 2) == bg; x++) {}
                m[k] = BX + BW / 2 - x;
            }
            snprintf(name, sizeof name, "room: opens up as SIZE grows (half widths: the far wall %d %d %d px, the mouth %d %d %d px)",
                     w[0], w[1], w[2], m[0], m[1], m[2]);
            check(name, w[0] < w[1] && w[1] < w[2] && m[0] < m[1] && m[1] < m[2] && w[0] >= 4 && w[2] < m[2]);
        }
        {   /* moon: the lit (full colour) pixels grow with pct */
            uint32_t n[3], k, x, y;
            for (k = 0; k < 3; k++) {
                PIC(CR_G_MOON, k * 127, 128);
                for (n[k] = 0, y = BY; y < BY + BH; y++) for (x = BX; x < BX + BW; x++) n[k] += PX(x, y) == ink;
            }
            snprintf(name, sizeof name, "moon: the lit part grows with pct (%u %u %u px)", n[0], n[1], n[2]);
            check(name, n[0] < n[1] && n[1] < n[2]);
        }
        {   /* echoes: the bars (ink runs along the row just above the baseline): fewer as the spacing grows */
            uint32_t n[2], k, x, on;
            for (k = 0; k < 2; k++) {
                PIC(CR_G_ECHOES, k * 255, 255);
                for (n[k] = on = 0, x = BX; x < BX + BW; x++) {
                    int i = PX(x, BY + BH - 4) != bg;
                    n[k] += i && !on;
                    on = (uint32_t)i;
                }
            }
            snprintf(name, sizeof name, "echoes: the repeats spread with TIME (%u bars at pct 0, %u at 1)", n[0], n[1]);
            check(name, n[0] > n[1] && n[1] >= 2u);
        }
        {   /* lfo: the zero crossings (ink runs on the centre row) grow with the rate */
            uint32_t n[2], k, x, on;
            for (k = 0; k < 2; k++) {
                PIC(CR_G_LFO, k * 255, 255);
                for (n[k] = on = 0, x = BX; x < BX + BW; x++) {
                    int i = PX(x, BY + BH / 2) != bg;
                    n[k] += i && !on;
                    on = (uint32_t)i;
                }
            }
            snprintf(name, sizeof name, "lfo: the crossings grow with RATE (%u at pct 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 4u);
        }
        {   /* clip: the peak flattens: the top stroke's row (fully inked pixels: not the dashed clip line's half pixels)
             * widens from a point (pct 0: a clean sine) to most of the half cycle (pct 1: nearly square) */
            uint32_t n[2], k, x;
            for (k = 0; k < 2; k++) {
                PIC(CR_G_CLIP, k * 255, 128);
                for (n[k] = 0, x = BX; x < BX + BW / 2; x++) n[k] += PX(x, BY + 2) == ink;
            }
            snprintf(name, sizeof name, "clip: the peak flattens with the drive (%u px wide at pct 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > 2u * n[0] + 8u);
        }
        {   /* mix: the wet square fills; gate / range: wider; shift: the square climbs; arrow: four pictures */
            uint32_t n[2], k, x, y, cyy[2];
            static uint16_t ar[4][240 * 40];
            for (k = 0; k < 2; k++) {
                PIC(CR_G_MIX, k * 255, 128);
                for (n[k] = 0, y = BY; y < BY + BH; y++) for (x = BX; x < BX + BW; x++) n[k] += PX(x, y) == ink;
            }
            snprintf(name, sizeof name, "mix: the wet square fills with the amount (%u px at 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 200u);
            for (k = 0; k < 2; k++) {
                PIC(CR_G_GATE, k * 255, 128);
                for (n[k] = 0, x = BX; x < BX + BW; x++) n[k] += PX(x, BY + 2 + 4) != bg;
            }
            snprintf(name, sizeof name, "gate: the pulse widens with pct (%u px at 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 20u);
            for (k = 0; k < 2; k++) {
                PIC(CR_G_RANGE, k * 255, 128);
                for (n[k] = 0, y = BY; y < BY + BH; y++) for (x = BX; x < BX + BW; x++) n[k] += PX(x, y) == ink;
            }
            snprintf(name, sizeof name, "range: the thick segment grows with pct (%u px at 0, %u at 1)", n[0], n[1]);
            check(name, n[1] > n[0] + 60u);
            for (k = 0; k < 2; k++) {
                uint32_t sy = 0, c = 0;
                PIC(CR_G_SHIFT, k * 255, 128);
                for (y = BY; y < BY + BH; y++) for (x = BX + BW / 2 - 1; x <= BX + BW / 2 + 1; x++) if (PX(x, y) == ink) sy += y, c++;
                cyy[k] = c ? sy / c : 0;
            }
            snprintf(name, sizeof name, "shift: the square climbs the staff with pct (centre row %u at 0, %u at 1)", cyy[0], cyy[1]);
            check(name, cyy[1] + 10u < cyy[0]);
            for (k = 0; k < 4; k++) {
                PIC(CR_G_ARROW, k * 64 + 20, 128);
                memcpy(ar[k], cv_px, sizeof ar[k]);
            }
            check("arrow: up, down, up and down, random: four different pictures",
                  memcmp(ar[0], ar[1], sizeof ar[0]) && memcmp(ar[1], ar[2], sizeof ar[0]) && memcmp(ar[2], ar[3], sizeof ar[0]) &&
                  memcmp(ar[0], ar[2], sizeof ar[0]) && memcmp(ar[0], ar[3], sizeof ar[0]));
        }
#undef PX
#undef PIC
    }

    {   /* the knob row: the hot cell's value on a block of its colour; a cell or the hot cell changing composes only the
         * row's strips */
        uint32_t k, found = 0;
        for (k = 0; k < CR_NSCREENS && !found; k++)
            if (CR_SCREENS[k].kind == CR_K_KNOBROW && !CR_SCREENS[k].hot_r) {
                cr_screen_t s = CR_SCREENS[k];
                uint32_t n0, n1, x, y, ry = (uint32_t)cr_kr_rowy(&s);
                uint16_t orange = swap16(CR_NAMED[CR_COL_ORANGE]);
                found = 1;
                s.anim = 0;
                render(&s, 0);
                for (n0 = 0, y = ry + 50u; y < ry + 70u; y++) for (x = 64; x < 116; x++) n0 += host_screen[y * 240u + x] == orange;
                s.hot_r = 1;
                s.hot_c = 1;
                cr_draw(&s, 0);
                check("knob row: the hot cell composes only the row's strips (2)", cr_dc.drawn == 2u);
                for (n1 = 0, y = ry + 50u; y < ry + 70u; y++) for (x = 64; x < 116; x++) n1 += host_screen[y * 240u + x] == orange;
                write_ppm(dir, "knobrow_hot");
                snprintf(name, sizeof name, "knob row: the hot cell's value on a filled block (%u orange px, %u not hot)", n1, n0);
                check(name, n1 > n0 + 500u);
                memcpy(a, host_screen, sizeof a);
                snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "62");
                s.cell[0][1].pct = 140;
                cr_draw(&s, 0);
                check("knob row: a cell's value changes: only the row's strips composed", cr_dc.drawn == 2u);
                memcpy(a, host_screen, sizeof a);
                cr_draw_invalidate();
                cr_draw(&s, 0);
                check("knob row: the partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
                {   /* PERF / BASS: a text-only cell (Behaviour, Swing) and a null cell (a mode with fewer knobs) */
                    uint16_t bg = host_screen[(ry - 4u) * 240u + 2u];
                    uint32_t ink0 = 0, ink2 = 0;
                    s.hot_r = 0;
                    s.cell[0][0].flags = CR_CF_ON;
                    s.cell[0][0].glyph = CR_G_NONE;
                    snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Behaviour");
                    snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "Chords");
                    memset(&s.cell[0][2], 0, sizeof s.cell[0][2]);
                    cr_draw_invalidate();
                    cr_draw(&s, 0);
                    write_ppm(dir, "knobrow_text_null");
                    for (y = ry; y < ry + 75u && y < 240u; y++)
                        for (x = 0; x < 60u; x++) ink0 += host_screen[y * 240u + x] != bg;
                    for (y = ry; y < ry + 75u && y < 240u; y++)
                        for (x = 124; x < 176u; x++) ink2 += host_screen[y * 240u + x] != bg;
                    snprintf(name, sizeof name, "knob row: a text-only cell (%u ink px) and a null cell (%u: its dash) render",
                             ink0, ink2);
                    check(name, ink0 > 100u && ink2 > 0u && ink2 < ink0);
                }
            }
        check("knob row: a knob row state is in the table", found);
    }

    {   /* the knob row's keyboard band (KEY): the lit tonic yellow in the band, the cells under it; a dim cell grey */
        cr_screen_t s;
        uint32_t x, y, yb = 0, ink = 0, grey = 0, grey0 = 0, ry, k;
        uint16_t yel = swap16(CR_NAMED[CR_COL_YELLOW]), gr = swap16(CR_NAMED[CR_COL_GREY]), bg;
        cr_screen_clear(&s);
        s.kind = CR_K_KNOBROW;
        s.kr_band = 1;
        s.header = 1;
        s.col = CR_COL_YELLOW;
        snprintf(s.label, sizeof s.label, "key");
        snprintf(s.footer, sizeof s.footer, "MIN: minor");
        s.lit = 1u << 7;                              /* C4 */
        s.lit_col[7] = CR_COL_YELLOW;
        for (k = 0; k < 4u; k++) {
            s.cell[0][k].flags = CR_CF_ON;
            snprintf(s.cell[0][k].label, sizeof s.cell[0][k].label, "K%u", k + 1u);
            snprintf(s.cell[0][k].value, sizeof s.cell[0][k].value, "C");
        }
        s.n_rows = 1;
        ry = (uint32_t)cr_kr_rowy(&s);
        render(&s, 0);
        bg = host_screen[(ry - 4u) * 240u + 2u];
        for (y = 30; y < ry; y++) for (x = 0; x < 240u; x++) yb += host_screen[y * 240u + x] == yel;
        for (y = ry; y < 240u && y < ry + 72u; y++) for (x = 0; x < 240u; x++) ink += host_screen[y * 240u + x] != bg;
        write_ppm(dir, "knobrow_keyboard");
        snprintf(name, sizeof name, "knob row, keyboard band: the tonic yellow in the band (%u px), the row under it (%u ink)", yb, ink);
        check(name, yb > 200u && ink > 200u);
        for (y = ry; y < ry + 72u && y < 240u; y++) for (x = 0; x < 60u; x++) grey0 += host_screen[y * 240u + x] == gr;
        s.cell[0][0].flags |= CR_CF_DIM;
        render(&s, 0);
        for (y = ry; y < ry + 72u && y < 240u; y++) for (x = 0; x < 60u; x++) grey += host_screen[y * 240u + x] == gr;
        write_ppm(dir, "knobrow_dim");
        snprintf(name, sizeof name, "knob row: a dim cell is drawn grey (%u grey px, %u not dim)", grey, grey0);
        check(name, grey > grey0 + 30u);
    }

    {   /* the KEY layer's two pages (0.17): page 1 with Single = "Split Bass" (the longest value: inside its cell),
         * page 2: Key plays / Keyboard on KNOB 1 / 2, KNOB 3 / 4 free (a dash each); the label carries the page,
         * the footer "SELECT: page" */
        static const char *const LB[4] = {"Tonic", "Scale", "Transpose", "Single"};
        static const char *const VA[4] = {"D", "Minor", "+0", "Full"};
        static uint16_t one[240u * 240u];
        cr_screen_t s;
        uint32_t x, y, ry, k, out = 0, in = 0, c01 = 0, c23 = 0;
        uint16_t bg;
        cr_screen_clear(&s);
        s.kind = CR_K_KNOBROW;
        s.kr_band = 1;
        s.header = 1;
        s.orient = 1;
        s.col = CR_COL_YELLOW;
        s.title_col = CR_COL_WHITE;
        snprintf(s.label, sizeof s.label, "key \267 keys pick \267 1/2");
        snprintf(s.footer, sizeof s.footer, "SELECT: page \267 MAJ MIN: scale \267 GLO: play");
        s.lit = 1u << 9;
        s.lit_col[9] = CR_COL_YELLOW;
        for (k = 0; k < 4u; k++) {
            s.cell[0][k].flags = CR_CF_ON;
            snprintf(s.cell[0][k].label, sizeof s.cell[0][k].label, "%s", LB[k]);
            snprintf(s.cell[0][k].value, sizeof s.cell[0][k].value, "%s", VA[k]);
        }
        s.n_rows = 1;
        ry = (uint32_t)cr_kr_rowy(&s);
        lint("key_page1", &s);
        render(&s, 0);
        memcpy(one, host_screen, sizeof one);
        snprintf(s.cell[0][3].value, sizeof s.cell[0][3].value, "Split Bass");
        lint("key_page1_splitbass", &s);
        render(&s, 0);
        write_ppm(dir, "key_page1_splitbass");
        for (y = 0; y < 240u; y++)
            for (x = 0; x < 240u; x++) {
                if (host_screen[y * 240u + x] == one[y * 240u + x]) continue;
                if (x >= 180u && y >= ry) in++; else out++;
            }
        snprintf(name, sizeof name, "KEY page 1: \"Split Bass\" changes its own cell only (%u px in it, %u outside)", in, out);
        check(name, in > 40u && out == 0u && sizeof s.cell[0][3].value > strlen("Split Bass"));
        for (k = 0; k < 4u; k++)
            memset(&s.cell[0][k], 0, sizeof s.cell[0][k]);
        s.cell[0][0].flags = s.cell[0][1].flags = CR_CF_ON;
        snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Key plays");
        snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "Notes");
        snprintf(s.cell[0][1].label, sizeof s.cell[0][1].label, "Keyboard");
        snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "Chords");
        snprintf(s.label, sizeof s.label, "key \267 keys pick \267 2/2");
        lint("key_page2", &s);
        render(&s, 0);
        write_ppm(dir, "key_page2");
        bg = host_screen[(ry - 4u) * 240u + 2u];
        for (y = ry; y < 240u && y < ry + 72u; y++)
            for (x = 0; x < 240u; x++)
                if (host_screen[y * 240u + x] != bg) { if (x < 120u) c01++; else c23++; }
        snprintf(name, sizeof name, "KEY page 2: Key plays / Keyboard drawn (%u ink px), KNOB 3 / 4 a dash each (%u)", c01, c23);
        check(name, c01 > 200u && c23 > 0u && c23 * 4u < c01);
    }

    {   /* QUAD's primitives (docs/QUAD.md, design/choralroot-fm1-quad-mockups.json): the algo band, the ade2 band, the
         * filter's base-width window, the harm / detune / lfowave glyphs, big / ratio / span-2 cells, the bipolar knob */
        uint16_t bg = swap16(T_BG), orange = swap16(CR_NAMED[CR_COL_ORANGE]), blue = swap16(CR_NAMED[CR_COL_BLUE]);
        uint16_t mid = swap16(T_MID), line = swap16(T_LINE);
        int32_t ia = -1, id = -1, ib = -1, ir = -1;
        uint32_t x, y, k;
#define HS(x_, y_) host_screen[(uint32_t)(y_) * 240u + (uint32_t)(x_)]
#define COUNT(n_, x0_, y0_, x1_, y1_, cond_) do { n_ = 0; for (y = (y0_); y < (uint32_t)(y1_); y++) \
            for (x = (x0_); x < (uint32_t)(x1_); x++) n_ += (cond_); } while (0)
        for (k = 0; k < CR_NSCREENS; k++) {
            if (CR_SCREENS[k].kind != CR_K_EDIT8) continue;
            if (CR_SCREENS[k].wide == CR_W_ALGO && ia < 0) ia = (int32_t)k;
            if (CR_SCREENS[k].wide == CR_W_ADE2 && id < 0) id = (int32_t)k;
            if (CR_SCREENS[k].wide == CR_W_FILTER && CR_SCREENS[k].wv[4] && ib < 0) ib = (int32_t)k;
            if (CR_SCREENS[k].cell[0][3].glyph == CR_G_RATIO && ir < 0) ir = (int32_t)k;
        }
        check("QUAD: the algo, ade2, base-width and ratio states are in the table", ia >= 0 && id >= 0 && ib >= 0 && ir >= 0);
        if (ia >= 0) {   /* algo: the manual's eight diagrams (cr_algo_geom): four boxes where the geometry puts them
                          * (outline columns inked, the letter inside), no two boxes, loops, side lines or labels
                          * overlapping, every algorithm rendered (algo_<n>.ppm) and linted, 8 different pictures */
            static uint16_t pic[8][240 * 100];
            cr_screen_t s = CR_SCREENS[ia];
            uint32_t n, boxes4 = 1, differ = 1, apart = 1, j, thin, thick;
            for (n = 1; n <= 8u; n++) {
                uint32_t found = 0;
                int32_t ox[4], oy[4], sx[4], lx[2], bx[6][4], nb = 0, q, r2;
                char an[16];
                const cr_algo_t *al = &CR_ALGOS[n - 1];
                s.wv[0] = (uint8_t)n;
                s.anim = 0;
                render(&s, 0);
                snprintf(an, sizeof an, "algo_%u", n);
                lint(an, &s);
                write_ppm(dir, an);
                memcpy(pic[n - 1], host_screen + 22u * 240u, sizeof pic[0]);
                cr_algo_geom(n, ox, oy, sx, lx);
                for (k = 0; k < 4u; k++) {
                    uint32_t cx = (uint32_t)(ox[k] / 16), cy = (uint32_t)(oy[k] / 16), el, er, in;
                    COUNT(el, cx - 8u, cy - 6u, cx - 6u, cy + 7u, HS(x, y) != bg);
                    COUNT(er, cx + 6u, cy - 6u, cx + 8u, cy + 7u, HS(x, y) != bg);
                    COUNT(in, cx - 4u, cy - 4u, cx + 5u, cy + 5u, HS(x, y) != bg);
                    found += el >= 13u && er >= 13u && in >= 8u;
                    bx[nb][0] = ox[k] / 16 - 8; bx[nb][1] = oy[k] / 16 - 8;   /* (a box and its outline) */
                    bx[nb][2] = ox[k] / 16 + 8; bx[nb][3] = oy[k] / 16 + 8; nb++;
                    if (sx[k] != ox[k] && !(((al->x | al->y) >> k) & 1u)) apart = 0;
                    for (q = 0; q < 4; q++)          /* a side line passes no other box */
                        if (q != (int32_t)k && sx[k] != ox[k] && oy[q] > oy[k] && sx[k] / 16 + 1 >= ox[q] / 16 - 8 &&
                            sx[k] / 16 - 1 <= ox[q] / 16 + 8) {
                            fprintf(rep, "algo %u: %s's side line crosses %s\n", n, CR_OPS[k], CR_OPS[q]);
                            apart = 0;
                        }
                }
                k = al->fb;                          /* the loop's box: up and left of its operator */
                bx[nb][0] = ox[k] / 16 - 13; bx[nb][1] = oy[k] / 16 - 12 < 24 ? 24 : oy[k] / 16 - 12; bx[nb][2] = ox[k] / 16 - 9; bx[nb][3] = oy[k] / 16 - 8; nb++;
                for (j = 0; j < 2u; j++) {           /* the labels X / Y: 8 x 10 px over the baseline */
                    bx[nb][0] = lx[j] / 16 - 4; bx[nb][1] = 24 + 92 - 9; bx[nb][2] = lx[j] / 16 + 4; bx[nb][3] = 24 + 92; nb++;
                }
                for (q = 0; q < nb; q++) {
                    if (bx[q][0] < 52 || bx[q][2] > 236 || bx[q][1] < 24 || bx[q][3] > 24 + 96) {
                        fprintf(rep, "algo %u: item %d off the band\n", n, (int)q);
                        apart = 0;
                    }
                    for (r2 = q + 1; r2 < nb; r2++)
                        if (bx[q][0] < bx[r2][2] && bx[r2][0] < bx[q][2] && bx[q][1] < bx[r2][3] && bx[r2][1] < bx[q][3]) {
                            fprintf(rep, "algo %u: items %d and %d overlap\n", n, (int)q, (int)r2);
                            apart = 0;
                        }
                }
                if (found != 4u) {
                    boxes4 = 0;
                    fprintf(rep, "algo %u: %u of 4 boxes found\n", n, found);
                }
            }
            for (n = 0; n < 8u; n++)
                for (j = n + 1u; j < 8u; j++) differ &= memcmp(pic[n], pic[j], sizeof pic[0]) != 0;
            check("algo band: four operator boxes (outlines and letters) in every algorithm", boxes4);
            check("algo band: the eight algorithms draw eight different diagrams", differ);
            check("algo band: no boxes, loops, side lines or labels overlap; all on the band (8 algorithms)", apart);
            {                                        /* algo 3: X takes C (dotted) and B2 (dotted), Y B1 (dotted);
                                                      * algo 7: X C dotted, A solid: count the ink on the lines */
                int32_t ox[4], oy[4], sx[4], lx[2];
                uint32_t dots, solid;
                s.wv[0] = 7; s.wv[1] = 0;
                render(&s, 0);
                cr_algo_geom(7, ox, oy, sx, lx);
                COUNT(dots, (uint32_t)(ox[0] / 16) - 1u, (uint32_t)(oy[0] / 16) + 9u, (uint32_t)(ox[0] / 16) + 2u, 24u + 82u, HS(x, y) != bg);
                COUNT(solid, (uint32_t)(sx[1] / 16) - 1u, (uint32_t)(oy[0] / 16) + 9u, (uint32_t)(sx[1] / 16) + 2u, 24u + 82u, HS(x, y) != bg);
                snprintf(name, sizeof name, "algo band: a direct carrier's line dotted, an enveloped one solid (%u < %u ink px)", dots, solid);
                check(name, dots + 3u < solid);
            }
            s.wv[0] = 3;                                         /* A's loop over its top left */
            {
                int32_t ox[4], oy[4], sx[4], lx[2];
                uint32_t lx0, ly0;
                cr_algo_geom(3, ox, oy, sx, lx);
                lx0 = (uint32_t)(ox[1] / 16) - 13u; ly0 = (uint32_t)(oy[1] / 16) - 12u;
                s.wv[1] = 0;
                render(&s, 0);
                COUNT(thin, lx0, ly0, lx0 + 12u, ly0 + 9u, HS(x, y) != bg);
                s.wv[1] = 255;
                render(&s, 0);
                COUNT(thick, lx0, ly0, lx0 + 12u, ly0 + 9u, HS(x, y) != bg);
            }
            snprintf(name, sizeof name, "algo band: the feedback loop's stroke grows with fdbk (%u ink px at 0, %u at 255)", thin, thick);
            check(name, thick > thin + 10u);
            s.wv[1] = 77;
            render(&s, 0);
            s.wv[0] = 5;
            cr_draw(&s, 0);
            snprintf(name, sizeof name, "algo band: a new algorithm composes the band's strips only (%u)", cr_dc.drawn);
            check(name, cr_dc.drawn == 4u);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("algo band: the partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
        }
        if (id >= 0) {   /* ade2: a curve in each half, a level bar at each half's right, filled to its level */
            cr_screen_t s = CR_SCREENS[id];
            uint32_t ca, cb, ba, bb, fa, fb2;
            render(&s, 0);
            COUNT(ca, 10u, 42u, 100u, 110u, HS(x, y) != bg);
            COUNT(cb, 130u, 42u, 220u, 110u, HS(x, y) != bg);
            COUNT(ba, 108u, 42u, 109u, 108u, HS(x, y) != bg);
            COUNT(bb, 228u, 42u, 229u, 108u, HS(x, y) != bg);
            COUNT(fa, 109u, 42u, 110u, 108u, HS(x, y) == orange);        /* A: a segment lit (decay): its colour */
            COUNT(fb2, 229u, 42u, 230u, 108u, HS(x, y) == mid);          /* B: MID */
            snprintf(name, sizeof name, "ade2 band: two curves (%u, %u ink px), two bars (%u, %u rows), filled to A %u / B %u rows",
                     ca, cb, ba, bb, fa, fb2);
            check(name, ca > 150u && cb > 150u && ba >= 60u && bb >= 60u && fa > fb2 + 5u && fb2 > 20u);
        }
        if (ib >= 0) {   /* the base-width window: two dashed orange edges and the dim fill between them; none when off */
            cr_screen_t s = CR_SCREENS[ib];
            uint32_t bx = (128u + 3584u * s.wv[5] / 255u) >> 4, e = s.wv[5] + s.wv[6] > 255u ? 255u : s.wv[5] + s.wv[6];
            uint32_t ex = (128u + 3584u * e / 255u) >> 4, eb, ee, eb0, fill = swap16(ux_mix(T_BG, CR_NAMED[CR_COL_ORANGE], 12));
            int fill_on, fill_off;
            render(&s, 0);
            COUNT(eb, bx, 28u, bx + 1u, 114u, HS(x, y) != bg);
            COUNT(ee, ex, 28u, ex + 1u, 114u, HS(x, y) != bg);
            fill_on = HS(bx + 6u, 100u) == fill;
            s.wv[4] = 0;
            render(&s, 0);
            COUNT(eb0, bx, 28u, bx + 1u, 114u, HS(x, y) != bg);
            fill_off = HS(bx + 6u, 100u) == bg;
            snprintf(name, sizeof name, "filter band, base-width window: dashed edges at %u and %u (%u, %u px; %u off), the dim fill",
                     bx, ex, eb, ee, eb0);
            check(name, eb >= 35u && ee >= 35u && eb0 < 12u && fill_on && fill_off);
        }
        if (ir >= 0) {   /* ratio: the numerator over the denominator in its cell */
            cr_screen_t s = CR_SCREENS[ir];
            int32_t yn = -1, yd = -1, xn = 0;
            render(&s, 0);
            for (k = 0; k < nbox; k++) {
                if (!strcmp(boxes[k].s, "0.50")) yn = boxes[k].y1, xn = boxes[k].x0;
                if (!strcmp(boxes[k].s, "1.00") && boxes[k].x0 >= 180) yd = boxes[k].y0;
            }
            check("ratio cell: the numerator above the denominator, in the fourth column", yn > 0 && yd > yn && xn >= 180);
            s.cell[0][3].pct = 64;                               /* 16.00 / 16.00: both whole, two decimals each */
            s.cell[0][3].pct2 = 64;
            render(&s, 0);
            for (yn = 0, k = 0; k < nbox; k++)
                yn += !strcmp(boxes[k].s, "16.00") && boxes[k].x0 >= 180;
            s.cell[0][3].pct = 1;                                /* 0.25 / 1.00 */
            s.cell[0][3].pct2 = 4;
            render(&s, 0);
            for (yd = 0, k = 0; k < nbox; k++)
                yd += (!strcmp(boxes[k].s, "0.25") || !strcmp(boxes[k].s, "1.00")) && boxes[k].x0 >= 180;
            snprintf(name, sizeof name, "ratio cell: 16.00 over 16.00 (%d), 0.25 over 1.00 (%d): the parts uncut", yn, yd);
            check(name, yn == 2 && yd == 2);
        }
        {   /* the new glyphs: ink, and a change with pct (and the wave) */
            static uint16_t g0[240 * 40];
            static const uint8_t NG[3] = {CR_G_HARM, CR_G_DETUNE, CR_G_LFOWAVE};
            int ok = 1;
            uint32_t gi, w, w2, ink0, ink1;
            cr_cell_t c;
#define GL(gl_, p1_, wv_, fl_) do { memset(&c, 0, sizeof c); c.flags = (uint16_t)(CR_CF_ON | (fl_)); c.glyph = (gl_); \
            c.pct = (uint8_t)(p1_); c.wave = (uint8_t)(wv_); cr_cv_begin(); cv_oy = 0; cr_clip_all(); \
            cr_cglyph(&c, 20 * 16, 2 * 16, 48 * 16, 36 * 16, CR_NAMED[CR_COL_GREEN]); } while (0)
            for (gi = 0; gi < 3u; gi++) {
                uint32_t out;
                GL(NG[gi], 128, 0, 0);
                COUNT(ink0, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                COUNT(out, 0u, 0u, 240u, 40u, cv_px[y * 240u + x] != bg && !(x >= 20u && x < 68u && y >= 2u && y < 38u));
                memcpy(g0, cv_px, sizeof g0);
                GL(NG[gi], 250, 0, 0);
                COUNT(ink1, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                if (ink0 < 40u || ink1 < 40u || out || !memcmp(g0, cv_px, sizeof g0)) {
                    ok = 0;
                    fprintf(rep, "glyph %u: ink %u / %u, %u outside, %s with pct\n", NG[gi], ink0, ink1, out,
                            memcmp(g0, cv_px, sizeof g0) ? "changes" : "SAME");
                }
            }
            check("harm, detune, lfowave: ink in their box, none outside, a change with pct", ok);
            {   /* the FX layer's glyphs of 0.18 (docs/FX.md): ink in the box at both ends of the value, none outside,
                 * and the picture changes with it (pct; SPREAD with pct2 too) */
                static const struct { uint8_t g; const char *name; } FG[7] = {
                    {CR_G_TAIL, "tail"}, {CR_G_PRE, "pre"}, {CR_G_CUT, "cut"}, {CR_G_WIDTH, "width"}, {CR_G_SPREAD, "spread"},
                    {CR_G_SYNC, "sync"}, {CR_G_PING, "ping"}};
                int fok = 1;
                for (gi = 0; gi < 7u; gi++) {
                    uint32_t out, out1, same2 = 0;
                    GL(FG[gi].g, 0, 0, 0);
                    COUNT(ink0, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                    COUNT(out, 0u, 0u, 240u, 40u, cv_px[y * 240u + x] != bg && !(x >= 20u && x < 68u && y >= 2u && y < 38u));
                    memcpy(g0, cv_px, sizeof g0);
                    GL(FG[gi].g, 255, 0, 0);
                    COUNT(ink1, 20u, 2u, 68u, 38u, cv_px[y * 240u + x] != bg);
                    COUNT(out1, 0u, 0u, 240u, 40u, cv_px[y * 240u + x] != bg && !(x >= 20u && x < 68u && y >= 2u && y < 38u));
                    if (ink0 < 40u || ink1 < 40u || out || out1 || !memcmp(g0, cv_px, sizeof g0)) {
                        fok = 0;
                        fprintf(rep, "fx glyph %s: ink %u / %u, %u / %u outside, %s with pct\n", FG[gi].name, ink0, ink1, out, out1,
                                memcmp(g0, cv_px, sizeof g0) ? "changes" : "SAME");
                    }
                    if (FG[gi].g == CR_G_SPREAD) {              /* the lower row closes up as the spread grows */
                        GL(CR_G_SPREAD, 128, 0, 0);
                        memcpy(g0, cv_px, sizeof g0);
                        memset(&c, 0, sizeof c);
                        c.flags = CR_CF_ON; c.glyph = CR_G_SPREAD; c.pct = 128; c.pct2 = 255;
                        cr_cv_begin(); cv_oy = 0; cr_clip_all();
                        cr_cglyph(&c, 20 * 16, 2 * 16, 48 * 16, 36 * 16, CR_NAMED[CR_COL_GREEN]);
                        same2 = !memcmp(g0, cv_px, sizeof g0);
                        if (same2) { fok = 0; fprintf(rep, "fx glyph spread: the same at pct2 0 and 1\n"); }
                    }
                }
                check("tail, pre, cut, width, spread, sync, ping: ink in their box at both ends, none outside, a change with the value", fok);
            }
            GL(CR_G_DETUNE, 0, 0, 0);                            /* detune at 0: one flat line (2 px + its edges) */
            for (y = 0, w = 0; y < 40u; y++) {
                uint32_t r = 0;
                for (x = 20; x < 68u; x++) r += cv_px[y * 240u + x] != bg;
                w += r > 0u;
            }
            GL(CR_G_DETUNE, 255, 0, 0);
            for (y = 0, w2 = 0; y < 40u; y++) {
                uint32_t r = 0;
                for (x = 20; x < 68u; x++) r += cv_px[y * 240u + x] != bg;
                w2 += r > 0u;
            }
            snprintf(name, sizeof name, "detune: flat at 0 (%u rows inked), broken up at 1 (%u rows)", w, w2);
            check(name, w <= 4u && w2 >= 12u);
            {   /* lfowave: seven different waves; the phase slides the shape; a span-2 cell draws two cycles */
                static uint16_t wv7[7][240 * 40];
                uint32_t i2, j2, diff = 1;
                for (i2 = 0; i2 < 7u; i2++) {
                    GL(CR_G_LFOWAVE, 0, i2, 0);
                    memcpy(wv7[i2], cv_px, sizeof wv7[0]);
                }
                for (i2 = 0; i2 < 7u; i2++)
                    for (j2 = i2 + 1u; j2 < 7u; j2++) diff &= memcmp(wv7[i2], wv7[j2], sizeof wv7[0]) != 0;
                check("lfowave: tri, sine, square, saw, ramp, exp, random: seven different pictures", diff);
                GL(CR_G_LFOWAVE, 64, CR_LW_SAW, 0);
                check("lfowave: the start phase slides the shape", memcmp(wv7[CR_LW_SAW], cv_px, sizeof wv7[0]) != 0);
                {   /* square, one cycle vs two: the vertical edges (columns inked over most of the height) double */
                    uint32_t e1 = 0, e2 = 0, r, xc;
                    GL(CR_G_LFOWAVE, 0, CR_LW_SQUARE, 0);
                    for (xc = 26; xc < 68u; xc++) { COUNT(r, xc, 6u, xc + 1u, 34u, cv_px[y * 240u + x] != bg); e1 += r >= 24u; }
                    GL(CR_G_LFOWAVE, 0, CR_LW_SQUARE, CR_CF_SPAN2);
                    for (xc = 26; xc < 68u; xc++) { COUNT(r, xc, 6u, xc + 1u, 34u, cv_px[y * 240u + x] != bg); e2 += r >= 24u; }
                    snprintf(name, sizeof name, "lfowave: a span-2 cell draws two cycles (%u edge columns, %u with one)", e2, e1);
                    check(name, e2 > e1);
                }
            }
#undef GL
        }
        {   /* cells: big (no bar, a larger value), span 2 (two columns, the cells after it shift), the bipolar knob */
            cr_screen_t s;
            int32_t h0 = 0, h1 = 0, xt = -1, xd = -1;
            uint32_t bar0, bar1, gl, gr, run, lb, rb, lb2, rb2;
            cr_screen_clear(&s);
            s.kind = CR_K_EDIT8;
            s.n_rows = 1;
            snprintf(s.title, sizeof s.title, "QUAD TEST");
            s.cell[0][0].flags = CR_CF_ON | CR_CF_PCT;
            s.cell[0][0].pct = 128;
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Algo");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "3");
            render(&s, 0);
            for (k = 0; k < nbox; k++) if (!strcmp(boxes[k].s, "3")) h0 = boxes[k].y1 - boxes[k].y0;
            COUNT(bar0, 8u, 30u + 26u + 17u - 3u, 52u, 30u + 26u + 17u + 3u, HS(x, y) == line || HS(x, y) == blue);
            s.cell[0][0].flags |= CR_CF_BIG;
            render(&s, 0);
            write_ppm(dir, "quad_big_cell");
            for (k = 0; k < nbox; k++) if (!strcmp(boxes[k].s, "3")) h1 = boxes[k].y1 - boxes[k].y0;
            COUNT(bar1, 8u, 30u + 26u + 17u - 3u, 52u, 30u + 26u + 17u + 3u, HS(x, y) == line);
            snprintf(name, sizeof name, "big cell: no bar (%u px, %u without big), a larger value (%d px tall, %d)", bar1, bar0, h1, h0);
            check(name, bar0 > 60u && bar1 == 0u && h1 > h0 + 3);
            memset(s.cell, 0, sizeof s.cell);
            s.cell[0][0].flags = CR_CF_ON | CR_CF_SPAN2 | CR_CF_PCT;
            s.cell[0][0].glyph = CR_G_LFOWAVE;
            s.cell[0][0].wave = CR_LW_TRI;
            s.cell[0][0].pct = 64;
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Wave \267 Phase");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "Tri \267 90");
            s.cell[0][1].flags = CR_CF_ON;
            snprintf(s.cell[0][1].label, sizeof s.cell[0][1].label, "Trig");
            snprintf(s.cell[0][1].value, sizeof s.cell[0][1].value, "Free");
            s.cell[0][2].flags = CR_CF_ON | CR_CF_PCT | CR_CF_BIP;
            s.cell[0][2].pct = 188;
            snprintf(s.cell[0][2].label, sizeof s.cell[0][2].label, "Depth");
            snprintf(s.cell[0][2].value, sizeof s.cell[0][2].value, "+30");
            render(&s, 0);
            write_ppm(dir, "quad_span_cell");
            for (k = 0; k < nbox; k++) {
                if (!strcmp(boxes[k].s, "Trig")) xt = boxes[k].x0;
                if (!strcmp(boxes[k].s, "Depth")) xd = boxes[k].x0;
            }
            COUNT(gl, 6u, 56u, 58u, 90u, HS(x, y) == blue);
            COUNT(gr, 62u, 56u, 114u, 90u, HS(x, y) == blue);
            COUNT(run, 4u, 128u, 116u, 130u, HS(x, y) == blue);
            snprintf(name, sizeof name, "span cell: the wave over both columns (%u, %u px), its bar across (%u px), Trig at %d, Depth at %d",
                     gl, gr, run, xt, xd);
            check(name, gl > 40u && gr > 40u && run >= 220u && xt >= 120 && xt < 180 && xd >= 180);
            memset(s.cell, 0, sizeof s.cell);
            s.cell[0][0].flags = CR_CF_ON | CR_CF_PCT | CR_CF_BIPOLAR;
            s.cell[0][0].glyph = CR_G_KNOB;
            s.cell[0][0].pct = 179;                              /* +0.2: the arc right of 12 o'clock */
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Speed");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "+24");
            render(&s, 0);
            write_ppm(dir, "quad_bipolar_knob");
            COUNT(lb, 5u, 56u, 30u, 90u, HS(x, y) == blue);
            COUNT(rb, 30u, 56u, 55u, 90u, HS(x, y) == blue);
            s.cell[0][0].pct = 77;                               /* -0.2: left of it */
            render(&s, 0);
            COUNT(lb2, 5u, 56u, 30u, 90u, HS(x, y) == blue);
            COUNT(rb2, 30u, 56u, 55u, 90u, HS(x, y) == blue);
            snprintf(name, sizeof name, "bipolar knob: the arc from 12 o'clock flips across 0.5 (left / right %u / %u, then %u / %u)",
                     lb, rb, lb2, rb2);
            check(name, rb > lb + 10u && lb2 > rb2 + 10u);
        }
#undef COUNT
#undef HS
    }

    {   /* the looper's PLAY menu (CR_K_LOOPMENU, design/choralroot-fm1-looper-mockups.json): the slot strip's selected
         * cell red and taller; the playhead where lm_pos puts it, its move composing only the timeline's strips; a
         * struck mark in Replace's span; the step cursor's blink composing only its row's strip */
        int32_t it = -1, ir = -1, is = -1, ix = -1;
        uint32_t x, y, k, n0, n1, n2;
        uint16_t red = swap16(CR_RED), dim = swap16(T_DIM);
#define HS(x_, y_) host_screen[(uint32_t)(y_) * 240u + (uint32_t)(x_)]
#define COUNT(n_, x0_, y0_, x1_, y1_, cond_) do { n_ = 0; for (y = (y0_); y < (uint32_t)(y1_); y++) \
            for (x = (x0_); x < (uint32_t)(x1_); x++) n_ += (cond_); } while (0)
        for (k = 0; k < CR_NSCREENS; k++) {
            const cr_screen_t *q = &CR_SCREENS[k];
            if (q->kind != CR_K_LOOPMENU) continue;
            if (ix < 0 && q->lm_mid == CR_LM_TEXT && !q->lm_jump && q->lm_slot == 2u) ix = (int32_t)k;
            if (it < 0 && q->lm_mid == CR_LM_TIMELINE && !q->lm_free && !q->lm_erase1) it = (int32_t)k;
            if (ir < 0 && q->lm_mid == CR_LM_TIMELINE && q->lm_erase1) ir = (int32_t)k;
            if (is < 0 && q->lm_mid == CR_LM_STEPS) is = (int32_t)k;
        }
        check("loop menu: the idle, playing, Replace and Step states are in the table", ix >= 0 && it >= 0 && ir >= 0 && is >= 0);
        if (ix >= 0) {   /* slot 3 (x 48..68): red from row 29; slot 2 and 4: none red, nothing above row 34 */
            cr_screen_t s = CR_SCREENS[ix];
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, 49u, 29u, 67u, 50u, HS(x, y) == red);
            COUNT(n1, 49u, 29u, 67u, 33u, HS(x, y) == red);
            COUNT(n2, 6u, 28u, 47u, 56u, HS(x, y) == red);
            snprintf(name, sizeof name, "loop menu: the selected slot red and 5 px taller (%u red px, %u above the others; %u left of it)",
                     n0, n1, n2);
            check(name, n0 > 200u && n1 > 50u && n2 == 0u);
            s.lm_slot = 4;                       /* slot 5, empty: a red outline only */
            render(&s, 0);
            COUNT(n0, 92u, 29u, 110u, 50u, HS(x, y) == red);
            COUNT(n1, 96u, 34u, 106u, 46u, HS(x, y) == red);
            check("loop menu: an empty selected slot is a red outline (no fill)", n0 > 50u && n1 < n0 / 4u);
        }
        if (it >= 0) {   /* the playhead: a red column at x 20 + pos * 50 (4 bars), moving composes the timeline only */
            cr_screen_t s = CR_SCREENS[it];
            int32_t sy = cr_lm_tl_y(&s), px = 20 + (int32_t)s.lm_pos * (200 / (int32_t)s.lm_bars) / 256;
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, (uint32_t)px - 1u, (uint32_t)sy - 2u, (uint32_t)px + 1u, (uint32_t)sy + 42u, HS(x, y) == red);
            s.lm_pos = (uint16_t)(s.lm_pos + 64u);                    /* a quarter bar on */
            cr_draw(&s, 0);
            COUNT(n1, (uint32_t)px - 1u, (uint32_t)sy - 2u, (uint32_t)px + 1u, (uint32_t)sy + 42u, HS(x, y) == red);
            COUNT(n2, (uint32_t)px + 11u, (uint32_t)sy - 2u, (uint32_t)px + 14u, (uint32_t)sy + 42u, HS(x, y) == red);
            snprintf(name, sizeof name, "loop menu: the playhead at x %d (%u red px), a quarter bar on at %d (%u; %u left)",
                     px, n0, px + 12, n2, n1);
            check(name, n0 >= 40u && n2 >= 40u && n1 < 30u);
            snprintf(name, sizeof name, "loop menu: the playhead moves: only the timeline's strips composed (%u)", cr_dc.drawn);
            check(name, cr_dc.drawn >= 1u && cr_dc.drawn <= 2u);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("loop menu: the playhead's partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
        }
        if (ir >= 0) {   /* a mark at 2.25 bars (x 132) inside the span 2.1 .. 2.4: grey, a red x over it */
            cr_screen_t s = CR_SCREENS[ir];
            int32_t sy = cr_lm_tl_y(&s), mx = 20 + 225 * 50 / 100;
            s.anim = 0;
            render(&s, 0);
            write_ppm(dir, "loopmenu_struck");
            COUNT(n0, (uint32_t)mx - 1u, (uint32_t)sy + 5u, (uint32_t)mx + 2u, (uint32_t)sy + 35u, HS(x, y) == dim);
            COUNT(n1, (uint32_t)mx - 5u, (uint32_t)sy + 5u, (uint32_t)mx + 6u, (uint32_t)sy + 35u, HS(x, y) == red);
            s.lm_erase0 = s.lm_erase1 = 0;
            render(&s, 0);
            COUNT(n2, (uint32_t)mx - 1u, (uint32_t)sy + 5u, (uint32_t)mx + 2u, (uint32_t)sy + 35u, HS(x, y) == dim);
            snprintf(name, sizeof name, "loop menu: a struck mark: grey (%u px, %u unstruck) under a red x (%u red px)", n0, n2, n1);
            check(name, n0 >= 6u && n2 == 0u && n1 >= 10u);
        }
        if (is >= 0) {   /* the cursor's outline (red around a grey step); its blink: its row's strip only */
            cr_screen_t s = CR_SCREENS[is];
            int32_t gy = cr_lm_st_y(&s), per = cr_lm_per(&s), c = (int32_t)(s.lm_pos & 63u) % per;
            int32_t cx0 = cr_lm_st_x(per, c) / 16, cy0 = gy + (int32_t)(s.lm_pos & 63u) / per * 9;
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, (uint32_t)cx0 - 3u, (uint32_t)cy0 - 3u, (uint32_t)cx0 + 14u, (uint32_t)cy0 + 10u, HS(x, y) == red);
            s.lm_pos |= CR_LM_CUR_OFF;
            cr_draw(&s, 0);
            COUNT(n1, (uint32_t)cx0 - 3u, (uint32_t)cy0 - 3u, (uint32_t)cx0 + 14u, (uint32_t)cy0 + 10u, HS(x, y) == red);
            snprintf(name, sizeof name, "loop menu: the step cursor blinks (%u red px lit, %u off), only its strip composed (%u)",
                     n0, n1, cr_dc.drawn);
            check(name, n0 >= 30u && n1 + 20u < n0 && cr_dc.drawn == 1u && cr_dc.blits == 1u);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("loop menu: the blink's partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
            s.lm_pos = (uint16_t)((s.lm_pos & 63u) + 16u);            /* the cursor a bar down: both rows' strips */
            cr_draw(&s, 0);
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            check("loop menu: the cursor moving rows: the partial draw equals a full one", !memcmp(a, host_screen, sizeof a));
        }
        /* 0.18. The mark of a loop not saved yet (lm_dirty: the slot + 1): a 3 px square in its cell's top right
         * corner, in the number's colour: the background's on a filled cell, the cell's on an outlined one */
        if (ix >= 0) {
            cr_screen_t s = CR_SCREENS[ix];
            uint16_t bg = swap16(T_BG);
            s.anim = 0;
            render(&s, 0);
            COUNT(n0, 62u, 31u, 72u, 37u, HS(x, y) == bg);          /* slot 3 (selected, filled): its top right */
            memcpy(a, host_screen, sizeof a);
            s.lm_dirty = 3;
            cr_draw(&s, 0);
            COUNT(n1, 62u, 31u, 72u, 37u, HS(x, y) == bg);
            snprintf(name, sizeof name, "loop menu: a loop not saved yet: a mark in its slot's corner (%u -> %u background px there), "
                     "the strip redrawn (%u)", n0, n1, cr_dc.drawn);
            check(name, n1 == n0 + 9u && cr_dc.drawn >= 1u && memcmp(a, host_screen, sizeof a) != 0);
            write_ppm(dir, "loopmenu_dirty");
            s.lm_dirty = 5;                      /* slot 5 (not selected) */
            s.lm_used &= (uint16_t)~(1u << 4);
            render(&s, 0);
            COUNT(n0, 106u, 35u, 120u, 42u, HS(x, y) == dim);
            s.lm_dirty = 0;
            render(&s, 0);
            COUNT(n1, 106u, 35u, 120u, 42u, HS(x, y) == dim);
            check("loop menu: the mark on an empty, unselected slot (a cleared loop not saved): dim, as its number", n0 >= n1 + 6u);
        }
        /* the PLAY menu's two pages and the loop editor (cr_ui.c cu_loop_cells, cu_ledit_cells, cu_loopmenu): the
         * bottom line, the cells; the editor's grid ends where the loop ends (lm_steps) */
        if (ix >= 0 && is >= 0) {
            static const char *const LB1[4] = {"Length", "Rate", "Quantize", "Count-in"};
            static const char *const V1[4] = {"4 bars", "x1.5", "1/16", "On"};
            static const char *const LBE[4] = {"Step", "Length", "Steps", "Velocity"};
            static const char *const VE[4] = {"2/3", "2", "3", "100"};
            cr_screen_t s = CR_SCREENS[ix];
            uint16_t line = swap16(T_LINE);
            uint32_t c;
            s.anim = 0;
            memset(s.cell, 0, sizeof s.cell);
            for (c = 0; c < 4u; c++) {
                s.cell[0][c].flags = CR_CF_ON;
                snprintf(s.cell[0][c].label, sizeof s.cell[0][c].label, "%s", LB1[c]);
                snprintf(s.cell[0][c].value, sizeof s.cell[0][c].value, "%s", V1[c]);
            }
            s.cell[0][2].glyph = CR_G_ECHOES; s.cell[0][2].pct = 170; s.cell[0][2].pct2 = 255;
            s.cell[0][3].glyph = CR_G_GATE; s.cell[0][3].pct = 255;
            snprintf(s.foot, sizeof s.foot, "1/2 \267 keys: play \267 GLO: loops");
            render(&s, 0);
            COUNT(n0, 20u, 228u, 220u, 240u, HS(x, y) != swap16(T_BG));
            write_ppm(dir, "loopmenu_page1");
            check("loop menu, page 1: Length / Rate / Quantize / Count-in over the bottom line (the page, the keys, GLO)", n0 > 100u);
            memset(s.cell, 0, sizeof s.cell);
            s.cell[0][0].flags = CR_CF_ON;
            snprintf(s.cell[0][0].label, sizeof s.cell[0][0].label, "Level");
            snprintf(s.cell[0][0].value, sizeof s.cell[0][0].value, "100");
            s.cell[0][0].glyph = CR_G_BAR; s.cell[0][0].pct = 255;
            snprintf(s.foot, sizeof s.foot, "2/2 \267 keys: play \267 GLO: loops");
            memcpy(a, host_screen, sizeof a);
            render(&s, 0);
            COUNT(n1, 62u, 170u, 240u, 222u, HS(x, y) == swap16(T_TEXT));
            write_ppm(dir, "loopmenu_page2");
            check("loop menu, page 2: Level alone, the other three cells empty", memcmp(a, host_screen, sizeof a) != 0 && n1 < 40u);
            s = CR_SCREENS[is];                  /* the editor: a 3-step loop at 1/4, step 2 two cells long: 4 cells */
            s.anim = 0;
            s.n_items = 1; s.item0 = 0; s.sel = 0;
            snprintf(s.item[0], sizeof s.item[0], "Edit");
            s.lm_bars = 1; s.lm_beats = 4; s.lm_lane[0] = 0xBull; s.lm_pos = 1; s.lm_steps = 4; s.lm_dirty = (uint8_t)(s.lm_slot + 1u);
            snprintf(s.sub, sizeof s.sub, "Am");
            snprintf(s.value, sizeof s.value, "step 2/3");
            snprintf(s.foot, sizeof s.foot, "SELECT: step \267 REC: clear \267 EDIT: done");
            s.right[0] = 0;
            memset(s.cell, 0, sizeof s.cell);
            for (c = 0; c < 4u; c++) {
                s.cell[0][c].flags = CR_CF_ON;
                snprintf(s.cell[0][c].label, sizeof s.cell[0][c].label, "%s", LBE[c]);
                snprintf(s.cell[0][c].value, sizeof s.cell[0][c].value, "%s", VE[c]);
            }
            s.cell[0][3].glyph = CR_G_BAR; s.cell[0][3].pct = 200;
            render(&s, 0);
            {
                int32_t gy = cr_lm_st_y(&s);
                COUNT(n0, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == red);
                COUNT(n1, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == line);
                write_ppm(dir, "loopmenu_editor");
                s.lm_steps = 3;                  /* the loop a cell shorter: the fourth cell is not drawn */
                render(&s, 0);
                COUNT(n2, 20u, (uint32_t)gy, 220u, (uint32_t)gy + 7u, HS(x, y) == red);
                snprintf(name, sizeof name, "loop editor: the grid is the loop's cells (4: %u red px, %u grey), one fewer when the loop "
                         "ends a cell earlier (%u red)", n0, n1, n2);
                check(name, n0 > 600u && n1 > 200u && n2 + 200u < n0);
            }
        }
#undef COUNT
#undef HS
    }

    {   /* the arp screen (the Chord view while an arpeggio plays): the hop alone moves -> only the strips from the
         * arc to the notes' line are composed (the name's are not), and the screen is the full draw's */
        int found = 0;
        for (k = 0; k < CR_NSCREENS && !found; k++)
            if (CR_SCREENS[k].kind == CR_K_ARP) {
                cr_screen_t s = CR_SCREENS[k];
                int same = 1;
                uint32_t most = 0, p;
                found = 1;
                s.anim = 0;
                render(&s, 0);
                for (p = 0; p < 2u * s.n_notes + 1u; p++) {           /* round the notes twice, then none (-1) */
                    s.pos = (int8_t)(p < 2u * s.n_notes ? (int32_t)((s.pos + 1) % (int32_t)s.n_notes) : -1);
                    cr_draw(&s, 0);
                    if (cr_dc.drawn > most) most = cr_dc.drawn;
                    memcpy(a, host_screen, sizeof a);
                    render(&s, 0);
                    same &= !memcmp(a, host_screen, sizeof a);
                    memcpy(b, host_screen, sizeof b);
                }
                snprintf(name, sizeof name, "arp: the hop moves: only its strips composed (%u of %u)", most,
                         (unsigned)CR_NSTRIP);
                check(name, most >= 1u && most <= 3u);
                check("arp: the hop's partial draw equals a full one", same);
                s.pos = 0;
                render(&s, 0);
                check("arp: another note sounding is another screen", memcmp(b, host_screen, sizeof b) != 0);
            }
        check("arp: the screen is in the table (the main sheet's arp state)", found);
    }

    {   /* the knob row's arp band (kr_band 2: the PERF layer while notes are stepped): a knob row of the table with a
         * footer (the fx sheet's), its band the arp of the main sheet's arp state. The hop alone moves: at most the
         * two strips over the row are composed, the row's pixels (and the footer's) never change, the partial draw is
         * the full one's; the band differs from the picker's and has ink between the label and the row */
        int fk = -1, fa = -1;
        for (k = 0; k < CR_NSCREENS; k++) {
            if (fk < 0 && CR_SCREENS[k].kind == CR_K_KNOBROW && !CR_SCREENS[k].kr_band && CR_SCREENS[k].footer[0])
                fk = (int)k;
            if (fa < 0 && CR_SCREENS[k].kind == CR_K_ARP)
                fa = (int)k;
        }
        check("knob row arp band: a knob row with a footer and the arp state are in the table", fk >= 0 && fa >= 0);
        if (fk >= 0 && fa >= 0) {
            cr_screen_t s = CR_SCREENS[fk];
            uint32_t ry, most = 0, p, i, ink = 0;
            int same = 1, row = 1;
            s.anim = 0;
            render(&s, 0);
            memcpy(b, host_screen, sizeof b);                         /* the picker band */
            s.kr_band = 2;
            s.name = CR_SCREENS[fa].name;
            s.n_notes = CR_SCREENS[fa].n_notes;
            memcpy(s.note, CR_SCREENS[fa].note, sizeof s.note);
            s.hop_col = CR_COL_WHITE;
            s.pos = 0;
            ry = (uint32_t)cr_kr_rowy(&s);
            render(&s, 0);
            check("knob row arp band: not the picker's band", memcmp(b, host_screen, ry * 240u * sizeof b[0]) != 0);
            check("knob row arp band: the row and the footer are the picker screen's",
                  !memcmp(b + ry * 240u, host_screen + ry * 240u, (240u - ry) * 240u * sizeof b[0]));
            for (i = (CR_PY0 + 16u) * 240u; i < ry * 240u; i++) ink += host_screen[i] != host_screen[0];
            check("knob row arp band: the name, the notes and the hop are drawn in the band", ink > 400u);
            memcpy(b, host_screen, sizeof b);
            for (p = 0; p < 2u * s.n_notes + 1u; p++) {               /* round the notes twice, then none (-1) */
                s.pos = (int8_t)(p < 2u * s.n_notes ? (int32_t)((s.pos + 1) % (int32_t)s.n_notes) : -1);
                cr_draw(&s, 0);
                if (cr_dc.drawn > most) most = cr_dc.drawn;
                row &= !memcmp(b + ry * 240u, host_screen + ry * 240u, (240u - ry) * 240u * sizeof b[0]);
                memcpy(a, host_screen, sizeof a);
                render(&s, 0);
                same &= !memcmp(a, host_screen, sizeof a);
            }
            snprintf(name, sizeof name, "knob row arp band: a hop composes the band's strips only (%u of %u; the row's two never)",
                     most, (unsigned)CR_NSTRIP);
            check(name, most >= 1u && most <= 2u && cr_strips_of(CR_KA_BY(&s) - 40, CR_KA_BY(&s) + 14) == 6u);
            check("knob row arp band: the knob row's pixels are unchanged across the hops", row);
            check("knob row arp band: the hop's partial draw equals a full one", same);
            s.hot_r = 1;                                              /* a cell turned under the arp: the row's strips */
            s.hot_c = 0;
            cr_draw(&s, 0);
            check("knob row arp band: a hot cell composes the row's strips (2), not the band's", cr_dc.drawn == 2u);
        }
    }

    {   /* animations: pure and settling */
        int pure = 1, settle = 1, moving = 1;
        for (i = 0; i < CR_NSCREENS; i++) {
            const cr_screen_t *s = &CR_SCREENS[i];
            cr_screen_t st = *s;
            if (!s->anim) continue;
            render(s, CR_TEST_MID);
            memcpy(a, host_screen, sizeof a);
            render(&CR_SCREENS[(i + 1u) % CR_NSCREENS], 33u);
            render(s, 17u);
            render(s, CR_TEST_MID);
            pure &= !memcmp(a, host_screen, sizeof a);
            st.anim = 0;
            render(&st, 0);
            memcpy(b, host_screen, sizeof b);
            moving &= memcmp(a, b, sizeof a) != 0 || !(s->anim & (CR_A_SQUEEZE | CR_A_SLIDE | CR_A_FILL));
            if (s->anim & CR_A_STRIPES) continue;                       /* (the stripes run until the screen changes) */
            render(s, 5000u);
            settle &= !memcmp(b, host_screen, sizeof b) && !cr_anim_busy(s, 5000u);
            if (memcmp(b, host_screen, sizeof b)) fprintf(rep, "not settled: %s\n", CR_SCREEN_SLUGS[i]);
        }
        check("an animation frame is a function of (state, anim_ms) only", pure);
        check("mid-animation frames differ from the settled ones (squeeze, slide, fill)", moving);
        check("past their duration the animations equal the settled screen", settle);
    }

    fprintf(rep, "\n%u lint findings, %u ellipsised texts\n", nfind, nell);
    fclose(rep);
    printf("cr_draw: %u screens x 2 frames rendered to %s/ppm; %u lint findings, %u ellipsised; report %s\n",
           CR_NSCREENS, dir, nfind, nell, path);
    return nfind || nfail ? 1 : 0;
}
