/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca); the grain player after Felucca's GRAIN,
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GRAIN: a granular engine over material it renders itself (docs/GRAIN.md; ChoralRoot, FELUCCA_GRAIN; engine 8).
 *
 * The material: two buffers of GS_N samples at GS_RATE (grain_src.c: 743 ms at 22.05 kHz, 32 KB each, POOL), the
 * sound's sources A and B, each rendered from four knobs (TYPE TONE MOTION SEED) by the generator, deterministic.
 * The sound stores the knobs, never audio. The buffers are one pair for the unit: the first part on GRAIN (the chord
 * part when it is) owns them, its source knobs render them; a second part on GRAIN plays the same two with its own
 * grain knobs and MORPH.
 *
 * The player (Felucca's eng_grain.c over RAM: no ADPCM decoder, no seek index): a pool of GP_NG grains a part, shared
 * by its voices (a voice's share GP_NG / sounding voices, at least 2), at most GP_POLY voices. A grain: a source (A or
 * B: MORPH is the share of a voice's grains that read B, spread evenly over them), a position, a length (SIZE, 10 ..
 * 393 ms), a rate (the note against its source's root, PITCH, a random detune), a direction, a Hann window (table,
 * linear interpolation), a level 1 / sqrt(overlap). A voice starts its grains DENS times a second (1 .. 98 /s; SPRD
 * jitters the interval); POS (+ SCAN: a slow triangle over the buffer) is the centre, SPRD the random spread round it.
 * RAND: the random detune (quadratic, up to +-1 octave) and the share of reversed grains (up to 50 %). The matrix's
 * SHP moves MORPH, its FLT the SVF's CUT (CUT RES a voice); the platform's ADSR is the voice's amplitude.
 *
 * The render (docs/GRAIN.md section 1): grain_tick, once a block on core 0 (fx.c mix_block, before the parts render),
 * compares the owner's source knobs with what the buffers hold and posts a request; the main loop's grain_poll (main.c's
 * idle loop, the emulator's idle) renders in slices of GRAIN_SLICE samples: it locks the source, the next tick fades
 * the grains reading it (the other source's play on) and no grain starts on it, the poll waits a block, renders, swaps
 * the knobs in and unlocks. Latest wins: a knob turned during a render restarts it. GRAIN_ASYNC 0 (hostsim, regress,
 * the tests: no main loop): the tick renders synchronously, whole.
 *
 * State: the patch (grain_patch, GQ_NP values a part), the macros as last seen, the pool (gp_part_t, eng_state's
 * union), the scheduler (grs); per voice s[0] the countdown to the next grain, s[1] MORPH's count, s[2..3] the filter. */
#include "grain_src.c"
#ifndef ENGI_GRAIN
#define ENGI_GRAIN 8u
#endif
#ifndef GRAIN_ASYNC
#define GRAIN_ASYNC 0            /* 1: the renders in slices from the main loop (grain_poll: the unit, the emulator);
                                  * 0: a render completes inside the block that asks for it (a host without a main loop) */
#endif
#define GRAIN_NPART 2u           /* parts with a GRAIN patch (ChoralRoot: chord, bass) */
#define GP_NG 16                 /* grains a part */
#define GP_POLY 8                /* engine_t.poly */
#define GP_STEP_MAX (2u << 16)   /* the rate's cap: two source samples an output sample */
#define GP_RATE_Q16 ((GS_RATE << 16) / 44100u)
#define GRAIN_SLICE 1024u        /* samples a poll (DRONE, the heaviest: 1.6 .. 2.4 ms on the unit) */
#define GRAIN_BLOB 20u           /* 'G', version, GQ_NP values, zeros */
#define GRAIN_MAGIC 0x47u
#define GRAIN_VER 1u

/* --------------------------------------------------------- the patch --- */
enum { GQ_TYPE1, GQ_TONE1, GQ_MOTION1, GQ_SEED1, GQ_TYPE2, GQ_TONE2, GQ_MOTION2, GQ_SEED2,   /* SRC 1, SRC 2 */
       GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD,                                                    /* GRAIN */
       GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN,                                                 /* GRAIN+ */
       GQ_CUT, GQ_RES,                                                                       /* FILTER */
       GQ_NP };
_Static_assert(GQ_NP + 2u <= GRAIN_BLOB, "GRAIN patch layout");
/* the macros P_E0..P_E7: source A's four, then MORPH SIZE DENS POS */
static const uint8_t GRAIN_MAC[8] = {GQ_TYPE1, GQ_TONE1, GQ_MOTION1, GQ_SEED1, GQ_MORPH, GQ_SIZE, GQ_DENS, GQ_POS};
static const int8_t GRAIN_DEF[GQ_NP] = {GT_DRONE, 64, 64, 0, GT_BELL, 64, 64, 0, 92, 88, 50, 40, 0, 14, 0, 30, 100, 20};
static int8_t grain_patch[GRAIN_NPART][GQ_NP];
static int16_t grain_mlast[GRAIN_NPART][8];      /* P_E0..P_E7 as last put in the patch */
static uint8_t grain_user_pending;               /* upool.c up_loading: a GRAIN user slot + 1 is being loaded */
static int (*grain_store_read)(uint32_t k, uint8_t *b);   /* upool.c: slot k's GRAIN patch, 0 = there is one */

static inline int32_t grain_min(uint32_t i) { return i == GQ_PITCH ? -24 : 0; }
static inline int32_t grain_max(uint32_t i) { return i == GQ_PITCH ? 24 : i == GQ_TYPE1 || i == GQ_TYPE2 ? GS_NTYPE - 1 : 127; }
static inline int8_t grain_clampv(uint32_t i, int32_t v) { return (int8_t)clamp(v, grain_min(i), grain_max(i)); }
static uint32_t grain_tr(const track_t *t) { return (uint32_t)(t - trk); }

/* the deep pages' columns (the UI contract, docs/GRAIN.md section 3) */
#define GC_TYPE(d) {"TYPE", F_ENUM, 0, GS_NTYPE - 1, d, GS_TYPE_NAMES, 0}
#define GC_TONE {"TONE", F_PCT, 0, 127, 64, 0, 0}
#define GC_MOTION {"MOTION", F_PCT, 0, 127, 64, 0, 0}
#define GC_SEED {"SEED", F_INT, 0, 127, 0, 0, 0}
#define GC_SIZE {"SIZE", F_PCT, 0, 127, 92, 0, 0}
#define GC_DENS {"DENS", F_PCT, 0, 127, 88, 0, 0}
#define GC_POS {"POS", F_PCT, 0, 127, 50, 0, 0}
#define GC_SPRD {"SPRD", F_PCT, 0, 127, 40, 0, 0}
#define GC_MORPH {"MORPH", F_PCT, 0, 127, 0, 0, 0}
#define GC_RAND {"RAND", F_PCT, 0, 127, 14, 0, 0}
#define GC_PITCH {"PITCH", F_SEMI, -24, 24, 0, 0, 0}
#define GC_SCAN {"SCAN", F_PCT, 0, 127, 30, 0, 0}
#define GC_CUT {"CUT", F_CUTOFF, 0, 127, 100, 0, 0}
#define GC_RES {"RES", F_PCT, 0, 127, 20, 0, 0}
#define GC_NONE {0, 0, 0, 0, 0, 0, 0}
/* the platform's envelope and LFO as pages of the engine (the editor wants the sections OSC FILTER ENV LFO: cr_edit.c
 * ce_deep): the track's P_ATK .. P_REL and P_LRATE P_LWAVE P_LD_PIT P_LD_FLT, as params.c TP[] describes them */
static const char *const GRAIN_N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
#define GC_ATK {"ATK", F_TIME, 0, 127, 10, 0, 0}
#define GC_DEC {"DEC", F_TIME, 0, 127, 70, 0, 0}
#define GC_SUS {"SUS", F_PCT, 0, 127, 90, 0, 0}
#define GC_REL {"REL", F_TIME, 0, 127, 60, 0, 0}
#define GC_LRATE {"RATE", F_LFOHZ, 0, 127, 60, 0, 0}
#define GC_LWAVE {"WAVE", F_ENUM, 0, 4, 0, GRAIN_N_LWAVE, 0}
#define GC_LPIT {"PIT", F_BIPCT, -64, 63, 0, 0, 0}
#define GC_LFLT {"FLT", F_BIPCT, -64, 63, 0, 0, 0}
static const eng_page_t GRAIN_PAGES[] = {
    {"SRC 1", {GC_TYPE(GT_DRONE), GC_TONE, GC_MOTION, GC_SEED}},   /* 0 OSC: a stack of the two sources */
    {"SRC 2", {GC_TYPE(GT_BELL), GC_TONE, GC_MOTION, GC_SEED}},
    {"GRAIN", {GC_SIZE, GC_DENS, GC_POS, GC_SPRD}},                /* 2 .. one edit8 screen of two lanes */
    {"GRAIN+", {GC_MORPH, GC_RAND, GC_PITCH, GC_SCAN}},
    {"FILTER", {GC_CUT, GC_RES, GC_NONE, GC_NONE}},                /* 4 FILTER: the curve */
    {"ENV", {GC_ATK, GC_DEC, GC_SUS, GC_REL}},                     /* 5 ENV: the platform's ADSR */
    {"LFO", {GC_LRATE, GC_LWAVE, GC_LPIT, GC_LFLT}},               /* 6 LFO: the platform's */
};
#define GRAIN_NPAGES ((uint32_t)NELEM(GRAIN_PAGES))
#define GQ_X 0xFFu               /* an empty column */
#define GQ_T(id) (0x80u | (id))  /* a track parameter */
static const uint8_t GRAIN_MAP[NELEM(GRAIN_PAGES)][4] = {
    {GQ_TYPE1, GQ_TONE1, GQ_MOTION1, GQ_SEED1},
    {GQ_TYPE2, GQ_TONE2, GQ_MOTION2, GQ_SEED2},
    {GQ_SIZE, GQ_DENS, GQ_POS, GQ_SPRD},
    {GQ_MORPH, GQ_RAND, GQ_PITCH, GQ_SCAN},
    {GQ_CUT, GQ_RES, GQ_X, GQ_X},
    {GQ_T(P_ATK), GQ_T(P_DEC), GQ_T(P_SUS), GQ_T(P_REL)},
    {GQ_T(P_LRATE), GQ_T(P_LWAVE), GQ_T(P_LD_PIT), GQ_T(P_LD_FLT)},
};
_Static_assert(P_LD_FLT < 0x7F, "GRAIN_MAP's track ids");

static void grain_init_patch(int8_t *p) { memcpy(p, GRAIN_DEF, GQ_NP); }
static void grain_macros_out(track_t *t)
{
    uint32_t tr = grain_tr(t), k;
    if (tr >= GRAIN_NPART)
        return;
    for (k = 0; k < 8u; k++) {
        int16_t v = grain_patch[tr][GRAIN_MAC[k]];
        t->p[P_E0 + k] = v;
        grain_mlast[tr][k] = v;
    }
}
static int32_t grain_get(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = grain_tr(t), i;
    if (page >= GRAIN_NPAGES || col > 3u || (i = GRAIN_MAP[page][col]) == GQ_X)
        return 0;
    if (i & 0x80u)
        return t->p[i & 0x7Fu];
    return tr < GRAIN_NPART ? grain_patch[tr][i] : GRAIN_DEF[i];
}
static void grain_set(track_t *t, uint32_t page, uint32_t col, int32_t v)
{
    uint32_t tr = grain_tr(t), i, k;
    if (page >= GRAIN_NPAGES || col > 3u || (i = GRAIN_MAP[page][col]) == GQ_X)
        return;
    if (i & 0x80u) {                             /* a track parameter: the column's range */
        const param_desc_t *d = &GRAIN_PAGES[page].col[col];
        t->p[i & 0x7Fu] = (int16_t)clamp(v, d->min, d->max);
        RING_PUBLISH();
        return;
    }
    if (tr >= GRAIN_NPART)
        return;
    grain_patch[tr][i] = grain_clampv(i, v);
    for (k = 0; k < 8u; k++)                     /* a macro's value: the track's P_E too (grain_block sees no change) */
        if (GRAIN_MAC[k] == i) {
            t->p[P_E0 + k] = grain_patch[tr][i];
            RING_PUBLISH();
            grain_mlast[tr][k] = grain_patch[tr][i];
        }
}
/* the patch <-> the blob: 'G', version, the values as bytes (PITCH signed), zeros */
static void grain_pack(const int8_t *p, uint8_t *b)
{
    uint32_t i;
    memset(b, 0, GRAIN_BLOB);
    b[0] = GRAIN_MAGIC;
    b[1] = GRAIN_VER;
    for (i = 0; i < GQ_NP; i++)
        b[2 + i] = (uint8_t)p[i];
}
static int grain_unpack(const uint8_t *b, int8_t *p)      /* 1 = a valid blob */
{
    uint32_t i;
    if (!b || b[0] != GRAIN_MAGIC || b[1] != GRAIN_VER)
        return 0;
    for (i = 0; i < GQ_NP; i++)
        p[i] = grain_clampv(i, (int8_t)b[2 + i]);
    return 1;
}
static void grain_blob_get(const track_t *t, uint8_t *out)
{
    uint32_t tr = grain_tr(t);
    int8_t p[GQ_NP];
    if (tr < GRAIN_NPART)
        grain_pack(grain_patch[tr], out);
    else {
        grain_init_patch(p);
        grain_pack(p, out);
    }
}
static void grain_blob_set(track_t *t, const uint8_t *in)
{
    uint32_t tr = grain_tr(t);
    int8_t p[GQ_NP];
    if (tr >= GRAIN_NPART)
        return;
    if (!grain_unpack(in, p))
        grain_init_patch(p);
    memcpy(grain_patch[tr], p, GQ_NP);
    grain_macros_out(t);
}

/* ------------------------------------------------------------ presets --- */
/* the pool after INIT (docs/PRESETS.md): each preset's patch (the sources' TYPE TONE MOTION SEED twice, SIZE DENS
 * POS SPRD, MORPH RAND PITCH SCAN, CUT RES) once, as a list; preset_t takes its macros from the same list (GM), with
 * the ADSR and the sends */
#define GP_CLOUD_PAD GT_DRONE, 64, 64, 0, GT_BELL, 64, 64, 0, 92, 88, 50, 40, 40, 14, 0, 30, 100, 20
#define GP_SHIMMER GT_BELL, 64, 64, 0, GT_BELL, 104, 104, 11, 70, 100, 30, 30, 64, 24, 12, 20, 110, 10
#define GP_BREATH_PAD GT_BREATH, 64, 64, 0, GT_VOWEL, 64, 20, 3, 92, 88, 50, 40, 64, 10, 0, 30, 96, 30
#define GP_FROZEN GT_VOWEL, 28, 20, 3, GT_BREATH, 28, 20, 3, 108, 72, 40, 0, 0, 10, 0, 12, 92, 10
#define GP_STRUM GT_PLUCK, 64, 100, 0, GT_METAL, 64, 64, 0, 60, 80, 20, 30, 0, 10, 0, 0, 110, 0
#define GP_SWARM GT_CHIRP, 64, 64, 0, GT_SWEEP, 64, 64, 0, 48, 110, 50, 60, 64, 60, 0, 50, 120, 20
#define GP_ACID_DUST GT_SWEEP, 104, 104, 11, GT_PLUCK, 104, 104, 11, 30, 118, 50, 80, 40, 40, 0, 60, 127, 40
#define GP_GLITCH GT_METAL, 64, 64, 0, GT_DRONE, 64, 64, 0, 24, 112, 64, 100, 64, 90, 0, 0, 127, 0
static const int8_t GRAIN_PP[][GQ_NP] = {
    {GP_CLOUD_PAD}, {GP_SHIMMER}, {GP_BREATH_PAD}, {GP_FROZEN}, {GP_STRUM}, {GP_SWARM}, {GP_ACID_DUST}, {GP_GLITCH},
};
/* the macros of a patch list: source A's four, MORPH SIZE DENS POS (GRAIN_MAC's order) */
#define GM_(t1, n1, m1, s1, t2, n2, m2, s2, sz, dn, ps, sp, mo, ra, pi, sc, cu, re) {t1, n1, m1, s1, mo, sz, dn, ps}
#define GM(...) GM_(__VA_ARGS__)
static const preset_t GRAIN_PRESETS[] = {
    /* name, the macros, {A D S R}, fenv, mono */
    {"CLOUD PAD", GM(GP_CLOUD_PAD), {70, 90, 120, 90}, 0, 0, FX(0, 40, 25, 85), PAT(5)},
    {"SHIMMER", GM(GP_SHIMMER), {30, 90, 110, 90}, 0, 0, FX(0, 50, 40, 90), PAT(7)},
    {"BREATH PAD", GM(GP_BREATH_PAD), {60, 90, 120, 100}, 0, 0, FX(0, 30, 30, 90), PAT(5)},
    {"FROZEN", GM(GP_FROZEN), {50, 100, 127, 100}, 0, 0, FX(0, 30, 20, 95), PAT(5)},
    {"STRUM", GM(GP_STRUM), {0, 70, 100, 60}, 0, 0, FX(0, 20, 30, 60), PAT(3)},
    {"SWARM", GM(GP_SWARM), {10, 80, 110, 70}, 0, 0, FX(0, 30, 40, 70), PAT(4)},
    {"ACID DUST", GM(GP_ACID_DUST), {0, 60, 90, 40}, 0, 0, FX(20, 0, 40, 30), PAT(1)},
    {"GLITCH", GM(GP_GLITCH), {0, 70, 100, 30}, 0, 0, FX(10, 0, 50, 25), PAT(4)},
};
#define GRAIN_NPRESETS ((uint32_t)NELEM(GRAIN_PRESETS))
_Static_assert(NELEM(GRAIN_PP) == NELEM(GRAIN_PRESETS), "GRAIN presets");
static void grain_blob_preset(track_t *t, uint32_t k)
{
    uint32_t tr = grain_tr(t);
    if (tr >= GRAIN_NPART)
        return;
    memcpy(grain_patch[tr], GRAIN_PP[k < GRAIN_NPRESETS ? k : 0], GQ_NP);
    grain_macros_out(t);
}

/* a sound load put GRAIN into track t (eng_fm6.c fm6_track_loaded calls it on every load path: a factory preset, a
 * user slot, INIT, an engine switch): its patch. A user slot (grain_user_pending, upool.c up_loading): its stored
 * patch; a slot without one is a Felucca GRAIN record (SRC POS SIZE DENS PTCH SPRD RAND TONE as its P_E): INIT's
 * sources with its SIZE DENS POS SPRD PITCH RAND and TONE as CUT; else a preset whose macros the track holds: its
 * patch; else the init patch with the track's macros on it (INIT) */
static void grain_track_loaded(const track_t *ct)
{
    track_t *t = (track_t *)ct;
    uint32_t tr = grain_tr(t), pend = grain_user_pending, k;
    uint8_t b[GRAIN_BLOB];
    int8_t p[GQ_NP];
    grain_user_pending = 0;
    if (tr >= GRAIN_NPART || t->eng_req != ENGI_GRAIN)
        return;
    if (pend && grain_store_read && !grain_store_read(pend - 1u, b) && grain_unpack(b, p)) {
        memcpy(grain_patch[tr], p, GQ_NP);
        grain_macros_out(t);
        return;
    }
    grain_init_patch(p);
    if (pend) {                                  /* an old record: Felucca's eight in their old places */
        p[GQ_SIZE] = grain_clampv(GQ_SIZE, t->p[P_E2]);
        p[GQ_DENS] = grain_clampv(GQ_DENS, t->p[P_E3]);
        p[GQ_POS] = grain_clampv(GQ_POS, t->p[P_E1]);
        p[GQ_SPRD] = grain_clampv(GQ_SPRD, t->p[P_E5]);
        p[GQ_PITCH] = grain_clampv(GQ_PITCH, t->p[P_E4]);
        p[GQ_RAND] = grain_clampv(GQ_RAND, t->p[P_E6]);
        p[GQ_CUT] = grain_clampv(GQ_CUT, t->p[P_E7]);
        memcpy(grain_patch[tr], p, GQ_NP);
        grain_macros_out(t);
        return;
    }
    if (t->preset < GRAIN_NPRESETS) {
        for (k = 0; k < 8u && t->p[P_E0 + k] == GRAIN_PRESETS[t->preset].e[k]; k++)
            ;
        if (k == 8u) {
            grain_blob_preset(t, t->preset);
            return;
        }
    }
    for (k = 0; k < 8u; k++)
        p[GRAIN_MAC[k]] = grain_clampv(GRAIN_MAC[k], t->p[P_E0 + k]);
    memcpy(grain_patch[tr], p, GQ_NP);
    grain_macros_out(t);
}

/* -------------------------------------------------------------- the player --- */
#if !FELUCCA_SAMPLE
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (eng_sample.c's, when SAMPLE is not built) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}
#endif
/* sin^2(pi i / 256), Q15: the grain window */
static const int16_t GP_HANN[257] = {
    0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593, 705, 827, 958, 1098,
    1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542, 2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518,
    4799, 5086, 5381, 5682, 5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
    10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378, 14778, 15178, 15580, 15981,
    16383, 16786, 17187, 17589, 17989, 18389, 18787, 19184, 19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280,
    22653, 23023, 23388, 23750, 24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
    27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639, 30832, 31018, 31194, 31361,
    31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369, 32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762,
    32767, 32762, 32747, 32723, 32688, 32644, 32590, 32526, 32452, 32369, 32276, 32174, 32062, 31940, 31809, 31669,
    31520, 31361, 31194, 31018, 30832, 30639, 30436, 30225, 30006, 29778, 29543, 29299, 29048, 28789, 28523, 28249,
    27968, 27681, 27386, 27085, 26777, 26463, 26143, 25817, 25486, 25149, 24806, 24459, 24107, 23750, 23388, 23023,
    22653, 22280, 21903, 21523, 21139, 20753, 20364, 19973, 19580, 19184, 18787, 18389, 17989, 17589, 17187, 16786,
    16384, 15981, 15580, 15178, 14778, 14378, 13980, 13583, 13187, 12794, 12403, 12014, 11628, 11244, 10864, 10487,
    10114, 9744, 9379, 9017, 8660, 8308, 7961, 7618, 7281, 6950, 6624, 6304, 5990, 5682, 5381, 5086,
    4799, 4518, 4244, 3978, 3719, 3468, 3224, 2989, 2761, 2542, 2331, 2128, 1935, 1749, 1573, 1406,
    1247, 1098, 958, 827, 705, 593, 491, 398, 315, 241, 177, 123, 79, 44, 20, 5,
    0,
};
/* grain level by overlap (grains sounding at once on average): 1 / sqrt(n), Q15 */
static const int16_t GP_NORM[GP_NG + 1] = {32767, 32767, 23170, 18918, 16384, 14654, 13377, 12385, 11585, 10922,
                                           10362, 9880, 9459, 9088, 8757, 8460, 8192};
typedef struct {
    const int16_t *src;          /* the buffer it reads (A or B) */
    uint32_t pos;                /* forward: the next source sample to fetch; reverse: the index of a */
    int32_t frac;                /* Q16 between a and b */
    uint32_t step;               /* Q16 source samples an output sample */
    uint32_t wph, winc, left;    /* window phase (one turn = the grain), output samples to go */
    int32_t a, b, gain;
    uint8_t owner;               /* voice index + 1, 0 = free */
    uint8_t rev, kill;           /* kill: its buffer is about to be rendered: fade over this block */
} gp_grain_t;
typedef struct {
    gp_grain_t g[GP_NG];
    uint32_t scan;               /* SCAN's phase */
    int32_t rng;
} gp_part_t;
static gp_part_t *grain_part(uint32_t part);     /* engines.c: eng_state's member, 0 for a part without one */
static int16_t grain_buf[2][GS_N] __attribute__((section(".pool")));   /* the sources A and B */

/* the scheduler: the audio side (grain_tick, core 0) posts; the main loop (grain_poll) renders */
static struct {
    volatile uint8_t req;        /* bit k: source k is to be rendered */
    volatile uint8_t lock;       /* bit k: source k is being rendered: no grain starts on it (the poll holds it) */
    volatile uint8_t ack;        /* bit k: the audio side saw the lock and faded the grains on it */
    volatile uint32_t blocks;    /* audio blocks seen (grain_tick) */
    volatile uint32_t fade_block;   /* the block that faded them: the poll writes once the next has begun */
    volatile int32_t root16[2];  /* each buffer's root pitch (PLUCK's is off C4) */
    uint8_t cur[2][4];           /* the knobs each buffer holds (or is being rendered toward); 0xFF: nothing yet */
    uint8_t want[2][4];          /* what the owner's patch asks */
    uint8_t stage, which;        /* the poll's: 0 idle, 1 locked and waiting for the fade, 2 rendering `which` */
    uint8_t owner;               /* the part whose sources render + 1, 0 none */
    uint32_t renders;            /* renders completed (the tests, GEEK OUT) */
    gs_t g;
} grs = {0, 0, 0, 0, 0, {GS_ROOT16, GS_ROOT16}, {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}}, {{0}}, 0, 0, 0, 0, {0}};

static inline uint32_t gp_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }
static inline int32_t gp_win(uint32_t wph)
{
    uint32_t wi = wph >> 24;
    return GP_HANN[wi] + (((GP_HANN[wi + 1u] - GP_HANN[wi]) * (int32_t)((wph >> 9) & 0x7FFFu)) >> 15);
}

/* start a grain of voice v (vi) at its interval iv (for the overlap's level); morph the voice's MORPH now (0 .. 127);
 * pos_q8 the centre (POS + SCAN), 0 .. 127 << 8 */
static void gp_spawn(gp_part_t *P, const int8_t *p, voice_t *v, uint32_t vi, const vmod_t *m, uint32_t iv,
                     int32_t morph, int32_t pos_q8)
{
    gp_grain_t *g = 0;
    uint32_t i, n = GS_N, len, step, span, c, sp, r1, r2, ov, rev, which;
    int32_t d16, amt, pos;
    for (i = 0; i < GP_NG && !g; i++)
        if (!P->g[i].owner)
            g = &P->g[i];
    if (!g)
        return;
    v->s[1] += morph;                            /* the voice's grains read B in MORPH's share, spread evenly */
    which = v->s[1] >= 127 ? 1u : 0u;
    if (which)
        v->s[1] -= 127;
    if ((grs.lock >> which) & 1u)                /* its buffer is being rendered: this grain is not */
        return;
    r1 = noise32(&P->rng);
    r2 = noise32(&P->rng);
    len = (441u * pow2_q16(p[GQ_SIZE] * 8)) >> 16;         /* 10 .. 393 ms */
    amt = p[GQ_RAND] * p[GQ_RAND] * 192 / (127 * 127);
    d16 = m->pitch16 - grs.root16[which] + p[GQ_PITCH] * 16 + (((int32_t)(r1 & 0xFFFFu) - 32768) * amt >> 15);
    step = (pow2_q16(clamp(d16, -1536, 576)) >> 8) * (GP_RATE_Q16 >> 8);
    step = step > GP_STEP_MAX ? GP_STEP_MAX : step < 256u ? 256u : step;
    rev = ((r1 >> 16) & 255u) < (uint32_t)p[GQ_RAND];       /* RAND 127: half of them */
    span = (len * (step >> 4)) >> 12;
    if (span + 3u > n) {
        span = n - 3u;
        len = (span << 8) / (step >> 8);
    }
    if (len < 16u)
        return;
    c = gp_scale(n - 1u, (uint32_t)clamp(pos_q8, 0, 127 << 8) * 65535u / (127u << 8));
    sp = gp_scale(n, (uint32_t)p[GQ_SPRD] * 516u);
    pos = (int32_t)c + (int32_t)gp_scale(sp, r2 & 0xFFFFu) - (int32_t)(sp >> 1);
    pos = rev ? clamp(pos, (int32_t)span, (int32_t)n - 2) : clamp(pos, 0, (int32_t)(n - 3u - span));
    g->src = grain_buf[which];
    g->step = step;
    g->frac = 0;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    g->left = len;
    g->rev = (uint8_t)rev;
    g->kill = 0;
    ov = len / (iv ? iv : 1u);
    g->gain = GP_NORM[ov < 1u ? 1u : ov > GP_NG ? GP_NG : ov];
    g->owner = (uint8_t)(vi + 1u);
    g->a = g->src[pos];
    g->b = g->src[pos + 1];
    g->pos = rev ? (uint32_t)pos : (uint32_t)pos + 2u;
}

/* one grain into acc over n samples; returns 0 when it ended. The window (times the grain's level) ramps linearly
 * over the block between its exact values at the block's ends; a killed grain ramps to 0 and ends here */
static int gp_run(gp_grain_t *g, int32_t *acc, uint32_t n)
{
    uint32_t i, m = n < g->left ? n : g->left, step = g->step, pos = g->pos;
    int32_t a = g->a, b = g->b, frac = g->frac, w0, w1, wq, dq;
    const int16_t *src = g->src;
    if (g->kill)
        g->left = m;
    w0 = mulq15(gp_win(g->wph), g->gain);
    g->wph += g->winc * m;
    g->left -= m;
    w1 = g->left ? mulq15(gp_win(g->wph), g->gain) : 0;
    wq = w0 * 1024;
    dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
    if (!g->rev) {
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac += (int32_t)step;
            while (frac >= 65536) {
                frac -= 65536;
                a = b;
                b = pos < GS_N ? src[pos++] : a;
            }
        }
    } else {
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac -= (int32_t)step;
            while (frac < 0) {
                frac += 65536;
                b = a;
                if (pos)
                    a = src[--pos];
            }
        }
    }
    g->pos = pos;
    g->a = a;
    g->b = b;
    g->frac = frac;
    if (!g->left)
        g->owner = 0;
    return g->left != 0;
}

