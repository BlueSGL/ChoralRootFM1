/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot's delay and reverb (firmware/src/cr_fx.c, docs/FX.md) on the host, through hostsim.c with CR_FX 1:
 *   build/host/cr_fx_test [OUTDIR]            (sh tests/run_cr_tests.sh; OUTDIR: build/fx-review)
 *  1. the coefficient arithmetic: e^-x, the fourth root, the knobs' curves;
 *  2. tempo sync: the two sides' times in samples for a table of tempi, divisions and spreads, and the first echo of
 *     an impulse where the table says;
 *  3. the half rate: the delay path's frequency response (flat to 9 kHz, gone by 12.5 kHz), what a 15 kHz send leaves
 *     below 11 kHz (the alias), and the path's noise under a -12 dB sine;
 *  4. stability: every corner of the parameters, an impulse and 10 s of the loudest send (noise, then a square), then
 *     silence, 60 s in all: bounded, no offset, and exactly 0 at the end (DECAY Inf: never louder than it was);
 *  5. the reverb's bound: at DECAY Inf with the lines swinging and SIZE swept, the lines' energy never grows without
 *     an input;
 *  6. smooth changes: Time, Spread, Sync, Mode, Size, Pre-delay swept under a held tone: no sample step above what
 *     the steady signal has;
 *  7. the level against Felucca's delay and ROOM at their defaults (a copy below), the same send: the reverb's wet
 *     under a held chord and the delay's first repeat (both sides' energy, Stereo and ping-pong) within 1 dB;
 *  8. mono: Spread 0 in Stereo mode is the same sample left and right; ping-pong's sum has every repeat;
 *  9. silence in, silence out: after the tail both effects switch off (exact zeros) and the block costs almost
 *     nothing; the decay times against the knob;
 * 10. the cost: host instructions a 128-frame block with both fully on, and emu.c's device estimate (budget: 350 us).
 * WAVs for tools/fx_plots.py into OUTDIR: the reverb's impulse responses (small, default, large, Inf), the delay's
 * repeats (stereo, ping-pong, spread). */
#define CR_FX 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("cr_fx: %-104s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static uint32_t xs = 0x1234567u;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}
static const char *outdir = "build/fx-review";

/* ------------------------------------------------------------------ driving --- */
static void fx_reset(void)
{
    memset(&xf, 0, sizeof xf);
    memset(xd_buf, 0, sizeof xd_buf);
    memset(xr_buf, 0, sizeof xr_buf);
    memset(xr_pbuf, 0, sizeof xr_pbuf);
    xf_defaults();
    xf_bpm = 120;
}
#define MAXS (122u * 44100u)
static int32_t *WL, *WR;                                /* the wet, a run */
/* a run of `n` samples (a multiple of CTL): gen(i) is the send's sample; which: bit 0 into the delay, bit 1 the reverb.
 * hook (if any) runs before every block (its first sample's index) */
typedef int32_t (*gen_t)(uint32_t i);
typedef void (*hook_t)(uint32_t i);
static void run(uint32_t n, gen_t gen, uint32_t which, hook_t hook)
{
    static int32_t sd[CTL], sr[CTL], ml[CTL], mr[CTL];
    uint32_t i, k;
    for (i = 0; i < n && i + CTL <= MAXS; i += CTL) {       /* (whole blocks: up to CTL - 1 samples past n) */
        if (hook)
            hook(i);
        for (k = 0; k < CTL; k++) {
            int32_t v = gen ? gen(i + k) : 0;
            sd[k] = which & 1u ? v : 0;
            sr[k] = which & 2u ? v : 0;
            ml[k] = mr[k] = 0;
        }
        xf_block(sd, sr, ml, mr, CTL);
        memcpy(WL + i, ml, sizeof ml);
        memcpy(WR + i, mr, sizeof mr);
    }
}
static void wav_write(const char *name, uint32_t n, double gain)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s", outdir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    wav_hdr(f, n);
    for (i = 0; i < n; i++)
        wav_put(f, (int32_t)(WL[i] * gain), (int32_t)(WR[i] * gain));
    fclose(f);
}
static double rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (double)(b - a));
}
static double db(double x) { return 20.0 * log10(x > 1e-9 ? x : 1e-9); }

/* the sends of the tests */
static int32_t g_imp(uint32_t i) { return i == 64u ? 80000 : 0; }
static double g_f = 1000.0, g_a = 20000.0;
static uint32_t g_len = 0xFFFFFFFFu;
static int32_t g_sine(uint32_t i) { return i < g_len ? (int32_t)(g_a * sin(2.0 * M_PI * g_f * i / 44100.0)) : 0; }
static int32_t g_noise(uint32_t i) { return i < g_len ? noise((int32_t)g_a) : 0; }
/* the loudest send: 4 parts' clamps (+-524287 each); noise, then a 110 Hz square */
static int32_t g_loud(uint32_t i)
{
    if (i == 64u) return 2097148;
    if (i < 5u * 44100u) return noise(2097148);
    if (i < 10u * 44100u) return (i / 200u) & 1u ? 2097148 : -2097148;
    return 0;
}
/* a held chord (D3 F#3 A3 D4, twelve harmonics each, 1 / n), ~+-14000 at its peaks: a loud pad's send */
static int32_t g_chord(uint32_t i)
{
    static const double F[4] = {146.83, 185.0, 220.0, 293.66};
    double s = 0, t = i / 44100.0, e;
    uint32_t k, h;
    if (i >= g_len)
        return 0;
    for (k = 0; k < 4u; k++)
        for (h = 1; h <= 12u; h++)
            s += sin(2.0 * M_PI * F[k] * h * t + k * 1.3 + h * 0.7) / h;
    e = t < 0.01 ? t / 0.01 : 1.0;
    return (int32_t)(s * e * 3000.0);
}

