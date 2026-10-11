/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot's delay and reverb (CR_FX 1; docs/FX.md), included by fx.c in the place of Felucca's mono tempo delay and
 * its ROOM / SPRING reverb, which stay as they are for Felucca's units (CR_FX 0: its goldens). Both take the mono
 * send bus of fx.c and add a stereo wet to the mix.
 *
 * THE HALF RATE. Both run at FS / 2 = 22.05 kHz: the two sends go down through a half-band filter (xf_down), the
 * summed stereo wet comes back up through one pair of them (xf_up). The filter is a two-path polyphase all-pass
 * (tools/fx_halfband.py: 2 + 2 first-order sections, one multiply each, -54 dB above 12.5 kHz, flat to 9.5 kHz,
 * -3 dB at 11 kHz). What it buys: a second of delay a side in 88 KB (at 44.1 kHz: 176 KB, more than the pool had
 * left), a reverb of 0.71 s of lines in 41 KB, and half the work for both. What it costs: nothing above ~10 kHz in
 * the wet, which a bucket-brigade echo and a large dark hall do not have either. The dry mix is untouched.
 *
 * THE DELAY (xd_*), in the spirit of the Moog Matriarch's stereo analogue delay: two lines, L and R, XD_MAX samples
 * (1.000 s) each. Each side's read is a four-point cubic (Catmull-Rom) at a position that GLIDES to its target (a
 * lag of ~46 ms, at most half a sample a sample: a new time bends the pitch of what is in the line, as a tape or a
 * BBD clock does, and never clicks) plus the WOW (one slow and one faster sine, the same on both sides so that
 * Spread 0 stays mono). The loop: the read -> a one-pole low-pass (TONE) -> a one-pole low cut (40 Hz) -> the
 * feedback gain -> + the input -> tanh (dsp.c softclip) -> the line: every repeat is darker, thinner in the bass and
 * a little more compressed than the last, and the line cannot clip. STEREO: each side feeds itself. PING-PONG: the
 * input goes into L only, L feeds R and R feeds L. SPREAD is R's time as a part of L's (1 .. 1/2 free; 1:1, 3:4,
 * 2:3, 1:2 synced: a dotted and a triplet relation). SYNC: a division of the tempo (xf_bpm; a division longer than
 * the line plays at half), else milliseconds.
 *
 * THE REVERB (xr_*): a feedback delay network of eight lines (after Jot and Chaigne 1991, and Stautner and Puckette
 * 1982), mixed by an 8 x 8 Hadamard matrix (three stages of butterflies, no multiply), behind a pre-delay and four
 * Schroeder all-pass diffusers (Schroeder 1962). Per line: a linear-interpolated read at a gliding length (SIZE: a
 * quarter to the whole of XR_LEN; four of the eight also swing on two slow sines, MOD: the tail is chorused and its
 * modes never stand still), a one-pole low-pass (DAMP) and the gain that gives every line the same RT60 (DECAY:
 * 0.3 .. 72 s, the top position "Inf": 1 - 2^-13). The input: a two-pole low cut (LOW CUT, 20 .. 500 Hz) in front
 * of the pre-delay, scaled by (1 - g^2)^(1/4) so that a long decay does not come out 20 dB louder than a short one.
 * The output: four lines a side with alternating signs, and WIDTH (mid / side).
 * The bound (tests/cr_fx_test.c): the Hadamard matrix times 181 / 512 (< 1 / sqrt 8) is a contraction, each line's
 * gain is <= 32764 / 32768, a one-pole low-pass and a linear interpolation never exceed 1, and every product is
 * rounded TOWARDS ZERO (the line's own rounding: XR_RB, below): the loop cannot gain, and what is left of a tail
 * falls to exactly 0.
 *
 * NOTHING SOUNDS, NOTHING RUNS. Every product rounds towards zero and every one-pole is written y = x + c (y - x), so
 * a silent input leaves the lines at exactly 0 after the tail; a counter per effect (the samples since the last
 * non-zero one written) then switches its loop off until a send is non-zero again (Felucca's buses ran always).
 *
 * Fixed point: lines int16, states and sums int32, coefficients Q15 (the half-band's Q11), positions Q8 samples.
 * No division and no 64-bit in the per-sample loops; the coefficients are worked out when a parameter or the tempo
 * changes (xf_coefs: a hash of the parameters, compared every block). Memory, all in .pool: 2 x 22114 x 2 (delay)
 * + 16384 x 2 + 4096 x 2 (reverb) + ~740 B of state = 130 KB; Felucca's pair was 143 KB. */

/* ------------------------------------------------------------- the parameters --- */
/* global (the session's FX block, cr_settings.h CSS_FXP_*): a sound carries its sends (P_DLY, P_REV) and nothing here */
enum { XP_DTIME,                 /* the delay's time, free: 0..127 = 21 .. 1000 ms (xf_ms) */
       XP_DDIV,                  /* .. synced: XF_DIV's index */
       XP_DSPREAD,               /* R's time as a part of L's, free: 0..127 = 1 .. 0.5 */
       XP_DRATIO,                /* .. synced: 0 1:1, 1 3:4, 2 2:3, 3 1:2 */
       XP_DFDBK,                 /* feedback 0..127 = 0 .. 0.97 */
       XP_DSYNC,                 /* 0 free (ms), 1 the tempo's divisions */
       XP_DMODE,                 /* 0 stereo, 1 ping-pong */
       XP_DTONE,                 /* the loop's low-pass: 0..127 = ~400 Hz .. ~9 kHz */
       XP_DWOW,                  /* the time's wobble: 0..127 = 0 .. 2 ms */
       XP_RSIZE,                 /* the lines' lengths: 0..127 = 1/4 .. 1 of XR_LEN */
       XP_RDAMP,                 /* the loop's low-pass: 0 open .. 127 ~600 Hz */
       XP_RDECAY,                /* RT60: 0..126 = 0.3 .. 72 s, 127 "Inf" */
       XP_RPRE,                  /* pre-delay: 0..127 = 0 .. 138 ms */
       XP_RMOD,                  /* the modulated lines' swing: 0..127 = 0 .. 0.9 ms */
       XP_RLOW,                  /* the input's low cut: 0..127 = 20 .. 500 Hz */
       XP_RWIDTH,                /* 0 mono .. 127 full */
       XP_N };
static const struct { int16_t min, max, def; } XFP[XP_N] = {
    [XP_DTIME] = {0, 127, 80}, [XP_DDIV] = {0, 12, 6}, [XP_DSPREAD] = {0, 127, 0}, [XP_DRATIO] = {0, 3, 0},
    [XP_DFDBK] = {0, 127, 58}, [XP_DSYNC] = {0, 1, 1}, [XP_DMODE] = {0, 1, 1}, [XP_DTONE] = {0, 127, 84},
    [XP_DWOW] = {0, 127, 24}, [XP_RSIZE] = {0, 127, 88}, [XP_RDAMP] = {0, 127, 56}, [XP_RDECAY] = {0, 127, 52},
    [XP_RPRE] = {0, 127, 16}, [XP_RMOD] = {0, 127, 56}, [XP_RLOW] = {0, 127, 60}, [XP_RWIDTH] = {0, 127, 127}};
static int16_t xfp[XP_N];                 /* the values (xf_defaults at power-on, then the session's) */
static uint16_t xf_bpm = 120;             /* the tempo the synced delay follows (cr_ui.c: the engine's, 20..300) */

/* the synced divisions, in 48ths of a quarter note; cr_ui.c's names in the same order */
#define XF_NDIV 13u
static const uint8_t XF_DIV[XF_NDIV] = {6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96};
static const uint8_t XF_RATIO[4][2] = {{1, 1}, {3, 4}, {2, 3}, {1, 2}};

#define XF_FS2 (FS / 2u)
#define XF_NH (CTL / 2u)                  /* a block at the half rate */
_Static_assert(CTL == 32, "cr_fx.c: a block is 32 samples (the ramps' >> 4)");

static void xf_defaults(void)
{
    uint32_t i;
    for (i = 0; i < XP_N; i++)
        xfp[i] = XFP[i].def;
}

/* base << (p / per), linear within the octave: an exponential knob without a table */
static uint32_t xf_oct(uint32_t base, uint32_t p, uint32_t per)
{
    return ((base << (p / per)) * (per + p % per)) / per;
}
static uint32_t xf_ms(uint32_t p)         /* XP_DTIME -> ms */
{
    uint32_t ms = xf_oct(21, p, 23);
    return ms > 1000u ? 1000u : ms;
}
static uint32_t xf_rt_ms(uint32_t p)      /* XP_RDECAY -> RT60 in ms (127: "Inf", not this) */
{
    return xf_oct(300, p, 16);
}
static uint32_t xf_lowcut_k(uint32_t p)   /* XP_RLOW -> the one-pole's coefficient, Q15 (Hz = k * 0.1071) */
{
    return 187u + p * p * 4482u / 16129u;
}

/* ------------------------------------------------------------------ the state --- */
#define XD_MAX 22050u                     /* the delay's longest time a side, half-rate samples: 1.000 s */
#define XD_N (XD_MAX + 64u)               /* a line: + the wow's swing and the interpolator's points */
#define XD_MIN (48u << 8)                 /* (the shortest read: the wow's swing above the interpolator's 2) */
static int16_t xd_buf[2][XD_N] __attribute__((section(".pool")));

/* the reverb's eight lines at SIZE 127, half-rate samples (57 .. 125 ms; primes, a ratio of ~1.12 from one to the
 * next), in ONE ring: line i writes at w + XR_BASE[i] and reads up to XR_LEN[i] + XR_GUARD behind it */
#define XR_N 16384u
#define XR_M (XR_N - 1u)
#define XR_GUARD 24u                      /* the modulation's swing (<= 20), the interpolator's point, a spare */
static const uint16_t XR_LEN[8] = {1259, 1433, 1607, 1801, 2003, 2221, 2477, 2767};
static const uint16_t XR_BASE[8] = {1283, 2740, 4371, 6196, 8223, 10468, 12969, 15760};
_Static_assert(15760 <= XR_N, "the eight lines and their guards fit the ring");
static int16_t xr_buf[XR_N] __attribute__((section(".pool")));
/* the pre-delay (behind the write index) and the four diffusers (ahead of it), one ring */
#define XR_PN 4096u
#define XR_PM (XR_PN - 1u)
#define XR_PRE_MAX (127u * 24u)           /* 3048 samples: 138 ms */
static const uint16_t XR_AP[4] = {113, 173, 263, 397};          /* 5.1, 7.8, 11.9, 18.0 ms */
static const uint16_t XR_APB[4] = {114, 288, 552, 950};         /* their write offsets */
_Static_assert(950 + XR_PRE_MAX + 2 <= XR_PN, "the pre-delay and the diffusers fit their ring");
static int16_t xr_pbuf[XR_PN] __attribute__((section(".pool")));

typedef struct { int32_t x[4], y[4]; } xf_hb_t;                 /* a half-band filter: each section's last in, out */
static struct {
    /* the coefficients (xf_coefs) */
    uint32_t hash;
    int32_t d_tgt[2], d_fb, d_c, d_wow;   /* delay: the sides' times (Q8), feedback and the tone's c (Q15), wow (Q8) */
    uint8_t d_ping, on;                   /* .. ping-pong; on: the coefficients are set (power-on: no glides) */
    int32_t r_tgt[8], r_g[8];             /* reverb: the lines' lengths (Q8) and gains (Q15) */
    int32_t r_c, r_ig, r_lc, r_pre, r_mod, r_w;   /* damp's c, the input gain, the low cut's c (Q15); pre-delay,
                                                   * swing (Q8); width (Q8) */
    /* the delay */
    uint32_t d_w, d_live;                 /* the write index; half-rate samples until the lines are known silent */
    int32_t d_cur[2], d_wv, d_dw;         /* the gliding times (Q8); the wow now and its step a sample */
    int32_t d_lp[2], d_hp[2];
    int32_t d_fbc, d_cc, d_pm;            /* the feedback and the tone's c in use (eased to d_fb, d_c a block at a
                                           * time); ping-pong's part of the routing, 0 .. 256 (eased a sample at a
                                           * time: a mode change writes no step into the lines) */
    uint32_t d_ph[2];
    /* the reverb */
    uint32_t r_w0, r_pw, r_live;
    int32_t r_cur[8], r_mv[4], r_dm[4];   /* the gliding lengths; the four swings now and their steps */
    int32_t r_lp[8], r_hp[2], r_pcur;
    int32_t r_gc[8], r_igc, r_cc, r_wc;   /* the gains, the input gain, damp's c (eased a block at a time), the width
                                           * (a sample) */
    int32_t r_rb;                         /* the lines' rounding (xr_run): XR_RB, or 0 while the tail is nearly gone */
    uint32_t r_ph[2];
    uint8_t r_glide;
    /* the half rate */
    xf_hb_t dn[2], up[2];
    uint32_t up_live;
    int32_t t[4][XF_NH];                  /* a block's sends at the half rate, and its wet */
} xf __attribute__((section(".pool")));

static inline int32_t xf_tz15(int32_t v) { return (v + ((v >> 31) & 32767)) >> 15; }   /* / 32768 towards 0 */
static inline int32_t xf_tz10(int32_t v) { return (v + ((v >> 31) & 1023)) >> 10; }
static inline int32_t xf_tz11(int32_t v) { return (v + ((v >> 31) & 2047)) >> 11; }
static inline int32_t xf_tz8(int32_t v) { return (v + ((v >> 31) & 255)) >> 8; }
static inline int32_t xf_tz3(int32_t v) { return (v + ((v >> 31) & 7)) >> 3; }
/* a one-pole low-pass, y = x + c (y - x) (c Q15 = 1 - k; |x|, |y| <= 32767 so the product fits): the difference
 * shrinks at every step, so y reaches x exactly and no rounding holds an offset */
static inline int32_t xf_lp(int32_t y, int32_t x, int32_t c) { return x + xf_tz15((y - x) * c); }

/* ------------------------------------------------------------- the half-band --- */
/* Q11 (tools/fx_halfband.py: the search's Q13 values / 4): path 0's two sections, path 1's. They run on the sends
 * and the wet at their full resolution (|x| <= 131068 = 4 x 32767): the second section of path 1 sees at most
 * (1 + 2 a) of that and its difference x - y at most 6.3 x 131068, times 1785 < 2^31. (At the lines' own 16 bits the
 * eight sections' rounding was the path's noise floor: -68 dB under a -12 dB sine; so: -84 dB, the lines') */
static const int16_t XF_HB[4] = {199, 1264, 695, 1785};
static inline int32_t xf_ap(xf_hb_t *h, uint32_t k, int32_t x)
{
    int32_t y = xf_tz11((x - h->y[k]) * XF_HB[k]) + h->x[k];
    h->x[k] = x;
    h->y[k] = y;
    return y;
}
/* n samples of a send -> n / 2 at a quarter of its level (a loud chord's send is ~+-40000, the lines are int16),
 * saturated */
static void xf_down(xf_hb_t *h, const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n; i += 2u) {
        int32_t a = clamp(in[i], -131068, 131068), b = clamp(in[i + 1u], -131068, 131068);
        out[i >> 1] = clamp(xf_tz3(xf_ap(h, 1, xf_ap(h, 0, b)) + xf_ap(h, 3, xf_ap(h, 2, a))), -32767, 32767);
    }
}
/* nh samples of wet (saturated at +-131068) -> 2 nh, added to out */
static void xf_up(xf_hb_t *h, const int32_t *in, int32_t *out, uint32_t nh)
{
    uint32_t j;
    for (j = 0; j < nh; j++) {
        int32_t x = clamp(in[j], -131068, 131068);
        out[2u * j] += xf_ap(h, 1, xf_ap(h, 0, x));
        out[2u * j + 1u] += xf_ap(h, 3, xf_ap(h, 2, x));
    }
}

/* ------------------------------------------------------------ coefficients --- */
/* e^-(y / 65536), Q15: e^-n from a table, the fraction as (1 - u + u^2 / 2 - u^3 / 6)^4 with u a quarter of it */
static int32_t xf_expn(uint32_t y)
{
    static const uint16_t E[8] = {32768, 12055, 4435, 1631, 600, 221, 81, 30};
    uint32_t n = y >> 16, u = (y & 0xFFFFu) >> 2, u2 = (u * u) >> 16, p;
    if (n >= 8u)
        return 0;
    p = 65536u - u + (u2 >> 1) - (u2 * u / 6u >> 16);
    p = (uint32_t)(((uint64_t)p * p) >> 16);
    p = (uint32_t)(((uint64_t)p * p) >> 16);
    p = (p * E[n]) >> 16;
    return p > 32767u ? 32767 : (int32_t)p;
}
static uint32_t xf_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    for (; b; b >>= 2) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
    }
    return r;
}
#define XR_G_INF 32764                    /* DECAY 127: 1 - 2^-13 a pass (an RT60 of an hour; the rounding ends it) */
#define XD_HPC 32397                      /* the delay loop's low cut: c of ~40 Hz at 22.05 kHz */

