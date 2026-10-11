/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb). Mono buses, stereo dry mix. */
#ifndef CR_FX
#define CR_FX 0                  /* 1 (choralroot.c, the emulator): the delay and the reverb are ChoralRoot's (cr_fx.c,
                                  * docs/FX.md: a stereo delay and a large reverb at half the rate, their parameters
                                  * global); 0: Felucca's below, as its golden renders have them */
#endif
#ifndef FELUCCA_PERFORM
#define FELUCCA_PERFORM 1        /* 0 (choralroot.c, the emulator): no FX hold layer (perform.c). Its keys are seq.c's
                                  * (keyboard_block: perf_press) and its knobs, solo and kill ui_layer.c's and
                                  * ui_input.c's, none of them in a unit without Felucca's sequencer and UI
                                  * (FELUCCA_SEQ 0): there perf_begin could only ever return 0 and pf.mute stay 0, so
                                  * the mix is the same without the calls, and perform.c leaves the image */
#endif
#if !FELUCCA_PERFORM && defined(FELUCCA_SEQ) && FELUCCA_SEQ
#error "FELUCCA_PERFORM 0 needs FELUCCA_SEQ 0 (seq.c and ui_layer.c drive perform.c)"
#endif
#define CHO_LEN 2048u
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
#if !CR_FX
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
#endif
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
    uint16_t dly_dl, dly_old, dly_xf;    /* DELAY: the tap in use, the one it fades from, samples left of the fade */
} fx;
#define DLY_XF 512u                      /* fx_smooth: a new delay time (TIME, the tempo, a MIDI clock) crossfades in
                                          * 11.6 ms: the read index jumping was a click on every tempo step while it rang */

/* SPRING (G_RTYPE 1): one spring of a spring tank, mono like the other buses, in the ROOM's own buffers (no
 * RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass) ->
 * SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#if !CR_FX
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");
#endif

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain (straight into tanh over the full band, it would sound like a
 * broken digital fuzz). State per part (track_t dist_*). */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* ChoralRoot (cr_out.c sets it; 0 in Felucca, its golden renders and reverb_test's bit-exact buses): the delay's
 * new times crossfade (DLY_XF), and the limiter is smoothed. A 6-note chord with the bass keeps the limiter
 * working (pre-limiter peaks ~2x LIM_T on ANALOG / WHEEL sounds, 60 % of the samples limited), and its 4-sample
 * attack re-attacking on every new peak of the beating chord modulated the gain at audio rate: -49 dB of
 * sidebands above 1 kHz, a crackle on held chords, louder with the FX sends. Smoothed (lim_g): -57 dB, the same
 * loudness; the peaks the slower gain lets through (to ~25000) go into the soft clipper's near-linear part */
static uint8_t fx_smooth;
#if CR_FX
static volatile uint8_t fx_off;       /* ChoralRoot's FX tap at off (cr_ui.c cu_fx_apply): no part sends into the buses */
static int32_t fx_sg[NPART][3];       /* each part's send gains as the last block left them (Q15: chorus, delay, reverb):
                                       * a new send, FX on / off, a CC is reached as a ramp over the block, not as a step
                                       * in what the delay and the reverb are fed (a click at every detent of Send) */
#endif
static int32_t lim_g = 32768;
/* Options > USB Level = Fixed (Felucca 1.0.5's MENU > USB LEVEL FIXED, #42: record over USB with the speaker turned
 * down): the mix goes to master_out at the full MASTER level, USB audio's master pair takes that (choralroot.c's
 * mix_block shim, usb_audio_stream.c), and only then does MASTER scale what the DAC gets (usb_fixed_dac, audio.c).
 * 0 (Master, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after the USB tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    for (i = 0; i < 2u * n; i++)
        out[i] = (out[i] * m) >> 12;                /* (|out| <= 32767 after the soft clip: fits) */
}
static volatile uint8_t fx_lowcut;     /* settings: 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the small speaker):
                                        * 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset of up
 * to +-31 at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+: what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is clipped at its
 * own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and added.
 * The low-pass has 4 poles (Felucca 1.0.5, #42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself,
 * late, and cancelled up to 6 dB of it; now under 0.5 dB) */