/* -------------------------------------------- Felucca's buses, for the level --- */
static int16_t old_dly[65536], old_comb[1116 + 1188 + 1277 + 1356], old_ap[556 + 441];
static void old_run(uint32_t n, gen_t gen, uint32_t which, int32_t *wet)
{
    static const uint16_t RC[4] = {1116, 1188, 1277, 1356}, RA[2] = {556, 441};
    uint32_t dly_w = 0, ci[4] = {0}, ai[2] = {0}, i, k, dl = 44100u * 60u / 120u / 2u;   /* 1/8 at 120 */
    int32_t dly_lp = 0, clp[4] = {0};
    int32_t fb = 60 * 230, col = 2000 + 70 * 240, dmix = 90 * 258, size = 25000 + 90 * 50, damp = 32767 - 60 * 200;
    memset(old_dly, 0, sizeof old_dly);
    memset(old_comb, 0, sizeof old_comb);
    memset(old_ap, 0, sizeof old_ap);
    for (i = 0; i < n; i++) {
        int32_t s = gen(i), y = 0, x, a = 0, in;
        int16_t *c = old_comb;
        if (which & 1u) {
            x = old_dly[(dly_w - dl) & 65535u];
            dly_lp += mulq15(x - dly_lp, col);
            old_dly[dly_w & 65535u] = (int16_t)clamp((s >> 1) + mulq15(dly_lp, fb), -32768, 32767);
            dly_w++;
            y += mulq15(x << 1, dmix);
        }
        if (which & 2u) {
            in = mulq15(s, 2580);
            for (k = 0; k < 4u; k++) {
                int32_t o = c[ci[k]];
                clp[k] = o + mulq15(clp[k] - o, 32767 - damp);
                c[ci[k]] = (int16_t)clamp(in + mulq15(clp[k], size), -32768, 32767);
                if (++ci[k] >= RC[k]) ci[k] = 0;
                a += o;
                c += RC[k];
            }
            c = old_ap;
            for (k = 0; k < 2u; k++) {
                int32_t o = c[ai[k]], v = a + (o >> 1);
                c[ai[k]] = (int16_t)clamp(v, -32768, 32767);
                a = o - a;
                if (++ai[k] >= RA[k]) ai[k] = 0;
                c += RA[k];
            }
            y += a;
        }
        wet[i] = y;
    }
}

/* ----------------------------------------------------------------- the tests --- */
static void t_maths(void)
{
    uint32_t y, ok = 1, p;
    double worst = 0;
    for (y = 0; y < 8u * 65536u; y += 97u) {
        double want = exp(-(double)y / 65536.0) * 32768.0, got = xf_expn(y), e = (fabs(got - want) - 1.5) / want;
        if (e > worst) worst = e;
    }
    printf("cr_fx:   e^-x: the worst error %.3f %% past 1.5 of 32768\n", worst * 100.0);
    check("e^-x (the lines' gains) within 1.5 / 32768 + 0.3 % over 0 .. 8", worst < 0.003);
    for (y = 1; y < 32768u; y += 13u) {
        double want = pow(y / 32768.0, 0.25) * 32768.0, got = xf_isqrt(xf_isqrt(y << 15) << 15);
        if (fabs(got - want) > 3.0 + want * 0.001) ok = 0;
    }
    check("the fourth root (the reverb's input gain against its decay)", ok);
    ok = xf_ms(0) == 21u && xf_ms(127) == 1000u && xf_rt_ms(0) == 300u && xf_rt_ms(126) > 60000u;
    for (p = 1; p < 128u; p++)
        if (xf_ms(p) < xf_ms(p - 1u) || xf_rt_ms(p) <= xf_rt_ms(p - 1u) || xf_lowcut_k(p) < xf_lowcut_k(p - 1u)) ok = 0;
    check("the knobs' curves rise: time 21 .. 1000 ms, decay 0.3 .. 72 s, low cut 20 .. 500 Hz", ok);
}

