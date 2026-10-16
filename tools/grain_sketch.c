/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* GRAIN's sketches (docs/GRAIN.md): a source generator that renders its own material from four knobs (TYPE TONE
 * MOTION SEED) into a short buffer, and a grain player over that buffer (Felucca's eng_grain.c player without the
 * ADPCM decoder), rendered to WAVs for the owner to hear before the engine is built.
 *
 *   cc -O2 -w -Ibuild/gen -o build/host/grain_sketch tools/grain_sketch.c -lm
 *   build/host/grain_sketch build/grain-sketch              (the WAVs and INDEX.md there; ~130 files, ~30 MB)
 *   build/host/grain_sketch build/grain-sketch src          (the sources and the seed tours only, no granulation)
 *
 * The core (gs_* the generator, gp_* the player) is written as the firmware would have it: int32 fixed point, no
 * float, no libc, dsp.c's blocks (osc_saw with its polyBLEP, osc_pulse, osc_tri, sine_i, the trapezoidal SVF,
 * noise32, softclip) and the tables of build/gen/felucca_tables.h (pitch_inc, SVF_G). The generator renders in
 * slices (gs_begin, gs_step n samples at a time, gs_finish): the firmware chunks the render across main-loop turns
 * or core 1 jobs, the grains playing the old buffer meanwhile. GS_UNIT 1 compiles the core alone for the device
 * compiler (tools/grain_cost.py counts its flash and its loops from the listing). */
#include <stdint.h>
#ifndef GS_UNIT
#define GS_UNIT 0
#endif
#if !GS_UNIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)
#define GS_API static
#define GS_FAM static
#else
#define GS_API
#define GS_FAM static __attribute__((noinline))   /* one listing a family (tools/grain_cost.py) */
#endif
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"

/* ======================================================================= the source generator (gs_) === */
#define GS_RATE 22050u           /* the buffer's rate: half the output rate (the delay and the reverb run there too) */
#ifndef GS_N
#define GS_N 16384u              /* samples a buffer: 743 ms, 32 KB (docs/GRAIN.md "The buffer") */
#endif
#define GS_ROOT16 960            /* C4: the pitch the material is rendered at; the player repitches against it */
#define GS_PEAK 28000            /* gs_finish normalizes the buffer to this peak */
#define GS_NTYPE 8
enum { GT_PLUCK, GT_DRONE, GT_BELL, GT_BREATH, GT_CHIRP, GT_SWEEP, GT_VOWEL, GT_METAL };
static const char *const GS_TYPE_NAMES[GS_NTYPE] = {"PLUCK", "DRONE", "BELL", "BREATH", "CHIRP", "SWEEP", "VOWEL", "METAL"};

/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (eng_sample.c's) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}
/* a phase increment at the buffer's rate (pitch_inc is 44.1 kHz's: twice it) */
static inline uint32_t gs_inc(int32_t p16) { return pitch_inc((uint32_t)clamp(p16, 0, 2047)) << 1; }
/* the SVF's band-pass output (dsp.c has the low-pass; eng_va.c takes v1 the same way) */
static inline int32_t gs_svf_bp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v1;
}
/* exp decay: x -= x * k >> 16 once a control tick (32 samples) gives the time constant tau_ms */
static inline int32_t gs_kctl(uint32_t tau_ms) { return (int32_t)(65536u * 32u * 1000u / (GS_RATE * (tau_ms ? tau_ms : 1u)) / 1u); }
/* a 32-bit phase increment for a rate in hundredths of a hertz */
static inline uint32_t gs_lfo_inc(uint32_t hz_x100) { return hz_x100 * 1948u; }   /* 2^32 / 22050 / 100 = 1947.8 */
static inline int16_t gs_clip(int32_t y) { return (int16_t)clamp(y, -32767, 32767); }

typedef struct {
    uint8_t type, tone, motion, seed;
    uint32_t pos;                /* samples rendered so far */
    int32_t root16;              /* the pitch the buffer holds (PLUCK's integer period moves it off GS_ROOT16) */
    int32_t rng;
    uint32_t ph[8], inc[8];      /* oscillators */
    int32_t a[8];                /* their levels / indices (decaying) */
    int32_t s[12];               /* the family's other state */
    tsvf_t f[3];
    int32_t ic[6];
} gs_t;

static inline uint32_t gs_rnd(gs_t *g) { return noise32(&g->rng); }
static inline int32_t gs_r15(gs_t *g) { return (int32_t)(gs_rnd(g) >> 17); }          /* 0..32767 */
static inline int32_t gs_white(gs_t *g) { return gs_r15(g) * 2 - 32767; }             /* -32767..32767 */

/* ---- PLUCK: a Karplus-Strong string, the buffer its own delay line. TONE: the burst's and the loop's brightness.
 * MOTION: the decay (long ring .. damped), above 64 re-plucks (a strum of up to five) */
#define GS_PLUCK_N 84u           /* the period: 22050 / 84 = 262.5 Hz */
#define GS_PLUCK_ROOT16 961      /* .. is C4 + 6 ct */
static void gs_pluck_begin(gs_t *g)
{
    g->root16 = GS_PLUCK_ROOT16;
    g->s[0] = 1500 + g->tone * 240;                         /* the burst's low-pass, Q15 */
    g->s[1] = 65536 - 30 - g->motion * 7;                   /* the loop's decay, Q16 */
    g->s[2] = 16384 - g->tone * 100;                        /* the loop's second tap: 1/2 (dull) .. 1/9 (bright) */
    g->s[3] = g->motion > 64 ? (int32_t)(GS_N / (1u + ((uint32_t)g->motion - 64u) / 14u)) : 0;   /* re-pluck period */
    g->s[4] = GS_PLUCK_N;                                    /* burst samples to go */
    g->s[5] = 0;                                             /* the burst filter */
    g->s[6] = g->s[3];                                       /* the next re-pluck */
}
GS_FAM void gs_pluck(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    int32_t k = g->s[0], dec = g->s[1], b = g->s[2], a = 32768 - b, lp = g->s[5];
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t y = 0;
        if (i >= GS_PLUCK_N + 1u)
            y = (((buf[i - GS_PLUCK_N] * a + buf[i - GS_PLUCK_N - 1u] * b) >> 15) * dec) >> 16;
        if (g->s[4] > 0) {
            lp += ((gs_white(g) - lp) * k) >> 15;
            y += lp >> 1;
            g->s[4]--;
        }
        if (g->s[3] && (int32_t)i == g->s[6]) {
            g->s[4] = GS_PLUCK_N;
            g->s[6] += g->s[3];
        }
        buf[i] = gs_clip(y);
    }
    g->s[5] = lp;
}