static int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;
enum { SB_LP1, SB_LP2, SB_LP3, SB_LP4, SB_ENV, SB_H1, SB_H2, SB_HL, SB_N };   /* (the states above, in a block's copy) */
static inline int32_t spk_bass(int32_t *sb, int32_t m)
{
    int32_t a, t, u;
    sb[SB_LP1] += ((m - sb[SB_LP1]) * 692) >> 15;
    sb[SB_LP2] += ((sb[SB_LP1] - sb[SB_LP2]) * 692) >> 15;
    sb[SB_LP3] += ((sb[SB_LP2] - sb[SB_LP3]) * 692) >> 15;
    sb[SB_LP4] += ((sb[SB_LP3] - sb[SB_LP4]) * 692) >> 15;
    a = sb[SB_LP4] < 0 ? -sb[SB_LP4] : sb[SB_LP4];
    if (a > sb[SB_ENV])
        sb[SB_ENV] += (a - sb[SB_ENV]) >> 2;
    else if (sb[SB_ENV] > 0)
        sb[SB_ENV] -= (sb[SB_ENV] >> 11) + 1;
    t = clamp(sb[SB_LP4] * 8, -sb[SB_ENV], sb[SB_ENV]);
    sb[SB_H1] += (t - sb[SB_H1]) >> 5;
    u = t - sb[SB_H1];
    sb[SB_H2] += (u - sb[SB_H2]) >> 5;
    u -= sb[SB_H2];
    sb[SB_HL] += (u - sb[SB_HL]) >> 3;
    return sb[SB_HL] * 3;
}

/* The master over a block, in place on its frames (o: left, right, ...): the DC blocker, LOWCUT / BASS+, the limiter,
 * the soft clip. The states are the globals above, held in locals over the block (a call a sample loaded and stored
 * them every sample); lowcut and smooth are constants where a loop is built for them (master_block), so a sample has
 * no test of the settings. */
static inline __attribute__((always_inline)) void master_loop(int32_t *o, uint32_t n, uint32_t lowcut, int smooth)
{
    int32_t dl = dc_l, el = dce_l, dr = dc_r, er = dce_r, env = lim_env, lg = lim_g;
    int32_t l1 = lc_l1, l2 = lc_l2, r1 = lc_r1, r2 = lc_r2, e0 = lce[0], e1 = lce[1], e2 = lce[2], e3 = lce[3];
    int32_t sb[SB_N] = {sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl};
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t l = o[2u * i], r = o[2u * i + 1u], al, ar, a;
        l = dc_block(l, &dl, &el);
        r = dc_block(r, &dr, &er);
        if (lowcut) {                 /* two one-pole high-passes, error feedback as dc_block (the */
            uint32_t sh = lowcut == 2u ? 5u : 6u;       /* rounded step stopped at |x - lc| < 32: an offset) */
            int32_t b = lowcut == 2u ? spk_bass(sb, (l + r) >> 1) : 0;
            l = lowcut1(l, &l1, &e0, sh);
            l = lowcut1(l, &l2, &e1, sh) + b;
            r = lowcut1(r, &r1, &e2, sh);
            r = lowcut1(r, &r2, &e3, sh) + b;
        }
        al = l < 0 ? -l : l;
        ar = r < 0 ? -r : r;
        a = al > ar ? al : ar;
        if (smooth) {                /* ChoralRoot: the peak held at once, the gain eased down over ~1.5 ms */
            int32_t gt;
            if (a > env)
                env = a;
            else if (env > LIM_T)
                env -= ((env - LIM_T) >> 12) + 1;
            gt = env > LIM_T ? (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)env) : 32768;
            if (gt < lg)
                lg += (gt - lg) >> 6;
            else
                lg = gt;
            if (lg < 32768) {
                l = ((l >> 4) * lg) >> 11;
                r = ((r >> 4) * lg) >> 11;
            }
        } else {
            if (a > env)
                env += (a - env) >> 2;
            else if (env > LIM_T)
                env -= ((env - LIM_T) >> 12) + 1;
            if (env > LIM_T) {
                int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)env);   /* < 32768 */
                l = ((l >> 4) * g) >> 11;                    /* >> 4 first: |l| may be far above Q15 */
                r = ((r >> 4) * g) >> 11;
            }
        }
        o[2u * i] = softclip(l);
        o[2u * i + 1u] = softclip(r);
    }
    dc_l = dl;
    dce_l = el;
    dc_r = dr;
    dce_r = er;
    lim_env = env;
    lim_g = lg;
    if (lowcut) {
        lc_l1 = l1;
        lc_l2 = l2;
        lc_r1 = r1;
        lc_r2 = r2;
        lce[0] = e0;
        lce[1] = e1;
        lce[2] = e2;
        lce[3] = e3;
    }
    if (lowcut == 2u) {
        sb_lp1 = sb[SB_LP1];
        sb_lp2 = sb[SB_LP2];
        sb_lp3 = sb[SB_LP3];
        sb_lp4 = sb[SB_LP4];
        sb_env = sb[SB_ENV];
        sb_h1 = sb[SB_H1];
        sb_h2 = sb[SB_H2];
        sb_hl = sb[SB_HL];
    }
}
static __attribute__((noinline)) void master_block(int32_t *o, uint32_t n)
{
    uint32_t lowcut = fx_lowcut;                        /* (the main loop's: it does not run under this block) */
    if (fx_smooth) {
        if (!lowcut)
            master_loop(o, n, 0u, 1);
        else if (lowcut == 2u)
            master_loop(o, n, 2u, 1);
        else
            master_loop(o, n, 1u, 1);
    } else {
        if (!lowcut)
            master_loop(o, n, 0u, 0);
        else if (lowcut == 2u)
            master_loop(o, n, 2u, 0);
        else
            master_loop(o, n, 1u, 0);
    }
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

#if FELUCCA_PERFORM
#include "perform.c"                                 /* the FX hold layer's effects (the master) */
#endif

#if CR_FX
#include "cr_fx.c"                                   /* ChoralRoot's delay and reverb (xf_block, after the chorus) */
#else
static uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB: level as before the allpass fix */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v is a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.sp / 4u; i++)
        rev_u.sp[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
}