static void t_sync(void)
{
    static const uint16_t BPM[] = {20, 60, 90, 100, 120, 121, 140, 174, 240, 300};
    static const char *const DN[XF_NDIV] = {"1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4",
                                            "1/2T", "1/4.", "1/2"};
    uint32_t b, d, q, ok = 1, l, r, shown = 0;
    for (b = 0; b < sizeof BPM / sizeof BPM[0]; b++)
        for (d = 0; d < XF_NDIV; d++)
            for (q = 0; q < 4u; q++) {
                double want = 22050.0 * 60.0 / BPM[b] * XF_DIV[d] / 48.0, wr;
                fx_reset();
                xf_bpm = BPM[b];
                xfp[XP_DSYNC] = 1;
                xfp[XP_DDIV] = (int16_t)d;
                xfp[XP_DRATIO] = (int16_t)q;
                xf_delay_times(&l, &r);
                while (want > 22050.5) want /= 2.0;
                wr = want * XF_RATIO[q][0] / XF_RATIO[q][1];
                if (fabs(l - want) > 0.51 || fabs(r - wr) > 1.01 || l > XD_MAX || r > l) {
                    ok = 0;
                    printf("cr_fx:   %u BPM %s ratio %u: %u / %u samples, want %.1f / %.1f\n", BPM[b], DN[d], q, l, r, want, wr);
                }
                if (BPM[b] == 120 && (d == 6u || d == 8u || d == 9u) && shown++ < 12u)
                    printf("cr_fx:   120 BPM %-5s %u:%u  L %5u R %5u half-rate samples (%.1f / %.1f ms)\n", DN[d],
                           XF_RATIO[q][0], XF_RATIO[q][1], l, r, l / 22.05, r / 22.05);
            }
    check("sync: 10 tempi x 13 divisions x 4 spreads: L within half a sample of the division (halved to fit 1 s), R its part", ok);
    {   /* the echo is where the table says: an impulse, Stereo, no feedback; the first repeat's peak */
        static const struct { uint16_t bpm, div, ratio; } C[4] = {{120, 6, 0}, {120, 8, 1}, {90, 9, 2}, {140, 3, 3}};
        uint32_t k, i;
        ok = 1;
        for (k = 0; k < 4u; k++) {
            uint32_t pl = 0, pr = 0;
            int32_t ml = 0, mr = 0;
            fx_reset();
            xf_bpm = C[k].bpm;
            xfp[XP_DDIV] = (int16_t)C[k].div;
            xfp[XP_DRATIO] = (int16_t)C[k].ratio;
            xfp[XP_DMODE] = 0;
            xfp[XP_DFDBK] = 0;
            xfp[XP_DWOW] = 0;
            xfp[XP_DTONE] = 127;
            xf_delay_times(&l, &r);
            run(2u * 44100u, g_imp, 1, 0);
            for (i = 0; i < 2u * 44100u; i++) {
                if (abs(WL[i]) > ml) ml = abs(WL[i]), pl = i;
                if (abs(WR[i]) > mr) mr = abs(WR[i]), pr = i;
            }
            /* (the impulse is at sample 64; the two half-band filters and the loop's low-pass delay it ~6 samples) */
            if (abs((int32_t)pl - (int32_t)(64u + 2u * l)) > 14 || abs((int32_t)pr - (int32_t)(64u + 2u * r)) > 14) ok = 0;
            printf("cr_fx:   %u BPM %s %u:%u: the echo's peak at %u / %u (the times: %u / %u at 44.1 kHz)\n", C[k].bpm,
                   DN[C[k].div], XF_RATIO[C[k].ratio][0], XF_RATIO[C[k].ratio][1], pl - 64u, pr - 64u, 2u * l, 2u * r);
        }
        check("sync: an impulse's first repeat lands on each side's time (within 14 samples: the filters' delay)", ok);
    }
}

/* the amplitude of frequency f in x[a..b) (a projection on its sine and cosine) */
static double tone_amp(const int32_t *x, uint32_t a, uint32_t b, double f)
{
    double s = 0, c = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        s += x[i] * sin(2.0 * M_PI * f * i / 44100.0);
        c += x[i] * cos(2.0 * M_PI * f * i / 44100.0);
    }
    return 2.0 * sqrt(s * s + c * c) / (double)(b - a);
}
static void t_halfrate(void)
{
    static const double F[] = {50, 100, 200, 1000, 3000, 5000, 7000, 9000, 10000, 11000, 12500, 15000};
    double ref = 0, g[12], lo = 0, hi = 0;
    uint32_t k, n = 44100u, a = 22050u;
    int ok = 1;
    printf("cr_fx:   the delay path (Tone open, no feedback, one repeat), dB re 1 kHz:");
    for (k = 0; k < 12u; k++) {
        fx_reset();
        xfp[XP_DMODE] = 0;
        xfp[XP_DFDBK] = 0;
        xfp[XP_DWOW] = 0;
        xfp[XP_DTONE] = 127;
        xfp[XP_DSYNC] = 0;
        xfp[XP_DTIME] = 20;
        g_f = F[k];
        g_a = 8000;
        g_len = 0xFFFFFFFFu;
        run(n, g_sine, 1, 0);
        g[k] = tone_amp(WL, a, n, F[k]);
        if (F[k] == 1000) ref = g[k];
    }
    for (k = 0; k < 12u; k++) {
        double d = db(g[k] / ref);
        printf(" %g:%.1f", F[k], d);
        if (F[k] >= 200 && F[k] <= 9000) { if (d < lo) lo = d; if (d > hi) hi = d; }
        if (F[k] >= 12500 && d > -40) ok = 0;          /* (a send up there comes back as its alias: below) */
    }
    printf("\n");
    check("the half rate: 200 Hz .. 9 kHz within +0.5 / -4 dB (Tone's pole at 9 kHz), 12.5 kHz below -40 dB", ok && hi < 0.5 && lo > -4.0);
    {   /* a 15 kHz send: what folds below 11 kHz (15 kHz would land on 7.05 kHz); loud, to see it at all */
        double al, in;
        g_f = 15000;
        g_a = 100000;
        fx_reset();
        xfp[XP_DMODE] = 0; xfp[XP_DFDBK] = 0; xfp[XP_DWOW] = 0; xfp[XP_DTONE] = 127; xfp[XP_DSYNC] = 0; xfp[XP_DTIME] = 20;
        run(n, g_sine, 1, 0);
        al = tone_amp(WL, a, n, 22050.0 - 15000.0);
        in = ref * 100000.0 / 8000.0;                   /* (the line's tanh has that send 1.7 dB down: on the safe side) */
        printf("cr_fx:   a 15 kHz send leaves %.1f dB at 7.05 kHz (re a 1 kHz send's repeat)\n", db(al / in));
        check("the half rate: the alias of a 15 kHz send is below -50 dB", db(al / in) < -50.0);
    }
    {   /* noise and distortion: a 1 kHz send at -12 dB of the line (tanh's harmonics) and at -36 dB (the path's noise:
         * the repeat less its fundamental and harmonics 2 .. 9; whole cycles in the window, so the projections are exact) */
        static const double LV[2] = {32767.0, 2068.0};  /* (>> 2 at the line: 8192 and 517 of 32767) */
        static double y[44100];
        double f0 = 1000.0, nf[2], hd[2];
        uint32_t i, h, q;
        for (q = 0; q < 2u; q++) {
            double sig, res = 0, e = 0;
            g_f = f0;
            g_a = LV[q];
            fx_reset();
            xfp[XP_DMODE] = 0; xfp[XP_DFDBK] = 0; xfp[XP_DWOW] = 0; xfp[XP_DTONE] = 127; xfp[XP_DSYNC] = 0; xfp[XP_DTIME] = 20;
            run(n, g_sine, 1, 0);
            for (i = a; i < n; i++) y[i] = WL[i];
            sig = tone_amp(WL, a, n, f0);
            for (h = 1; h <= 9u; h++) {
                double sn = 0, c = 0;
                for (i = a; i < n; i++) {
                    sn += y[i] * sin(2.0 * M_PI * f0 * h * i / 44100.0);
                    c += y[i] * cos(2.0 * M_PI * f0 * h * i / 44100.0);
                }
                sn *= 2.0 / (n - a);
                c *= 2.0 / (n - a);
                if (h > 1u) e += sn * sn + c * c;
                for (i = a; i < n; i++)
                    y[i] -= sn * sin(2.0 * M_PI * f0 * h * i / 44100.0) + c * cos(2.0 * M_PI * f0 * h * i / 44100.0);
            }
            for (i = a; i < n; i++) res += y[i] * y[i];
            /* re the line's full scale: a full-scale sine's repeat would be sig * 32767 / (LV / 4) */
            nf[q] = db(sqrt(res / (n - a)) / (sig * 32767.0 / (LV[q] / 4.0) / sqrt(2.0)));
            hd[q] = db(sqrt(e) / sig);
        }
        printf("cr_fx:   a 1 kHz send's repeat: at -12 dB of the line harmonics %.1f dB (tanh), at -36 dB %.1f dB; the noise %.1f dB re "
               "the line's full scale (%.1f dB under the -36 dB sine)\n", hd[0], hd[1], nf[1], nf[1] + 36.0);
        check("the delay path's noise is 84 dB under the line's full scale; a -36 dB send's harmonics below -50 dB (dsp.c's tanh table)",
              nf[1] < -84.0 && hd[1] < -50.0);
    }
}