/* the delay's two times, half-rate samples (the unit tests' table): L, R */
static void xf_delay_times(uint32_t *l, uint32_t *r)
{
    uint32_t t, num = 1, den = 1;
    if (xfp[XP_DSYNC]) {
        uint32_t bpm = xf_bpm < 20u ? 20u : xf_bpm > 300u ? 300u : xf_bpm;
        const uint8_t *q = XF_RATIO[xfp[XP_DRATIO] & 3];
        /* a quarter note is FS2 * 60 / bpm; the division 48ths of it, rounded */
        t = (XF_FS2 * 60u * XF_DIV[(uint32_t)xfp[XP_DDIV] % XF_NDIV] + 24u * bpm) / (48u * bpm);
        while (t > XD_MAX)
            t >>= 1;                       /* longer than the line: at half (1/2 at 100 BPM plays as 1/4) */
        num = q[0];
        den = q[1];
    } else {
        t = xf_ms((uint32_t)xfp[XP_DTIME]) * XF_FS2 / 1000u;
        num = 254u - (uint32_t)xfp[XP_DSPREAD];
        den = 254u;
    }
    *l = t;
    *r = (t * num + den / 2u) / den;
}
/* the lines' lengths at XP_RSIZE, Q8 samples: 64 .. 254 / 256 of XR_LEN */
static uint32_t xr_size_q8(void) { return xf_oct(64, (uint32_t)xfp[XP_RSIZE], 64); }