#endif

static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */

/* process the three buses for one block; sends in, wet stereo-equal out */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet,
                     uint32_t n)
{
    uint32_t i;
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
#if CR_FX                                               /* the chorus alone: the delay and the reverb are xf_block's */
    (void)dly_in;
    (void)rev_in;
#else
    uint32_t dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258, rt;
    if (dl != fx.dly_dl) {                              /* ChoralRoot (fx_smooth): a new delay time crossfades */
        if (fx.dly_dl && fx_smooth) {
            fx.dly_old = fx.dly_xf > DLY_XF / 2u ? fx.dly_old : fx.dly_dl;   /* (mid-fade: the nearer of the two) */
            fx.dly_xf = DLY_XF;
        }
        fx.dly_dl = (uint16_t)dl;
    }
#endif
    for (i = 0; i < n; i++) {
        int32_t y = 0, r;
#if !CR_FX
        int32_t x;
#endif
        /* chorus: modulated short delay, 5..15 ms */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(fx.cho_ph) + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        fx.cho_w++;
#if !CR_FX
        /* delay with a low-passed feedback */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        if (fx.dly_xf) {                                /* from the old tap to the new one, linear */
            int32_t xo = dly_buf[(fx.dly_w - fx.dly_old) & (DLY_LEN - 1u)];
            x += (int32_t)(((xo - x) * (int32_t)fx.dly_xf) / (int32_t)DLY_XF);
            fx.dly_xf--;
        }
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        y += mulq15(x << 1, dmix);
#endif
        wet[i] = y;
    }
#if !CR_FX
    rt = song.g[G_RTYPE] == 1;
    if (rt != fx.rtype) {                               /* the model changed: the old one's block fades out, */
        int32_t *t = part_buf, g = 65536, d = 65536 / (int32_t)n;   /* its buffers are cleared, the new */
        for (i = 0; i < n; i++)                                     /* one starts from silence */
            t[i] = 0;
        if (fx.rtype)
            rev_spring(rev_in, t, n);
        else
            rev_room(rev_in, t, n);
        for (i = 0; i < n; i++, g -= d)
            wet[i] += mulq16(t[i], (uint32_t)g);
        rev_clear();
        fx.rtype = (uint8_t)rt;
        return;
    }
    if (rt)
        rev_spring(rev_in, wet, n);
    else
        rev_room(rev_in, wet, n);
#endif
}