/* ---- DRONE: three detuned saws (the third a pulse on odd seeds) and a sub sine through the SVF, its cutoff swept
 * by a triangle LFO. TONE: the cutoff. MOTION: the detune and the sweep's rate */
static void gs_drone_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16), d = 80u + (uint32_t)g->motion * 12u;    /* Q16: 0.12 .. 2.4 % */
    uint32_t i;
    g->inc[0] = inc0;
    g->inc[1] = inc0 + (uint32_t)mulq16((int32_t)inc0, d);
    g->inc[2] = inc0 - (uint32_t)mulq16((int32_t)inc0, d);
    g->inc[3] = inc0 >> 1;
    for (i = 0; i < 4; i++)
        g->ph[i] = gs_rnd(g);
    g->s[0] = (g->seed & 1u) ? (int32_t)(0x20000000u + ((uint32_t)gs_r15(g) << 13)) : 0;   /* the pulse's width */
    g->ph[4] = gs_rnd(g);                                    /* the LFO */
    g->inc[4] = gs_lfo_inc(25u + (uint32_t)g->motion * 3u);  /* 0.25 .. 4.06 Hz */
    g->s[1] = (40 << 8) + (g->tone * 70 << 8) / 127;        /* the cutoff's centre, Q8 index 40 .. 110 */
    g->s[2] = ((16 + g->motion / 4) << 8);                   /* the sweep's depth */
    g->s[3] = 40 + ((g->seed >> 1) & 3u) * 20;               /* resonance */
}
GS_FAM void gs_drone(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    uint32_t i, pw = (uint32_t)g->s[0];
    for (i = i0; i < i1; i++) {
        int32_t x, y;
        if (!(i & 31u)) {
            int32_t cut = g->s[1] + ((osc_tri(g->ph[4]) * g->s[2]) >> 15);
            tsvf_coef(&g->f[0], cut, g->s[3]);
            g->ph[4] += g->inc[4] * 32u;
        }
        x = osc_saw(g->ph[0], g->inc[0]) + osc_saw(g->ph[1], g->inc[1]);
        x += pw ? osc_pulse(g->ph[2], g->inc[2], pw) : osc_saw(g->ph[2], g->inc[2]);
        x += (sine_i(g->ph[3]) * 13107) >> 15;
        g->ph[0] += g->inc[0];
        g->ph[1] += g->inc[1];
        g->ph[2] += g->inc[2];
        g->ph[3] += g->inc[3];
        y = tsvf_lp(&g->f[0], x >> 2, &g->ic[0], &g->ic[1]);
        buf[i] = gs_clip(soft_knee(y, 24000));
    }
}

/* ---- BELL: two-operator FM, the modulator at an inharmonic ratio (by SEED), the index decaying. TONE: the index.
 * MOTION: the index's decay (slow .. a tick), above 64 a second detuned pair beats against the first */
static const uint16_t GS_BELL_RATIO[12] = {361, 512, 707, 888, 1075, 1329, 1792, 768, 443, 597, 128, 384};   /* Q8 */
static void gs_bell_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16), r = GS_BELL_RATIO[g->seed % 12u], tau;
    g->inc[0] = inc0;
    g->inc[1] = (inc0 >> 8) * r;
    g->ph[1] = gs_rnd(g) & 0x3FFFFFFFu;
    g->a[0] = ((100 + g->tone * 16) * 20861) >> 8;           /* the index: 0.4 .. 8.3 rad, as (sine >> 3) x this */
    tau = 600u - (uint32_t)g->motion * 560u / 127u;          /* 600 .. 40 ms */
    g->s[0] = gs_kctl(tau);
    g->a[1] = 32767;
    g->s[1] = gs_kctl(450);
    g->s[2] = g->motion > 64;
    if (g->s[2]) {
        g->inc[2] = inc0 + (inc0 >> 12) * ((uint32_t)g->motion - 64u) / 2u;   /* up to 0.8 %: a 2 .. 4 Hz beat */
        g->inc[3] = (g->inc[2] >> 8) * r;
        g->ph[3] = gs_rnd(g) & 0x3FFFFFFFu;
    }
}
GS_FAM void gs_bell(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t y, amp;
        if (!(i & 31u)) {
            g->a[0] -= (g->a[0] * g->s[0]) >> 16;
            if (i >= 64u)
                g->a[1] -= (g->a[1] * g->s[1]) >> 16;
        }
        amp = i < 64u ? (int32_t)i * 512 : g->a[1];
        y = sine_i(g->ph[0] + (uint32_t)((sine_i(g->ph[1]) >> 3) * g->a[0]));
        if (g->s[2]) {
            y += sine_i(g->ph[2] + (uint32_t)((sine_i(g->ph[3]) >> 3) * g->a[0])) >> 1;
            g->ph[2] += g->inc[2];
            g->ph[3] += g->inc[3];
            y = (y * 21845) >> 15;
        }
        g->ph[0] += g->inc[0];
        g->ph[1] += g->inc[1];
        buf[i] = gs_clip((y * amp) >> 15);
    }
}

/* ---- BREATH: white noise through a resonant band-pass whose centre wanders (a random walk) under a tremolo.
 * TONE: the centre. MOTION: the walk's depth and the tremolo's rate and depth */
static void gs_breath_begin(gs_t *g)
{
    g->s[0] = (45 << 8) + (g->tone * 63 << 8) / 127;        /* the centre, Q8 index 45 .. 108 */
    g->s[1] = g->s[0];                                       /* the cutoff now */
    g->s[2] = g->s[0];                                       /* the walk's target */
    g->s[3] = (g->motion * 24 / 127) << 8;                   /* the walk's depth */
    g->s[4] = 60 + ((g->seed >> 2) & 3u) * 15;               /* resonance */
    g->inc[0] = gs_lfo_inc(30u + (uint32_t)g->motion * 5u);  /* the tremolo: 0.3 .. 6.6 Hz */
    g->ph[0] = gs_rnd(g);
    g->a[0] = g->motion * 32767 / 254;                       /* its depth: up to a half */
    g->a[1] = 32767;
}
GS_FAM void gs_breath(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        if (!(i & 31u)) {
            if (!(i & 1023u))
                g->s[2] = g->s[0] + (((gs_r15(g) - 16384) * g->s[3]) >> 14);
            g->s[1] += (g->s[2] - g->s[1]) >> 4;
            tsvf_coef(&g->f[0], g->s[1], g->s[4]);
            g->a[1] = 32767 - ((g->a[0] * (32767 - osc_tri(g->ph[0]))) >> 16);
            g->ph[0] += g->inc[0] * 32u;
        }
        buf[i] = gs_clip((gs_svf_bp(&g->f[0], gs_white(g) >> 1, &g->ic[0], &g->ic[1]) * g->a[1]) >> 15);
    }
}