/* the corners */
typedef struct { const char *name; int16_t p[XP_N]; uint16_t bpm; } corner_t;
#define DL(time, div, spr, rat, fb, sync, mode, tone, wow) time, div, spr, rat, fb, sync, mode, tone, wow
#define RV(size, damp, decay, pre, mod, low, width) size, damp, decay, pre, mod, low, width
static const corner_t CORNERS[] = {
    {"defaults", {DL(80, 6, 0, 0, 58, 1, 1, 84, 24), RV(88, 56, 52, 16, 56, 60, 127)}, 120},
    {"delay: 1 s, feedback max, tone open, wow max, stereo; reverb: size max, decay 126, damp 0, mod max",
     {DL(127, 12, 0, 0, 127, 0, 0, 127, 127), RV(127, 0, 126, 127, 127, 0, 127)}, 120},
    {"delay: 21 ms, feedback max, tone shut, ping-pong, spread max; reverb: size 0, decay 126, damp max",
     {DL(0, 0, 127, 3, 127, 0, 1, 0, 0), RV(0, 127, 126, 0, 0, 127, 0)}, 120},
    {"delay: synced 1/2 at 20 BPM (folds), ratio 1:2, feedback max, wow max; reverb: decay 0, size max, low cut max",
     {DL(64, 12, 64, 3, 127, 1, 1, 127, 127), RV(127, 64, 0, 127, 127, 127, 64)}, 20},
    {"delay: synced 1/32 at 300 BPM, feedback max, tone open; reverb: size 0, decay 0, damp 0, mod max",
     {DL(64, 0, 0, 0, 127, 1, 0, 127, 127), RV(0, 0, 0, 0, 127, 0, 127)}, 300},
    {"reverb: decay Inf, size max, damp 0, mod 0 (the frozen hall); delay: feedback 0",
     {DL(80, 6, 0, 0, 0, 1, 1, 84, 0), RV(127, 0, 127, 0, 0, 0, 127)}, 120},
    {"reverb: decay Inf, size max, damp 0, mod max, pre max; delay: feedback max, 1 s",
     {DL(127, 12, 0, 0, 127, 0, 1, 127, 127), RV(127, 0, 127, 127, 127, 0, 127)}, 120},
    {"reverb: decay Inf, size 0, damp max, mod max",
     {DL(80, 6, 0, 0, 58, 1, 1, 84, 24), RV(0, 127, 127, 0, 127, 127, 127)}, 120},
};
static void t_corners(void)
{
    uint32_t k, i, n = 60u * 44100u;
    for (k = 0; k < sizeof CORNERS / sizeof CORNERS[0]; k++) {
        const corner_t *c = &CORNERS[k];
        double pk = 0, mean = 0, r10, r60, last = 0;
        uint32_t zero_from = n, live;
        int inf = c->p[XP_RDECAY] >= 100 || (c->p[XP_DFDBK] >= 120 && c->p[XP_DTONE] >= 64 && !(c->bpm == 300)), ok;
                                                        /* (an RT60 of 22 s and more, a long delay at feedback 0.97: not over in 50 s) */
        char line[256];
        fx_reset();
        memcpy(xfp, c->p, sizeof xfp);
        xf_bpm = c->bpm;
        xs = 0x1234567u;
        run(n, g_loud, 3, 0);
        for (i = 0; i < n; i++) {
            double a = fabs((double)WL[i]) > fabs((double)WR[i]) ? fabs((double)WL[i]) : fabs((double)WR[i]);
            if (a > pk) pk = a;
            if (WL[i] | WR[i]) zero_from = n;
            else if (zero_from == n) zero_from = i;
        }
        for (i = n - 10u * 44100u; i < n; i++) mean += WL[i] + WR[i];
        mean /= 2.0 * 10.0 * 44100.0;
        r10 = rms(WL, 10u * 44100u, 11u * 44100u);
        r60 = rms(WL, 59u * 44100u, 60u * 44100u);
        last = fabs(mean);
        live = xf.d_live + xf.r_live + xf.up_live;
        /* the wet is two saturated effects: each at most 65534 a side out of the interpolator's input, and the
         * half-band's overshoot on top of it */
        ok = pk <= 65534.0 * 4.7 && (inf ? r60 <= r10 * 1.001 + 1.0 && last < 2.0 + r60 * 0.02
                                         : zero_from < n && !live);
        snprintf(line, sizeof line, "stable 60 s: %.72s", c->name);
        check(line, ok);
        if (inf)
            printf("cr_fx:   peak %.0f; the wet 1 s after the input %.0f rms, 50 s later %.0f rms (%.1f dB), the last 10 s' mean %.2f\n",
                   pk, r10, r60, db(r60 / (r10 > 1 ? r10 : 1)), mean);
        else
            printf("cr_fx:   peak %.0f; exactly 0 from %.2f s after the input stopped, both effects off (floor: 0)\n", pk,
                   zero_from / 44100.0 - 10.0);
    }
}