static void grain_note_on(track_t *t, voice_t *v)
{
    uint32_t tr = grain_tr(t), vi = (uint32_t)(v - t->v) % NVOICE, i;
    gp_part_t *P = tr < GRAIN_NPART ? grain_part(tr) : 0;
    v->s[0] = 0;                                 /* the first grain at once */
    v->s[1] = 63;                                /* a half-way MORPH: the first grain A's, the second B's */
    if (!v->env) {                               /* a fresh voice: no grains left from before, the filter clear */
        if (P)
            for (i = 0; i < GP_NG; i++)
                if (P->g[i].owner == vi + 1u)
                    P->g[i].owner = 0;
        v->s[2] = v->s[3] = 0;
    }
}

/* SCAN's phase increment a block: a round of the buffer in 20 s (SCAN 1) .. 0.5 s (127), 0 off */
static uint32_t grain_scan_inc(int32_t scan)
{
    return scan ? (((155834u >> 4) * (pow2_q16(scan * 8) >> 4)) >> 8) : 0u;
}

/* per part and block: the macros into the patch, grains of ended voices go, SCAN moves */
static void grain_block(track_t *t)
{
    uint32_t tr = grain_tr(t), k;
    gp_part_t *P;
    int8_t *p;
    if (tr >= GRAIN_NPART || !(P = grain_part(tr)))
        return;
    p = grain_patch[tr];
    for (k = 0; k < 8u; k++)                     /* a knob, the editor, the matrix, motion moved a macro */
        if (t->p[P_E0 + k] != grain_mlast[tr][k]) {
            grain_mlast[tr][k] = t->p[P_E0 + k];
            p[GRAIN_MAC[k]] = grain_clampv(GRAIN_MAC[k], t->p[P_E0 + k]);
        }
    if (!P->rng)
        P->rng = 0x2545F491;
    for (k = 0; k < GP_NG; k++)
        if (P->g[k].owner && !t->v[(P->g[k].owner - 1u) % NVOICE].active)
            P->g[k].owner = 0;
    P->scan += grain_scan_inc(p[GQ_SCAN]);
}

