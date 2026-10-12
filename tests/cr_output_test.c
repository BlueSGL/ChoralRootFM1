/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Options > Output: Classic / Clean (firmware/src/fx.c, docs/AUDIO-QUALITY.md) on the host, through hostsim.c:
 *   build/host/cr_output_test                 (sh tests/run_cr_tests.sh)
 *  1. the knee: the identity up to CLN_K, monotonic, odd, never past full scale, no step and no corner at the knee;
 *  2. the limiter's gain: clean_gain against the 64-bit product, to a unit, over its whole range;
 *  3. the staging: under the threshold the Clean chain is its input (after the DC blocker) to the bit; over it the
 *     limiter holds the peak at the threshold and its gain does not move with a 40 Hz note's period (Classic's does);
 *  4. MASTER last: at -24 and -40 dB the Clean chain's DAC word has the bits under the Q15 sample and its error
 *     against the exact product stays under -70 / -65 dB of the signal (Classic: MASTER in front, the Q15 grid);
 *  5. the switch: Classic <-> Clean under a held tone, both ways: the chain changes after OUT_FADE_BLK blocks at a
 *     silent block, no sample step above the steady signal's own, and the fade ends;
 *  6. Classic is the old line: out_master in Classic = the mix line and master_block, sample for sample, and leaves
 *     the DAC word's low bits alone;
 *  7. USB Level = Fixed in Clean: USB's tap gets the full-level Q15 sample whatever MASTER says, and the DAC word is
 *     the one USB Level = Master gives. */
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
    cln_env = CLN_T4;
    cln_gt = cln_g = 32768;
    cln_hold = 0;
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
        if (x > 0 && x < 200000 && (y - prev > 1 || y - prev < 0)) ok_slope = 0;   /* slope within 0..1, no step */
        prev = y;
    }
    check("knee: the identity up to CLN_K (-2.5 dBFS)", ok_id);
    check("knee: monotonic, odd, slope between 0 and 1 (no step, no corner at the knee)", ok_mono && ok_odd && ok_slope);
    check("knee: never past the Q15 sample's full scale (131071 in Q17); far over it ends 0.08 dB under (the table's tanh 2)",
          ok_max && clean_knee(1 << 22) >= 129800);
    /* the first 1 % past the knee is still the line to 1 part in 2000 (the tanh starts with slope 1) */
    check("knee: 1 % over the knee is within 0.05 % of the straight line",
          CLN_K4 + 983 - clean_knee(CLN_K4 + 983) <= 1);
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
    static int32_t o[2];
    uint32_t i;
    int exact = 1;
    int32_t dl = 0, el = 0, gmin = 32768, gmax = 0, cmin = 32768, cmax = 0, pk = 0;
    double a;
    rest(1);
    /* under the threshold: the chain is the DC blocker and nothing else */
    for (i = 0; i < 44100u; i++) {
        int32_t x = (int32_t)lrint(4.0 * 24000.0 * sin(2 * M_PI * 441.0 * i / 44100.0)), ref;
        o[0] = o[1] = x;
        ref = dc_block(x, &dl, &el);
        clean_block(o, 1u);
        if (o[0] != ref || o[1] != ref)
            exact = 0;
    }
    check("staging: a tone at -2.7 dBFS, just under the knee, comes out as it went in (after the DC blocker), bit for bit",
          exact && cln_g == 32768);
    /* 6 dB over: a 40 Hz note with a 1 kHz tone on it; after the attack the gain stands still */
    rest(1);
    for (i = 0; i < 2u * 44100u; i++) {
        double t = i / 44100.0;
        o[0] = o[1] = (int32_t)lrint(4.0 * (40000.0 * sin(2 * M_PI * 40.0 * t) + 12000.0 * sin(2 * M_PI * 1000.0 * t)));
        clean_block(o, 1u);
        if (i > 44100u) {
            if (cln_g < gmin) gmin = cln_g;
            if (cln_g > gmax) gmax = cln_g;
            if (o[0] > pk) pk = o[0];
        }
    }
    a = 20 * log10((double)gmax / gmin);
    printf("cr_output: Clean limiter under a 40 Hz + 1 kHz pair 6 dB over: gain %.2f dB, moving %.3f dB; peak out %d of %d\n",
           20 * log10(gmax / 32768.0), a, pk, CLN_T4);
    check("staging: 6 dB over the threshold the Clean limiter's gain stands still over the note's period (< 0.02 dB)",
          a < 0.02 && gmax < 32768);
    check("staging: and the peak is held at the threshold or up to 0.3 dB under it (the hold's margin)",
          pk <= CLN_T4 + CLN_T4 / 1000 && pk >= CLN_T4 - CLN_T4 / 30);
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
                if (blk >= 200u && blk < 240u && d > step_sw) step_sw = d;
                prev = o[2 * k];
            }
        }
        printf("cr_output: %s: the chain changed in block +%u, the fade ended in block +%u; largest sample step %d "
               "during the change, %d in the steady tone before and after\n", dir ? "Clean -> Classic" : "Classic -> Clean",
               changed_at - 200u, fade_end - 200u, step_sw, step_steady);
        check(dir ? "switch Clean -> Classic: after 16 blocks, at a silent sample, the fade over after 32"
                  : "switch Classic -> Clean: after 16 blocks, at a silent sample, the fade over after 32",
              changed_at == 200u + OUT_FADE_BLK - 1u && fade_end == 200u + 2u * OUT_FADE_BLK - 1u && silent_ok);
        check(dir ? "switch Clean -> Classic: no sample step above the steady tone's own (+ 5 %)"
                  : "switch Classic -> Clean: no sample step above the steady tone's own (+ 5 %)",
              step_sw <= step_steady + step_steady / 20);
    }
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
    printf("cr_output: CLN_TRIM %d / 4096, CLN_T %d, CLN_K %d, hold %u, release >> %d, make-up %d / 4096; LIM_T %d\n",
           CLN_TRIM, CLN_T, CLN_K, (unsigned)CLN_HOLD, CLN_REL, CLN_MAKEUP, LIM_T);
    t_knee();
    t_gain();
    t_staging();
    t_master();
    t_switch();
    t_classic();
    t_usb();
    printf(bad ? "cr_output_test: %d FAILED\n" : "cr_output_test: all ok\n", bad);
    return bad != 0;
}