/* ---- CHIRP: a sine sweeping over a span of semitones, repeated; a second one a fifth up on some seeds. TONE: the
 * span (half an octave .. nearly five). MOTION: how many chirps fill the buffer (1 .. 16) */
static void gs_chirp_begin(gs_t *g)
{
    int32_t span = 16 * (6 + g->tone * 51 / 127), c = 1 + g->motion / 8;
    g->s[0] = (int32_t)(GS_N / (uint32_t)c);                 /* a chirp's length */
    g->s[1] = (int32_t)((uint32_t)span * 65536u / (uint32_t)g->s[0]);   /* the pitch's step a sample, Q16 */
    g->s[2] = (GS_ROOT16 - span / 2) << 16;                  /* the start */
    g->s[3] = span << 16;
    g->s[4] = (g->seed & 1u) ? -1 : 1;                       /* down / up */
    g->s[5] = (g->seed >> 1) & 1u;                           /* alternate */
    g->s[6] = (g->seed >> 2) & 1u;                           /* the second voice */
    g->s[7] = 0;                                             /* the sample within the chirp */
    g->s[8] = g->s[4] > 0 ? g->s[2] : g->s[2] + g->s[3];     /* the pitch now, Q16 */
}
GS_FAM void gs_chirp(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t k = g->s[7], gain, y, p;
        uint32_t inc;
        if (k >= g->s[0]) {
            k = g->s[7] = 0;
            if (g->s[5])
                g->s[4] = -g->s[4];
            g->s[8] = g->s[4] > 0 ? g->s[2] : g->s[2] + g->s[3];
        }
        gain = k < 64 ? k * 512 : g->s[0] - 1 - k < 64 ? (g->s[0] - 1 - k) * 512 : 32767;
        p = g->s[8] >> 16;
        inc = gs_inc(p);
        g->ph[0] += inc;
        y = sine_i(g->ph[0]);
        if (g->s[6]) {
            g->ph[1] += gs_inc(p + 112);
            y = ((y + (sine_i(g->ph[1]) >> 1)) * 21845) >> 15;
        }
        g->s[8] += g->s[4] * g->s[1];
        g->s[7] = k + 1;
        buf[i] = gs_clip((y * gain) >> 15);
    }
}

/* ---- SWEEP: a saw and a pulse (an octave down on some seeds), driven, through the SVF with its cutoff sweeping
 * down, up or both. TONE: the resonance and the sweep's top. MOTION: how many sweeps fill the buffer (1 .. 8) */
static void gs_sweep_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16);
    g->inc[0] = inc0;
    g->inc[1] = (g->seed & 4u) ? inc0 >> 1 : inc0;
    g->ph[0] = gs_rnd(g);
    g->ph[1] = gs_rnd(g);
    g->s[0] = (int32_t)(0x80000000u - (g->seed & 3u) * 0x10000000u);   /* the pulse's width: 50 .. 12.5 % */
    g->s[1] = 30 + g->tone * 89 / 127;                       /* resonance 30 .. 119 */
    g->s[2] = 20 << 8;                                       /* the cutoff's floor */
    g->s[3] = ((70 + g->tone * 42 / 127) << 8) - g->s[2];    /* .. and its span to the top (70 .. 112) */
    g->s[4] = (int32_t)(GS_N / (1u + (uint32_t)g->motion * 7u / 127u));   /* a sweep's length */
    g->s[5] = (g->seed >> 3) & 3u;                           /* 0 down, 1 up, 2 up then down, 3 down then up */
    g->s[6] = 0;
}
GS_FAM void gs_sweep(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    uint32_t i, pw = (uint32_t)g->s[0];
    for (i = i0; i < i1; i++) {
        int32_t x, y;
        if (!(i & 31u)) {
            int32_t k = g->s[6], L = g->s[4], f;             /* the sweep's position 0 .. 65535 */
            f = (int32_t)(((uint32_t)k << 16) / (uint32_t)L);
            if (g->s[5] >= 2)
                f = f < 32768 ? f * 2 : (65535 - f) * 2;
            if (g->s[5] == 0 || g->s[5] == 3)
                f = 65535 - f;
            tsvf_coef(&g->f[0], g->s[2] + ((g->s[3] * f) >> 16), g->s[1]);
            g->s[6] = k + 32 >= L ? 0 : k + 32;
        }
        x = (osc_saw(g->ph[0], g->inc[0]) + osc_pulse(g->ph[1], g->inc[1], pw)) >> 1;
        g->ph[0] += g->inc[0];
        g->ph[1] += g->inc[1];
        x = softclip(x + (x >> 1));
        y = tsvf_lp(&g->f[0], x, &g->ic[0], &g->ic[1]);
        buf[i] = gs_clip(soft_knee(y, 20000));
    }
}

/* ---- VOWEL: a narrow pulse at the root with vibrato through three band-passes at a vowel's formants, the vowel
 * travelling round A E I O U. TONE: where on the cycle it starts. MOTION: how far it travels (none .. two
 * rounds) and the vibrato's depth */
static const uint16_t GS_VOWEL[5][3] = {                     /* F1 F2 F3 as SVF cutoff indices, Q8 */
    {16512, 18432, 22528},                                   /* A  730 1090 2440 Hz */
    {14848, 21248, 22630},                                   /* E  530 1840 2480 */
    {11264, 22272, 23680},                                   /* I  270 2290 3010 */
    {15104, 17152, 22528},                                   /* O  570  840 2410 */
    {11776, 17280, 22170},                                   /* U  300  870 2240 */
};
static void gs_vowel_begin(gs_t *g)
{
    g->s[0] = (int32_t)(((uint32_t)g->tone * 5u * 65536u) / 128u + ((uint32_t)g->seed % 5u) * 65536u);   /* position */
    g->s[1] = (int32_t)(((uint32_t)g->motion * 65536u / 127u) * 320u / GS_N);   /* .. its step a control tick */
    g->s[2] = 2 + g->motion / 8;                             /* vibrato depth, 1/16 st */
    g->inc[1] = gs_lfo_inc(450u + ((uint32_t)g->seed & 7u) * 30u);   /* 4.5 .. 6.6 Hz */
    g->ph[1] = gs_rnd(g);
    g->inc[0] = gs_inc(GS_ROOT16);
}
GS_FAM void gs_vowel(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    uint32_t i;
    for (i = i0; i < i1; i++) {
        int32_t x, y;
        if (!(i & 31u)) {
            uint32_t vp = (uint32_t)g->s[0] % (5u << 16), vi = vp >> 16, vj = (vi + 1u) % 5u, k;
            int32_t fr = (int32_t)(vp & 0xFFFFu) >> 8;
            for (k = 0; k < 3; k++)
                tsvf_coef(&g->f[k], GS_VOWEL[vi][k] + (((int32_t)GS_VOWEL[vj][k] - GS_VOWEL[vi][k]) * fr >> 8), 100);
            g->s[0] = (int32_t)((vp + (uint32_t)g->s[1]) % (5u << 16));
            g->inc[0] = gs_inc(GS_ROOT16 + ((osc_tri(g->ph[1]) * g->s[2]) >> 15));
            g->ph[1] += g->inc[1] * 32u;
        }
        x = osc_pulse(g->ph[0], g->inc[0], 0x1999999Au) >> 1;
        g->ph[0] += g->inc[0];
        y = gs_svf_bp(&g->f[0], x, &g->ic[0], &g->ic[1]);
        y += (gs_svf_bp(&g->f[1], x, &g->ic[2], &g->ic[3]) * 19661) >> 15;
        y += (gs_svf_bp(&g->f[2], x, &g->ic[4], &g->ic[5]) * 9830) >> 15;
        buf[i] = gs_clip(soft_knee(y, 24000));
    }
}