/* one block of the whole mix (shared with hostsim.c): events -> each part (with its modulation matrix)
 * -> dist -> SLICER -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet[CTL], mix_l[CTL], mix_r[CTL];
#if FELUCCA_UAC
/* USB audio's "ChoralRoot In" (usb_audio_stream.c ua_audio), one block's frames of six channels: 0-1 the master
 * (choralroot.c's mix_block shim: before the click), 2-3 the CHORD part, 4-5 the BASS part,
 * each part's dry stereo as it goes into the mix (after DIST, LEVEL, pan and the stereo side, before the sends, the
 * FX buses and MASTER), at half level (-6 dB: a loud chord there is ~2x the limiter's threshold) and saturated to
 * 16 bits. Filled only while the computer records (ua_stage_on: the shim sets it for the block from ChoralRoot In's
 * alternate setting); else the ISR does none of this */
#define UA_CAP_CHANNELS 6u
#define UA_PART_CAPTURED 2u                             /* parts 0 (CHORD) and 1 (BASS) */
static int16_t ua_stage[CTL * UA_CAP_CHANNELS];
static uint8_t ua_stage_on;
static inline int16_t ua_sat(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }
#endif

/* A part's block b (sd: its stereo side, when side) at its level into the mix: the peak, the three sends (sg: their
 * gains, each moving by its dg a sample: ChoralRoot's ramp; Felucca's: the gains, dg 0), the pan, USB audio's tap
 * (pc). k: the block's values (MX_*). side, sends and tap are constants where a loop is built for them (the
 * mix_loop_* below): a part that sends nothing has no send in its loop, one that is not recorded no tap. Returns the
 * peak. */
enum { MX_LVL, MX_GL, MX_GR, MX_XMAX, MX_PK, MX_SG, MX_DG = MX_SG + 3, MX_N = MX_DG + 3 };
static inline __attribute__((always_inline)) int32_t mix_part_loop(const int32_t *b, const int32_t *sd, uint32_t n,
                                                                   const int32_t *k, int16_t *pc, int side, int sends,
                                                                   int tap)
{
    int32_t lvl = k[MX_LVL], gl = k[MX_GL], gr = k[MX_GR], xmax = k[MX_XMAX], pk = k[MX_PK];
    int32_t s0 = k[MX_SG], s1 = k[MX_SG + 1], s2 = k[MX_SG + 2], d0 = k[MX_DG], d1 = k[MX_DG + 1], d2 = k[MX_DG + 2];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x, y = 0, a, l, rr;
        if (side) {
            x = ((b[i] >> 2) * lvl) >> 10;
            y = ((sd[i] >> 2) * lvl) >> 10;
        } else {
            /* pre-shift: 8 loud voices; saturate where the level (+7.5 dB) or the pan would overflow (FM6's 16
             * voices keep Dexed's headroom; Melodee's) */
            x = clamp(((clamp(b[i], -884000, 884000) >> 2) * lvl) >> 10, -524287, 524287);
        }
        a = x < 0 ? -x : x;
        if (a > pk)
            pk = a;
        if (sends) {
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow */
            s0 += d0;
            if (s0)
                send_c[i] += mulq15(xs, s0);
            s1 += d1;
            if (s1)
                send_d[i] += mulq15(xs, s1);
            s2 += d2;
            if (s2)
                send_r[i] += mulq15(xs, s2);
        }
        if (side) {
            l = ((x - y) * gl) >> 12;
            rr = ((x + y) * gr) >> 12;
        } else {
            l = (x * gl) >> 12;
            rr = (x * gr) >> 12;
        }
        mix_l[i] += l;
        mix_r[i] += rr;
#if FELUCCA_UAC
        if (tap) {
            pc[i * UA_CAP_CHANNELS] = ua_sat(l >> 1);
            pc[i * UA_CAP_CHANNELS + 1u] = ua_sat(rr >> 1);
        }
#endif
    }
    (void)pc;
    (void)tap;
    return pk;
}
/* the loops: a function each (two calls of one function that differ in a constant are folded into one by the
 * compiler before it inlines, and the constant with them); S: with the side, F: with the sends, U: with USB's tap */
