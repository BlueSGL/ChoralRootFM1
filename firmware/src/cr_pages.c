/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the sound editor's data (cr_edit.c builds the editor from these). The platform pages of four
 * parameters, KNOB 1..4:
 *
 *   EDIT 1, EDIT 2  the engine's own parameters (engines.c edit[0..3], edit[4..7], titled by its page_title)
 *   ENV             ATK DEC SUS REL
 *   LFO             RATE WAVE, its depth to the pitch and the filter
 *   MOD             the envelope to the filter / pitch / shape (Felucca's ENV DEST: P_ED_*), the LFO to the amplitude
 *   FX              the sends DIST CHO DLY REV
 *   MIX, MIX 2      LEVEL PAN VOICE GLIDE | TRANS DETUNE PRIO GLMODE
 *
 * and an engine's deep pages (core.h eng_deep_t: the VA's 31). Ranges, defaults and the value text are Felucca's
 * (params.c TP / engines.c edit[] through track_desc, param_format); this file chooses the pages, the long labels
 * and the steps. Pure functions of a track: cr_edit.c owns the state. */

enum { CP_EDIT1, CP_EDIT2, CP_ENV, CP_LFO, CP_MOD, CP_FX, CP_MIX, CP_MIX2, CP_N };
static const struct { const char *title; uint8_t id[4]; } CP_PAGES[CP_N] = {
    {0, {P_E0, P_E1, P_E2, P_E3}},
    {0, {P_E4, P_E5, P_E6, P_E7}},
    {"ENV", {P_ATK, P_DEC, P_SUS, P_REL}},
    {"LFO", {P_LRATE, P_LWAVE, P_LD_PIT, P_LD_FLT}},
    {"MOD", {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_LD_AMP}},
    {"FX", {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"MIX", {P_LEVEL, P_PAN, P_VOICE, P_GLIDE}},
    {"MIX", {P_TRANS, P_DETUNE, P_PRIO, P_GLMODE}},
};
static const struct { uint8_t id; const char *label; } CP_LABEL[] = {
    {P_ATK, "Attack"}, {P_DEC, "Decay"}, {P_SUS, "Sustain"}, {P_REL, "Release"},
    {P_LRATE, "Rate"}, {P_LWAVE, "Wave"}, {P_LD_PIT, "Vibrato"}, {P_LD_FLT, "Wah"},
    {P_ED_FLT, "Env>Filter"}, {P_ED_PIT, "Env>Pitch"}, {P_ED_SHP, "Env>Shape"}, {P_LD_AMP, "Tremolo"},
    {P_DIST, "Drive"}, {P_CHOR, "Chorus"}, {P_DLY, "Delay"}, {P_REV, "Reverb"},
    {P_LEVEL, "Level"}, {P_PAN, "Pan"}, {P_VOICE, "Voice"}, {P_GLIDE, "Glide"},
    {P_TRANS, "Trans"}, {P_DETUNE, "Detune"}, {P_PRIO, "Priority"}, {P_GLMODE, "Gl. mode"},
};

/* the engine's deep pages, 0: none (the eight P_E only) */
#ifdef CR_LAYOUT_PREVIEW
/* (the layout tool's preview only: tools/layout/layout_web.c, docs/LAYOUT-TOOL.md. The tool may hand the editor
 * another table for an engine, a layout being designed; 0, or the built-in one back: as the unit. The macro is the
 * preview module's alone: the device build and the emulators do not have this) */
static const eng_deep_t *(*cp_deep_hook)(const track_t *t, const eng_deep_t *builtin);
#endif
static const eng_deep_t *cp_deep(const track_t *t)
{
    const eng_deep_t *d = ENGINES[eng_idx(t->eng_req)]->deep;
#ifdef CR_LAYOUT_PREVIEW
    if (cp_deep_hook)
        d = cp_deep_hook(t, d);
#endif
    return d && d->pages && d->npages && d->get && d->set ? d : 0;
}

static const param_desc_t *cp_desc(const track_t *t, uint32_t id) { return track_desc(t, id); }

/* ------------------------------------------- knob acceleration, OPT snap --- */
/* (docs/EDITOR.md "Knob acceleration and OPT snap"; the owner, 2026-10-14.) Every knob that edits a VALUE (the sound
 * editor of every engine, the FX layer, the layers' knob rows, the main view's KNOB 3 / 4, Options > Velocity) steps
 * through kn_*: a slow turn moves the finest step (one stored step: 0.01 of a hi-res value, 1 of an integer one), a
 * quick turn more, up to 1/20 of the range a detent (a quick spin of about a turn, 24 detents, crosses it). The
 * speed is the time between detents of the same knob (cr_ui.c cu_knob calls kn_turn with the UI clock, fm1_ms: the
 * emulator's script clock, so a script's waits set it exactly); a reversal or a rest starts slow again. Pickers
 * stepping through lists (PRESETS, ALGORITHM, SELECT, F_ENUM, named choices) are not values: one detent, one item.
 * OPT (GLO, the editor's SHIFT) held: a value snaps instead (kn_snap): one target a detent (kn_grid). */
/* THE CURVE: the time from the previous detent (ms, its share of the frame's detents) -> the step, in 64ths of the
 * quickest (kn_lin: the range / 20; 0: the finest step). The first row whose ms the gap reaches */
static const struct { uint16_t ms; uint8_t share; } KN_CURVE[] = {
    {150, 0}, {110, 1}, {80, 2}, {60, 4}, {45, 8}, {34, 16}, {25, 32}, {0, 64}};
static struct {
    uint32_t t[8];                        /* each knob role's last detent (fm1_ms; 0: none yet) */
    int8_t dir[8];                        /* .. its direction */
    uint8_t share;                        /* the detent being handled: KN_CURVE's share */
    uint8_t snap;                         /* .. OPT held (cr_ui.c cu_shift) */
} kn;
/* a turn of knob role (s detents this frame) at now: its speed for the kn_* below */
static void kn_turn(uint32_t role, int32_t s, uint32_t now, uint32_t snap)
{
    uint32_t r = role & 7u, n = (uint32_t)(s < 0 ? -s : s), gap = now - kn.t[r], k = 0;
    int8_t dir = (int8_t)(s < 0 ? -1 : 1);
    if (!n)
        return;
    if (n > 1u)
        gap = 16u / n;                    /* (several detents in one UI frame (16 ms): that quick) */
    else if (!kn.t[r] || gap > 1000u || dir != kn.dir[r])
        gap = 1000u;                      /* (the first detent, after a rest, a reversal: slow) */
    while (gap < KN_CURVE[k].ms)
        k++;
    kn.share = KN_CURVE[k].share;
#ifdef KN_QUICK
    if (KN_QUICK())                       /* (the emulator: a script's "knobs quick", tools/emu/emu_hooks.h) */
        kn.share = 64;
#endif
    kn.snap = (uint8_t)(snap != 0);
    kn.t[r] = now ? now : 1u;
    kn.dir[r] = dir;
}
/* lo .. hi stepped s detents at the current speed, clamped (so both ends are reached exactly). The quickest step is
 * the range / 20 rounded (the detent of before 0.22, about 5 %: 6 of 0..127, 5 of 0..99) */
static int32_t kn_lin(int32_t v0, int32_t s, int32_t lo, int32_t hi)
{
    int32_t st = (int32_t)(((hi - lo) * (int32_t)kn.share + 10 * 64) / (20 * 64));
    return clamp(v0 + s * (st < 1 ? 1 : st), lo, hi);
}
/* THE HI-RES HOOK: the stored steps of a whole number (1: none). A hi-res parameter stores 1/100 units (its finest
 * step 0.01, a whole number 100 steps: OPT snaps to whole numbers; LFO speeds to the speed list x 100). Declared by
 * the descriptor's format: core.h FMT_HI (FM TONE's F_C100 F_O100 F_C100I F_FRQ100); another way to mark a column
 * hi-res goes here, the one place that asks */
static int32_t kn_whole(const param_desc_t *d)
{
    int32_t w = 1;
    const char *n;
#ifdef FMT_HI
    if (FMT_HI(d->fmt))
        return 100;
#endif
    if (d->fmt == F_INT && d->names && d->max - d->min + 1 > 64 && (n = d->names[0]) != 0) {   /* a value named by
                                                         * its number with decimals (QUAD's OFS "-1.00" .. "+1.00"):
                                                         * hi-res too, its decimals the whole number's steps */
        while (*n && *n != '.')
            n++;
        if (*n)
            while (*++n >= '0' && *n <= '9')
                w *= 10;
    }
    return w;
}
static int kn_lab(const param_desc_t *d, const char *a)
{
    const char *l = d->label;
    while (l && *a && *l == *a)
        l++, a++;
    return l && !*a && !*l;
}
/* an LFO speed (a bipolar SPEED, the Digitone's): OPT snaps it along KN_LFO */
static int kn_lfo(const param_desc_t *d) { return (kn_lab(d, "SPEED") || kn_lab(d, "SPD")) && d->min < 0; }
static const int8_t KN_LFO[8] = {-64, -32, -16, -8, 8, 16, 32, 64};
/* a named choice: one item a detent, no speed, no snap (F_ENUM, on / off, a note name, a list of names one a value of
 * at most 64 (FM6's ALG, COARSE), a ratio pair (QUAD's RATIO B: k names over k x k values) */
static int kn_list(const param_desc_t *d)
{
    uint32_t k = 0, n = (uint32_t)(d->max - d->min + 1);
    if (d->fmt == F_ENUM || d->fmt == F_ONOFF || d->fmt == F_NOTE)
        return 1;
    if (!d->names || d->max < d->min)
        return 0;
    while (d->names[k])
        k++;
    return (k == n && n <= 64u) || (k > 1u && k * k == n);
}
/* THE SNAP GRID: OPT's targets are the multiples of the grid from the anchor, and both ends; 0: no snap (one step
 * a detent, as a small range steps anyway). A range's grid is the nice number nearest (geometric) to 1/8 of it
 * (centred: 1/16): 0..127 -> 16 (0 16 .. 112 127), -64..63 -> 8, 0..99 / 0..100 -> 10, -50..50 -> 5, 0..120 -> 16,
 * 1..1000 -> 100; semitones (F_SEMI, an offset in st) -> 12, the DX7 break point -> its Cs; at most 17 values: none;
 * hi-res: whole numbers */
static int32_t kn_grid_rng(int32_t lo, int32_t hi, int centred)
{
    static const uint16_t NICE[] = {1, 2, 4, 5, 8, 10, 16, 20, 25, 32, 50, 64, 100, 200, 250, 500, 1000};
    int32_t n = hi - lo + 1;
    uint32_t i, q = centred ? 256u : 64u, m;              /* (x = n / 8 or n / 16: x^2 = n^2 / q) */
    if (n <= 17)
        return 0;
    m = (uint32_t)(n > 8000 ? 8000 : n);
    for (i = 0; i + 1u < NELEM(NICE); i++)                 /* the first nice number above the geometric mean's x */
        if (m * m < (uint32_t)NICE[i] * NICE[i + 1u] * q)
            break;
    return NICE[i];
}
static int32_t kn_grid(const param_desc_t *d, int32_t *anchor)
{
    int32_t w = kn_whole(d);
    *anchor = d->fmt == F_OFS ? (d->min + d->max) / 2 : d->fmt == F_FMNOTE ? 3 : 0;
    if (w > 1)
        return w;
    if (d->fmt == F_SEMI || (d->fmt == F_OFS && d->unit && d->unit[0] == 's' && d->unit[1] == 't') || d->fmt == F_FMNOTE)
        return d->max - d->min > 12 ? 12 : 0;
    return kn_grid_rng(d->min, d->max, d->min < 0 || d->fmt == F_OFS);
}
/* from v0 s targets along the grid g from a (0: a step a detent), clamped */
static int32_t kn_snap_rng(int32_t v0, int32_t s, int32_t lo, int32_t hi, int32_t g, int32_t a)
{
    int32_t v = clamp(v0, lo, hi), o;
    if (g <= 0)
        return clamp(v0 + s, lo, hi);
    for (; s > 0 && v < hi; s--) {
        o = v - a;
        o = (o >= 0 ? o / g : -((-o + g - 1) / g)) * g + g;      /* the next multiple above */
        v = clamp(a + o, lo, hi);
    }
    for (; s < 0 && v > lo; s++) {
        o = v - a;
        o = (o >= 0 ? (o + g - 1) / g : -((-o) / g)) * g - g;    /* .. below */
        v = clamp(a + o, lo, hi);
    }
    return v;
}
static int32_t kn_snap(const param_desc_t *d, int32_t v0, int32_t s)
{
    int32_t a, g, w = kn_whole(d), v = clamp(v0, d->min, d->max), k;
    if (kn_lfo(d)) {                                      /* the speed list (none past it: the end) */
        for (; s > 0; s--) {
            for (k = 0; k < 8 && KN_LFO[k] * w <= v; k++)
                ;
            v = k < 8 ? clamp(KN_LFO[k] * w, d->min, d->max) : d->max;
        }
        for (; s < 0; s++) {
            for (k = 7; k >= 0 && KN_LFO[k] * w >= v; k--)
                ;
            v = k >= 0 ? clamp(KN_LFO[k] * w, d->min, d->max) : d->min;
        }
        return v;
    }
    g = kn_grid(d, &a);
    return kn_snap_rng(v0, s, d->min, d->max, g, a);
}

/* KNOB detents -> the new value (Felucca's ranges; an F_ENUM skips its aliases: params.c enum_step): a named choice
 * one item a detent, a value at the knob's speed (kn_lin), snap (OPT held): kn_snap */
static int32_t cp_dstep(const param_desc_t *d, int32_t v0, int32_t s, uint32_t snap)
{
    int32_t v;
    uint32_t no, k;
    const uint8_t *ord = d->fmt == F_ENUM ? enum_order(d, &no) : 0;
    if (ord) {                                           /* (a display order: s places along it, clamped) */
        for (k = 0; k < no && ord[k] != v0; k++)
            ;
        return ord[clamp((int32_t)(k < no ? k : 0u) + s, 0, (int32_t)no - 1)];
    }
    if (!kn_list(d))
        return snap ? kn_snap(d, v0, s) : kn_lin(v0, s, d->min, d->max);
    v = clamp(v0 + s, d->min, d->max);
    if (d->fmt == F_ENUM && v != v0)
        v = enum_step(d, v0, v);
    return v;
}
static int32_t cp_step(const track_t *t, uint32_t id, int32_t s, uint32_t snap)
{
    return cp_dstep(cp_desc(t, id), t->p[id], s, snap);
}

/* Felucca's value text and its unit, joined when they fit ("25.0ms", "+50%", "SAW") */
static void cp_value(const param_desc_t *d, int32_t v, char *out, uint32_t n)
{
    char val[12];
    const char *unit;
    uint32_t l;
    param_format(d, v, val, &unit);
    str_cpy(out, val, n);
    l = str_len(out);
    if (l + str_len(unit) + 1u <= n)
        str_cpy(out + l, unit, n - l);
}

/* ------------------------------------------------------ deep pages --- */
/* the long labels of the deep pages' short ones (else the short one, capitalised: "SYNC/RING" -> "Sync/ring") */
static const struct { const char *s, *l; } CP_DLABEL[] = {
    {"ATK", "Attack"}, {"DEC", "Decay"}, {"SUS", "Sustain"}, {"REL", "Release"}, {"CUT", "Cutoff"},
    {"RES", "Reso"}, {"KTRK", "Key trk"}, {"FENV", "Env amt"}, {"VEL", "Velocity"}, {"SRC", "Source"},
    {"DST", "Dest"}, {"AMT", "Amount"},
    /* FM6 (eng_fm6.c's pages) */
    {"VSENS", "Vel sens"}, {"AMS", "AM sens"}, {"RSCL", "Rate scl"}, {"BREAK", "Break pt"}, {"LDEPTH", "L depth"},
    {"RDEPTH", "R depth"}, {"ALG", "Algo"}, {"FB", "Feedback"}, {"TRNSP", "Transp"}, {"OSYNC", "Osc sync"},
    {"R1", "Rate 1"}, {"R2", "Rate 2"}, {"R3", "Rate 3"}, {"R4", "Rate 4"}, {"L1", "Level 1"}, {"L2", "Level 2"},
    {"L3", "Level 3"}, {"L4", "Level 4"}, {"PMD", "PM depth"}, {"AMD", "AM depth"}, {"PMS", "PM sens"},
    {"PMODE", "Porta md"}, {"DXVEL", "DX vel"}, {"BRTH", "Breath"}, {"AFTER", "Aftertch"},
    /* CZ-1 (eng_cz.c's pages) */
    {"R5", "Rate 5"}, {"R6", "Rate 6"}, {"R7", "Rate 7"}, {"R8", "Rate 8"}, {"L5", "Level 5"}, {"L6", "Level 6"},
    {"L7", "Level 7"}, {"L8", "Level 8"}, {"W.KEY1", "DCW key 1"}, {"W.KEY2", "DCW key 2"}, {"V.WAV1", "DCW vel 1"},
    {"V.WAV2", "DCW vel 2"}, {"A.KEY1", "DCA key 1"}, {"A.KEY2", "DCA key 2"}, {"V.AMP1", "DCA vel 1"},
    {"V.AMP2", "DCA vel 2"}, {"V.PIT", "Pitch vel"},
#if FELUCCA_PROPHET
    /* PROPHET (eng_prophet_test.c's pages) */
    {"MIX A", "Osc A"}, {"MIX B", "Osc B"}, {"OSC B", "Osc B"}, {"FREQ A", "Freq A"}, {"FREQ B", "Freq B"},
    {"PW A", "PW A"}, {"PW B", "PW B"}, {"LOFREQ", "Lo freq"}, {"KEYBD", "Keyboard"}, {"RELSW", "Rel sw"},
    {"PRIO", "Priority"}, {"REV", "Filter rev"},
#endif
};
static int cp_up(int ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; }
static int cp_eq(const char *a, const char *b)
{
    while (*a && cp_up(*a) == cp_up(*b))
        a++, b++;
    return !*a && !*b;
}
static int cp_has(const char *s, const char *sub)        /* sub in s, case-blind */
{
    uint32_t i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; sub[j] && s[i + j] && cp_up(s[i + j]) == cp_up(sub[j]); j++)
            ;
        if (!sub[j])
            return 1;
    }
    return 0;
}
/* the column of deep page dp labelled l, -1 none */
static int32_t cp_dcol(const eng_page_t *pg, const char *l)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (pg->col[k].label && cp_eq(pg->col[k].label, l))
            return (int32_t)k;
    return -1;
}
/* v of d as Q8 of its range */
static uint32_t cp_q8(const param_desc_t *d, int32_t v)
{
    return d->max > d->min ? (uint32_t)(clamp(v, d->min, d->max) - d->min) * 256u / (uint32_t)(d->max - d->min) : 0u;
}