static __attribute__((noinline)) void xf_coefs(void)   /* (when a parameter moves: its divides out of xf_block) */
{
    uint32_t l, r, i, k, sz = xr_size_q8(), e;
    int32_t gm;
    xf_delay_times(&l, &r);
    xf.d_tgt[0] = (int32_t)(l << 8);
    xf.d_tgt[1] = (int32_t)(r << 8);
    for (i = 0; i < 2u; i++) {
        if (xf.d_tgt[i] < (int32_t)XD_MIN) xf.d_tgt[i] = (int32_t)XD_MIN;
    }
    xf.d_fb = xfp[XP_DFDBK] * 250;
    k = xf_oct(3540, (uint32_t)xfp[XP_DTONE], 41);
    xf.d_c = 32767 - (int32_t)(k > 30500u ? 30500u : k);
    xf.d_wow = xfp[XP_DWOW] * 80;
    xf.d_ping = xfp[XP_DMODE] != 0;

    for (i = 0; i < 8u; i++) {
        int32_t t = (int32_t)((XR_LEN[i] * sz + 128u) & ~255u);   /* whole samples: a line at rest reads one sample,
                                                                    * and loses no treble to the interpolation */
        if (xf.r_tgt[i] != t)
            xf.r_glide = 1;
        xf.r_tgt[i] = t;
        /* g = 10^(-3 L / RT60) = e^-(6.9078 L / RT60), L and RT60 in samples */
        xf.r_g[i] = xfp[XP_RDECAY] >= 127 ? XR_G_INF
                  : xf_expn((uint32_t)(((uint32_t)t >> 8) * 452709u / (xf_rt_ms((uint32_t)xfp[XP_RDECAY]) * 441u / 20u)));
        if (xf.r_g[i] > XR_G_INF) xf.r_g[i] = XR_G_INF;
    }
    k = (uint32_t)xfp[XP_RDAMP];
    k = (32767u >> (k / 47u)) - (32767u >> (k / 47u + 1u)) * (k % 47u) / 47u;
    xf.r_c = 32767 - (int32_t)k;
    gm = xf.r_g[4];
    e = 32768u - (uint32_t)((gm * gm) >> 15);                    /* 1 - g^2, Q15 */
    xf.r_ig = (int32_t)xf_isqrt(xf_isqrt(e << 15) << 15);         /* its fourth root, Q15 */
    if (xf.r_ig > 32767) xf.r_ig = 32767;
    xf.r_lc = 32767 - (int32_t)xf_lowcut_k((uint32_t)xfp[XP_RLOW]);
    xf.r_pre = xfp[XP_RPRE] * 24 * 256;
    xf.r_mod = xfp[XP_RMOD] * 40;
    xf.r_w = (xfp[XP_RWIDTH] * 256 + 63) / 127;
}