static void grain_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t tr = grain_tr(t), vi = (uint32_t)(v - t->v) % NVOICE, i, mine = 0, nact = 0, iv, share;
    gp_part_t *P = tr < GRAIN_NPART ? grain_part(tr) : 0;
    const int8_t *p;
    int32_t acc[CTL], morph, pos_q8, ic1 = v->s[2], ic2 = v->s[3];
    tsvf_t f;
    if (!P)
        return;
    p = grain_patch[tr];
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (i = 0; i < NVOICE; i++)
        nact += t->v[i].active;
    for (i = 0; i < GP_NG; i++)
        if (P->g[i].owner == vi + 1u)
            mine += (uint32_t)gp_run(&P->g[i], acc, n);
    iv = (44100u * pow2_q16(-p[GQ_DENS] * 10)) >> 16;      /* the next grain: DENS a second, 1 .. 98 /s */
    v->s[0] -= (int32_t)n;
    if (v->s[0] <= 0) {
        uint32_t r = noise32(&P->rng);
        int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * p[GQ_SPRD]) / 254;   /* +-SPRD / 2, Q15 */
        v->s[0] += (int32_t)iv + mulq15((int32_t)iv, j);
        if (v->s[0] < 0)
            v->s[0] = 0;
        share = nact > 1u ? (GP_NG / nact > 2u ? GP_NG / nact : 2u) : GP_NG;
        morph = clamp((p[GQ_MORPH] << 8) + m->shape - (64 << 8), 0, 127 << 8) >> 8;   /* the matrix's SHP */
        pos_q8 = (p[GQ_POS] << 8) + (p[GQ_SCAN] ? (osc_tri(P->scan) * (64 << 8)) >> 15 : 0);
        if (mine < share)
            gp_spawn(P, p, v, vi, m, iv, morph, pos_q8);
    }
    tsvf_coef(&f, clamp((p[GQ_CUT] << 8) + m->cutoff, 0, 127 << 8), p[GQ_RES]);
    for (i = 0; i < n; i++) {
        int32_t y = tsvf_lp(&f, clamp(acc[i], -65535, 65535), &ic1, &ic2);
        out[i] += voice_amp(clamp(y, -32767, 32767), m, i);
    }
    v->s[2] = ic1;
    v->s[3] = ic2;
}