/* the reverb's bound: the lines' energy, sampled once a second with no input */
static double lines_energy(void)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < XR_N; i++)
        e += (double)xr_buf[i] * xr_buf[i];
    return e;
}
static uint32_t sweep_from, sweep_ms = 30;
static void h_size_sweep(uint32_t i)
{
    if (i >= sweep_from && !((i - sweep_from) % (sweep_ms * 44100u / 1000u / CTL * CTL))) {
        uint32_t k = (i - sweep_from) / (sweep_ms * 44100u / 1000u / CTL * CTL);
        xfp[XP_RSIZE] = (int16_t)((k / 127u) & 1u ? 127 - k % 127u : k % 127u);
    }
}
static void t_bound(void)
{
    uint32_t s, ok = 1;
    double e0, e, worst = 0;
    fx_reset();
    xfp[XP_RDECAY] = 127;
    xfp[XP_RDAMP] = 0;
    xfp[XP_RMOD] = 127;
    xfp[XP_RSIZE] = 127;
    xfp[XP_RLOW] = 0;
    g_a = 60000;
    g_len = 3u * 44100u;
    xs = 99u;
    run(3u * 44100u, g_noise, 2, 0);
    e0 = lines_energy();
    sweep_from = 0;
    for (s = 0; s < 40u; s++) {
        run(44100u - 44100u % CTL, 0, 2, s >= 10u && s < 25u ? h_size_sweep : 0);
        e = lines_energy();
        if (e / e0 > worst) worst = e / e0;
        if (e > e0 * 1.26) ok = 0;                       /* (1 dB: a second's swing of the modulated lines) */
    }
    printf("cr_fx:   DECAY Inf, MOD 127, no input for 40 s (SIZE swept 0 .. 127 .. 0 from 10 to 25 s): the lines' energy at most "
           "%.2f dB of where it started, %.1f dB at the end\n", 10.0 * log10(worst), 10.0 * log10(e / e0));
    check("the reverb's loop never gains: the lines' energy with no input stays within 1 dB of its start, and falls", ok && e < e0);
}

/* smooth changes. A step of d in a signal shows as a second difference of d; a smooth signal's second difference
 * is a small part of its first (a 220 Hz sine: 3 %; played 1.5 x as fast by a glide: 5 %). The measure: the largest
 * second difference of the wet as a part of the largest first difference in the window (d1 out) */