/* ------------------------------------------------------------------ the delay --- */
/* a line's sample `d` (Q8, >= 2 << 8) behind the write index w: Catmull-Rom through the four points round it; a
 * whole number of samples reads that sample itself */
static inline int32_t xd_read(const int16_t *b, uint32_t w, uint32_t d)
{
    uint32_t p = w + XD_N - (d >> 8) - 2u;
    int32_t f = (int32_t)(d & 255u), y2, y1, y0, ym, a, c;
    if (p >= XD_N)
        p -= XD_N;
    if (p + 3u < XD_N) {
        y2 = b[p]; y1 = b[p + 1u]; y0 = b[p + 2u]; ym = b[p + 3u];
    } else {
        y2 = b[p];
        y1 = b[p + 1u >= XD_N ? p + 1u - XD_N : p + 1u];
        y0 = b[p + 2u >= XD_N ? p + 2u - XD_N : p + 2u];
        ym = b[p + 3u - XD_N];
    }
    if (!f)
        return y0;
    a = 3 * (y0 - y1) + y2 - ym;
    c = 2 * ym - 5 * y0 + 4 * y1 - y2 + ((a * f) >> 8);
    c = y1 - ym + ((c * f) >> 8);
    return clamp(y0 + ((c * f) >> 9), -32767, 32767);
}
/* the wet's level, Q12 (tests/cr_fx_test.c, against Felucca's delay at its defaults and the same send): in Stereo each
 * side plays a repeat as Felucca's mono delay played it on both (XD_WET / 4 / 4096 = 0.78 of the send, less the loop's filters: Felucca's 0.709); in ping-pong
 * the repeat is on one side only, so 3 dB up (XD_WET + 256 XD_WPP): the same power in the room */