/* ---------------------------------------------------------- the render --- */
/* the main loop's slice (GRAIN_ASYNC): lock, wait for the fade, render, unlock; latest wins */
static void grain_render_whole(uint32_t k);
#if GRAIN_ASYNC
static void fm1_irq_off(void);                   /* (hal/fm1_irq.h's, the emulator's: defined after this file) */
static void fm1_irq_on(void);
static void grain_poll(void)
{
    uint32_t k = grs.which, bit = 1u << k;
    switch (grs.stage) {
    case 0:
        if (!grs.req)
            return;
        k = (grs.req & 1u) ? 0u : 1u;
        bit = 1u << k;
        grs.which = (uint8_t)k;
        memcpy(grs.cur[k], grs.want[k], 4);
        fm1_irq_off();
        grs.req &= (uint8_t)~bit;
        grs.ack &= (uint8_t)~bit;
        grs.lock |= bit;
        fm1_irq_on();
        grs.stage = 1;
        return;
    case 1:
        if (!(grs.ack & bit) || grs.blocks == grs.fade_block)   /* the block that faded them must be over */
            return;
        gs_begin(&grs.g, grs.cur[k][0], grs.cur[k][1], grs.cur[k][2], grs.cur[k][3]);
        grs.stage = 2;
        /* fall through */
    default:
        if (memcmp(grs.cur[k], grs.want[k], 4)) {              /* a knob moved meanwhile: start over on the new values */
            memcpy(grs.cur[k], grs.want[k], 4);
            fm1_irq_off();
            grs.req &= (uint8_t)~bit;
            fm1_irq_on();
            gs_begin(&grs.g, grs.cur[k][0], grs.cur[k][1], grs.cur[k][2], grs.cur[k][3]);
        }
        if (gs_step(&grs.g, grain_buf[k], GRAIN_SLICE) < GS_N)
            return;
        gs_finish(grain_buf[k]);
        grs.root16[k] = grs.g.root16;
        grs.renders++;
        fm1_irq_off();
        grs.lock &= (uint8_t)~bit;
        grs.ack &= (uint8_t)~bit;
        fm1_irq_on();
        grs.stage = 0;
        return;
    }
}
#endif
/* a whole render here and now (GRAIN_ASYNC 0: the audio side itself, no grain reads meanwhile) */
static void grain_render_whole(uint32_t k)
{
    memcpy(grs.cur[k], grs.want[k], 4);
    grs.req &= (uint8_t)~(1u << k);
    gs_begin(&grs.g, grs.cur[k][0], grs.cur[k][1], grs.cur[k][2], grs.cur[k][3]);
    while (gs_step(&grs.g, grain_buf[k], GS_N) < GS_N)
        ;
    gs_finish(grain_buf[k]);
    grs.root16[k] = grs.g.root16;
    grs.renders++;
}