static double max_step(uint32_t a, uint32_t b, double *d1)
{
    double m1 = 0, m2 = 0;
    uint32_t i;
    for (i = a + 2u; i < b; i++) {
        double p = fabs((double)WL[i] - WL[i - 1u]), q = fabs((double)WR[i] - WR[i - 1u]);
        double p2 = fabs((double)WL[i] - 2.0 * WL[i - 1u] + WL[i - 2u]), q2 = fabs((double)WR[i] - 2.0 * WR[i - 1u] + WR[i - 2u]);
        if (p > m1) m1 = p;
        if (q > m1) m1 = q;
        if (p2 > m2) m2 = p2;
        if (q2 > m2) m2 = q2;
    }
    if (d1)
        *d1 = m1;
    return m2 / (m1 > 1.0 ? m1 : 1.0);
}
static uint32_t sw_par, sw_lo, sw_hi;
static void h_sweep(uint32_t i)                         /* a detent every 30 ms, up then down; a toggle every 400 ms */
{
    uint32_t per = (sw_hi - sw_lo <= 3u ? 400u : 30u) * 44100u / 1000u / CTL * CTL, k;
    if (i < sweep_from || (i - sweep_from) % per)
        return;
    k = (i - sweep_from) / per;
    if (sw_hi - sw_lo <= 3u)
        xfp[sw_par] = (int16_t)(sw_lo + k % (sw_hi - sw_lo + 1u));
    else {
        uint32_t span = sw_hi - sw_lo, ph = k % (2u * span);
        xfp[sw_par] = (int16_t)(sw_lo + (ph < span ? ph : 2u * span - ph));
    }
}
static void h_tempo(uint32_t i)                         /* the tempo 120 -> 60 -> 180 in steps of 1 BPM every 30 ms */
{
    uint32_t per = 30u * 44100u / 1000u / CTL * CTL, k;
    if (i < sweep_from || (i - sweep_from) % per)
        return;
    k = (i - sweep_from) / per;
    xf_bpm = (uint16_t)(k < 60u ? 120u - k : k < 180u ? 60u + (k - 60u) : 180u);
}
static void t_smooth(void)
{
    static const struct { const char *name; uint8_t which, par, lo, hi, sync; } S[] = {
        {"Time (free, 21 .. 1000 ms)", 1, XP_DTIME, 0, 127, 0}, {"Time (synced divisions)", 1, XP_DDIV, 0, 12, 1},
        {"Spread (free)", 1, XP_DSPREAD, 0, 127, 0}, {"Spread (synced)", 1, XP_DRATIO, 0, 3, 1},
        {"Sync on / off", 1, XP_DSYNC, 0, 1, 1}, {"Mode stereo / ping-pong", 1, XP_DMODE, 0, 1, 1},
        {"Tone", 1, XP_DTONE, 0, 127, 1}, {"Wow", 1, XP_DWOW, 0, 127, 1}, {"Feedback", 1, XP_DFDBK, 0, 127, 1},
        {"Size", 2, XP_RSIZE, 0, 127, 1}, {"Pre-delay", 2, XP_RPRE, 0, 127, 1}, {"Decay", 2, XP_RDECAY, 0, 127, 1},
        {"Damp", 2, XP_RDAMP, 0, 127, 1}, {"Mod", 2, XP_RMOD, 0, 127, 1}, {"Low cut", 2, XP_RLOW, 0, 127, 1},
        {"Width", 2, XP_RWIDTH, 0, 127, 1}};
    uint32_t k, n = 12u * 44100u;
    double base[3] = {0, 0, 0}, d1;
    g_f = 220.0;
    g_a = 16000.0;
    g_len = 0xFFFFFFFFu;
    for (k = 1; k <= 2u; k++) {                         /* the steady signal's own, each effect */
        fx_reset();
        run(4u * 44100u, g_sine, k, 0);
        base[k] = max_step(3u * 44100u, 4u * 44100u, &d1);
    }
    printf("cr_fx:   a held 220 Hz send, steady: the wet's largest second difference is %.1f %% (delay), %.1f %% (reverb) of its "
           "largest step\n", base[1] * 100.0, base[2] * 100.0);
    for (k = 0; k < sizeof S / sizeof S[0]; k++) {
        double m;
        char line[200];
        fx_reset();
        xfp[XP_DSYNC] = S[k].sync;
        sw_par = S[k].par;
        sw_lo = S[k].lo;
        sw_hi = S[k].hi;
        sweep_from = 2u * 44096u;
        run(n, g_sine, S[k].which, h_sweep);
        m = max_step(2u * 44096u, n, &d1);
        snprintf(line, sizeof line, "smooth: %s swept under a held tone: second difference %.1f %% of the largest step (%.0f)",
                 S[k].name, m * 100.0, d1);
        check(line, m <= 0.25);
    }
    fx_reset();
    sweep_from = 2u * 44096u;
    run(n, g_sine, 1, h_tempo);
    {
        double m = max_step(2u * 44096u, n, &d1);
        char line[200];
        snprintf(line, sizeof line, "smooth: the tempo stepped 120 -> 60 -> 180 BPM under a synced delay: second difference %.1f %%",
                 m * 100.0);
        check(line, m <= 0.25);
    }
}

static void t_level(void)
{
    static int32_t ow[4u * 44100u];
    uint32_t n = 4u * 44100u - (4u * 44100u) % CTL, a = 44100u, b = 3u * 44100u;
    double o, nw;
    g_len = 3u * 44100u;
    /* the reverb */
    old_run(n, g_chord, 2, ow);
    o = rms(ow, a, b);
    fx_reset();
    run(n, g_chord, 2, 0);
    nw = sqrt((pow(rms(WL, a, b), 2) + pow(rms(WR, a, b), 2)) / 2.0);
    printf("cr_fx:   a held chord's reverb, 1 .. 3 s: Felucca's ROOM %.0f rms, the new one %.0f rms a side (%+.2f dB)\n", o, nw, db(nw / o));
    check("level: the reverb's wet at its defaults within 1 dB of ROOM's at its defaults (the same send)", fabs(db(nw / o)) < 1.0);
    /* the delay: a repeat's power in the room. Felucca's played each repeat on both sides; Stereo does the same, and
     * ping-pong plays it on one side 3 dB up. The first repeat of a 10 ms burst of the chord, both sides' energy */
    {
        static const char *const MD[2] = {"Stereo", "ping-pong"};
        uint32_t m, i, dl = 44100u * 60u / 120u / 2u;   /* 1/8 at 120 BPM */
        double eo = 0, en;
        g_len = 441u;
        old_run(n, g_chord, 1, ow);
        for (i = dl - 100u; i < dl + 1500u; i++) eo += 2.0 * (double)ow[i] * ow[i];
        for (m = 0; m < 2u; m++) {
            char line[160];
            fx_reset();
            xfp[XP_DMODE] = (int16_t)m;
            xfp[XP_DWOW] = 0;
            run(n, g_chord, 1, 0);
            for (en = 0, i = dl - 100u; i < dl + 1500u; i++) en += (double)WL[i] * WL[i] + (double)WR[i] * WR[i];
            snprintf(line, sizeof line, "level: the delay's first repeat in %s: %+.2f dB of Felucca's (both sides' energy, the same send)",
                     MD[m], 10.0 * log10(en / eo));
            check(line, fabs(10.0 * log10(en / eo)) < 1.0);
        }
        g_len = 3u * 44100u;
        old_run(n, g_chord, 1, ow);
        o = rms(ow, a, b);
        fx_reset();
        run(n, g_chord, 1, 0);
        nw = sqrt((pow(rms(WL, a, b), 2) + pow(rms(WR, a, b), 2)) / 2.0);
        printf("cr_fx:   a held chord's delay, 1 .. 3 s: Felucca's %.0f rms, the new one %.0f rms a side (%+.2f dB: its repeats overlap "
               "on one side at a time)\n", o, nw, db(nw / o));
    }
}