/* ---- METAL: six sine partials at inharmonic ratios (by SEED), each decaying at its own rate, a noise strike.
 * TONE: how far the partials spread (clustered gong .. bright spray) and the strike. MOTION: the decay */
static void gs_metal_begin(gs_t *g)
{
    uint32_t inc0 = gs_inc(GS_ROOT16), k, spread = 384u + (uint32_t)g->tone * 13u;   /* Q8: 1.5 .. 8 */
    uint32_t tau0 = 900u - (uint32_t)g->motion * 820u / 127u;                        /* 900 .. 80 ms */
    for (k = 0; k < 6; k++) {
        uint32_t r = k ? 256u + (((uint32_t)gs_r15(g) * spread) >> 15) : 256u;
        g->inc[k] = (inc0 >> 8) * r;
        g->ph[k] = gs_rnd(g);
        g->a[k] = (int32_t)((12000u + ((uint32_t)gs_r15(g) >> 3)) / (k + 1u));
        g->s[k] = gs_kctl(tau0 * 10u / (10u + 4u * k));
    }
    g->s[6] = 90;                                            /* the strike's samples */
    g->s[7] = g->tone * 64;                                  /* .. and level */
}
GS_FAM void gs_metal(gs_t *g, int16_t *buf, uint32_t i0, uint32_t i1)
{
    uint32_t i, k;
    for (i = i0; i < i1; i++) {
        int32_t y = 0;
        if (!(i & 31u))
            for (k = 0; k < 6; k++)
                g->a[k] -= (g->a[k] * g->s[k]) >> 16;
        for (k = 0; k < 6; k++) {
            y += (sine_i(g->ph[k]) * g->a[k]) >> 15;
            g->ph[k] += g->inc[k];
        }
        if (g->s[6] > 0) {
            y += (gs_white(g) * g->s[7]) >> 14;
            g->s[6]--;
        }
        buf[i] = gs_clip(y);
    }
}

/* ---- the API the firmware would have */
GS_API void gs_begin(gs_t *g, uint32_t type, uint32_t tone, uint32_t motion, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < sizeof *g / 4u; i++)
        ((uint32_t *)g)[i] = 0;
    g->type = (uint8_t)(type % GS_NTYPE);
    g->tone = (uint8_t)(tone & 127u);
    g->motion = (uint8_t)(motion & 127u);
    g->seed = (uint8_t)(seed & 127u);
    g->root16 = GS_ROOT16;
    g->rng = (int32_t)(0x9E3779B9u ^ ((seed & 127u) * 0x01000193u + g->type * 0x61C88647u + 1u));
    switch (g->type) {
    case GT_PLUCK: gs_pluck_begin(g); break;
    case GT_DRONE: gs_drone_begin(g); break;
    case GT_BELL: gs_bell_begin(g); break;
    case GT_BREATH: gs_breath_begin(g); break;
    case GT_CHIRP: gs_chirp_begin(g); break;
    case GT_SWEEP: gs_sweep_begin(g); break;
    case GT_VOWEL: gs_vowel_begin(g); break;
    default: gs_metal_begin(g); break;
    }
}
/* render the next n samples into buf (at most up to GS_N); returns the samples rendered so far */
GS_API uint32_t gs_step(gs_t *g, int16_t *buf, uint32_t n)
{
    uint32_t i0 = g->pos, i1 = i0 + n;
    if (i1 > GS_N)
        i1 = GS_N;
    if (i1 <= i0)
        return g->pos;
    switch (g->type) {
    case GT_PLUCK: gs_pluck(g, buf, i0, i1); break;
    case GT_DRONE: gs_drone(g, buf, i0, i1); break;
    case GT_BELL: gs_bell(g, buf, i0, i1); break;
    case GT_BREATH: gs_breath(g, buf, i0, i1); break;
    case GT_CHIRP: gs_chirp(g, buf, i0, i1); break;
    case GT_SWEEP: gs_sweep(g, buf, i0, i1); break;
    case GT_VOWEL: gs_vowel(g, buf, i0, i1); break;
    default: gs_metal(g, buf, i0, i1); break;
    }
    g->pos = i1;
    return i1;
}
/* the whole buffer rendered: take its DC out (a one-pole high-pass at ~20 Hz: PLUCK's loop and SWEEP's pulse carry
 * some) and normalize it to GS_PEAK (a gain of at most 8); returns the gain, Q12 */
GS_API int32_t gs_finish(int16_t *buf)
{
    uint32_t i;
    int32_t pk = 1, gain, x1 = 0, y = 0;
    for (i = 0; i < GS_N; i++) {
        int32_t x = buf[i], a;
        y = x - x1 + ((y * 32581) >> 15);
        x1 = x;
        buf[i] = gs_clip(y);
        a = y < 0 ? -y : y;
        pk = a > pk ? a : pk;
    }
    pk = pk > 32767 ? 32767 : pk;
    gain = GS_PEAK * 4096 / pk;
    gain = gain > 8 * 4096 ? 8 * 4096 : gain;
    for (i = 0; i < GS_N; i++)
        buf[i] = gs_clip((buf[i] * gain) >> 12);
    return gain;
}

/* ============================================================================ the grain player (gp_) === */
/* eng_grain.c's player over a RAM buffer: no decoder, no seek index, a reverse grain reads backwards. The grain's
 * rate is the note against the buffer's root (GS_ROOT16), PITCH and the random detune, times GS_RATE / 44100 */