#define MIX_LOOP_FN(name, side_, sends_, tap_) \
    static __attribute__((noinline)) int32_t name(const int32_t *b, const int32_t *sd, uint32_t n, const int32_t *k, \
                                                  int16_t *pc) \
    { \
        return mix_part_loop(b, sd, n, k, pc, side_, sends_, tap_); \
    }
MIX_LOOP_FN(mix_loop, 0, 0, 0)
MIX_LOOP_FN(mix_loop_f, 0, 1, 0)
MIX_LOOP_FN(mix_loop_s, 1, 0, 0)
MIX_LOOP_FN(mix_loop_sf, 1, 1, 0)
#if FELUCCA_UAC
MIX_LOOP_FN(mix_loop_u, 0, 0, 1)
MIX_LOOP_FN(mix_loop_fu, 0, 1, 1)
MIX_LOOP_FN(mix_loop_su, 1, 0, 1)
MIX_LOOP_FN(mix_loop_sfu, 1, 1, 1)
#else
#define mix_loop_u mix_loop
#define mix_loop_fu mix_loop_f
#define mix_loop_su mix_loop_s
#define mix_loop_sfu mix_loop_sf
#endif
#undef MIX_LOOP_FN

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
/* (mix_part's second half: the part rendered into b, its side into side when side_on, nr voices) */
static void mix_part_out(track_t *t, int32_t *b, int32_t *side, uint8_t side_on, uint32_t nr, uint32_t n)
{
    int32_t *sd;
    int16_t *pc = 0;
#if FELUCCA_UAC
    if (ua_stage_on && (uint32_t)(t - trk) < UA_PART_CAPTURED)
        pc = ua_stage + 2u + 2u * (uint32_t)(t - trk);
#endif
    if (nr)
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (MODP(t).on)
            mod_end(t);
        return;
    }
    {
        int32_t lv = t->p[P_LEVEL] & 127, lvl, pan = t->p[P_PAN];
        if (t->trim && lv)                              /* the sound's trim (ChoralRoot), after LEVEL: 0.5 dB steps */
            lv = clamp(lv + t->trim, 1, 127);
        lvl = LEVEL_Q12[lv];
#if FELUCCA_VA
        pan = va_pan(t, pan);                           /* eng_va.c: VA's matrix PAN */
#endif
#if FELUCCA_QUAD
        pan = quad_pan(t, pan);                         /* eng_quad.c: QUAD's LFO -> PAN */
#endif
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
        int32_t xmax, k[MX_N], *g = k + MX_SG, *dg = k + MX_DG;
        int sends;
        dg[0] = dg[1] = dg[2] = 0;
#if CR_FX
        int32_t *sg = fx_sg[t - trk];
        if (fx_off)
            c = d = r = 0;
        dg[0] = (c - sg[0]) >> CTL_LOG2;                /* (CTL samples a block; the ramp's last fraction: a step of
                                                         * under 32 / 32768) */
        dg[1] = (d - sg[1]) >> CTL_LOG2;
        dg[2] = (r - sg[2]) >> CTL_LOG2;
        g[0] = sg[0];
        g[1] = sg[1];
        g[2] = sg[2];
        xmax = c > sg[0] ? c : sg[0];
        xmax = d > xmax ? d : sg[1] > xmax ? sg[1] : xmax;
        xmax = r > xmax ? r : sg[2] > xmax ? sg[2] : xmax;
        xmax = 0x7FFFFFFF / (xmax | 1);
        sends = (c | d | r | sg[0] | sg[1] | sg[2]) != 0;   /* (all 0: the ramps stay at 0, no bus gets a sample) */
#define FX_SEND_END() (sg[0] = c, sg[1] = d, sg[2] = r)
#else
        g[0] = c;
        g[1] = d;
        g[2] = r;
        xmax = c > d ? c : d;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        sends = (c | d | r) != 0;
#define FX_SEND_END() ((void)0)
#endif
        /* a stereo side (voice.c part_side: VA's SPREAD / USPREAD): DIST works on the mid, the SLICER plays the part
         * mono, the FX layer's mute fades both; the sends take the mid */
        sd = side_on && !t->p[P_SLCR] && !slicer_busy(t) ? side : 0;
        track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
#if FELUCCA_PERFORM
        if ((pf.mute >> (t - trk)) & 1u) {
            if (sd) {
                int32_t mg = pf.mg[t - trk];
                perf_mute((uint32_t)(t - trk), sd, n);
                pf.mg[t - trk] = mg;
            }
            perf_mute((uint32_t)(t - trk), b, n);       /* perform.c: a black key in the FX layer */
        }
#endif
        k[MX_LVL] = lvl;
        k[MX_GL] = gl;
        k[MX_GR] = gr;
        k[MX_XMAX] = xmax;
        k[MX_PK] = pk;
        /* the loop of this block: with or without the side, the sends, the tap */
        if (sd)
            pk = sends ? (pc ? mix_loop_sfu : mix_loop_sf)(b, sd, n, k, pc) : (pc ? mix_loop_su : mix_loop_s)(b, sd, n, k, pc);
        else
            pk = sends ? (pc ? mix_loop_fu : mix_loop_f)(b, sd, n, k, pc) : (pc ? mix_loop_u : mix_loop)(b, sd, n, k, pc);
        t->peak = pk;
        FX_SEND_END();
#undef FX_SEND_END
    }
    if (MODP(t).on)
        mod_end(t);                                     /* the stored values back */
}

