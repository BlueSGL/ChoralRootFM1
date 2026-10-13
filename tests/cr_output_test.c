/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Options > Output: Classic / Clean (firmware/src/fx.c, docs/AUDIO-QUALITY.md) on the host, through hostsim.c:
 *   build/host/cr_output_test                 (sh tests/run_cr_tests.sh)
 *  1. the knee: the identity up to CLN_K, monotonic, odd, never past full scale, no step and no corner at the knee;
 *  2. the limiter's gain: clean_gain against the 64-bit product, to a unit, over its whole range;
 *  3. the staging: under the threshold the Clean chain is its input (after the DC blocker) to the bit, CLN_LA samples
 *     late; over it the limiter holds the peak at the threshold and its gain does not move with a 40 Hz note's period
 *     (Classic's does);
 *  3b. the look-ahead: the block maximum against a brute-force one over random blocks; no sample leaves the limiter
 *     above the threshold (= the knee's straight part) for bursts, steps and noise up to 12 dB over it, from the first
 *     sample on, and far over it; the gain never above 1, never a step between samples; no DC added; the delay is
 *     exactly CLN_LA samples; a call that is not whole blocks (the slow way) keeps the same bound;
 *  4. MASTER last: at -24 and -40 dB the Clean chain's DAC word has the bits under the Q15 sample and its error
 *     against the exact product stays under -70 / -65 dB of the signal (Classic: MASTER in front, the Q15 grid);
 *  5. the switch: Classic <-> Clean under a held tone, both ways: the chain changes after OUT_FADE_BLK blocks and the
 *     silent ones that let Clean's delay run out, at a silent sample, no sample step above the steady signal's own,
 *     and the fade ends; nothing of what was in the delay is heard when Clean comes back;
 *  6. Classic is the old line: out_master in Classic = the mix line and master_block, sample for sample, and leaves
 *     the DAC word's low bits alone;
 *  7. USB Level = Fixed in Clean: USB's tap gets the full-level Q15 sample whatever MASTER says, and the DAC word is
 *     the one USB Level = Master gives. */
#define CLN_GAIN_TAP                    /* fx.c: the limiter's gain of each sample of the last clean_block */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("cr_output: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static void rest(uint32_t clean)
{
    dc_l = dc_r = dce_l = dce_r = 0;
    lim_env = LIM_T;
    lim_g = 32768;
    fx_usb_fixed = 0;
    fx_lowcut = 0;
    fx_out_pin = 0;
    fx_output_set(clean, 1);
}

static void t_knee(void)
{
    int32_t x, prev = -1, ok_id = 1, ok_mono = 1, ok_odd = 1, ok_max = 1, ok_slope = 1;
    for (x = 0; x < (1 << 23); x += x < 200000 ? 1 : 97) {
        int32_t y = clean_knee(x);
        if (x <= CLN_K4 && y != x) ok_id = 0;
        if (y < prev) ok_mono = 0;
        if (clean_knee(-x) != -y) ok_odd = 0;
        if (y > 131071) ok_max = 0;
        if (x > 0 && x < 200000 && (y - prev > 2 || y - prev < 0)) ok_slope = 0;   /* slope within 0..1, no step (two
                                                                                    * roundings: at most 2 of Q17) */
        prev = y;
    }
    check("knee: the identity up to CLN_K (-2.0 dBFS, the limiter's threshold)", ok_id && CLN_K >= CLN_T);
    check("knee: monotonic, odd, slope between 0 and 1 (no step, no corner at the knee)", ok_mono && ok_odd && ok_slope);
    check("knee: never past the Q15 sample's full scale (131071 in Q17); far over it ends 0.07 dB under (the table's tanh 2)",
          ok_max && clean_knee(1 << 22) >= 129800);
    /* the first 1 % past the knee is still the line (the tanh starts with slope 1) */
    check("knee: 1 % over the knee is within one Q15 unit of the straight line",
          CLN_K4 + 1040 - clean_knee(CLN_K4 + 1040) <= 4);
}

static void t_gain(void)
{
    uint32_t s = 12345u, i;
    int ok = 1;
    for (i = 0; i < 2000000u; i++) {
        int32_t x, g, d;
        s = s * 1664525u + 1013904223u;
        x = (int32_t)(s >> 8) - (1 << 23);              /* +-2^23 */
        s = s * 1664525u + 1013904223u;
        g = (int32_t)(s >> 17);                         /* 0..32767 */
        d = clean_gain(x, g) - (int32_t)(((int64_t)x * g) >> 15);
        if (d < -1 || d > 0)
            ok = 0;
    }
    check("limiter gain: clean_gain = the 64-bit product >> 15, at most one unit under it, over +-2^23", ok);
}

#define N (4u * 44100u)
static int32_t *buf;
/* a tone of amplitude amp (the mix's units) at f Hz through out_master, n frames; the DAC words into buf */
static void tone(double amp, double f, uint32_t n, double amp2, double f2)
{
    static int32_t l[CTL], r[CTL], w[CTL], o[2 * CTL];
    uint32_t i, k;
    for (i = 0; i + CTL <= n; i += CTL) {
        for (k = 0; k < CTL; k++) {
            double t = (double)(i + k) / 44100.0;
            l[k] = r[k] = (int32_t)lrint(amp * sin(2 * M_PI * f * t) + amp2 * sin(2 * M_PI * f2 * t));
            w[k] = 0;
        }
        out_master(o, l, r, w, CTL);
        if (fx_usb_fixed)
            usb_fixed_dac(o, CTL);
        for (k = 0; k < 2 * CTL; k++)
            buf[2 * i + k] = o[k] * 128 + (fx_out_blk ? out_frac[k] : 0);
    }
}

static void t_staging(void)
{
    static int32_t o[2 * CTL], ref[CLN_LA + CTL];
    uint32_t i, k;
    int exact = 1;
    int32_t dl = 0, el = 0, gmin = 32768, gmax = 0, cmin = 32768, cmax = 0, pk = 0, gtop = 0, gbot = 32768;
    double a = 0;
    rest(1);
    /* under the threshold: the chain is the DC blocker and the delay, and nothing else */
    memset(ref, 0, sizeof ref);
    for (i = 0; i < 44096u; i += CTL) {
        for (k = 0; k < CTL; k++) {
            int32_t x = (int32_t)lrint(4.0 * 25900.0 * sin(2 * M_PI * 441.0 * (i + k) / 44100.0));
            o[2 * k] = o[2 * k + 1] = x;
            ref[CLN_LA + k] = dc_block(x, &dl, &el);
        }
        clean_block(o, CTL);
        for (k = 0; k < CTL; k++)
            if (o[2 * k] != ref[k] || o[2 * k + 1] != ref[k])
                exact = 0;
        memmove(ref, ref + CTL, CLN_LA * sizeof ref[0]);
    }
    printf("cr_output: the Clean chain's delay: %u samples, %.2f ms\n", (unsigned)CLN_LA, CLN_LA / 44.1);
    check("staging: a tone at -2.04 dBFS, just under the threshold, comes out as it went in (after the DC blocker), bit "
          "for bit, exactly CLN_LA samples late", exact && cln_g == 32768);
    /* 6 dB over: a 40 Hz note with a 1 kHz tone on it; after the attack the gain stands still */
    rest(1);
    for (i = 0; i < 2u * 44096u; i += CTL) {
        for (k = 0; k < CTL; k++) {
            double t = (i + k) / 44100.0;
            o[2 * k] = o[2 * k + 1] =
                (int32_t)lrint(4.0 * (40000.0 * sin(2 * M_PI * 40.0 * t) + 12000.0 * sin(2 * M_PI * 1000.0 * t)));
        }
        clean_block(o, CTL);
        for (k = 0; k < CTL && i > 44100u; k++) {
            if (cln_gtap[k] < gmin) gmin = cln_gtap[k];
            if (cln_gtap[k] > gmax) gmax = cln_gtap[k];
            if (o[2 * k] > pk) pk = o[2 * k];
        }
        if (i > 44100u && (i / CTL) % 35u == 34u) {     /* a period of the 40 Hz note (25.4 ms of blocks): its movement */
            double m = 20 * log10((double)gmax / gmin);
            if (m > a) a = m;
            if (gmax > gtop) gtop = gmax;
            if (gmin < gbot) gbot = gmin;
            gmin = 32768;
            gmax = 0;
        }
    }
    printf("cr_output: Clean limiter under a 40 Hz + 1 kHz pair 6 dB over: gain %.2f dB, moving %.4f dB within a period of "
           "the note, %.3f dB over the second (the creep down to the settled peak); peak out %d of %d\n",
           20 * log10(gtop / 32768.0), a, 20 * log10((double)gtop / gbot), pk, CLN_T4);
    check("staging: 6 dB over the threshold the Clean limiter's gain stands still over the note's period (< 0.02 dB)",
          a < 0.02 && gtop < 32768);
    check("staging: and the peak is held at the threshold or up to 0.3 dB under it (the hold's margin)",
          pk <= CLN_T4 && pk >= CLN_T4 - CLN_T4 / 30);
    /* Classic on the same (its own threshold, 6 dB over): the gain rides the 40 Hz period */
    rest(0);
    fx_smooth = 1;
    for (i = 0; i < 2u * 44100u; i++) {
        double t = i / 44100.0;
        o[0] = o[1] = (int32_t)lrint(27700.0 * sin(2 * M_PI * 40.0 * t) + 8300.0 * sin(2 * M_PI * 1000.0 * t));
        master_block(o, 1u);
        if (i > 44100u) {
            if (lim_g < cmin) cmin = lim_g;
            if (lim_g > cmax) cmax = lim_g;
        }
    }
    printf("cr_output: Classic limiter on the same pair: gain moving %.3f dB every period\n", 20 * log10((double)cmax / cmin));
    check("staging: (the reference) Classic's gain moves more than 0.3 dB with every period of that note",
          20 * log10((double)cmax / cmin) > 0.3);
}

/* ---- the hold (0.19's third round) ----
 * A tone of 441 Hz through clean_block whose level follows lvl(sample) (dB re the threshold), n samples; the gain of
 * every sample into g[] (Q15). Returns nothing: the checks read g. */
static void hold_run(double (*lvl)(uint32_t), uint32_t n, int32_t *g)
{
    static int32_t o[2 * CTL];
    uint32_t i, k;
    rest(1);
    for (i = 0; i + CTL <= n; i += CTL) {
        for (k = 0; k < CTL; k++) {
            double a = 4.0 * CLN_T * pow(10.0, lvl(i + k) / 20.0);
            o[2 * k] = o[2 * k + 1] = (int32_t)lrint(a * sin(2 * M_PI * 441.0 * (i + k) / 44100.0));
        }
        clean_block(o, CTL);
        for (k = 0; k < CTL; k++)
            g[i + k] = cln_gtap[k];
    }
}
#define HS(ms) ((uint32_t)((ms) * 441u / 10u))          /* ms -> samples */
static double hl_step(uint32_t i) { return i < HS(1000) ? 3.0 : -6.0; }            /* 3 dB over for 1 s, then under */
static double hl_hit(uint32_t i) { return i >= HS(500) && i < HS(520) ? 12.0 : -6.0; }   /* a 20 ms hit 12 dB over */
static double hl_wave(uint32_t i) { return i < HS(100) ? 3.0 : ((i / HS(150)) & 1u) ? 2.0 : 3.0; }   /* 3 / 2 dB over */
static double hl_hit_then(uint32_t i) { return i >= HS(500) && i < HS(520) ? 12.0 : i < HS(500) ? -6.0 : 2.0; }
/* the first sample from `from` on whose gain is above the gain at `from` (the release begins); n if none */
static uint32_t hold_rise(const int32_t *g, uint32_t from, uint32_t n)
{
    uint32_t i;
    for (i = from; i < n; i++)
        if (g[i] > g[from])
            return i;
    return n;
}
static void t_hold(void)
{
    static int32_t g[3 * 44100];
    uint32_t n = 3u * 44100u / CTL * CTL, i, r, back;
    int32_t lo, hi;
    double dip = 0;
    /* a chord's level (no hit: 3 dB over from rest is under CLN_HIT's 3.5 dB): held CLN_HOLD after its last peak */
    hold_run(hl_step, n, g);
    r = hold_rise(g, HS(1000), n);
    printf("cr_output: the hold: 3 dB over, then under: the gain %.2f dB, let go %.1f ms after the level fell (CLN_HOLD "
           "%.1f ms)\n", 20 * log10(g[HS(1000)] / 32768.0), (r - HS(1000)) / 44.1, CLN_HOLD / 44.1);
    check("hold: a level 3 dB over the threshold is held 200 ms after its last peak (CLN_HOLD, to 3 blocks), then released",
          r >= HS(1000) + CLN_HOLD - 2u * CTL && r <= HS(1000) + CLN_HOLD + 3u * CTL && g[n - 1u] == 32768);
    /* a hit: 12 dB over for 20 ms out of a quiet tone: the short hold, and the level is back as it was in 0.18's rounds */
    hold_run(hl_hit, n, g);
    r = hold_rise(g, HS(520) + 2u * CTL, n);
    for (back = n; back > HS(520) && g[back - 1u] >= 30934; back--)   /* (30934: 0.5 dB under 1) */
        ;
    for (i = HS(500); i < HS(1500); i++)
        dip += 20 * log10(g[i] / 32768.0);
    dip /= HS(1000);
    printf("cr_output: the hold: a 20 ms hit 12 dB over: the gain %.2f dB, let go %.1f ms after it (CLN_HOLD_HIT %.1f ms), "
           "within 0.5 dB of 1 after %.0f ms; the mean dip over the second after the hit %.2f dB\n",
           20 * log10(g[HS(520)] / 32768.0), (r - HS(520)) / 44.1, CLN_HOLD_HIT / 44.1, (back - HS(500)) / 44.1, dip);
    check("hold: a hit (more than 3.5 dB above the envelope) gets the short hold: let go 40 ms after it (to 3 blocks)",
          r >= HS(520) + CLN_HOLD_HIT - 2u * CTL && r <= HS(520) + CLN_HOLD_HIT + 3u * CTL);
    check("hold: 12 dB down after a 20 ms hit, the level is back within 0.5 dB in under 520 ms; the second's mean dip under 3.5 dB",
          back - HS(500) < HS(520) && dip > -3.5 && g[n - 1u] == 32768);
    /* held again: a level that swings 1 dB every 150 ms (inside CLN_REHOLD's 1.2 dB): the gain does not move at all */
    hold_run(hl_wave, n, g);
    for (i = HS(200), lo = 32768, hi = 0; i < n; i++) {
        if (g[i] < lo) lo = g[i];
        if (g[i] > hi) hi = g[i];
    }
    printf("cr_output: the hold: a level swinging between 3 and 2 dB over every 150 ms: the gain moves %.4f dB\n",
           20 * log10((double)hi / lo));
    check("hold: a level that swings 1 dB every 150 ms (peaks within 1.2 dB of the envelope, the top back inside the hold) "
          "keeps one gain", hi == lo && hi < 32768);
    /* after a hit the long hold is back: the hit's envelope lets go down to the tone 2 dB over, stops within 1.2 dB
     * of it and creeps the rest of the way: no step, and nothing left of the hit */
    hold_run(hl_hit_then, n, g);
    for (i = HS(520) + CLN_HOLD_HIT + 4u * CTL, r = 0; i < n; i++)
        if (g[i] < g[i - 1u])
            r++;                                        /* (after the hit's hold the gain only ever comes back up) */
    for (i = n - HS(300), lo = 32768, hi = 0; i < n; i++) {
        if (g[i] < lo) lo = g[i];
        if (g[i] > hi) hi = g[i];
    }
    printf("cr_output: the hold: a hit, then a tone 2 dB over: the gain 1 s after the hit %.2f dB, at the end (2.5 s) %.2f dB, "
           "moving %.4f dB over the last 300 ms\n", 20 * log10(g[HS(1500)] / 32768.0), 20 * log10(hi / 32768.0),
           20 * log10((double)hi / lo));
    check("hold: after a hit the envelope lets go down to the level that follows (2 dB over): within 0.3 dB of it after 2.5 s, "
          "never turned down again on the way, under 0.1 dB in 300 ms at the end", !r &&
          fabs(20 * log10(hi / 32768.0) + 2.0) < 0.3 && 20 * log10((double)hi / lo) < 0.1);
}

/* ---- the look-ahead ---- */
static uint32_t lrng = 2463534242u;
static uint32_t lr(void) { lrng ^= lrng << 13; lrng ^= lrng >> 17; lrng ^= lrng << 5; return lrng; }
/* a test signal of kind k, sample i (Q17, both channels differ): up to `top` in amplitude */
static void la_sig(uint32_t kind, uint32_t i, int32_t top, int32_t *l, int32_t *r)
{
    static int32_t lvl, hold;
    double t = i / 44100.0;
    switch (kind) {
    case 0:                                             /* noise in bursts of random level and length */
        if (!hold) {
            hold = (int32_t)(lr() % 700u) + 1;
            lvl = (lr() & 3u) ? (int32_t)(lr() % (uint32_t)top) : 0;
        }
        hold--;
        *l = lvl ? (int32_t)(lr() % (2u * (uint32_t)lvl + 1u)) - lvl : 0;
        *r = lvl ? (int32_t)(lr() % (2u * (uint32_t)lvl + 1u)) - lvl : 0;
        break;
    case 1:                                             /* a single full-level sample now and then, else a quiet tone */
        *l = (int32_t)lrint(20000.0 * sin(2 * M_PI * 300.0 * t));
        *r = *l;
        if (lr() % 211u == 0u) *l = (lr() & 1u) ? top : -top;
        if (lr() % 197u == 0u) *r = (lr() & 1u) ? top : -top;
        break;
    case 2:                                             /* a square at full level, gated: steps from nothing */
        *l = *r = ((i / 3000u) & 1u) ? (((i / 37u) & 1u) ? top : -top) : 0;
        break;
    default:                                            /* a kick: a falling sine, 0.5 ms of attack, every 250 ms */
        {
            double u = fmod(t, 0.25), f = 48.0 + 112.0 * exp(-u / 0.025);
            double e = (u < 0.0005 ? u / 0.0005 : 1.0) * exp(-u / 0.12);
            *l = (int32_t)lrint(top * e * sin(2 * M_PI * f * u));
            *r = (int32_t)lrint(0.8 * top * e * sin(2 * M_PI * f * u + 0.4));
        }
        break;
    }
}

static void t_lookahead(void)
{
    static int32_t o[2 * CTL], in[2 * CTL], hist[4][CTL], dly[CLN_LA + CTL], ref[CLN_LA + CTL];
    static const int32_t TOP[3] = {4 * 26000 * 2, 4 * 26000 * 4, 0x7FFFFF};   /* 6 dB, 12 dB, 30 dB over the threshold */
    static const uint32_t CALL[5] = {CTL, 1u, 7u, 13u, 32u};
    uint32_t kind, lv, blk, k, c, nb = CLN_LA_K + 1u;
    int32_t worst = 0, gtop = 0, gstep = 0, gprev, prev_env, env_low = 0, env_miss = 0, wm_bad = 0;
    int bound = 1, bound_short = 1, late = 1;
    int64_t sum_in = 0, sum_out = 0, nsum = 0;
    _Static_assert(CLN_LA_K + 1u <= 4u && CLN_LA_B == CTL, "t_lookahead's brute-force window (the build's own constants)");
    /* 1. whole blocks, as the ISR calls it */
    for (kind = 0; kind < 4u; kind++)
        for (lv = 0; lv < 3u; lv++) {
            rest(1);
            memset(hist, 0, sizeof hist);
            memset(dly, 0, sizeof dly);
            gprev = 32768;
            for (blk = 0; blk < 3000u; blk++) {
                int32_t brute = 0, dl0 = dc_l, el0 = dce_l, dr0 = dc_r, er0 = dce_r;
                for (k = 0; k < CTL; k++) {
                    la_sig(kind, blk * CTL + k, TOP[lv], &in[2 * k], &in[2 * k + 1]);
                    o[2 * k] = in[2 * k];
                    o[2 * k + 1] = in[2 * k + 1];
                }
                /* the reference: what goes into the delay (after the DC blocker), its peak per block, by brute force
                 * over this block and the CLN_LA_K before it */
                memmove(hist[1], hist[0], 3 * sizeof hist[0]);
                for (k = 0; k < CTL; k++) {
                    int32_t l = dc_block(in[2 * k], &dl0, &el0), r = dc_block(in[2 * k + 1], &dr0, &er0);
                    int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
                    al = al > 0x7FFFFF ? 0x7FFFFF : al;
                    ar = ar > 0x7FFFFF ? 0x7FFFFF : ar;
                    hist[0][k] = al > ar ? al : ar;
                    dly[CLN_LA + k] = hist[0][k];
                }
                for (c = 0; c < nb; c++)
                    for (k = 0; k < CTL; k++)
                        if (hist[c][k] > brute)
                            brute = hist[c][k];
                prev_env = cln_env;
                clean_block(o, CTL);
                if (cln_env < brute && brute > CLN_T4) env_low++;        /* the envelope covers every sample in the delay */
                if (brute > prev_env && cln_env != brute) env_miss++;    /* and a new maximum is taken exactly */
                for (c = 0; c < CLN_LA_K; c++) {        /* the kept peaks: each block's own, the newest first */
                    int32_t m = 0;
                    for (k = 0; k < CTL; k++)
                        if (hist[c][k] > m) m = hist[c][k];
                    if (cln_pk[c] != m) wm_bad++;
                }
                for (k = 0; k < CTL; k++) {
                    int32_t al = o[2 * k] < 0 ? -o[2 * k] : o[2 * k], ar = o[2 * k + 1] < 0 ? -o[2 * k + 1] : o[2 * k + 1];
                    int32_t g = cln_gtap[k], d = g - gprev;
                    if (al > worst) worst = al;
                    if (ar > worst) worst = ar;
                    if (al > CLN_T4 || ar > CLN_T4) bound = 0;
                    /* the gain fits the sample that goes out under the threshold: g x <= T (64-bit) */
                    if ((int64_t)dly[k] * g > (int64_t)CLN_T4 * 32768) bound = 0;
                    if (g > gtop) gtop = g;
                    d = d < 0 ? -d : d;
                    if (d > gstep) gstep = d;
                    gprev = g;
                }
                memmove(dly, dly + CTL, CLN_LA * sizeof dly[0]);
            }
        }
    printf("cr_output: look-ahead, 12 runs of 96000 samples up to 30 dB over: the largest sample out %d of %d; the gain's "
           "largest step between two samples %d of 32768 (%.3f dB)\n", worst, CLN_T4, gstep,
           20 * log10(32768.0 / (32768.0 - gstep)));
    check("look-ahead: the envelope is never under the brute-force maximum of the delay and the new block", !env_low);
    check("look-ahead: a new maximum is taken exactly, in the block it arrives in", !env_miss && !wm_bad);
    check("look-ahead: no sample leaves the limiter above the threshold (the knee's straight part), from the first on",
          bound && worst <= CLN_T4);
    check("look-ahead: the gain is never above 1", gtop <= 32768);
    /* the largest step: the whole way from 1 to the gain for 30 dB over (0.03) in one block, a 32nd of it a sample */
    check("look-ahead: the gain glides: never more than a block's share of the way between two samples (no zipper)",
          gstep <= 32768 / (int32_t)CLN_LA_B + 1);
    /* 2. no DC: a tone 9 dB over with a moving level, and the same upside down: the two outputs cancel */
    {
        static int32_t pos[2000 * CTL];
        uint32_t pass;
        for (pass = 0; pass < 2u; pass++) {
            rest(1);
            for (blk = 0; blk < 2000u; blk++) {
                for (k = 0; k < CTL; k++) {
                    double t = (blk * CTL + k) / 44100.0;
                    int32_t x = (int32_t)lrint(4.0 * 73000.0 * sin(2 * M_PI * 441.0 * t) * (0.6 + 0.4 * sin(2 * M_PI * 3.0 * t)));
                    o[2 * k] = o[2 * k + 1] = pass ? -x : x;
                }
                clean_block(o, CTL);
                for (k = 0; k < CTL; k++) {
                    if (!pass) {
                        pos[blk * CTL + k] = o[2 * k];
                    } else {
                        int32_t d = o[2 * k] + pos[blk * CTL + k];
                        sum_out += d;
                        if (d < -sum_in) sum_in = -d;   /* (the largest difference of a pair) */
                        if (d > sum_in) sum_in = d;
                        nsum++;
                    }
                }
            }
        }
    }
    printf("cr_output: look-ahead, a tone 9 dB over with a moving level and the same inverted: the two outputs' sum: mean "
           "%.3f, largest %d (Q17 units: a quarter of a Q15 unit)\n", (double)sum_out / (double)nsum, (int)sum_in);
    /* (clean_gain rounds down: a limited sample is up to a unit of Q17 low, a quarter of a Q15 unit, -102 dBFS) */
    check("look-ahead: no DC shift while limiting: a signal and its mirror come out as mirrors to 4 of Q17, a Q15 unit "
          "(Classic's line shifts a limited sample by up to 16 Q15 units)", sum_in <= 4 && llabs(sum_out) <= 2 * nsum + nsum / 2);
    /* 3. the delay: exactly CLN_LA samples, for a signal the limiter leaves alone (noise at -8 dBFS) */
    rest(1);
    memset(ref, 0, sizeof ref);
    {
        int32_t dl = 0, el = 0;
        for (blk = 0; blk < 500u; blk++) {
            for (k = 0; k < CTL; k++) {
                int32_t x = (int32_t)(lr() % 100001u) - 50000;
                o[2 * k] = x;
                o[2 * k + 1] = -x;
                ref[CLN_LA + k] = dc_block(x, &dl, &el);
            }
            clean_block(o, CTL);
            for (k = 0; k < CTL; k++)
                if (o[2 * k] != ref[k])
                    late = 0;
            memmove(ref, ref + CTL, CLN_LA * sizeof ref[0]);
        }
    }
    check("look-ahead: under the threshold the output is the input CLN_LA samples late, bit for bit (noise)", late);
    /* 4. the slow way: calls of 1, 7, 13 samples and mixed with whole blocks keep the bound */
    for (c = 0; c < 5u; c++)
        for (kind = 0; kind < 4u; kind++) {
            uint32_t i = 0;
            rest(1);
            while (i < 60000u) {
                uint32_t n = c == 4u ? (lr() % 3u ? CTL : 1u + lr() % 31u) : CALL[c];
                for (k = 0; k < n; k++)
                    la_sig(kind, i + k, TOP[1], &o[2 * k], &o[2 * k + 1]);
                clean_block(o, n);
                for (k = 0; k < 2u * n; k++)
                    if (o[k] > CLN_T4 || o[k] < -CLN_T4)
                        bound_short = 0;
                i += n;
            }
        }
    check("look-ahead: calls that are not whole blocks (1, 7, 13 samples, mixed) keep every sample under the threshold",
          bound_short);
}

/* the error of the DAC words in buf against the exact tone * gain, dB re the signal; the 2 Hz DC blocker's phase is
 * taken out by fitting sine and cosine */
static double tone_err(double f, uint32_t n)
{
    double sa = 0, sb = 0, saa = 0, sbb = 0, e = 0, p = 0, A, B;
    uint32_t i, from = n / 2u;
    n -= 2u * CTL;                                      /* (tone() writes whole blocks) */
    for (i = from; i < n; i++) {
        double s = sin(2 * M_PI * f * i / 44100.0), c = cos(2 * M_PI * f * i / 44100.0);
        sa += buf[2 * i] * s;
        sb += buf[2 * i] * c;
        saa += s * s;
        sbb += c * c;
    }
    A = sa / saa;
    B = sb / sbb;
    for (i = from; i < n; i++) {
        double y = A * sin(2 * M_PI * f * i / 44100.0) + B * cos(2 * M_PI * f * i / 44100.0);
        e += (buf[2 * i] - y) * (buf[2 * i] - y);
        p += y * y;
    }
    return 10 * log10(e / p);
}

static void t_master(void)
{
    static const uint32_t M[2] = {258u, 41u};           /* -24 dB, -40 dB */
    uint32_t k, i, low = 0;
    for (k = 0; k < 2u; k++) {
        double ec, eo;
        song.master_q12 = M[k];
        rest(1);
        tone(3000.0, 441.0, N, 0, 0);                   /* a quiet note: 21 dB under full scale before MASTER */
        ec = tone_err(441.0, N);
        for (i = N; i < 2u * N - 2u * CTL; i++)
            low |= (uint32_t)buf[i] & 127u;
        rest(0);
        fx_smooth = 1;
        tone(3000.0, 441.0, N, 0, 0);
        eo = tone_err(441.0, N);
        printf("cr_output: MASTER %u / 4096: a quiet note's error re the note: Clean %.1f dB, Classic %.1f dB\n", M[k], ec, eo);
        /* (the test's own tone is rounded to the mix's integers: -77 dB of it, the floor of what can be read here) */
        check(k ? "MASTER -40 dB: Clean's error under -65 dB of the note, Classic's above -40 dB"
                : "MASTER -24 dB: Clean's error under -70 dB of the note, Classic's above -55 dB",
              k ? ec < -65.0 && eo > -40.0 : ec < -70.0 && eo > -55.0);
    }
    check("MASTER last: the Clean chain uses the DAC word's bits under the Q15 sample", low != 0);
    song.master_q12 = 4088;
}

static void t_switch(void)
{
    static int32_t l[CTL], r[CTL], w[CTL], o[2 * CTL];
    uint32_t dir;
    for (dir = 0; dir < 2u; dir++) {
        uint32_t blk, k, changed_at = 0, fade_end = 0, silent_ok = 0;
        int32_t prev = 0, step_steady = 0, step_sw = 0;
        song.master_q12 = 4088;
        rest(dir);
        fx_smooth = 1;
        for (blk = 0; blk < 400u; blk++) {
            for (k = 0; k < CTL; k++) {
                double t = (double)(blk * CTL + k) / 44100.0;
                l[k] = r[k] = (int32_t)lrint(9000.0 * sin(2 * M_PI * 220.0 * t) + 4000.0 * sin(2 * M_PI * 3000.0 * t));
                w[k] = 0;
            }
            if (blk == 200u)
                fx_output_set(!dir, 0);                 /* Options > Output, while it sounds */
            out_master(o, l, r, w, CTL);
            if (blk >= 200u && !changed_at && fx_out_mode != dir) {
                int32_t a = 0;
                changed_at = blk;
                for (k = CTL - 4u; k < CTL; k++)        /* the block that ended at the zero: its last samples */
                    a |= o[2 * k] > 64 || o[2 * k] < -64;
                silent_ok = !a;
            }
            if (blk > 200u && changed_at && !fade_end && !fx_out_fade)
                fade_end = blk;
            for (k = 0; k < CTL; k++) {
                int32_t d = o[2 * k] - prev;
                d = d < 0 ? -d : d;
                if (((blk >= 100u && blk < 200u) || blk >= 300u) && d > step_steady) step_steady = d;   /* either chain's */
                if (blk >= 200u && blk < 245u && d > step_sw) step_sw = d;
                prev = o[2 * k];
            }
        }
        printf("cr_output: %s: the chain changed in block +%u, the fade ended in block +%u; largest sample step %d "
               "during the change, %d in the steady tone before and after\n", dir ? "Clean -> Classic" : "Classic -> Clean",
               changed_at - 200u, fade_end - 200u, step_sw, step_steady);
        check(dir ? "switch Clean -> Classic: after 16 blocks and the delay's own, at a silent sample, the fade over 16 later"
                  : "switch Classic -> Clean: after 16 blocks and the delay's own, at a silent sample, the fade over 16 later",
              changed_at == 200u + OUT_FADE_BLK + OUT_FADE_FLUSH - 1u &&
              fade_end == 200u + 2u * OUT_FADE_BLK + OUT_FADE_FLUSH - 1u && silent_ok);
        check(dir ? "switch Clean -> Classic: no sample step above the steady tone's own (+ 5 %)"
                  : "switch Classic -> Clean: no sample step above the steady tone's own (+ 5 %)",
              step_sw <= step_steady + step_steady / 20);
    }
}

/* nothing stale: a loud tone all the time, Output to Classic and back: the delay is empty when the chain changes, and
 * Clean's first block after the change back is silence (its delay holds nothing of the last time it ran) */
static void t_stale(void)
{
    static int32_t l[CTL], r[CTL], w[CTL], o[2 * CTL];
    uint32_t blk, k, emptied = 1, was = 1;
    int32_t first = -1;
    song.master_q12 = 4088;
    rest(1);
    fx_smooth = 1;
    for (blk = 0; blk < 300u; blk++) {
        for (k = 0; k < CTL; k++) {
            double t = (double)(blk * CTL + k) / 44100.0;
            l[k] = r[k] = (int32_t)lrint(30000.0 * sin(2 * M_PI * 220.0 * t));
            w[k] = 0;
        }
        if (blk == 100u)
            fx_output_set(0, 0);                        /* to Classic, the delay full of the tone */
        if (blk == 200u)
            fx_output_set(1, 0);                        /* and back */
        out_master(o, l, r, w, CTL);
        if (was != fx_out_mode) {                       /* the chain has just changed (this block: the old one's last) */
            for (k = 0; k < 2u * CLN_DLEN; k++)
                if (cln_dl[k])
                    emptied = 0;
        } else if (fx_out_mode && blk > 200u && first < 0) {
            first = 0;                                  /* Clean's first block again */
            for (k = 0; k < 2u * CTL; k++)
                if (o[k] > first || -o[k] > first)
                    first = o[k] < 0 ? -o[k] : o[k];
        }
        was = fx_out_mode;
    }
    printf("cr_output: Clean's first block after Classic -> Clean under a tone: its largest sample %d (the Q15 sample)\n",
           (int)first);
    check("switch: the delay is empty when the chain changes, and Clean's first block back is silence (nothing stale)",
          emptied && first >= 0 && first <= 2 && fx_out_mode == 1u && !fx_out_fade);
}

static void t_classic(void)
{
    static int32_t l[CTL], r[CTL], w[CTL], o[2 * CTL], ref[2 * CTL];
    int32_t s_dc[4], s_env, s_g;
    uint32_t blk, k, same = 1, clean_bits = 0;
    uint32_t s = 99u;
    song.master_q12 = 3000;
    rest(0);
    fx_smooth = 1;
    memset(out_frac, 0x55, sizeof out_frac);            /* (Classic neither reads nor writes them) */
    for (blk = 0; blk < 2000u; blk++) {
        for (k = 0; k < CTL; k++) {
            s = s * 1664525u + 1013904223u;
            l[k] = (int32_t)(s >> 14) - (1 << 17);      /* loud noise: the limiter works */
            s = s * 1664525u + 1013904223u;
            r[k] = (int32_t)(s >> 14) - (1 << 17);
            s = s * 1664525u + 1013904223u;
            w[k] = (int32_t)(s >> 18) - (1 << 13);
            ref[2 * k] = (((l[k] + w[k]) >> 2) * (int32_t)song.master_q12) >> 10;   /* 0.18's mix line */
            ref[2 * k + 1] = (((r[k] + w[k]) >> 2) * (int32_t)song.master_q12) >> 10;
        }
        s_dc[0] = dc_l; s_dc[1] = dce_l; s_dc[2] = dc_r; s_dc[3] = dce_r;
        s_env = lim_env;
        s_g = lim_g;
        master_block(ref, CTL);                         /* 0.18's master on a copy of the state .. */
        dc_l = s_dc[0]; dce_l = s_dc[1]; dc_r = s_dc[2]; dce_r = s_dc[3];
        lim_env = s_env;
        lim_g = s_g;
        out_master(o, l, r, w, CTL);                    /* .. and the block as the firmware runs it */
        for (k = 0; k < 2 * CTL; k++)
            if (o[k] != ref[k])
                same = 0;
        clean_bits |= fx_out_blk;
    }
    for (k = 0; k < sizeof out_frac; k++)
        if (out_frac[k] != 0x55)
            same = 0;
    check("Classic: out_master is 0.18's mix line and master_block, sample for sample; the word's low bits untouched",
          same && !clean_bits);
    song.master_q12 = 4088;
}

static void t_usb(void)
{
    static int32_t l[CTL], r[CTL], w[CTL], o[2 * CTL];
    static int32_t word[2][64 * 2 * CTL], tap[64 * 2 * CTL], full[64 * 2 * CTL];
    uint32_t fixed, blk, k, same = 1, tap_ok = 1;
    for (fixed = 0; fixed < 3u; fixed++) {              /* 0: Master at -18 dB, 1: Fixed at -18 dB, 2: Master at full */
        song.master_q12 = fixed == 2u ? 4096u : 515u;
        rest(1);
        fx_usb_fixed = fixed == 1u;
        for (blk = 0; blk < 64u; blk++) {
            for (k = 0; k < CTL; k++) {
                double t = (double)(blk * CTL + k) / 44100.0;
                l[k] = r[k] = (int32_t)lrint(20000.0 * sin(2 * M_PI * 330.0 * t));
                w[k] = 0;
            }
            out_master(o, l, r, w, CTL);
            for (k = 0; k < 2 * CTL; k++) {
                if (fixed == 1u) tap[blk * 2 * CTL + k] = o[k];
                if (fixed == 2u) full[blk * 2 * CTL + k] = o[k];
            }
            if (fx_usb_fixed)
                usb_fixed_dac(o, CTL);
            if (fixed < 2u)
                for (k = 0; k < 2 * CTL; k++)
                    word[fixed][blk * 2 * CTL + k] = o[k] * 128 + out_frac[k];
        }
    }
    for (k = 0; k < 64 * 2 * CTL; k++) {
        int32_t d = word[0][k] - word[1][k], e = tap[k] - full[k];
        if (d < -1 || d > 1) same = 0;                  /* (one unit of the 24-bit word: two shifts against one) */
        if (e < -1 || e > 1) tap_ok = 0;                /* (MASTER 4096: the Q15 sample of the same chain) */
    }
    check("USB Level = Fixed, Clean: USB's tap is the full-level sample whatever MASTER says", tap_ok);
    check("USB Level = Fixed, Clean: the DAC word is USB Level = Master's (to a unit of 24 bits)", same);
    fx_usb_fixed = 0;
    song.master_q12 = 4088;
}

int main(void)
{
    buf = calloc(2u * N + 64u, sizeof *buf);
    if (!buf)
        return 2;
    song.master_q12 = 4088;
    printf("cr_output: CLN_TRIM %d / 4096, CLN_T %d, CLN_K %d, hold %u (after a hit %u; a hit: env >> %d above; held again "
           "within env >> %d), release >> %d, look-ahead %u samples (%u blocks of %u), make-up %d / 4096; LIM_T %d\n",
           CLN_TRIM, CLN_T, CLN_K, (unsigned)CLN_HOLD, (unsigned)CLN_HOLD_HIT, CLN_HIT, CLN_REHOLD, CLN_REL, (unsigned)CLN_LA,
           (unsigned)CLN_LA_K, (unsigned)CLN_LA_B, CLN_MAKEUP, LIM_T);
    t_knee();
    t_gain();
    t_staging();
    t_hold();
    t_lookahead();
    t_master();
    t_switch();
    t_stale();
    t_classic();
    t_usb();
    printf(bad ? "cr_output_test: %d FAILED\n" : "cr_output_test: all ok\n", bad);
    return bad != 0;
}