static int32_t g_burst(uint32_t i) { return i < 4410u ? g_chord(i) : 0; }
static void t_mono(void)
{
    uint32_t i, n = 4u * 44100u - (4u * 44100u) % CTL, same = 1;
    double sum, l, r;
    g_len = 0xFFFFFFFFu;
    fx_reset();
    xfp[XP_DMODE] = 0;
    xfp[XP_DWOW] = 127;
    xfp[XP_DFDBK] = 100;
    run(n, g_burst, 1, 0);
    for (i = 0; i < n; i++)
        if (WL[i] != WR[i]) same = 0;
    check("mono: Stereo mode at Spread 0 is the same sample left and right (the wow at its deepest too)", same && rms(WL, 0, n) > 100.0);
    fx_reset();
    xfp[XP_DWOW] = 0;
    run(n, g_burst, 1, 0);
    {   /* ping-pong: L + R holds both sides' energy (the repeats do not overlap, nothing cancels) */
        static int32_t m[4u * 44100u];
        for (i = 0; i < n; i++) m[i] = WL[i] + WR[i];
        sum = rms(m, 0, n);
        l = rms(WL, 0, n);
        r = rms(WR, 0, n);
    }
    printf("cr_fx:   ping-pong at Spread 0: L %.0f rms, R %.0f rms, L + R %.0f rms (their power sum: %.0f)\n", l, r, sum, sqrt(l * l + r * r));
    check("mono: ping-pong's L + R keeps every repeat (within 0.5 dB of the two sides' power)", fabs(db(sum / sqrt(l * l + r * r))) < 0.5);
}

/* RT60 from an impulse response: Schroeder's backward integral, the -5 .. -35 dB slope */
static double rt60(uint32_t n)
{
    static double e[MAXS / 64u];
    uint32_t nb = n / 64u, i, k, a = 0, b = 0;
    double tot = 0, acc = 0;
    for (k = 0; k < nb; k++) {
        e[k] = 0;
        for (i = 0; i < 64u; i++)
            e[k] += (double)WL[k * 64u + i] * WL[k * 64u + i] + (double)WR[k * 64u + i] * WR[k * 64u + i];
        tot += e[k];
    }
    for (k = 0; k < nb; k++) {
        double d = 10.0 * log10((tot - acc) / tot + 1e-30);
        acc += e[k];
        if (!a && d < -5.0) a = k;
        if (!b && d < -35.0) b = k;
    }
    return b > a ? (b - a) * 64.0 / 44100.0 * 2.0 : 0.0;
}
static int32_t g_click(uint32_t i) { return i >= 64u && i < 68u ? 120000 : 0; }
static int32_t g_bang(uint32_t i) { return i < 2205u ? noise(120000) : 0; }   /* 50 ms, the loudest a line takes */
static void t_silence(void)
{
    static const struct { uint8_t decay, size, damp, mod; double lo, hi; } R[] = {   /* (DAMP 0, MOD 0: the gains alone) */
        {0, 88, 0, 0, 0.22, 0.41}, {32, 88, 0, 0, 0.89, 1.62}, {48, 88, 0, 0, 1.78, 3.24}, {64, 88, 0, 0, 3.55, 6.5},
        {80, 127, 0, 0, 7.1, 13.0}, {48, 0, 0, 0, 1.78, 3.24}, {48, 127, 0, 0, 1.78, 3.24},
        {48, 88, 56, 56, 1.0, 3.24}, {48, 88, 127, 56, 0.3, 2.4}};
    uint32_t k, n, i, ok = 1;
    uint64_t t0, t1, t2;
    for (k = 0; k < sizeof R / sizeof R[0]; k++) {
        double t;
        fx_reset();
        xfp[XP_RDECAY] = R[k].decay;
        xfp[XP_RSIZE] = R[k].size;
        xfp[XP_RDAMP] = R[k].damp;
        xfp[XP_RMOD] = R[k].mod;
        xfp[XP_RLOW] = 0;
        n = (uint32_t)(xf_rt_ms(R[k].decay) * 1.6 * 44.1) + 2u * 44100u;
        n -= n % CTL;
        run(n, g_bang, 2, 0);
        t = rt60(n);
        printf("cr_fx:   DECAY %3u (%.1f s), SIZE %3u, DAMP %3u, MOD %3u: RT60 measured %.2f s\n", R[k].decay,
               xf_rt_ms(R[k].decay) / 1000.0, R[k].size, R[k].damp, R[k].mod, t);
        if (t < R[k].lo || t > R[k].hi) ok = 0;
    }
    check("the reverb's RT60 is DECAY's (0.3 .. 9.6 s within a factor of 1.35, DAMP and MOD 0) at any SIZE; DAMP shortens it", ok);
    {   /* every tail ends: DECAY up to 19 s x SIZE x DAMP x MOD, a loud 50 ms burst, then silence for 8 RT60 + 20 s */
        static const uint8_t DC[4] = {0, 40, 72, 96}, SZ[3] = {0, 64, 127}, DM[2] = {0, 127}, MD[2] = {0, 127};
        uint32_t a, b, c, d, cases = 0, ended = 0;
        double worst = 0;
        for (a = 0; a < 4u; a++)
            for (b = 0; b < 3u; b++)
                for (c = 0; c < 2u; c++)
                    for (d = 0; d < 2u; d++) {
                        fx_reset();
                        xfp[XP_RDECAY] = DC[a];
                        xfp[XP_RSIZE] = SZ[b];
                        xfp[XP_RDAMP] = DM[c];
                        xfp[XP_RMOD] = MD[d];
                        xfp[XP_RLOW] = 0;
                        n = (uint32_t)(xf_rt_ms(DC[a]) * 8.0 * 44.1) + 20u * 44100u;
                        if (n > MAXS - CTL) n = MAXS - CTL;
                        run(n, g_bang, 2, 0);
                        for (i = n; i > 0u && !(WL[i - 1u] | WR[i - 1u]); i--) {}
                        cases++;
                        if (!xf.r_live && !xf.up_live && i < n) ended++;
                        else printf("cr_fx:   DECAY %u SIZE %u DAMP %u MOD %u: not over after %.0f s\n", DC[a], SZ[b], DM[c], MD[d], n / 44100.0);
                        if (i / 44100.0 / (xf_rt_ms(DC[a]) / 1000.0) > worst) worst = i / 44100.0 / (xf_rt_ms(DC[a]) / 1000.0);
                    }
        printf("cr_fx:   %u of %u tails ended in exact silence, the reverb off; the longest took %.1f x its RT60\n", ended, cases, worst);
        check("every tail ends: DECAY 0.3 .. 19 s x SIZE x DAMP x MOD after a loud burst: exactly 0 and the reverb off", ended == cases);
    }
    /* the defaults, a chord stab into both: when do they switch off, and what does a block cost then */
    fx_reset();
    g_len = 0xFFFFFFFFu;
    n = 60u * 44100u - (60u * 44100u) % CTL;
    run(n, g_burst, 3, 0);
    for (i = n; i > 0u && !(WL[i - 1u] | WR[i - 1u]); i--) {}
    printf("cr_fx:   a 0.1 s chord stab into both at the defaults: the last non-zero wet sample at %.2f s; live counters %u %u %u\n",
           i / 44100.0, xf.d_live, xf.r_live, xf.up_live);
    check("silence in, silence out: the wet is exactly 0 and both effects are off within 60 s of a stab (defaults)",
          i < n - 44100u && !xf.d_live && !xf.r_live && !xf.up_live);
    {
        static int32_t z[CTL], ml[CTL], mr[CTL], s[CTL];
        uint32_t j, reps = 20000;
        for (j = 0; j < CTL; j++) s[j] = noise(20000);
        t0 = instr_now();
        for (j = 0; j < reps; j++) xf_block(z, z, ml, mr, CTL);
        t1 = instr_now();
        for (j = 0; j < reps; j++) xf_block(s, s, ml, mr, CTL);
        t2 = instr_now();
        if (t0 && t2 > t1) {
            double idle = (double)(t1 - t0) / reps * 4.0, on = (double)(t2 - t1) / reps * 4.0;
            printf("cr_fx:   host instructions a 128-frame block: off %.0f (%.1f us on the device), both on %.0f (%.0f us = %.1f %% of "
                   "2902 us; emu.c's 259 a us)\n", idle, idle / 259.0, on, on / 259.0, on / 259.0 / 29.02);
            check("the cost: both effects on <= 350 us a block (device estimate), off <= 5 us", on / 259.0 <= 350.0 && idle / 259.0 <= 5.0);
        } else {
            printf("cr_fx:   (no instruction counter on this host: the cost is tools/emu/perf.sh's)\n");
        }
    }
}