static void mix_part(track_t *t, uint32_t n)
{
    uint32_t nr;
    CR_PROF(CRP_FX);
    mod_begin(t);                                       /* the matrix's per-block values into t->p (mod.c) */
    nr = track_render(t, part_buf, n);
    CR_PROF(t == &trk[0] ? CRP_PART0 : t == &trk[1] ? CRP_PART1 : CRP_FX);
    mix_part_out(t, part_buf, part_side, part_side_on, nr, n);
}

/* The parts that sound: Felucca's four; ChoralRoot's units play the chord and the bass part only (engines.c
 * ENG_STATE_NPART: a part above them has no engine state and takes no note: cr_out.c's midi_event and the engine's
 * routes are parts 0 / 1), so their matrix, render and mix have nothing to do. What a silent part still does to the
 * others stays: its LFO's tick (voice.c lfo_rand: part 3's S&H draws from the shared generator, whose sequence the
 * engines' noise and the notes' RAND follow) and the SLICER's step clock (slicer.c: a stub in the unit). */
#define MIX_NPART ENG_STATE_NPART
static void mix_part_idle(track_t *t, uint32_t n)
{
    track_lfo_tick(t);
    slicer_track(t, 0, n);
}

#if CR_CPU1
/* ChoralRoot's second core (cr_cpu1.h, docs/DUALCORE.md): part 1's render as core 1's job, into its own buffers.
 * It touches trk[1] (its voices, its LFO's value, its patch, P_E0..7 during an engine fade), its engine's state
 * (eng_state[1], the engine's per-part arrays, fm6_core.c's second scratch) and these; nothing else */
static int32_t c1_buf[CTL] __attribute__((section(".pool"))), c1_side[CTL] __attribute__((section(".pool")));
static uint8_t c1_side_on;
static uint32_t c1_nr;
static int32_t c1_lfo[2];                               /* the parts' LFO values before this block's tick */
static void c1_part1_job(uint32_t n)
{
    uint32_t t0 = cr_dbg.prof_on ? cr_prof_now() : 0u;
    c1_nr = track_render_x(&trk[1], c1_buf, c1_side, &c1_side_on, n, &c1_lfo[1]);
    if (cr_dbg.prof_on)
        cr_dbg.cur[CRP_PART1] += cr_prof_less(cr_prof_now() - t0);
}
/* the split is one core's samples bit for bit unless part 0's render draws from the shared generator itself
 * (PHYS: a model change under a note) or both parts play VOICE (one breath-noise generator, eng_formant.c): then
 * this block renders on core 0 alone, as before */