#define XD_WET 12800
#define XD_WPP 21
static __attribute__((noinline)) void xd_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t nh)
{
    uint32_t w = xf.d_w, j, k;
    int32_t act = 0, fb, c, f[2], pt = xf.d_ping ? 256 : 0;
    xf.d_fbc += clamp(xf.d_fb - xf.d_fbc, -512, 512);
    xf.d_cc += clamp(xf.d_c - xf.d_cc, -96, 96);
    fb = xf.d_fbc;
    c = xf.d_cc;
    {   /* the wow, once a block: its target, reached as a ramp */
        int32_t t;
        xf.d_ph[0] += (uint32_t)(((uint64_t)330u << 32) * CTL / (1000u * FS));    /* 0.33 Hz */
        xf.d_ph[1] += (uint32_t)(((uint64_t)3100u << 32) * CTL / (1000u * FS));   /* 3.1 Hz, an eighth as deep */
        t = ((osc_sine(xf.d_ph[0]) + (osc_sine(xf.d_ph[1]) >> 3)) * (xf.d_wow >> 2)) >> 13;
        xf.d_dw = (t - xf.d_wv) >> 4;
        if (!xf.d_dw)
            xf.d_wv = t;
    }
    for (j = 0; j < nh; j++) {
        int32_t x = in[j], wl, wr, pm = xf.d_pm;
        if (pm != pt)
            xf.d_pm = pm += pm < pt ? 1 : -1;
        xf.d_wv += xf.d_dw;
        for (k = 0; k < 2u; k++) {
            int32_t d = xf.d_tgt[k] - xf.d_cur[k], a;
            if (d) {                                    /* the glide: a lag, at most half a sample a sample */
                int32_t s = d >> 10;
                xf.d_cur[k] += s > 128 ? 128 : s < -128 ? -128 : s ? s : 1;
            }
            d = xf.d_cur[k] + xf.d_wv;
            a = xd_read(xd_buf[k], w, (uint32_t)(d < (int32_t)XD_MIN ? (int32_t)XD_MIN : d));
            xf.d_lp[k] = xf_lp(xf.d_lp[k], a, c);
            xf.d_hp[k] = xf_lp(xf.d_hp[k], xf.d_lp[k], XD_HPC);
            f[k] = xf.d_lp[k] - xf.d_hp[k];
        }
        /* stereo (pm 0): each side takes the input and its own return; ping-pong (256): L the input and R's
         * return, R the return of L alone */
        wl = f[0] + xf_tz8((f[1] - f[0]) * pm);
        wr = f[0] + f[1] - wl;
        wl = softclip(x + xf_tz15(wl * fb));
        wr = softclip(x - xf_tz8(x * pm) + xf_tz15(wr * fb));
        xd_buf[0][w] = (int16_t)wl;
        xd_buf[1][w] = (int16_t)wr;
        act |= wl | wr | f[0] | f[1];
        if (++w >= XD_N)
            w = 0;
        pm = XD_WET + pm * XD_WPP;                      /* (65534 x 18176 < 2^31) */
        ol[j] += (f[0] * pm) >> 12;
        orr[j] += (f[1] * pm) >> 12;
    }
    xf.d_w = w;
    if (act)
        xf.d_live = XD_N + 2u * XF_NH;
    else
        xf.d_live = xf.d_live > nh ? xf.d_live - nh : 0u;
    if (!xf.d_live) {                                   /* silent: the filters' last fractions with it */
        xf.d_lp[0] = xf.d_lp[1] = xf.d_hp[0] = xf.d_hp[1] = 0;
        for (k = 0; k < 4u; k++)
            xf.dn[0].x[k] = xf.dn[0].y[k] = 0;
    }
}