#define GP_NG 16                 /* grains a part, shared by its voices */
#define GP_POLY 6
#define GP_STEP_MAX (2u << 16)   /* the rate's cap: two source samples an output sample */
#define GP_RATE_Q16 ((GS_RATE << 16) / 44100u)   /* 0.5 */
#define GP_XFADE (!GS_UNIT)      /* the crossfade comparison (gp_par_t.xfade): the sketches only, not the design */
/* sin^2(pi i / 256), Q15: the grain window (eng_grain.c GR_HANN) */
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
/* grain level by overlap: 1 / sqrt(n), Q15 */
static const int16_t GP_NORM[GP_NG + 1] = {32767, 32767, 23170, 18918, 16384, 14654, 13377, 12385, 11585, 10922,
                                           10362, 9880, 9459, 9088, 8757, 8460, 8192};
typedef struct {
    uint32_t pos;                /* forward: the next source sample to fetch; reverse: the index of a */
    int32_t frac;                /* Q16 between a and b */
    uint32_t step;               /* Q16 source samples an output sample */
    uint32_t wph, winc, left;    /* window phase (one turn = the grain), output samples to go */
    int32_t a, b, gain;
    const int16_t *src;          /* the buffer it reads: source A's or B's (MORPH) */
#if GP_XFADE
    const int16_t *src2;         /* xfade: the other one, mixed in per fetch (the comparison, not the design) */
    int32_t mix;                 /* xfade: B's share, Q15 */
#endif
    uint8_t owner, rev;
} gp_grain_t;
typedef struct {
    gp_grain_t g[GP_NG];
    const int16_t *src[2];       /* the part's two buffers, A and B (GS_N samples each) */
    int32_t root16[2];
    int32_t rng;
} gp_part_t;
/* the grain knobs; morph 0 .. 127: the share of grains that read B (0 all A, 127 all B); xfade 1: instead every
 * grain reads both buffers at its position and mixes them by morph (a plain crossfade, for the comparison) */
typedef struct { int32_t size, dens, pos_q8, sprd, rnd, pitch, cut, res, morph, xfade; } gp_par_t;
typedef struct {
    uint8_t active;
    int32_t note16, next;        /* the note; output samples to the next grain */
    int32_t macc;                /* MORPH's share spread evenly over the voice's grains (a Bresenham count) */
    tsvf_t f;
    int32_t ic1, ic2;
} gp_voice_t;

static inline uint32_t gp_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }
static inline int32_t gp_win(uint32_t wph)
{
    uint32_t wi = wph >> 24;
    return GP_HANN[wi] + (((GP_HANN[wi + 1u] - GP_HANN[wi]) * (int32_t)((wph >> 9) & 0x7FFFu)) >> 15);
}

GS_API void gp_init(gp_part_t *P, const int16_t *srca, int32_t roota, const int16_t *srcb, int32_t rootb)
{
    uint32_t i;
    for (i = 0; i < GP_NG; i++)
        P->g[i].owner = 0;
    P->src[0] = srca;
    P->src[1] = srcb;
    P->root16[0] = roota;
    P->root16[1] = rootb;
    P->rng = 0x2545F491;
}

/* start a grain of voice v (vi) with the interval iv between grains (for the overlap's level). MORPH: the voice's
 * grains read A or B in the share morph / 127, spread evenly (0.5: A B A B ..), so the ends are pure and the
 * middle is a granular blend; the grain's rate is against its own source's root */
GS_API void gp_spawn(gp_part_t *P, const gp_par_t *p, uint32_t vi, gp_voice_t *v, uint32_t iv)
{
    gp_grain_t *g = 0;
    uint32_t i, n = GS_N, len, step, span, c, sp, r1 = noise32(&P->rng), r2 = noise32(&P->rng), ov, rev, which;
    int32_t d16, amt, pos, note16 = v->note16;
    for (i = 0; i < GP_NG && !g; i++)
        if (!P->g[i].owner)
            g = &P->g[i];
    if (!g)
        return;
    v->macc += p->morph;
    which = v->macc >= 127 ? 1u : 0u;
    if (which)
        v->macc -= 127;
#if GP_XFADE
    if (p->xfade)
        which = 0;
#endif
    len = (441u * pow2_q16(p->size * 8)) >> 16;              /* 10 .. 393 ms */
    amt = p->rnd * p->rnd * 192 / (127 * 127);
    d16 = note16 - P->root16[which] + p->pitch * 16 + (((int32_t)(r1 & 0xFFFFu) - 32768) * amt >> 15);
    step = (pow2_q16(clamp(d16, -1536, 576)) >> 8) * (GP_RATE_Q16 >> 8);
    step = step > GP_STEP_MAX ? GP_STEP_MAX : step < 256u ? 256u : step;
    rev = ((r1 >> 16) & 255u) < (uint32_t)p->rnd;            /* RAND 127: half of them */
    span = (len * (step >> 4)) >> 12;
    if (span + 3u > n) {
        span = n - 3u;
        len = (span << 8) / (step >> 8);
    }
    if (len < 16u)
        return;
    c = gp_scale(n - 1u, (uint32_t)clamp(p->pos_q8, 0, 127 << 8) * 65535u / (127u << 8));
    sp = gp_scale(n, (uint32_t)p->sprd * 516u);
    pos = (int32_t)c + (int32_t)gp_scale(sp, r2 & 0xFFFFu) - (int32_t)(sp >> 1);
    pos = rev ? clamp(pos, (int32_t)span, (int32_t)n - 2) : clamp(pos, 0, (int32_t)(n - 3u - span));
    g->step = step;
    g->frac = 0;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    g->left = len;
    g->rev = (uint8_t)rev;
    ov = len / (iv ? iv : 1u);
    g->gain = GP_NORM[ov < 1u ? 1u : ov > GP_NG ? GP_NG : ov];
    g->owner = (uint8_t)(vi + 1u);
    g->src = P->src[which];
#if GP_XFADE
    g->src2 = p->xfade ? P->src[1] : 0;
    g->mix = p->morph * 32767 / 127;
#endif
    if (rev) {
        g->pos = (uint32_t)pos;
        g->a = g->src[pos];
        g->b = g->src[pos + 1];
    } else {
        g->a = g->src[pos];
        g->b = g->src[pos + 1];
        g->pos = (uint32_t)pos + 2u;
    }
#if GP_XFADE
    if (g->src2) {
        g->a += ((g->src2[pos] - g->a) * g->mix) >> 15;
        g->b += ((g->src2[pos + 1] - g->b) * g->mix) >> 15;
    }
#endif
}