/* the reverb's impulse responses and the delay's repeats, for tools/fx_plots.py and the ear */
static void demos(void)
{
    static const struct { const char *file; int16_t size, decay, damp; uint32_t secs; } IR[] = {
        {"ir_reverb_small.wav", 20, 32, 40, 4}, {"ir_reverb_default.wav", 88, 48, 56, 8}, {"ir_reverb_large.wav", 127, 80, 30, 20},
        {"ir_reverb_inf.wav", 127, 127, 20, 30}};
    static const struct { const char *file; int16_t mode, sync, div, ratio, fb; } DL_[] = {
        {"ir_delay_stereo.wav", 0, 1, 6, 0, 80}, {"ir_delay_pingpong.wav", 1, 1, 6, 0, 80},
        {"ir_delay_pingpong_dotted.wav", 1, 1, 9, 1, 80}};
    uint32_t k, n;
    for (k = 0; k < 4u; k++) {
        fx_reset();
        xfp[XP_RSIZE] = IR[k].size;
        xfp[XP_RDECAY] = IR[k].decay;
        xfp[XP_RDAMP] = IR[k].damp;
        n = IR[k].secs * 44100u - (IR[k].secs * 44100u) % CTL;
        run(n, g_click, 2, 0);
        wav_write(IR[k].file, n, 4.0);
    }
    for (k = 0; k < 3u; k++) {
        fx_reset();
        xfp[XP_DMODE] = DL_[k].mode;
        xfp[XP_DSYNC] = DL_[k].sync;
        xfp[XP_DDIV] = DL_[k].div;
        xfp[XP_DRATIO] = DL_[k].ratio;
        xfp[XP_DFDBK] = DL_[k].fb;
        n = 6u * 44100u - (6u * 44100u) % CTL;
        run(n, g_burst, 1, 0);
        wav_write(DL_[k].file, n, 1.0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1)
        outdir = argv[1];
    WL = calloc(MAXS, sizeof *WL);
    WR = calloc(MAXS, sizeof *WR);
    if (!WL || !WR)
        return 2;
    t_maths();
    t_sync();
    t_halfrate();
    t_level();
    t_mono();
    t_smooth();
    t_silence();
    t_bound();
    t_corners();
    demos();
    printf(bad ? "cr_fx_test: %d FAILED\n" : "cr_fx_test: all ok\n", bad);
    return bad != 0;
}