/* ----------------------------------------------------------------- the reverb --- */
#define XR_WET 9600                       /* Q12 (tests/cr_fx_test.c: Felucca's ROOM at its defaults, the same send) */
/* THE LINES' ROUNDING. Rounded towards zero, every pass round the loop takes half a unit off every sample: a tail
 * then ends in a straight line (DECAY Inf from a moderate send: silent in 90 s, not "nearly for ever"). Rounded to the
 * nearest, nothing is lost and nothing ends: the rounding's own noise goes round for ever at a few units, and the
 * loop never switches off. So: towards zero less 7/16 of a unit (XR_RB: a sixteenth of a unit lost a pass, an eighth
 * of the straight line's slope; Inf from a moderate send: a quarter of an hour), and plainly towards zero once a
 * whole block's largest sample is under XR_QUIET (the tail's last second: it ends, exactly) */
#define XR_RB 7168
#define XR_QUIET 8
static inline int32_t xr_sat(int32_t v)   /* a knee at 3/4 of the line's range, then its limit */
{
    if (v > 24576)
        v = 24576 + ((v - 24576) >> 2);
    else if (v < -24576)
        v = -24576 - ((-v - 24576) >> 2);
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}
static inline int32_t xr_tap(uint32_t wb, int32_t d)   /* the ring `d` (Q8) behind wb, between two samples */
{
    uint32_t p = wb - ((uint32_t)d >> 8);
    int32_t a = xr_buf[p & XR_M], f = d & 255;
    return f ? a + xf_tz8((xr_buf[(p - 1u) & XR_M] - a) * f) : a;
}
static __attribute__((noinline)) void xr_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t nh)
{
    uint32_t w = xf.r_w0, pw = xf.r_pw, j, i;
    int32_t act = 0, pk = 0, c, lc = xf.r_lc, ig, rb = xf.r_rb;
    for (i = 0; i < 8u; i++)
        xf.r_gc[i] += clamp(xf.r_g[i] - xf.r_gc[i], -96, 96);
    xf.r_igc += clamp(xf.r_ig - xf.r_igc, -512, 512);
    xf.r_cc += clamp(xf.r_c - xf.r_cc, -96, 96);
    ig = xf.r_igc;
    c = xf.r_cc;
    {   /* the four swings, once a block: lines 1 and 5 on one sine and its quarter turn, 3 and 7 on the other */
        xf.r_ph[0] += (uint32_t)(((uint64_t)310u << 32) * CTL / (1000u * FS));    /* 0.31 Hz */
        xf.r_ph[1] += (uint32_t)(((uint64_t)470u << 32) * CTL / (1000u * FS));    /* 0.47 Hz */
        for (i = 0; i < 4u; i++) {
            int32_t t = (osc_sine(xf.r_ph[i >> 1] + ((i & 1u) << 30)) * xf.r_mod) >> 15;
            xf.r_dm[i] = (t - xf.r_mv[i]) >> 4;
            if (!xf.r_dm[i])
                xf.r_mv[i] = t;
        }
    }
    for (j = 0; j < nh; j++) {
        int32_t x = in[j], v[8], r[8], d, l, rr, m, s;
        /* the low cut, two poles */
        xf.r_hp[0] = xf_lp(xf.r_hp[0], x, lc);
        x = clamp(x - xf.r_hp[0], -32767, 32767);
        xf.r_hp[1] = xf_lp(xf.r_hp[1], x, lc);
        x = clamp(x - xf.r_hp[1], -32767, 32767);
        /* the pre-delay: its length glides (a quarter of a sample a sample) */
        xr_pbuf[pw & XR_PM] = (int16_t)x;
        act |= x;
        d = xf.r_pre - xf.r_pcur;
        if (d)
            xf.r_pcur += d > 64 ? 64 : d < -64 ? -64 : d;
        {
            uint32_t p = pw - ((uint32_t)xf.r_pcur >> 8);
            int32_t a = xr_pbuf[p & XR_PM], f = xf.r_pcur & 255;
            x = f ? a + xf_tz8((xr_pbuf[(p - 1u) & XR_PM] - a) * f) : a;
        }
        /* four all-pass diffusers, g = 5 / 8 */
        for (i = 0; i < 4u; i++) {
            int32_t z = xr_pbuf[(pw + XR_APB[i] - XR_AP[i]) & XR_PM], u;
            u = clamp(x - xf_tz3(z * 5), -32767, 32767);
            xr_pbuf[(pw + XR_APB[i]) & XR_PM] = (int16_t)u;
            act |= u;
            x = z + xf_tz3(u * 5);
        }
        x = xf_tz15(clamp(x, -65534, 65534) * ig);
        /* the eight lines */
        if (xf.r_glide) {
            int32_t moving = 0;
            for (i = 0; i < 8u; i++) {
                int32_t st = XR_LEN[i] >> 6;
                d = xf.r_tgt[i] - xf.r_cur[i];
                xf.r_cur[i] += d > st ? st : d < -st ? -st : d;
                moving |= d;
            }
            xf.r_glide = moving != 0;
        }
        for (i = 0; i < 4u; i++) {
            xf.r_mv[i] += xf.r_dm[i];
            r[2u * i] = xr_tap(w + XR_BASE[2u * i], xf.r_cur[2u * i]);
            r[2u * i + 1u] = xr_tap(w + XR_BASE[2u * i + 1u], xf.r_cur[2u * i + 1u] + xf.r_mv[i]);
        }
        for (i = 0; i < 8u; i++) {                      /* (v: 5 more bits than the lines: a pass round the loop */
            xf.r_lp[i] = xf_lp(xf.r_lp[i], r[i], c);    /* rounds once, at the line, below) */
            v[i] = xf_tz10(xf.r_lp[i] * xf.r_gc[i]);
        }
        /* Hadamard: three stages of butterflies, then 181 / 512 (a hair under 1 / sqrt 8) */
        for (i = 0; i < 8u; i += 2u) { s = v[i]; v[i] = s + v[i + 1u]; v[i + 1u] = s - v[i + 1u]; }
        for (i = 0; i < 8u; i += 4u) {
            s = v[i]; v[i] = s + v[i + 2u]; v[i + 2u] = s - v[i + 2u];
            s = v[i + 1u]; v[i + 1u] = s + v[i + 3u]; v[i + 3u] = s - v[i + 3u];
        }
        for (i = 0; i < 4u; i++) { s = v[i]; v[i] = s + v[i + 4u]; v[i + 4u] = s - v[i + 4u]; }
        for (i = 0; i < 8u; i++) {                      /* (8 x 32767 x 32 x 181 < 2^31) */
            s = v[i] * 181;
            s = xr_sat(((s + (s < 0 ? 16383 - rb : rb)) >> 14) + x);   /* towards 0, less rb / 16384 (above) */
            xr_buf[(w + XR_BASE[i]) & XR_M] = (int16_t)s;
            pk |= s ^ (s >> 31);
            act |= xf.r_lp[i];
        }
        w++;
        pw++;
        /* out: two static and two swinging lines a side; width */
        l = r[0] - r[1] + r[4] - r[5];
        rr = r[2] - r[3] + r[6] - r[7];
        if (xf.r_wc != xf.r_w)
            xf.r_wc += xf.r_wc < xf.r_w ? 1 : -1;
        m = (l + rr) >> 1;
        s = (((l - rr) >> 1) * xf.r_wc) >> 8;
        ol[j] += ((m + s) * XR_WET) >> 12;            /* (|m +- s| <= 4 x 32767: fits) */
        orr[j] += ((m - s) * XR_WET) >> 12;
    }
    xf.r_w0 = w;
    xf.r_pw = pw;
    xf.r_rb = pk >= XR_QUIET ? XR_RB : 0;
    if (act | pk)
        xf.r_live = XR_N + 2u * XF_NH;
    else
        xf.r_live = xf.r_live > nh ? xf.r_live - nh : 0u;
    if (!xf.r_live) {
        xf.r_hp[0] = xf.r_hp[1] = 0;
        for (i = 0; i < 8u; i++)
            xf.r_lp[i] = 0;
        for (i = 0; i < 4u; i++)
            xf.dn[1].x[i] = xf.dn[1].y[i] = 0;
    }
}