static int c1_split_ok(void)
{
    const engine_t *e0 = ENGINES[trk[0].engine], *e1 = ENGINES[trk[1].engine];
    return e0 != &ENG_PHYS && !(e0 == &ENG_FORMANT && e1 == &ENG_FORMANT);
}
/* parts 0 and 1: everything shared first on core 0 (the matrices, the LFOs: their draws from the shared generator in
 * one core's order), part 1 to core 1, part 0 here, then both mixed in one core's order */
static void mix_parts_split(uint32_t n)
{
    track_t *t0 = &trk[0], *t1 = &trk[1];
    uint32_t nr0;
    int ran;
    CR_PROF(CRP_FX);
    mod_begin(t0);
    c1_lfo[0] = mulq15(t0->lfo_val, t0->lfo_fade);
    track_lfo_tick(t0);
    mod_begin(t1);
    c1_lfo[1] = mulq15(t1->lfo_val, t1->lfo_fade);
    track_lfo_tick(t1);
    cr_c1_split = 1;
    ran = cr_c1_check() && cr_c1_run(c1_part1_job, n);   /* (a second misread, a fault: given up) */
    CR_PROF_SKIP();                                     /* (the host's stub ran it here: counted as part1) */
    nr0 = track_render_x(t0, part_buf, part_side, &part_side_on, n, &c1_lfo[0]);
    CR_PROF(CRP_PART0);
    if (ran && cr_c1_wait())                            /* 8 ms or a fault: core 1 held for good (C1_GAVE_UP),
                                                         * core 0 does it */
        ran = 0;
    CR_PROF(CRP_WAIT);
    cr_c1_split = 0;
    if (ran)
        cr_dbg.c1_jobs++;
    else
        c1_part1_job(n);
    CR_PROF_SKIP();
    mix_part_out(t0, part_buf, part_side, part_side_on, nr0, n);
    mix_part_out(t1, c1_buf, c1_side, c1_side_on, c1_nr, n);
}
#endif

#if FELUCCA_PERFORM
/* the master with the FX layer's effects between its level and master_out (perform.c) */
static __attribute__((noinline)) void perf_master(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB Level Fixed: MASTER after) */
    for (i = 0; i < n; i++) {
        mix_l[i] = (((mix_l[i] + wet[i]) >> 2) * mg) >> 10;
        mix_r[i] = (((mix_r[i] + wet[i]) >> 2) * mg) >> 10;
    }
    perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        out[2u * i] = mix_l[i];
        out[2u * i + 1u] = mix_r[i];
    }
    master_block(out, n);
}
#endif

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
#if FELUCCA_PERFORM
    int perf;
#endif
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB Level Fixed: MASTER after) */
#if FELUCCA_UAC
    if (ua_stage_on)
        for (i = 0; i < n * UA_CAP_CHANNELS; i++)
            ua_stage[i] = 0;                            /* (a silent part: mix_part leaves its channels) */
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    events_block(n);
#if FELUCCA_PERFORM
    perf = perf_begin(n);                               /* the FX hold layer at work (perform.c) */
#endif
    CR_PROF(CRP_TICK);                                  /* (the engine tick before it: the mix_block shim) */
    i = 0;
#if CR_CPU1
    if (cr_dualcore_active() && c1_split_ok()) {
        mix_parts_split(n);
        i = 2;
    }
#endif
    for (; i < MIX_NPART; i++)
        mix_part(&trk[i], n);
    for (; i < NPART; i++)
        mix_part_idle(&trk[i], n);
#if FELUCCA_PERFORM
    if (perf)
        perf_pre(mix_l, mix_r, send_d, send_r, n);
#endif
    fx_buses(send_c, send_d, send_r, wet, n);
#if CR_FX
    xf_block(send_d, send_r, mix_l, mix_r, n);          /* (cr_fx.c: stereo, into the mix; the FX layer's master
                                                         * effects and the limiter after it, as the chorus) */
#endif
    CR_PROF(CRP_FX);
#if FELUCCA_PERFORM
    if (perf) {
        perf_master(out, n);
        return;
    }
#endif
    for (i = 0; i < n; i++) {
        out[2u * i] = (((mix_l[i] + wet[i]) >> 2) * mg) >> 10;
        out[2u * i + 1u] = (((mix_r[i] + wet[i]) >> 2) * mg) >> 10;
    }
    master_block(out, n);
}
