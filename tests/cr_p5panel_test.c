/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the sound editor on P5's compiled panel (firmware/src/cr_edit.c CE_S_FREE on cr_layout.c's tables,
 * docs/LAYOUT-TOOL.md "Step 4"), on the firmware as the emulator builds it: OSC .. MOD are the layout's screens (as
 * many, their lanes, their titles; MOD's last screen still the EXPR stack, 0.22), FX and MIX the platform's; every lane slot of the design turned through the editor
 * (ce_knob): a bound one moves its parameter (the engine's own, by its built-in page and column), marks the sound
 * edited and the slot hot, and the screen model (ce_screen) is CR_K_FREE with that hot cell; an empty slot does
 * nothing; SELECT runs across the lanes and the screens of a group and wraps; the patch blob carries the edits; another
 * engine (the VA) keeps its classic screens. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define OK(c, ...)                                                      \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
        } else {                                                        \
            printf("ok   ");                                            \
        }                                                               \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    } while (0)

/* the parameter of a slot's element now (the engine's get, or the part's) */
static int32_t pval(const track_t *t, const cr_lay_elem_t *e)
{
    return e->page == CR_LAY_PART ? t->p[e->col] : ENGINES[ENGI_PROPHET]->deep->get(t, e->page, e->col);
}

int main(void)
{
    static cr_screen_t s;
    track_t *t = &trk[CR_PART_CHORD];
    const eng_deep_t *d = ENGINES[ENGI_PROPHET]->deep;
    uint32_t li, g, k, ln, c, ns, n, turned = 0, inert = 0, bad = 0, badinert = 0, badscr = 0, lanes = 0;
    uint8_t b0[ENG_BLOB_MAX], b1[ENG_BLOB_MAX];
    ce_scr_t sc;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    cu_pool_load(0, ENGI_PROPHET, 1);
    cu_edit_open(0);
    li = cr_lay_find(ENGI_PROPHET);
    OK(li && cr_lay(li)->nscreens == 8u && ce_lay(t) == li, "P5 has its compiled layout (8 screens); the editor uses it");
    d->blob_get(t, b0);

    /* the groups: the layout's screens in OSC .. MOD (MOD's then EXPR), the platform's in FX and MIX */
    for (g = 0; g < CE_NSEC; g++) {
        uint32_t want, si, nl = 0;
        ns = ce_scr(t, g, 0, &sc);
        want = g <= CE_MOD ? (nl = cr_lay_group(li, g, 0, &si)) + (g == CE_MOD) : 1u;
        if (ns != want || (g <= CE_MOD) != (sc.type == CE_S_FREE)) badscr++;
        for (k = 0; g <= CE_MOD && k < nl; k++) {
            const cr_lay_screen_t *ls;
            cr_lay_group(li, g, k, &si);
            ls = cr_lay_scr(li, si);
            ce_scr(t, g, k, &sc);
            if (sc.ls != si || ce_lanes(t, g, &sc) != ls->nlanes) badscr++;
            lanes += ls->nlanes;
        }
    }
    OK(!badscr && lanes == 19u, "OSC .. MOD: the layout's screens (2 1 1 2 2), %u lanes; FX and MIX: the platform's", lanes);

    /* MOD's last screen: Expression (cr_edit.c CE_S_EXPR, 0.22), after the layout's, as on every engine */
    ns = ce_scr(t, CE_MOD, 0, &sc);
    ce_scr(t, CE_MOD, ns - 1u, &sc);
    cx.grp[0] = CE_MOD;
    cx.scr[0][CE_MOD] = (uint8_t)(ns - 1u);
    cx.lane[0][CE_MOD] = 1;
    ce_screen(&s, cu_now());
    OK(sc.type == CE_S_EXPR && ce_lanes(t, CE_MOD, &sc) == 2u && s.kind == CR_K_STACK && s.fl_eng == 0 &&
       !strcmp(s.page, "EXPR \267 AT") && !strcmp(s.cell[1][0].value, "AT"),
       "MOD %u: EXPR after the panel's POLYMOD and VOICE (a stack of two lanes, \"%s\")", (unsigned)ns, s.page);

    /* every lane slot turned */
    for (g = 0; g <= CE_MOD; g++) {
        uint32_t si0;
        ns = cr_lay_group(li, g, 0, &si0);          /* (the layout's screens; MOD's EXPR: above) */
        for (k = 0; k < ns; k++) {
            uint32_t si;
            const cr_lay_screen_t *ls;
            cr_lay_group(li, g, k, &si);
            ls = cr_lay_scr(li, si);
            for (ln = 0; ln < ls->nlanes; ln++)
                for (c = 0; c < 4u; c++) {
                    uint32_t x = cr_lay_slot(ls, ln, c);
                    cx.grp[0] = (uint8_t)g;
                    cx.scr[0][g] = (uint8_t)k;
                    cx.lane[0][g] = (uint8_t)ln;
                    cx.hot_r = 0;
                    psnd[0].edited = 0;
                    if (x == CR_LAY_EMPTY) {     /* an empty slot: nothing moves, nothing is hot or edited */
                        d->blob_get(t, b1);
                        ce_knob(c, 3, 0);
                        {
                            uint8_t b2[ENG_BLOB_MAX];
                            d->blob_get(t, b2);
                            if (memcmp(b1, b2, d->blob_size) || cx.hot_r || psnd[0].edited) badinert++;
                        }
                        inert++;
                        continue;
                    } else {
                        const cr_lay_elem_t *e = cr_lay_el(li, ls, x);
                        int32_t v0 = pval(t, e), v1, dir = 1;
                        const param_desc_t *pd = &d->pages[e->page].col[e->col];
                        if (e->page != CR_LAY_PART && v0 >= pd->max)
                            dir = -1;
                        ce_knob(c, dir, 0);
                        v1 = pval(t, e);
                        ce_screen(&s, cu_now());
                        if (v1 == v0 || !psnd[0].edited || cx.hot_r != ln + 1u || cx.hot_c != c || s.kind != CR_K_FREE ||
                            s.fl_eng != li || s.fl_scr != si || s.active != ln || s.hot_r != ln + 1u || s.hot_c != c ||
                            !(s.cell[ln][c].flags & CR_CF_ON) || strcmp(s.cell[ln][c].label, cr_lay_str(e->label)) ||
                            strcmp(s.page, cr_lay_str(ls->title))) {
                            bad++;
                            printf("  slot %s %u.%u K%u (page %u col %u): %d -> %d, edited %u, hot %u.%u, kind %u\n",
                                   CE_SEC_NAME[g], (unsigned)k + 1u, (unsigned)ln + 1u, (unsigned)c + 1u, e->page, e->col,
                                   (int)v0, (int)v1, psnd[0].edited, cx.hot_r, cx.hot_c, s.kind);
                        }
                        turned++;
                    }
                }
        }
    }
    OK(turned == 56u && !bad, "every bound slot turned (%u): its parameter moves, the sound edited, the slot hot, CR_K_FREE "
       "with its cell, label and title", turned);
    OK(inert == 19u * 4u - 56u && !badinert, "every empty slot (%u): a knob on it changes nothing, marks nothing", inert);
    d->blob_get(t, b1);
    OK(memcmp(b0, b1, d->blob_size) != 0, "the patch blob carries the edits (%u bytes)", (unsigned)d->blob_size);

    /* SELECT: across the lanes and the screens of each group (MOD's EXPR too), round to the start */
    for (g = 0, bad = 0; g <= CE_MOD; g++) {
        uint32_t want = 0, steps = 0;
        uint32_t nlk[8];
        ns = ce_scr(t, g, 0, &sc);
        for (k = 0; k < ns && k < 8u; k++) {
            ce_scr(t, g, k, &sc);
            want += nlk[k] = ce_lanes(t, g, &sc);
        }
        cx.grp[0] = (uint8_t)g;
        cx.scr[0][g] = 0;
        cx.lane[0][g] = 0;
        do {
            uint32_t k0 = cx.scr[0][g], l0 = cx.lane[0][g];
            ce_select(1);
            steps++;
            if (!(cx.lane[0][g] == l0 + 1u && cx.scr[0][g] == k0) &&
                !(l0 + 1u == nlk[k0] && cx.lane[0][g] == 0 && cx.scr[0][g] == (k0 + 1u) % ns))
                bad++;
        } while ((cx.scr[0][g] || cx.lane[0][g]) && steps < 40u);
        if (steps != want) bad++;
        ce_select(-1);
        n = ns - 1u;
        if (cx.scr[0][g] != n || cx.lane[0][g] != nlk[n] - 1u) bad++;
    }
    OK(!bad, "SELECT: lane by lane across each group's screens, wrapping; back from the first: the last lane");

    /* another engine keeps its classic screens */
    cu_pool_load(0, ENGI_VA, 1);
    ce_scr(&trk[CR_PART_CHORD], CE_OSC, 0, &sc);
    OK(!ce_lay(t) && sc.type != CE_S_FREE, "the VA: no compiled layout, its classic screens");
    printf("cr_p5panel: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