/* ------------------------------------------------------------------ the block --- */
/* a block of the two sends (the mono buses of fx.c) -> the stereo wet added to ml, mr. n: CTL (even) */
static __attribute__((noinline)) void xf_block(const int32_t *dly_in, const int32_t *rev_in, int32_t *ml,
                                               int32_t *mr, uint32_t n)
{
    uint32_t i, h = xf_bpm, nh = n >> 1;
    int32_t in_d = 0, in_r = 0, any = 0;
    int32_t *hd = xf.t[0], *hr = xf.t[1], *ol = xf.t[2], *orr = xf.t[3];
    for (i = 0; i < XP_N; i++)
        h = h * 31u + (uint16_t)xfp[i];
    if (h != xf.hash || !xf.on) {                       /* a parameter or the tempo moved (or power-on) */
        xf.hash = h;
        xf_coefs();
        if (!xf.on) {                                   /* power-on: everything that glides starts where it is set */
            for (i = 0; i < 8u; i++) {
                xf.r_cur[i] = xf.r_tgt[i];
                xf.r_gc[i] = xf.r_g[i];
            }
            xf.d_cur[0] = xf.d_tgt[0];
            xf.d_cur[1] = xf.d_tgt[1];
            xf.d_fbc = xf.d_fb;
            xf.d_cc = xf.d_c;
            xf.r_cc = xf.r_c;
            xf.d_pm = xf.d_ping ? 256 : 0;
            xf.r_igc = xf.r_ig;
            xf.r_wc = xf.r_w;
            xf.r_pcur = xf.r_pre;
            xf.r_glide = 0;
            xf.on = 1;
        }
    }
    for (i = 0; i < n; i++) {
        in_d |= dly_in[i];
        in_r |= rev_in[i];
    }
    if (!(in_d | in_r) && !xf.d_live && !xf.r_live && !xf.up_live)
        return;                                         /* nothing sent, the tails over: nothing runs */
    for (i = 0; i < nh; i++)
        ol[i] = orr[i] = 0;
    if (in_d || xf.d_live) {
        xf_down(&xf.dn[0], dly_in, hd, n);
        xd_run(hd, ol, orr, nh);
    }
    if (in_r || xf.r_live) {
        xf_down(&xf.dn[1], rev_in, hr, n);
        xr_run(hr, ol, orr, nh);
    }
    for (i = 0; i < nh; i++)
        any |= ol[i] | orr[i];
    if (any)
        xf.up_live = 256u;
    else if (xf.up_live)
        xf.up_live = xf.up_live > nh ? xf.up_live - nh : 0u;
    if (any || xf.up_live) {
        xf_up(&xf.up[0], ol, ml, nh);
        xf_up(&xf.up[1], orr, mr, nh);
    } else {
        for (i = 0; i < 4u; i++)
            xf.up[0].x[i] = xf.up[0].y[i] = xf.up[1].x[i] = xf.up[1].y[i] = 0;
    }
}
