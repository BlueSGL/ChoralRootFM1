/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot's delay and reverb (CR_FX 1; docs/FX.md), included by fx.c in the place of Felucca's mono tempo delay and
 * its ROOM / SPRING reverb, which stay as they are for Felucca's units (CR_FX 0: its goldens). Both take a mono
 * send bus of fx.c and add a stereo wet to the mix.
 *
 * THE RATES (build flags; docs/FX.md "Half rate or full"). CR_FX_DELAY_DS 1: the delay runs at the full 44.1 kHz,
 * straight on the send and into the mix: its repeats are heard beside the dry sound and keep its band. CR_FX_REVERB_DS
 * 2: the reverb runs at 22.05 kHz between a half-band filter down (xf_down) and a pair of them up (xf_up): a
 * two-path polyphase all-pass (tools/fx_halfband.py: 2 + 2 first-order sections, one multiply each, -54 dB above
 * 12.5 kHz, flat to 9.5 kHz). Either can be built the other way (1 or 2) to compare.
 *
 * THE DELAY (xd_*), in the spirit of the Moog Matriarch's stereo analogue delay: two lines, L and R, of XD_N
 * samples each (0.74 s at the full rate). Each side's read is a four-point cubic (Catmull-Rom) at a position that
 * GLIDES to its target (a lag of ~46 ms, at most half a sample a sample: a new time bends the pitch of what is in
 * the line, as a tape or a BBD clock does, and never clicks) plus the WOW (one slow and one faster sine, the same on
 * both sides so that Spread 0 stays mono). The loop: the read -> a one-pole low-pass (TONE) -> a one-pole low cut
 * (40 Hz) -> the feedback gain -> + the input -> a saturator that is a straight line up to half of the line's range
 * and a soft knee above it (xd_sat) -> the line. STEREO: each side feeds itself. PING-PONG: the input goes into L
 * only, L feeds R and R feeds L. SPREAD is R's time as a part of L's (1 .. 1/2 free; 1:1, 3:4, 2:3, 1:2 synced).
 * SYNC: a division of the tempo (xf_bpm; a division longer than the line plays at half), else milliseconds.
 *
 * THE REVERB (xr_*): a feedback delay network of eight lines (after Jot and Chaigne 1991, and Stautner and Puckette
 * 1982), mixed by an 8 x 8 Hadamard matrix (three stages of butterflies, no multiply), behind a pre-delay and four
 * Schroeder all-pass diffusers (Schroeder 1962). Per line: a linear-interpolated read at a gliding length (SIZE: a
 * quarter to the whole of XR_LEN; four of the eight also swing on two slow sines, MOD), a one-pole low-pass (DAMP)
 * and the gain that gives every line the same RT60 (DECAY: 0.3 .. 72 s, the top position "Inf": 1 - 2^-13). The
 * input: a two-pole low cut (LOW CUT, 20 .. 500 Hz) in front of the pre-delay, scaled by (1 - g^2)^(1/4). The
 * output: four lines a side with alternating signs, and WIDTH (mid / side).
 *
 * THE ARITHMETIC (the first build rounded every product towards zero, and the delay's saturator was dsp.c's tanh
 * table, whose first step is 255 / 256: every pass round a loop took a unit or two off every sample, a tail fell in
 * a straight line instead of dying away, and at a short delay time, 47 passes a second, a repeat at -40 dB was gone
 * in half a second whatever the feedback: "the delay just drops out"). Now every product inside a loop KEEPS ITS
 * REMAINDER for the next sample (xf_rs: first-order error feedback, as fx.c's dc_block does), and the saturators are
 * exactly 1 below their knee: nothing is lost but what the feedback and the filters take, a tail decays
 * exponentially down to the last unit of the 16-bit line (-90 dBFS of it) and goes on below that as ever sparser
 * single units until none is left. The loops' gains are below 1 (feedback <= 0.988; the Hadamard matrix times
 * 181 / 512 < 1 / sqrt 8 and each line's gain <= 32764 / 32768), so they cannot grow.
 *
 * WHERE A TAIL ENDS: at one unit of the line. When a whole block has written nothing larger than +-1 (d_q, r_q: -90
 * dBFS of the 16-bit line; with the wet's gain under -77 dBFS at the output at full MASTER, every line of the reverb
 * at once: in practice -84), the loop's products are rounded towards zero for the next block instead of keeping
 * their remainders: the last single units are not sent round again and the lines reach exactly 0 one pass later. A
 * block that writes anything larger is back to the remainders at once. Nothing above one unit is ever cut.
 * NOTHING SOUNDS, NOTHING RUNS. A counter per effect (the samples since the last non-zero one it wrote or held) then
 * switches its loop off until a send is non-zero again: a loop is skipped only when every sample of its lines and
 * every state of its filters is 0.
 *
 * Fixed point: lines int16, states and sums int32, coefficients Q15 (the half-band's Q11), positions Q8 samples.
 * No division, no 64-bit and no call in the per-sample loops (the four XF_HOT functions: with CR_FX_RAM 1 they are
 * placed in RAM, out of the XIP cache's way); the coefficients are worked out when a parameter or the tempo changes
 * (xf_coefs). Memory, all in .pool: 2 x 32768 x 2 (delay) + 16384 x 2 + 4096 x 2 (reverb) + the state. */

#ifndef CR_FX_DELAY_DS
#define CR_FX_DELAY_DS 1                 /* 1: the delay at FS; 2: at FS / 2 (a line then holds 1.49 s) */
#endif
#ifndef CR_FX_REVERB_DS
#define CR_FX_REVERB_DS 2                /* 2: the reverb at FS / 2; 1: at FS (twice the lines' memory and work) */
#endif
#ifndef CR_FX_RAM
#define CR_FX_RAM 0                      /* 1 (device builds only): the four per-sample loops in .ram_text, called
                                          * through a pointer as the flash driver is: no XIP cache miss in them */
#endif
#if (CR_FX_DELAY_DS != 1 && CR_FX_DELAY_DS != 2) || (CR_FX_REVERB_DS != 1 && CR_FX_REVERB_DS != 2)
#error "cr_fx.c: CR_FX_DELAY_DS and CR_FX_REVERB_DS are 1 or 2"
#endif
#define XF_HALF (CR_FX_DELAY_DS == 2 || CR_FX_REVERB_DS == 2)   /* something runs at the half rate */
#define XF_AI static inline __attribute__((always_inline))
#if CR_FX_RAM                            /* (as hal/fm1_flash.h's RAMFN and FL_FAR: XIP and RAM are too far apart for a
                                          * direct call, so through a pointer the compiler cannot fold back) */
#define XF_HOT static __attribute__((section(".ram_text"), noinline, used))
static inline void *xf_far(void *p) { void *volatile q = p; return q; }
#define XF_CALL(fn) ((__typeof__(&fn))xf_far((void *)&fn))
#else
#define XF_HOT static __attribute__((noinline))
#define XF_CALL(fn) fn
#endif

/* ------------------------------------------------------------- the parameters --- */
/* global (the session's FX block, cr_settings.h CSS_FXP_*), as the sends are (cr_ui.c): no sound load touches them */
enum { XP_DTIME,                 /* the delay's time, free: 0..127 = 16 ms .. XD_MS_MAX (xf_ms) */
       XP_DDIV,                  /* .. synced: XF_DIV's index */
       XP_DSPREAD,               /* R's time as a part of L's, free: 0..127 = 1 .. 0.5 */
       XP_DRATIO,                /* .. synced: 0 1:1, 1 3:4, 2 2:3, 3 1:2 */
       XP_DFDBK,                 /* feedback 0..127 = 0 .. 0.988 */
       XP_DSYNC,                 /* 0 free (ms), 1 the tempo's divisions */
       XP_DMODE,                 /* 0 stereo, 1 ping-pong */
       XP_DTONE,                 /* the loop's low-pass: 0..127 = 400 Hz .. 22 kHz (an octave every 22) */
       XP_DWOW,                  /* the time's wobble: 0..127 = 0 .. 2 ms */
       XP_RSIZE,                 /* the lines' lengths: 0..127 = 1/4 .. 1 of XR_LEN */
       XP_RDAMP,                 /* the loop's low-pass: 0 open .. 127 ~660 Hz */
       XP_RDECAY,                /* RT60: 0..126 = 0.3 .. 72 s, 127 "Inf" */
       XP_RPRE,                  /* pre-delay: 0..127 = 0 .. 138 ms */
       XP_RMOD,                  /* the modulated lines' swing: 0..127 = 0 .. 0.9 ms */
       XP_RLOW,                  /* the input's low cut: 0..127 = 20 .. 500 Hz */
       XP_RWIDTH,                /* 0 mono .. 127 full */
       XP_N };
static const struct { int16_t min, max, def; } XFP[XP_N] = {
    [XP_DTIME] = {0, 127, 91}, [XP_DDIV] = {0, 12, 6}, [XP_DSPREAD] = {0, 127, 0}, [XP_DRATIO] = {0, 3, 0},
    [XP_DFDBK] = {0, 127, 58}, [XP_DSYNC] = {0, 1, 1}, [XP_DMODE] = {0, 1, 1}, [XP_DTONE] = {0, 127, 70},
    [XP_DWOW] = {0, 127, 24}, [XP_RSIZE] = {0, 127, 88}, [XP_RDAMP] = {0, 127, 56}, [XP_RDECAY] = {0, 127, 52},
    [XP_RPRE] = {0, 127, 16}, [XP_RMOD] = {0, 127, 56}, [XP_RLOW] = {0, 127, 60}, [XP_RWIDTH] = {0, 127, 127}};
static int16_t xfp[XP_N];                 /* the values (xf_defaults at power-on, then the session's) */
static uint16_t xf_bpm = 120;             /* the tempo the synced delay follows (cr_ui.c: the engine's, 20..300) */

/* the synced divisions, in 48ths of a quarter note; cr_ui.c's names in the same order */
#define XF_NDIV 13u
static const uint8_t XF_DIV[XF_NDIV] = {6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96};
static const uint8_t XF_RATIO[4][2] = {{1, 1}, {3, 4}, {2, 3}, {1, 2}};

_Static_assert(CTL == 32, "cr_fx.c: a block is 32 samples (the ramps' shifts)");
#define XD_FS (FS / CR_FX_DELAY_DS)       /* the delay's rate, its samples a block and their log2 */
#define XD_NB (CTL / CR_FX_DELAY_DS)
#define XD_SH (CR_FX_DELAY_DS == 1 ? 5 : 4)
#define XR_FS (FS / CR_FX_REVERB_DS)
#define XR_NB (CTL / CR_FX_REVERB_DS)
#define XR_SH (CR_FX_REVERB_DS == 1 ? 5 : 4)
#define XR_X (2u / CR_FX_REVERB_DS)       /* the reverb's lengths against the half-rate table below */
#define XF_NH (CTL / 2u)                  /* a block at the half rate */
#define XF_IN 1u                          /* the lines' scale: the send >> 1 (xd_sat's comment) */

#define XD_N 32768u                       /* a delay line, samples (int16): 0.743 s at 44.1 kHz */
#define XD_M (XD_N - 1u)
#define XD_MAX (XD_N - 128u)              /* the longest time: the wow's swing (<= 89 samples) and the interpolator's
                                           * points above it */
#define XD_MS_MAX (XD_MAX * 1000u / XD_FS > 1000u ? 1000u : XD_MAX * 1000u / XD_FS)   /* 740 ms (half rate: 1000) */

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
static uint32_t xf_ms(uint32_t p)         /* XP_DTIME -> ms: 16 .. XD_MS_MAX */
{
    uint32_t ms = xf_oct(16, p, 23);
    return ms > XD_MS_MAX ? XD_MS_MAX : ms;
}
static uint32_t xf_rt_ms(uint32_t p)      /* XP_RDECAY -> RT60 in ms (127: "Inf", not this) */
{
    return xf_oct(300, p, 16);
}
static uint32_t xf_lowcut_hz(uint32_t p)  /* XP_RLOW -> Hz: 20 .. 500 */
{
    return 20u + p * p * 480u / 16129u;
}
static uint32_t xf_pre_ms(uint32_t p)     /* XP_RPRE -> ms: 0 .. 138 (24 samples at 22.05 kHz a step) */
{
    return p * 24000u / 22050u;
}

/* ------------------------------------------------------------------ the state --- */
static int16_t xd_buf[2][XD_N] __attribute__((section(".pool")));

/* the reverb's eight lines at SIZE 127, in samples at 22.05 kHz (57 .. 125 ms; primes, a ratio of ~1.12 from one to
 * the next; x XR_X at the full rate), in ONE ring: line i writes at w + base[i] and reads up to its length and
 * XR_GUARD behind it */
#define XR_N (16384u * XR_X)
#define XR_M (XR_N - 1u)
#define XR_GUARD 24u                      /* the modulation's swing (<= 20), the interpolator's point, a spare */
static const uint16_t XR_LEN[8] = {1259, 1433, 1607, 1801, 2003, 2221, 2477, 2767};
static const uint16_t XR_BASE[8] = {1283, 2740, 4371, 6196, 8223, 10468, 12969, 15760};
_Static_assert(15760 <= 16384, "the eight lines and their guards fit the ring");
static int16_t xr_buf[XR_N] __attribute__((section(".pool")));
/* the pre-delay (behind the write index) and the four diffusers (ahead of it), one ring */
#define XR_PN (4096u * XR_X)
#define XR_PM (XR_PN - 1u)
#define XR_PRE_MAX (127u * 24u)           /* 3048 samples at 22.05 kHz: 138 ms */
static const uint16_t XR_AP[4] = {113, 173, 263, 397};          /* 5.1, 7.8, 11.9, 18.0 ms */
static const uint16_t XR_APB[4] = {114, 288, 552, 950};         /* their write offsets */
_Static_assert(950 + XR_PRE_MAX + 2 <= 4096, "the pre-delay and the diffusers fit their ring");
static int16_t xr_pbuf[XR_PN] __attribute__((section(".pool")));

typedef struct { int32_t x[4], y[4], e[4]; } xf_hb_t;           /* a half-band filter: each section's last in, out
                                                                 * and its product's remainder */
static struct {
    /* the coefficients (xf_coefs) */
    uint32_t hash;
    int32_t d_tgt[2], d_fb, d_k, d_kh, d_wow;   /* delay: the sides' times (Q8), feedback, the tone's and the low
                                                 * cut's k (Q15), the wow's swing (Q8) */
    uint8_t d_ping, on;                   /* .. ping-pong; on: the coefficients are set (power-on: no glides) */
    uint8_t d_q, r_q;                     /* the delay's, the reverb's last block wrote nothing above one unit (above) */
    int32_t r_tgt[8], r_g[8];             /* reverb: the lines' lengths (Q8) and gains (Q15) */
    int32_t r_k, r_ig, r_lk, r_pre, r_mod, r_w;   /* damp's k (Q15), the input gain (Q15), the low cut's k; pre-delay,
                                                   * swing (Q8); width (Q8) */
    uint16_t r_base[8], r_step[8], r_ap[4], r_apb[4];   /* XR_BASE, the glide's step, XR_AP, XR_APB at this rate (in
                                                         * RAM: the loops read no constant from the flash) */
    /* the delay */
    uint32_t d_w, d_live;                 /* the write index; samples until the lines are known silent */
    int32_t d_cur[2], d_wv, d_dw, d_wt;   /* the gliding times (Q8); the wow now, its step a sample, its target */
    int32_t d_lp[2], d_hp[2], d_e[2][3];  /* the filters; the three products' remainders a side */
    int32_t d_fbc, d_kc, d_pm;            /* the feedback and the tone's k in use (eased a block at a time);
                                           * ping-pong's part of the routing, 0 .. 256 (eased a sample at a time: a
                                           * mode change writes no step into the lines) */
    uint32_t d_ph[2];
    /* the reverb */
    uint32_t r_w0, r_pw, r_live;
    int32_t r_cur[8], r_mv[4], r_dm[4], r_mt[4];   /* the gliding lengths; the four swings now, their steps, targets */
    int32_t r_lp[8], r_e1[8], r_e2[8], r_e3[8];   /* the lines' low-passes; their products', the lines' and the gains'
                                                   * remainders */
    int32_t r_hp[2], r_eh[2], r_pcur;
    int32_t r_gc[8], r_igc, r_kc, r_wc;   /* the gains, the input gain, damp's k (eased a block at a time), the width
                                           * (a sample) */
    uint32_t r_ph[2];
    uint8_t r_glide;
    /* the half rate */
    xf_hb_t dn[2], up[2];
    uint32_t up_live;
    int32_t t[4][XF_NH];                  /* a block's sends at the half rate, and its wet */
} xf __attribute__((section(".pool")));

XF_AI int32_t xf_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
XF_AI int32_t xf_tz15(int32_t v) { return (v + ((v >> 31) & 32767)) >> 15; }   /* / 32768 towards 0 (feed-forward) */
XF_AI int32_t xf_tz14(int32_t v) { return (v + ((v >> 31) & 16383)) >> 14; }
XF_AI int32_t xf_tz8(int32_t v) { return (v + ((v >> 31) & 255)) >> 8; }
XF_AI int32_t xf_tz3(int32_t v) { return (v + ((v >> 31) & 7)) >> 3; }
/* v / 2^sh with the remainder kept in *e for the next sample (0 .. 2^sh - 1): nothing is lost over time, and with
 * v at 0 the result is 0 (the remainder stays where it is) */
XF_AI int32_t xf_rs(int32_t v, int32_t *e, uint32_t sh)
{
    v += *e;
    *e = v & (int32_t)((1u << sh) - 1u);
    return v >> sh;
}
/* a one-pole low-pass step, y += k (x - y) with its remainder: y reaches x exactly (|x|, |y| <= 32767 and
 * k <= 32767: 65534 x 32767 + 32767 < 2^31) */
XF_AI int32_t xf_lp(int32_t y, int32_t x, int32_t k, int32_t *e) { return y + xf_rs((x - y) * k, e, 15); }

/* ------------------------------------------------------------- the half-band --- */
/* Q11 (tools/fx_halfband.py: the search's Q13 values / 4): path 0's two sections 199 and 1264, path 1's 695 and
 * 1785. They run on the sends and the wet at their full resolution (|x| <= 131068 = 4 x 32767): the second section
 * of path 1 sees at most (1 + 2 a) of that and its difference x - y at most 6.3 x 131068, times 1785 < 2^31. Each
 * product keeps its remainder (no dead band, no offset) */
XF_AI int32_t xf_ap(xf_hb_t *h, uint32_t k, int32_t a, int32_t x)
{
    int32_t y = xf_rs((x - h->y[k]) * a, &h->e[k], 11) + h->x[k];
    h->x[k] = x;
    h->y[k] = y;
    return y;
}
/* n samples of a send -> n / 2 at half its level (the lines' scale: XF_IN), saturated */
XF_HOT void xf_down(xf_hb_t *h, const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n; i += 2u) {
        int32_t a = xf_clamp(in[i], -131068, 131068), b = xf_clamp(in[i + 1u], -131068, 131068);
        out[i >> 1] = xf_clamp((xf_ap(h, 1, 1264, xf_ap(h, 0, 199, b)) + xf_ap(h, 3, 1785, xf_ap(h, 2, 695, a))) >> (1u + XF_IN),
                               -32767, 32767);
    }
}
/* nh samples of wet (saturated at +-131068) -> 2 nh, added to out */
XF_HOT void xf_up(xf_hb_t *h, const int32_t *in, int32_t *out, uint32_t nh)
{
    uint32_t j;
    for (j = 0; j < nh; j++) {
        int32_t x = xf_clamp(in[j], -131068, 131068);
        out[2u * j] += xf_ap(h, 1, 1264, xf_ap(h, 0, 199, x));
        out[2u * j + 1u] += xf_ap(h, 3, 1785, xf_ap(h, 2, 695, x));
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
/* a one-pole's k (Q15) for a corner of hz at the rate fs: 1 - e^(-2 pi hz / fs) (2 pi x 65536 = 16 x 25736) */
static int32_t xf_k(uint32_t hz, uint32_t fs)
{
    int32_t k = 32768 - xf_expn(hz * 25736u / (fs / 16u));
    return k > 32767 ? 32767 : k < 1 ? 1 : k;
}
#define XR_G_INF 32764                    /* DECAY 127: 1 - 2^-13 a pass: an RT60 of about an hour */

/* the delay's two times in its own samples (the unit tests' table): L, R */
static void xf_delay_times(uint32_t *l, uint32_t *r)
{
    uint32_t t, num = 1, den = 1;
    if (xfp[XP_DSYNC]) {
        uint32_t bpm = xf_bpm < 20u ? 20u : xf_bpm > 300u ? 300u : xf_bpm;
        const uint8_t *q = XF_RATIO[xfp[XP_DRATIO] & 3];
        /* a quarter note is XD_FS * 60 / bpm; the division 48ths of it, rounded */
        t = (XD_FS * 60u * XF_DIV[(uint32_t)xfp[XP_DDIV] % XF_NDIV] + 24u * bpm) / (48u * bpm);
        while (t > XD_MAX)
            t >>= 1;                       /* longer than the line: at half (1/4 at 60 BPM plays as 1/8) */
        num = q[0];
        den = q[1];
    } else {
        t = xf_ms((uint32_t)xfp[XP_DTIME]) * XD_FS / 1000u;
        num = 254u - (uint32_t)xfp[XP_DSPREAD];
        den = 254u;
    }
    *l = t;
    *r = (t * num + den / 2u) / den;
}
/* the lines' lengths at XP_RSIZE, Q8: 64 .. 254 / 256 of XR_LEN */
static uint32_t xr_size_q8(void) { return xf_oct(64, (uint32_t)xfp[XP_RSIZE], 64); }

static __attribute__((noinline)) void xf_coefs(void)   /* (when a parameter moves: its divides out of xf_block) */
{
    uint32_t l, r, i, k, sz = xr_size_q8(), e;
    int32_t gm;
    xf_delay_times(&l, &r);
    xf.d_tgt[0] = (int32_t)(l << 8);
    xf.d_tgt[1] = (int32_t)(r << 8);
    xf.d_fb = xfp[XP_DFDBK] * 255;
    xf.d_k = xf_k(xf_oct(400, (uint32_t)xfp[XP_DTONE], 22), XD_FS);
    xf.d_kh = xf_k(40, XD_FS);
    xf.d_wow = xfp[XP_DWOW] * (int32_t)(XD_FS * 2u * 256u / 1000u / 127u);   /* 0 .. 2 ms, Q8 samples */
    xf.d_ping = xfp[XP_DMODE] != 0;

    for (i = 0; i < 8u; i++) {
        int32_t t = (int32_t)((XR_LEN[i] * XR_X * sz + 128u) & ~255u);   /* whole samples: a line at rest reads one
                                                                           * sample, and loses no treble to the
                                                                           * interpolation */
        if (xf.r_tgt[i] != t)
            xf.r_glide = 1;
        xf.r_tgt[i] = t;
        /* g = 10^(-3 L / RT60) = e^-(6.9078 L / RT60), L and RT60 in samples (6.9078 x 65536 = 452709) */
        xf.r_g[i] = xfp[XP_RDECAY] >= 127 ? XR_G_INF
                  : xf_expn((uint32_t)(((uint32_t)t >> 8) * (452709u / XR_X) /
                                       (xf_rt_ms((uint32_t)xfp[XP_RDECAY]) * (XR_FS / 50u) / (20u * XR_X))));
        if (xf.r_g[i] > XR_G_INF) xf.r_g[i] = XR_G_INF;
        xf.r_base[i] = (uint16_t)(XR_BASE[i] * XR_X);
        xf.r_step[i] = (uint16_t)(XR_LEN[i] >> 6);
    }
    for (i = 0; i < 4u; i++) {
        xf.r_ap[i] = (uint16_t)(XR_AP[i] * XR_X);
        xf.r_apb[i] = (uint16_t)(XR_APB[i] * XR_X);
    }
    k = (uint32_t)xfp[XP_RDAMP];           /* the corner: 12 kHz, an octave down every 30, to 660 Hz; 0: open. At
                                            * DECAY Inf the damping is out of the loop: a one-pole takes a little of
                                            * everything at every pass (at 3.4 kHz: 1 kHz is gone in 15 s), and Inf
                                            * holds what is in it */
    xf.r_k = k && xfp[XP_RDECAY] < 127 ? xf_k((12000u >> (k / 30u)) - (12000u >> (k / 30u + 1u)) * (k % 30u) / 30u, XR_FS) : 32767;
    gm = xf.r_g[4];
    e = 32768u - (uint32_t)((gm * gm) >> 15);                    /* 1 - g^2, Q15 */
    xf.r_ig = (int32_t)xf_isqrt(xf_isqrt(e << 15) << 15);         /* its fourth root, Q15 */
    if (xf.r_ig > 32767) xf.r_ig = 32767;
    xf.r_lk = xf_k(xf_lowcut_hz((uint32_t)xfp[XP_RLOW]), XR_FS);
    xf.r_pre = xfp[XP_RPRE] * 24 * 256 * (int32_t)XR_X;
    xf.r_mod = xfp[XP_RMOD] * 40 * (int32_t)XR_X;
    xf.r_w = (xfp[XP_RWIDTH] * 256 + 63) / 127;
}

/* ------------------------------------------------------------------ the delay --- */
/* a line's sample `d` (Q8, >= 2 << 8) behind the write index w: Catmull-Rom through the four points round it; a
 * whole number of samples reads that sample itself */
XF_AI int32_t xd_read(const int16_t *b, uint32_t w, uint32_t d)
{
    uint32_t p = w - (d >> 8) - 2u;
    int32_t f = (int32_t)(d & 255u), y2, y1, y0, ym, a, c;
    y0 = b[(p + 2u) & XD_M];
    if (!f)
        return y0;
    y2 = b[p & XD_M];
    y1 = b[(p + 1u) & XD_M];
    ym = b[(p + 3u) & XD_M];
    a = 3 * (y0 - y1) + y2 - ym;
    c = 2 * ym - 5 * y0 + 4 * y1 - y2 + ((a * f) >> 8);
    c = y1 - ym + ((c * f) >> 8);
    return xf_clamp(y0 + ((c * f) >> 9), -32767, 32767);
}
/* THE LINES' SCALE: half the send (>> XF_IN). A loud chord's send is ~+-40000 (the limiter's threshold is 18000): 20000
 * in the line, a little into the delay's knee, as Felucca's delay took it; a quarter (the first build) left the wet 12 dB
 * of make-up gain and the 16-bit line's noise that much nearer: -85 dBFS at the output, so: -91 */
/* the line's saturator: itself up to +-16384 (half of the line's range),
 * then a parabola whose slope falls from 1 to 0 at +-49150 in, +-32767 out: no edge, and nothing lost below it */
XF_AI int32_t xd_sat(int32_t v)
{
    int32_t a = v < 0 ? -v : v;
    if (a <= 16384)
        return v;
    a = a >= 49150 ? 32767 : a - (int32_t)(((uint32_t)(a - 16384) * (uint32_t)(a - 16384)) >> 16);
    if (a > 32767)
        a = 32767;
    return v < 0 ? -a : a;
}
/* the wet's level, Q12 (tests/cr_fx_test.c, against Felucca's delay at its defaults and the same send): in Stereo each
 * side plays a repeat as Felucca's mono delay played it on both; in ping-pong the repeat is on one side only, so
 * 3 dB up (XD_WET + 256 XD_WPP): the same power in the room */
#define XD_WET 6120
#define XD_WPP 10
/* n samples of the send (>> sh: XF_IN, or 0 behind the down-sampler) -> the stereo wet, added to ol, orr */
XF_HOT void xd_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t n, uint32_t sh)
{
    uint32_t w = xf.d_w, j, k;
    int32_t act = 0, pk = 0, fb, kc, kh = xf.d_kh, f[2], pt = xf.d_ping ? 256 : 0, q = xf.d_q;
    xf.d_fbc += xf_clamp(xf.d_fb - xf.d_fbc, -512, 512);
    xf.d_kc += xf_clamp(xf.d_k - xf.d_kc, -96, 96);
    fb = xf.d_fbc;
    kc = xf.d_kc;
    xf.d_dw = (xf.d_wt - xf.d_wv) >> XD_SH;             /* the wow: to this block's target as a ramp */
    if (!xf.d_dw)
        xf.d_wv = xf.d_wt;
    for (j = 0; j < n; j++) {
        int32_t x = xf_clamp(in[j] >> sh, -32767, 32767), wl, wr, pm = xf.d_pm;
        if (pm != pt)
            xf.d_pm = pm += pm < pt ? 1 : -1;
        xf.d_wv += xf.d_dw;
        for (k = 0; k < 2u; k++) {
            int32_t d = xf.d_tgt[k] - xf.d_cur[k], a;
            if (d) {                                    /* the glide: a lag, at most half a sample a sample */
                int32_t s = d >> (6 + XD_SH);        /* (a lag of 2048 samples at 44.1 kHz: 46 ms) */
                xf.d_cur[k] += s > 128 ? 128 : s < -128 ? -128 : s ? s : 1;
            }
            d = xf.d_cur[k] + xf.d_wv;
            a = xd_read(xd_buf[k], w, (uint32_t)(d < 1024 ? 1024 : d));
            xf.d_lp[k] = xf_lp(xf.d_lp[k], a, kc, &xf.d_e[k][0]);
            xf.d_hp[k] = xf_lp(xf.d_hp[k], xf.d_lp[k], kh, &xf.d_e[k][1]);
            f[k] = xf.d_lp[k] - xf.d_hp[k];
        }
        /* stereo (pm 0): each side takes the input and its own return; ping-pong (256): L the input and R's
         * return, R the return of L alone */
        wl = f[0] + xf_tz8((f[1] - f[0]) * pm);
        wr = f[0] + f[1] - wl;
        wl = xd_sat(x + (q ? xf_tz15(wl * fb) : xf_rs(wl * fb, &xf.d_e[0][2], 15)));
        wr = xd_sat(x - xf_tz8(x * pm) + (q ? xf_tz15(wr * fb) : xf_rs(wr * fb, &xf.d_e[1][2], 15)));
        xd_buf[0][w & XD_M] = (int16_t)wl;
        xd_buf[1][w & XD_M] = (int16_t)wr;
        pk |= (wl ^ (wl >> 31)) | (wr ^ (wr >> 31));
        act |= wl | wr | xf.d_lp[0] | xf.d_lp[1] | xf.d_hp[0] | xf.d_hp[1];
        w++;
        pm = XD_WET + pm * XD_WPP;                      /* (65534 x 8680 < 2^31) */
        ol[j] += (f[0] * pm) >> 12;
        orr[j] += (f[1] * pm) >> 12;
    }
    xf.d_w = w;
    xf.d_q = pk < 2;
    if (act)
        xf.d_live = XD_N + 2u * XD_NB;
    else
        xf.d_live = xf.d_live > n ? xf.d_live - n : 0u;
}

/* ----------------------------------------------------------------- the reverb --- */
#define XR_WET 4800                       /* Q12 (tests/cr_fx_test.c: Felucca's ROOM at its defaults, the same send) */
XF_AI int32_t xr_sat(int32_t v)           /* itself up to 3/4 of the line's range, a knee above, then its limit */
{
    if (v > 24576)
        v = 24576 + ((v - 24576) >> 2);
    else if (v < -24576)
        v = -24576 - ((-v - 24576) >> 2);
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}
XF_AI int32_t xr_tap(uint32_t wb, int32_t d)   /* the ring `d` (Q8) behind wb, between two samples */
{
    uint32_t p = wb - ((uint32_t)d >> 8);
    int32_t a = xr_buf[p & XR_M], f = d & 255;
    return f ? a + xf_tz8((xr_buf[(p - 1u) & XR_M] - a) * f) : a;
}
XF_HOT void xr_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t n, uint32_t sh)
{
    uint32_t w = xf.r_w0, pw = xf.r_pw, j, i;
    int32_t act = 0, pk = 0, kc, lk = xf.r_lk, ig, q = xf.r_q;
    for (i = 0; i < 8u; i++)
        xf.r_gc[i] += xf_clamp(xf.r_g[i] - xf.r_gc[i], -96, 96);
    xf.r_igc += xf_clamp(xf.r_ig - xf.r_igc, -512, 512);
    xf.r_kc += xf_clamp(xf.r_k - xf.r_kc, -96, 96);
    ig = xf.r_igc;
    kc = xf.r_kc;
    for (i = 0; i < 4u; i++) {                          /* the four swings: to this block's targets as ramps */
        xf.r_dm[i] = (xf.r_mt[i] - xf.r_mv[i]) >> XR_SH;
        if (!xf.r_dm[i])
            xf.r_mv[i] = xf.r_mt[i];
    }
    for (j = 0; j < n; j++) {
        int32_t x = xf_clamp(in[j] >> sh, -32767, 32767), v[8], r[8], d, l, rr, m, s;
        /* the low cut, two poles */
        xf.r_hp[0] = xf_lp(xf.r_hp[0], x, lk, &xf.r_eh[0]);
        x = xf_clamp(x - xf.r_hp[0], -32767, 32767);
        xf.r_hp[1] = xf_lp(xf.r_hp[1], x, lk, &xf.r_eh[1]);
        x = xf_clamp(x - xf.r_hp[1], -32767, 32767);
        act |= xf.r_hp[0] | xf.r_hp[1];
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
            int32_t z = xr_pbuf[(pw + xf.r_apb[i] - xf.r_ap[i]) & XR_PM], u;
            u = xf_clamp(x - xf_tz3(z * 5), -32767, 32767);
            xr_pbuf[(pw + xf.r_apb[i]) & XR_PM] = (int16_t)u;
            act |= u;
            x = z + xf_tz3(u * 5);
        }
        x = xf_tz15(xf_clamp(x, -65534, 65534) * ig);
        /* the eight lines */
        if (xf.r_glide) {
            int32_t moving = 0;
            for (i = 0; i < 8u; i++) {
                int32_t st = xf.r_step[i];
                d = xf.r_tgt[i] - xf.r_cur[i];
                xf.r_cur[i] += d > st ? st : d < -st ? -st : d;
                moving |= d;
            }
            xf.r_glide = moving != 0;
        }
        for (i = 0; i < 4u; i++) {
            xf.r_mv[i] += xf.r_dm[i];
            r[2u * i] = xr_tap(w + xf.r_base[2u * i], xf.r_cur[2u * i]);
            r[2u * i + 1u] = xr_tap(w + xf.r_base[2u * i + 1u], xf.r_cur[2u * i + 1u] + xf.r_mv[i]);
        }
        for (i = 0; i < 8u; i++) {                      /* (v: 5 more bits than the lines: a pass round the loop */
            xf.r_lp[i] = xf_lp(xf.r_lp[i], r[i], kc, &xf.r_e1[i]);   /* rounds once, at the line, below) */
            v[i] = xf_rs(xf.r_lp[i] * xf.r_gc[i], &xf.r_e3[i], 10);
        }
        /* Hadamard: three stages of butterflies, then 181 / 512 (a hair under 1 / sqrt 8) */
        for (i = 0; i < 8u; i += 2u) { s = v[i]; v[i] = s + v[i + 1u]; v[i + 1u] = s - v[i + 1u]; }
        for (i = 0; i < 8u; i += 4u) {
            s = v[i]; v[i] = s + v[i + 2u]; v[i + 2u] = s - v[i + 2u];
            s = v[i + 1u]; v[i + 1u] = s + v[i + 3u]; v[i + 3u] = s - v[i + 3u];
        }
        for (i = 0; i < 4u; i++) { s = v[i]; v[i] = s + v[i + 4u]; v[i + 4u] = s - v[i + 4u]; }
        if (q) {                                        /* (the last units: not sent round again) */
            for (i = 0; i < 8u; i++) {
                s = xr_sat(xf_tz14(v[i] * 181) + x);
                xr_buf[(w + xf.r_base[i]) & XR_M] = (int16_t)s;
                pk |= s ^ (s >> 31);
            }
        } else {
            for (i = 0; i < 8u; i++) {                  /* (8 x 32767 x 32 x 181 + 16383 < 2^31) */
                s = xr_sat(xf_rs(v[i] * 181, &xf.r_e2[i], 14) + x);
                xr_buf[(w + xf.r_base[i]) & XR_M] = (int16_t)s;
                pk |= s ^ (s >> 31);
            }
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
        ol[j] += ((m + s) * XR_WET) >> 12;              /* (|m +- s| <= 4 x 32767: fits) */
        orr[j] += ((m - s) * XR_WET) >> 12;
    }
    xf.r_w0 = w;
    xf.r_pw = pw;
    xf.r_q = pk < 2;
    for (i = 0; i < 8u; i++)                            /* (the lines' low-passes: still holding something?) */
        act |= xf.r_lp[i];
    if (act | pk)
        xf.r_live = XR_N + 2u * XR_NB;
    else
        xf.r_live = xf.r_live > n ? xf.r_live - n : 0u;
}

/* ------------------------------------------------------------------ the block --- */
static void xf_hb_clear(xf_hb_t *h)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        h->x[i] = h->y[i] = 0;
}
/* a block of the two sends (the mono buses of fx.c) -> the stereo wet added to ml, mr. n: CTL */
static __attribute__((noinline)) void xf_block(const int32_t *dly_in, const int32_t *rev_in, int32_t *ml,
                                               int32_t *mr, uint32_t n)
{
    uint32_t i, h = xf_bpm, nh = n >> 1;
    int32_t in_d = 0, in_r = 0, any = 0;
    int32_t *hd = xf.t[0], *hr = xf.t[1], *ol = xf.t[2], *orr = xf.t[3];
    int dly, rev;
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
            xf.d_kc = xf.d_k;
            xf.r_kc = xf.r_k;
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
    dly = in_d || xf.d_live;
    rev = in_r || xf.r_live;
    if (!dly && !rev && !xf.up_live)
        return;                                         /* nothing sent, the tails over: nothing runs */
#if XF_HALF
    for (i = 0; i < nh; i++)
        ol[i] = orr[i] = 0;
#endif
    CR_PROFX_START();                                   /* (cr_cpu1.h: the stage's parts, for 73 DEBUG and perf.sh) */
    if (dly) {
        /* the wow, once a block: a 0.33 Hz sine and an eighth as much of a 3.1 Hz one */
        xf.d_ph[0] += (uint32_t)(((uint64_t)330u << 32) * CTL / (1000u * FS));
        xf.d_ph[1] += (uint32_t)(((uint64_t)3100u << 32) * CTL / (1000u * FS));
        xf.d_wt = ((osc_sine(xf.d_ph[0]) + (osc_sine(xf.d_ph[1]) >> 3)) * (xf.d_wow >> 3)) >> 12;
#if CR_FX_DELAY_DS == 2
        XF_CALL(xf_down)(&xf.dn[0], dly_in, hd, n);
        CR_PROFX(CRX_DOWN);
        XF_CALL(xd_run)(hd, ol, orr, nh, 0);
#else
        XF_CALL(xd_run)(dly_in, ml, mr, n, XF_IN);
#endif
        CR_PROFX(CRX_DELAY);
        if (!xf.d_live) {                               /* silent: the half-band's last fractions with it */
            xf_hb_clear(&xf.dn[0]);
            xf.d_lp[0] = xf.d_lp[1] = xf.d_hp[0] = xf.d_hp[1] = 0;
        }
    }
    if (rev) {
        /* the four swings, once a block: lines 1 and 5 on a 0.31 Hz sine and its quarter turn, 3 and 7 on a 0.47 Hz one */
        xf.r_ph[0] += (uint32_t)(((uint64_t)310u << 32) * CTL / (1000u * FS));
        xf.r_ph[1] += (uint32_t)(((uint64_t)470u << 32) * CTL / (1000u * FS));
        for (i = 0; i < 4u; i++)
            xf.r_mt[i] = (osc_sine(xf.r_ph[i >> 1] + ((i & 1u) << 30)) * xf.r_mod) >> 15;
#if CR_FX_REVERB_DS == 2
        XF_CALL(xf_down)(&xf.dn[1], rev_in, hr, n);
        CR_PROFX(CRX_DOWN);
        XF_CALL(xr_run)(hr, ol, orr, nh, 0);
#else
        XF_CALL(xr_run)(rev_in, ml, mr, n, XF_IN);
#endif
        CR_PROFX(CRX_REVERB);
        if (!xf.r_live)
            xf_hb_clear(&xf.dn[1]);
    }
#if XF_HALF
    for (i = 0; i < nh; i++)
        any |= ol[i] | orr[i];
    if (any)
        xf.up_live = 256u;
    else if (xf.up_live)
        xf.up_live = xf.up_live > nh ? xf.up_live - nh : 0u;
    if (any || xf.up_live) {
        XF_CALL(xf_up)(&xf.up[0], ol, ml, nh);
        XF_CALL(xf_up)(&xf.up[1], orr, mr, nh);
        CR_PROFX(CRX_UP);
    } else {
        xf_hb_clear(&xf.up[0]);
        xf_hb_clear(&xf.up[1]);
    }
#else
    (void)any; (void)hd; (void)hr; (void)ol; (void)orr; (void)nh;
#endif
}