/* once an audio block, on core 0, before the parts render (fx.c mix_block): the owner's source knobs against the
 * buffers; a locked source's grains fade. Out of line: its own line in tests/target_budget.txt, not the ISR's */
static __attribute__((noinline)) void grain_tick(void)
{
    uint32_t k, part, own = 0;
    grs.blocks++;
    for (part = 0; part < GRAIN_NPART && !own; part++)
        if (trk[part].eng_req == ENGI_GRAIN || trk[part].engine == ENGI_GRAIN)
            own = part + 1u;
    grs.owner = (uint8_t)own;
    if (own) {
        const int8_t *p = grain_patch[own - 1u];
        for (k = 0; k < 2; k++) {
            uint8_t w[4] = {(uint8_t)p[GQ_TYPE1 + 4 * k], (uint8_t)p[GQ_TONE1 + 4 * k], (uint8_t)p[GQ_MOTION1 + 4 * k],
                            (uint8_t)p[GQ_SEED1 + 4 * k]};
            memcpy(grs.want[k], w, 4);
            if (memcmp(w, grs.cur[k], 4))          /* (not what the buffer holds, nor what renders toward) */
                grs.req |= (uint8_t)(1u << k);
        }
    }
    if (grs.lock & ~grs.ack) {                   /* a source about to be rendered: its grains fade over this block */
        for (part = 0; part < GRAIN_NPART; part++) {
            gp_part_t *P = grain_part(part);
            if (P)
                for (k = 0; k < GP_NG; k++)
                    if (P->g[k].owner && ((grs.lock & ~grs.ack) >> (P->g[k].src == grain_buf[1])) & 1u)
                        P->g[k].kill = 1;
        }
        grs.fade_block = grs.blocks;
        grs.ack |= grs.lock;
    }
#if !GRAIN_ASYNC
    while (grs.req) {                            /* no main loop: render now, whole (no grain reads meanwhile) */
        k = (grs.req & 1u) ? 0u : 1u;
        for (part = 0; part < GRAIN_NPART; part++) {
            gp_part_t *P = grain_part(part);
            uint32_t i;
            if (P)
                for (i = 0; i < GP_NG; i++)
                    if (P->g[i].owner && P->g[i].src == grain_buf[k])
                        P->g[i].owner = 0;
        }
        grain_render_whole(k);
    }
#endif
}

static const eng_deep_t GRAIN_DEEP = {
    .npages = NELEM(GRAIN_PAGES),
    .pages = GRAIN_PAGES,
    .section = {0, 4, 5, 6, 0xFF, 0xFF, 0xFF, 0xFF},
    .get = grain_get,
    .set = grain_set,
    .blob_size = GRAIN_BLOB,
    .blob_get = grain_blob_get,
    .blob_set = grain_blob_set,
    .blob_preset = grain_blob_preset,
};

static const engine_t ENG_GRAIN = {
    .name = "GRAIN",
    .page_title = {"SRC", "GRAIN"},
    .edit = {
        GC_TYPE(GT_DRONE),
        GC_TONE,
        GC_MOTION,
        GC_SEED,
        GC_MORPH,
        GC_SIZE,
        GC_DENS,
        GC_POS,
    },
    .presets = GRAIN_PRESETS,
    .npresets = NELEM(GRAIN_PRESETS),
    .knob = {P_E4, P_E5, P_E6, P_E7},
    .poly = GP_POLY,
    .sampled = 1,
    .note_on = grain_note_on,
    .render = grain_render,
    .block = grain_block,
    .deep = &GRAIN_DEEP,
};