/* one grain into acc over n samples; returns 0 when it ended. The window (times the grain's level) ramps linearly
 * over the block between its exact values at the block's ends (eng_grain.c gr_run) */
GS_API int gp_run(gp_part_t *P, gp_grain_t *g, int32_t *acc, uint32_t n)
{
    uint32_t i, m = n < g->left ? n : g->left, step = g->step, pos = g->pos;
    int32_t a = g->a, b = g->b, frac = g->frac, w0, w1, wq, dq;
    const int16_t *src = g->src;
#if GP_XFADE
    if (g->src2) {                                           /* the crossfade comparison: both buffers a fetch */
        const int16_t *s2 = g->src2;
        int32_t mix = g->mix;
        w0 = mulq15(gp_win(g->wph), g->gain);
        g->wph += g->winc * m;
        g->left -= m;
        w1 = g->left ? mulq15(gp_win(g->wph), g->gain) : 0;
        wq = w0 * 1024;
        dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            if (!g->rev) {
                frac += (int32_t)step;
                while (frac >= 65536) {
                    frac -= 65536;
                    a = b;
                    b = pos < GS_N ? src[pos] + (((s2[pos] - src[pos]) * mix) >> 15) : a;
                    pos++;
                }
            } else {
                frac -= (int32_t)step;
                while (frac < 0) {
                    frac += 65536;
                    b = a;
                    if (pos) {
                        pos--;
                        a = src[pos] + (((s2[pos] - src[pos]) * mix) >> 15);
                    }
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
#endif
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

/* a voice's block: its grains, the next grain (DENS a second, SPRD jitters the interval), its filter. nact: the
 * part's sounding voices (the pool's share). out: CTL samples, Q15-ish (the caller applies the envelope) */
GS_API void gp_voice_block(gp_part_t *P, const gp_par_t *p, gp_voice_t *v, uint32_t vi, uint32_t nact, int32_t *out)
{
    int32_t acc[CTL];
    uint32_t i, mine = 0, iv, share;
    for (i = 0; i < CTL; i++)
        acc[i] = 0;
    for (i = 0; i < GP_NG; i++)
        if (P->g[i].owner == vi + 1u)
            mine += (uint32_t)gp_run(P, &P->g[i], acc, CTL);
    iv = (44100u * pow2_q16(-p->dens * 10)) >> 16;           /* 1 .. 98 a second */
    v->next -= CTL;
    if (v->next <= 0) {
        uint32_t r = noise32(&P->rng);
        int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * p->sprd) / 254;
        v->next += (int32_t)iv + mulq15((int32_t)iv, j);
        if (v->next < 0)
            v->next = 0;
        share = nact > 1u ? (GP_NG / nact > 2u ? GP_NG / nact : 2u) : GP_NG;
        if (mine < share)
            gp_spawn(P, p, vi, v, iv);
    }
    tsvf_coef(&v->f, p->cut << 8, p->res);
    for (i = 0; i < CTL; i++)
        out[i] = tsvf_lp(&v->f, clamp(acc[i], -65535, 65535), &v->ic1, &v->ic2);
}

/* a voice's grains are let go (a fresh voice) */
GS_API void gp_voice_on(gp_part_t *P, gp_voice_t *v, uint32_t vi, int32_t note16)
{
    uint32_t i;
    for (i = 0; i < GP_NG; i++)
        if (P->g[i].owner == vi + 1u)
            P->g[i].owner = 0;
    v->active = 1;
    v->note16 = note16;
    v->next = 0;
    v->macc = 63;                                            /* the first grain of a half-way morph is A's, the second B's */
    v->ic1 = v->ic2 = 0;
}

#if !GS_UNIT
/* ================================================================================== the sketches === */
static void wav_write(const char *path, const int16_t *s, uint32_t n, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t v;
    uint16_t a = 1, ch = 1, ba = 2, bits = 16;
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
    fwrite(s, 2, n, f);
    fclose(f);
}

static const struct { const char *name; uint8_t tone, motion, seed; } SRC_SET[3] = {
    {"mid", 64, 64, 0}, {"dark-slow", 28, 20, 3}, {"bright-fast", 104, 104, 11}};
static const struct { const char *name; gp_par_t p; int scan; const char *what; } GRAN_SET[3] = {
    {"pad", {92, 88, 50 << 8, 40, 14, 0, 100, 20}, 1,
     "SIZE 92 (160 ms) DENS 88 POS 50 % wandering +-20 % SPRD 40 RAND 14 CUT 100 RES 20: a cloud (Felucca's CLOUD PAD)"},
    {"glitch", {24, 112, 64 << 8, 100, 90, 0, 127, 0}, 0,
     "SIZE 24 (20 ms) DENS 112 POS 64 % SPRD 100 RAND 90 (detune +-oct, half reversed) CUT open: Felucca's GLITCH"},
    {"frozen", {108, 72, 10 << 8, 0, 10, 0, 92, 10}, 2,
     "SIZE 108 (250 ms) DENS 72 SPRD 0 RAND 10 CUT 92: POS scans 10 -> 90 % over the three seconds (a frozen read)"}};
static const uint8_t CHORD[4] = {48, 55, 60, 64};             /* C3 G3 C4 E4 */
#define SCENE_S 3.0
#define OFF_S 2.2
#define MSCENE_S 4.0             /* the morph scenes: longer */
#define MOFF_S 3.4
enum { M_STATIC, M_SWEEP, M_ENV };   /* MORPH over the scene: the knob as set; 0 -> 127 over the hold; 127 -> 0 in 0.6 s */

/* the granulated scene: a 4-note chord held off_s seconds on the buffers A and B, len_s seconds rendered at
 * 44.1 kHz, normalized to -2 dBFS; returns the peak before normalizing (Q15 of one voice's scale) */
static int32_t scene(const int16_t *A, int32_t rootA, const int16_t *B, int32_t rootB, const gp_par_t *par, int scan,
                     int mmode, double len_s, double off_s, const char *path)
{
    static gp_part_t P;
    gp_voice_t v[4];
    gp_par_t p = *par;
    uint32_t nb = (uint32_t)(len_s * FS / CTL), b, i, k, offb = (uint32_t)(off_s * FS / CTL), nact = 4;
    double *mix = calloc(nb * CTL, sizeof *mix), pk = 1;
    int16_t *out = malloc(nb * CTL * sizeof *out);
    int32_t env[4], tmp[CTL];
    gp_init(&P, A, rootA, B, rootB);
    memset(v, 0, sizeof v);
    for (k = 0; k < 4; k++) {
        gp_voice_on(&P, &v[k], k, CHORD[k] * 16);
        env[k] = 0;
    }
    for (b = 0; b < nb; b++) {
        double t = (double)b * CTL / FS;
        if (scan == 1)
            p.pos_q8 = par->pos_q8 + (int32_t)(25 * 256 * sin(2 * M_PI * 0.3 * t));
        else if (scan == 2)
            p.pos_q8 = (10 << 8) + (int32_t)((80 << 8) * t / len_s);
        if (mmode == M_SWEEP)
            p.morph = t < off_s ? (int32_t)(127 * t / off_s) : 127;
        else if (mmode == M_ENV)
            p.morph = t < 0.6 ? 127 - (int32_t)(127 * t / 0.6) : 0;
        if (b == offb)
            nact = 0;
        for (k = 0; k < 4; k++) {
            int32_t target = b < offb ? 32767 : 0, rate = b < offb ? 32767 * CTL / (FS * 30 / 1000) : 32767 * CTL / (FS * 400 / 1000);
            if (!v[k].active)
                continue;
            env[k] += env[k] < target ? (target - env[k] < rate ? target - env[k] : rate) : -(env[k] - target < rate ? env[k] - target : rate);
            gp_voice_block(&P, &p, &v[k], k, b < offb ? 4 : 4, tmp);
            for (i = 0; i < CTL; i++)
                mix[b * CTL + i] += (double)tmp[i] * env[k] / 32768.0;
            if (b >= offb && env[k] == 0) {              /* released: its grains fade out on their own */
                uint32_t left = 0;
                for (i = 0; i < GP_NG; i++)
                    left |= P.g[i].owner == k + 1u;
                if (!left)
                    v[k].active = 0;
            }
        }
    }
    for (i = 0; i < nb * CTL; i++)
        pk = fabs(mix[i]) > pk ? fabs(mix[i]) : pk;
    for (i = 0; i < nb * CTL; i++)
        out[i] = (int16_t)(mix[i] / pk * 32767 * 0.79);
    wav_write(path, out, nb * CTL, FS);
    free(mix);
    free(out);
    return (int32_t)pk;
}

static double rms_of(const int16_t *s, uint32_t n)
{
    double q = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        q += (double)s[i] * s[i];
    return sqrt(q / n) / 32768.0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/grain-sketch";
    int src_only = argc > 2 && !strcmp(argv[2], "src");
    static int16_t buf[GS_N], tour[8 * (GS_N + 2205)];
    char path[512], cmd[600];
    FILE *idx;
    uint32_t ty, k, s, i;
    gs_t g;
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    if (system(cmd))
        return 1;
    snprintf(path, sizeof path, "%s/INDEX.md", dir);
    idx = fopen(path, "w");
    if (!idx) {
        perror(path);
        return 1;
    }
    fprintf(idx, "# GRAIN sketches\n\nRendered by `tools/grain_sketch.c` (docs/GRAIN.md). The source buffers are %u samples at %u Hz "
                 "(%.0f ms), what the engine would hold in POOL; the granulated scenes are 44.1 kHz, a C3 G3 C4 E4 chord held "
                 "%.1f s, %.0f s in all.\n\n", GS_N, GS_RATE, 1000.0 * GS_N / GS_RATE, OFF_S, SCENE_S);
    fprintf(idx, "## Files\n\n- `src-<family>-<setting>.wav`: the buffer as rendered (the material the grains read).\n"
                 "- `seeds-<family>.wav`: the same family at TONE 64 MOTION 64, SEED 0 .. 7 in a row, 100 ms apart: what SEED gives.\n"
                 "- `gran-<family>-<setting>-<grains>.wav`: the buffer granulated.\n"
                 "- `morph-<A>-<B>-<how>.wav`: two buffers under one voice, MORPH between them (below).\n\n");
    fprintf(idx, "## The source settings\n\n| setting | TONE | MOTION | SEED |\n| --- | --- | --- | --- |\n");
    for (s = 0; s < 3; s++)
        fprintf(idx, "| %s | %u | %u | %u |\n", SRC_SET[s].name, SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed);
    fprintf(idx, "\n## The grain settings\n\n| grains | what |\n| --- | --- |\n");
    for (s = 0; s < 3; s++)
        fprintf(idx, "| %s | %s |\n", GRAN_SET[s].name, GRAN_SET[s].what);
    fprintf(idx, "\n## The families\n\n| family | TONE | MOTION | setting | peak before normalizing | RMS after | render ms (host) |\n"
                 "| --- | --- | --- | --- | --- | --- | --- |\n");
    for (ty = 0; ty < GS_NTYPE; ty++) {
        static const char *const WHAT[GS_NTYPE][2] = {
            {"the burst's and the string's brightness", "the decay; above 64 a strum of re-plucks"},
            {"the filter's cutoff", "the detune and the sweep's rate"},
            {"the FM index (soft .. clangy)", "the index's decay; above 64 a beating second pair"},
            {"the band-pass's centre", "the wander's depth, the tremolo's rate and depth"},
            {"the sweep's span", "chirps a buffer (1 .. 16)"},
            {"the resonance and the sweep's top", "sweeps a buffer (1 .. 8)"},
            {"the starting vowel (A E I O U round)", "how far it travels, the vibrato"},
            {"the partials' spread and the strike", "the decay (shimmer .. clang)"}};
        char fam[16];
        for (k = 0; GS_TYPE_NAMES[ty][k]; k++)
            fam[k] = (char)(GS_TYPE_NAMES[ty][k] | 32);
        fam[k] = 0;
        for (s = 0; s < 3; s++) {
            clock_t t0 = clock();
            int32_t gain, pk = 1;
            gs_begin(&g, ty, SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed);
            while (gs_step(&g, buf, 512u) < GS_N)         /* in slices, as the firmware would */
                ;
            for (i = 0; i < GS_N; i++)
                pk = abs(buf[i]) > pk ? abs(buf[i]) : pk;
            gain = gs_finish(buf);
            snprintf(path, sizeof path, "%s/src-%s-%s.wav", dir, fam, SRC_SET[s].name);
            wav_write(path, buf, GS_N, GS_RATE);
            fprintf(idx, "| %s | %s | %s | %s | %.2f (gain x%.2f) | %.3f | %.1f |\n", s ? "" : GS_TYPE_NAMES[ty],
                    s ? "" : WHAT[ty][0], s ? "" : WHAT[ty][1], SRC_SET[s].name, pk / 32767.0, gain / 4096.0,
                    rms_of(buf, GS_N), 1000.0 * (clock() - t0) / CLOCKS_PER_SEC);
            printf("%-7s %-12s peak %.2f gain x%.2f rms %.3f root16 %d\n", GS_TYPE_NAMES[ty], SRC_SET[s].name,
                   pk / 32767.0, gain / 4096.0, rms_of(buf, GS_N), g.root16);
            fflush(stdout);
            if (src_only)
                continue;
            for (k = 0; k < 3; k++) {
                snprintf(path, sizeof path, "%s/gran-%s-%s-%s.wav", dir, fam, SRC_SET[s].name, GRAN_SET[k].name);
                scene(buf, g.root16, buf, g.root16, &GRAN_SET[k].p, GRAN_SET[k].scan, M_STATIC, SCENE_S, OFF_S, path);
            }
        }
        for (s = 0; s < 8; s++) {                         /* the seed tour */
            gs_begin(&g, ty, 64, 64, s);
            while (gs_step(&g, buf, GS_N) < GS_N)
                ;
            gs_finish(buf);
            memcpy(tour + s * (GS_N + 2205), buf, GS_N * sizeof *buf);
            memset(tour + s * (GS_N + 2205) + GS_N, 0, 2205 * sizeof *buf);
        }
        snprintf(path, sizeof path, "%s/seeds-%s.wav", dir, fam);
        wav_write(path, tour, 8 * (GS_N + 2205), GS_RATE);
    }
    if (!src_only) {
        /* two sources and MORPH (the owner, 2026-10-15: "generate two waves, each with their own settings, and then
         * there's a parameter to morph between them"): eight pairs, the pad grains, MORPH swept A -> B over the
         * hold, held half way, and falling B -> A in 0.6 s (what an envelope on MORPH does); for two pairs the
         * crossfade comparison (every grain mixes both buffers) beside the granular morph */
        static const struct { uint8_t ta, sa, tb, sb; } PAIRS[8] = {
            {GT_PLUCK, 0, GT_VOWEL, 0}, {GT_DRONE, 0, GT_BELL, 0}, {GT_BREATH, 0, GT_METAL, 0},
            {GT_CHIRP, 1, GT_SWEEP, 0}, {GT_BELL, 1, GT_BELL, 2}, {GT_VOWEL, 1, GT_BREATH, 1},
            {GT_METAL, 1, GT_DRONE, 1}, {GT_SWEEP, 2, GT_PLUCK, 2}};
        static const char *const SHORT[3] = {"", ".dark", ".bright"};
        static const char *const MNAME[3] = {"half", "sweep", "env"};
        static int16_t bufb[GS_N];
        gs_t gb;
        fprintf(idx, "\n## Two sources, MORPH\n\n`morph-<A>-<B>-<how>.wav`: the pad grains over two buffers; each grain reads A or "
                     "B, the share set by MORPH and spread evenly over a voice's grains (0.5: A B A B ..). `half`: MORPH 64 held; "
                     "`sweep`: 0 -> 127 over the %.1f s hold; `env`: 127 -> 0 in the first 0.6 s (an envelope on MORPH: B's attack "
                     "into A's body). `-xfade`: the comparison, every grain mixing both buffers at its position by MORPH (a plain "
                     "crossfade; costs a second fetch a sample).\n\n| A | B | files |\n| --- | --- | --- |\n", MOFF_S);
        for (k = 0; k < 8; k++) {
            char fa[24], fb[24];
            uint32_t m;
            gs_begin(&g, PAIRS[k].ta, SRC_SET[PAIRS[k].sa].tone, SRC_SET[PAIRS[k].sa].motion, SRC_SET[PAIRS[k].sa].seed);
            while (gs_step(&g, buf, GS_N) < GS_N)
                ;
            gs_finish(buf);
            gs_begin(&gb, PAIRS[k].tb, SRC_SET[PAIRS[k].sb].tone, SRC_SET[PAIRS[k].sb].motion, SRC_SET[PAIRS[k].sb].seed);
            while (gs_step(&gb, bufb, GS_N) < GS_N)
                ;
            gs_finish(bufb);
            for (i = 0; GS_TYPE_NAMES[PAIRS[k].ta][i]; i++)
                fa[i] = (char)(GS_TYPE_NAMES[PAIRS[k].ta][i] | 32);
            fa[i] = 0;
            strcat(fa, SHORT[PAIRS[k].sa]);
            for (i = 0; GS_TYPE_NAMES[PAIRS[k].tb][i]; i++)
                fb[i] = (char)(GS_TYPE_NAMES[PAIRS[k].tb][i] | 32);
            fb[i] = 0;
            strcat(fb, SHORT[PAIRS[k].sb]);
            for (m = 0; m < 3; m++) {
                gp_par_t pp = GRAN_SET[0].p;
                pp.morph = 64;
                snprintf(path, sizeof path, "%s/morph-%s-%s-%s.wav", dir, fa, fb, MNAME[m]);
                scene(buf, g.root16, bufb, gb.root16, &pp, GRAN_SET[0].scan, (int)m, MSCENE_S, MOFF_S, path);
            }
            if (k < 2) {
                gp_par_t pp = GRAN_SET[0].p;
                pp.xfade = 1;
                snprintf(path, sizeof path, "%s/morph-%s-%s-sweep-xfade.wav", dir, fa, fb);
                scene(buf, g.root16, bufb, gb.root16, &pp, GRAN_SET[0].scan, M_SWEEP, MSCENE_S, MOFF_S, path);
            }
            fprintf(idx, "| %s %s | %s %s | `morph-%s-%s-{half,sweep,env}.wav`%s |\n", GS_TYPE_NAMES[PAIRS[k].ta],
                    SRC_SET[PAIRS[k].sa].name, GS_TYPE_NAMES[PAIRS[k].tb], SRC_SET[PAIRS[k].sb].name, fa, fb,
                    k < 2 ? ", `-sweep-xfade.wav`" : "");
            printf("morph %s -> %s\n", fa, fb);
        }
    }
    fprintf(idx, "\n## Listening order\n\n1. `seeds-*.wav`, one a family: is the family worth having, does SEED give variety.\n"
                 "2. `src-*-mid.wav` against `-dark-slow` and `-bright-fast`: do TONE and MOTION move it where expected.\n"
                 "3. `gran-*-pad.wav`: the sound of the engine as a pad; then `-frozen` (the scan through the material) and "
                 "`-glitch`.\n4. `morph-*-sweep.wav`: one material becoming another; `-env` for what an envelope on MORPH gives; "
                 "`-sweep-xfade` against `-sweep` for the two ways to blend.\n\nThe granulated scenes have no reverb, no delay and a plain 30 ms / 400 ms envelope: the unit adds "
                 "its effects and the sound's ADSR.\n");
    fclose(idx);
    printf("wrote %s/INDEX.md\n", dir);
    return 0;
}
#endif
