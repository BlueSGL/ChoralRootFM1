/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* CZ-1's pops, looked for in the host render (docs/CZ1.md "Pops", HISTORY 2026-10-12). Not a pass / fail suite (that is
 * tests/cr_cz_test.c's): the matrix that found them, kept to run again after a change of the engine.
 *
 * A run: one factory sound on part 0 (LEVEL 92, POLY, the sends at 0, its trim as cr_bank.c CB_TRIM), ChoralRoot's mix
 * (fx_smooth, voice_fade_steal, MASTER 2047 as at power-on), optionally the CZ-1 bass (SYNTH.BASS, MONO) on part 1,
 * one of the scenarios below on a 4-, 5- or 6-note chord at a root and a velocity. Each run in a fork()ed child (every
 * state as at boot). Looked at: the dry mix before the master (fx.c mix_l / mix_r: linear, 32768 = full scale at
 * MASTER's top) and the output after it.
 *   jump   a sample-to-sample step above FLOOR that is at least 4 times the largest step the signal itself makes in
 *          the 600 samples before and after it, or 1.25 periods of the run's lowest note if that is longer (the tone's
 *          own slope: a saw's edge or a narrow pulse once a cycle is not a pop, a cut level is)
 *   dip    a 32-sample window whose RMS is under 1/8 of the window two before it (18 dB in 1.5 ms) and that is back
 *          above half within 30 ms: a drop-out and return
 *   hole   a 32-sample window of zeros between sounding windows
 *   dc     the largest |mean| of 2048 samples (a step the DC filter rides out)
 *   peaks  the part after its level (track_t.peak; the mix loop clamps at 524287), the dry mix, the output, the
 *          limiter's lowest gain
 * Build with the sanitizer: any int32 wrap in the voice, the part sum or the mix aborts the run ("WRAP").
 *   cc -O2 -w -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src \
 *      -o build/host/cz_pops tests/cz_pops.c -lm
 *   build/host/cz_pops all A B              every run of CZ-1 presets A..B (0 INIT TONE, 1..64 Casio's): a line a run
 *                                           (384 runs a preset, about 10 s; the 65 in parallel: 3 minutes)
 *   build/host/cz_pops one E P S C R V B [out.wav [mix.wav]]   one run: engine E (this build's number: hostsim.c
 *                                           builds SLICE in, so CZ-1 is 15 here, 14 on the unit; the first field of a
 *                                           line of "all"), preset P, scenario S (0 hold, 1 retrig, 2 retrig-gap,
 *                                           3 change, 4 steal, 5 arp), chord C (0 .. 3), root R (a MIDI note),
 *                                           velocity V, bass B (0 / 1); prints its line and the samples around the
 *                                           worst jump, writes the output and the dry mix (a quarter of its level)
 *   build/host/cz_pops level                the engines' levels for the same chord (docs/CZ1.md "Level")
 * tools/cz_pops_report.py sorts the lines of "all". The probe (CZ_PROBE, the end of cz_native.c cz_native_render) is
 * this file's: the firmware and the other host builds do not define it. */
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#ifndef FM6_POLY
#define FM6_POLY 8               /* as choralroot.c */
#endif
#include <stdint.h>
/* the voice's block as cz_native.c rendered it: the lines' DCA gains at its two ends, their DCW depths, the two lines */
static void cz_probe(const void *t, const void *v, uint32_t first, uint32_t last, const int32_t *amp, const uint32_t *dcw,
                     const int32_t *l0, const int32_t *l1, uint32_t n, uint32_t mod, const void *m);
#define CZ_PROBE(t, v, first, last, amp, pd, line, n, mod, m) \
    do { \
        uint32_t dcw_[2] = {(first) == 0u ? pd[0].dcw : 0u, (last) == 1u ? pd[1].dcw : 0u}; \
        cz_probe(t, v, first, last, &amp[0][0], dcw_, line[0], line[1], n, mod, m); \
    } while (0)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* what the probe saw in a run: the peaks of a line, of a voice's sum before the voice's level and of what it adds to
 * the part; the largest step of a line's gain (DCA curve x the voice's ramp, Q15) from the end of a voice's block to the
 * start of its next, and of its DCW depth (0..1023), with the block and whether a note-on fell between */
static struct {
    int32_t line_pk, sum_pk, voice_pk, gstep, gstep_on, gjump, dstep, dstep_on;
    uint32_t gstep_blk;
} pb;
static uint32_t pb_blk;
static struct { uint32_t blk, age; int32_t g[2], last, valid; uint32_t dcw[2]; } pbv[NTRK][NVOICE];
static void cz_probe(const void *t_, const void *v_, uint32_t first, uint32_t last, const int32_t *amp, const uint32_t *dcw,
                     const int32_t *l0, const int32_t *l1, uint32_t n, uint32_t mod, const void *m_)
{
    const track_t *t = t_;
    const voice_t *v = v_;
    const vmod_t *m = m_;
    const int32_t *ln[2] = {l0, l1};
    uint32_t tr = (uint32_t)(t - trk) % NTRK, vi = (uint32_t)(v - t->v) % NVOICE, l, i;
    int32_t y0 = 0, y1 = 0;
    for (i = 0; i < n; i++) {
        int32_t y = first == last ? ln[first][i] : mod == 4u ? mulq15(l0[i], l1[i]) * 2 : (l0[i] + l1[i]) >> 1;
        int32_t o = voice_amp(y, m, i) * 4;
        for (l = first; l <= last; l++)
            pb.line_pk = abs(ln[l][i]) > pb.line_pk ? abs(ln[l][i]) : pb.line_pk;
        pb.sum_pk = abs(y) > pb.sum_pk ? abs(y) : pb.sum_pk;
        pb.voice_pk = abs(o) > pb.voice_pk ? abs(o) : pb.voice_pk;
        if (!i)
            y0 = o;
        y1 = o;
    }
    if (pbv[tr][vi].valid && pbv[tr][vi].blk + 1u == pb_blk) {
        int on = pbv[tr][vi].age != v->age;
        for (l = first; l <= last; l++) {
            int32_t g = mulq15(amp[2u * l], m->amp0), d = abs(g - pbv[tr][vi].g[l]);
            int32_t dd = abs((int32_t)dcw[l] - (int32_t)pbv[tr][vi].dcw[l]);
            if (d > pb.gstep) {
                pb.gstep = d;
                pb.gstep_on = on;
                pb.gstep_blk = pb_blk;
                pb.gjump = abs(y0 - pbv[tr][vi].last);
            }
            if (dd > pb.dstep) {
                pb.dstep = dd;
                pb.dstep_on = on;
            }
        }
    }
    for (l = first; l <= last; l++) {
        pbv[tr][vi].g[l] = mulq15(amp[2u * l + 1u], m->amp1);
        pbv[tr][vi].dcw[l] = dcw[l];
    }
    pbv[tr][vi].blk = pb_blk;
    pbv[tr][vi].age = v->age;
    pbv[tr][vi].last = y1;
    pbv[tr][vi].valid = 1;
}

enum { S_HOLD, S_RETRIG, S_RETRIG_GAP, S_CHANGE, S_STEAL, S_ARP, S_N };
static const char *const S_NAME[S_N] = {"hold", "retrig", "retrig-gap", "change", "steal", "arp"};
static const int8_t CHORD[4][6] = {{0, 4, 7, 11, -1, -1}, {0, 4, 7, 10, 14, -1}, {0, 4, 7, 9, 11, 14}, {0, 7, 12, 16, 19, 24}};
static const char *const C_NAME[4] = {"4", "5", "6", "6wide"};
static const uint8_t ROOTS[4] = {36, 60, 84, 96};
#define FLOOR 600               /* a jump's least step: -35 dB of full scale in the dry mix */
#define MAXS (6u * FS)

typedef struct { uint32_t blk; uint8_t part, note, vel, on; } ev_t;
static uint32_t lowest_period = 64;              /* samples: the lowest note of the run, and a quarter */
static ev_t ev[4096];
static uint32_t nev, nblk;
static void ev_add(uint32_t ms, uint32_t part, int note, uint32_t vel, uint32_t on)
{
    if (note < 0 || note > 127 || nev >= NELEM(ev))
        return;
    ev[nev].blk = ms * (FS / 100u) / (CTL * 10u);
    ev[nev].part = (uint8_t)part;
    ev[nev].note = (uint8_t)note;
    ev[nev].vel = (uint8_t)vel;
    ev[nev++].on = (uint8_t)on;
}
static void chord_ev(uint32_t ms, const int8_t *c, int root, uint32_t vel, uint32_t on, int bass)
{
    uint32_t i;
    for (i = 0; i < 6u; i++)
        if (c[i] >= 0)
            ev_add(ms, 0, root + c[i], vel, on);
    if (bass)
        ev_add(ms, 1, root - 12 < 24 ? root : root - 12, vel, on);
}
static void scenario(uint32_t s, uint32_t ci, int root, uint32_t vel, int bass)
{
    const int8_t *c = CHORD[ci];
    uint32_t i, k, nn = 0, end = 5000;
    nev = 0;
    for (i = 0; i < 6u; i++)
        nn += c[i] >= 0;
    switch (s) {
    case S_HOLD:
        chord_ev(0, c, root, vel, 1, bass);
        chord_ev(3000, c, root, vel, 0, bass);
        break;
    case S_RETRIG:                               /* the same chord struck again every 300 ms: off and on in one block */
        for (k = 0; k < 8u; k++) {
            if (k)
                chord_ev(k * 300u, c, root, vel, 0, bass);
            chord_ev(k * 300u, c, root, vel, 1, bass);
        }
        chord_ev(2400, c, root, vel, 0, bass);
        end = 3600;
        break;
    case S_RETRIG_GAP:                           /* .. let go 100 ms before */
        for (k = 0; k < 8u; k++) {
            chord_ev(k * 300u, c, root, vel, 1, bass);
            chord_ev(k * 300u + 200u, c, root, vel, 0, bass);
        }
        end = 3600;
        break;
    case S_CHANGE: {                             /* four chords with common tones, changed in one block */
        static const int8_t PR[4] = {0, -3, 5, 7};
        for (k = 0; k < 8u; k++) {
            if (k)
                chord_ev(k * 500u, c, root + PR[(k - 1u) & 3u], vel, 0, bass);
            chord_ev(k * 500u, c, root + PR[k & 3u], vel, 1, bass);
        }
        chord_ev(4000, c, root + PR[3], vel, 0, bass);
        end = 5200;
        break;
    }
    case S_STEAL:                                /* 12 notes 40 ms apart, all held: more than the voices there are */
        for (k = 0; k < 12u; k++)
            ev_add(k * 40u, 0, root + c[k % nn] + (k >= nn ? 12 : 0) + (k >= 2u * nn ? 12 : 0), vel, 1);
        if (bass)
            ev_add(0, 1, root - 12 < 24 ? root : root - 12, vel, 1);
        for (k = 0; k < 12u; k++)
            ev_add(1500, 0, root + c[k % nn] + (k >= nn ? 12 : 0) + (k >= 2u * nn ? 12 : 0), vel, 0);
        chord_ev(1700, c, root + 1, vel, 1, 0);  /* then three chords with no common tone, the tails still ringing */
        chord_ev(2000, c, root + 1, vel, 0, 0);
        chord_ev(2000, c, root + 6, vel, 1, 0);
        chord_ev(2300, c, root + 6, vel, 0, 0);
        chord_ev(2300, c, root + 3, vel, 1, 0);
        chord_ev(2800, c, root + 3, vel, 0, bass);
        end = 4000;
        break;
    default:                                     /* the arp: up and down at 200 BPM sixteenths, each note twice */
        if (bass)
            ev_add(0, 1, root - 12 < 24 ? root : root - 12, vel, 1);
        for (k = 0; k < 40u; k++) {
            uint32_t st = (k / 2u) % (2u * nn - 2u), j = st < nn ? st : 2u * nn - 2u - st;
            ev_add(k * 75u, 0, root + c[j], vel, 1);
            ev_add(k * 75u + 40u, 0, root + c[j], vel, 0);
        }
        if (bass)
            ev_add(3000, 1, root - 12 < 24 ? root : root - 12, vel, 0);
        end = 4200;
        break;
    }
    for (i = 1; i < nev; i++) {                  /* by time, the order of a block's events kept */
        ev_t x = ev[i];
        for (k = i; k && ev[k - 1u].blk > x.blk; k--)
            ev[k] = ev[k - 1u];
        ev[k] = x;
    }
    {
        uint32_t low = 127;
        for (i = 0; i < nev; i++)
            low = ev[i].note < low ? ev[i].note : low;
        lowest_period = (uint32_t)(FS / (440.0 * pow(2.0, ((double)low - 69.0) / 12.0)) * 1.25) + 32u;
    }
    nblk = end * (FS / 100u) / (CTL * 10u);
    if (nblk * CTL > MAXS)
        nblk = MAXS / CTL;
}

static int32_t *mx, *ol;                         /* the dry mix (mono), the output (left) */
static int16_t *ost;                             /* the output, stereo */
typedef struct {
    int32_t part_pk, mix_pk, out_pk, lim_min, dc;
    uint32_t njump, jump_at, ndip, dip_at, nhole, hole_at, ojump, ojump_at;
    int32_t jump_d, jump_ref, jump_ev, ojump_d, ojump_ref;
    uint32_t kills, steals;
    int32_t line_pk, sum_pk, voice_pk, gstep, gstep_on, gjump, dstep, dstep_on;
    uint32_t gstep_at;
} res_t;

/* jumps of x[0..n): the count, the worst (by step / reference) */
static uint32_t jumps(const int32_t *x, uint32_t n, int32_t floor_, uint32_t *at, int32_t *dmax, int32_t *refmax)
{
    uint32_t nw = n / 32u, w, i, cnt = 0, last = 0, span = lowest_period / 32u + 1u < 19u ? 19u : lowest_period / 32u + 1u;
    int32_t *bm = calloc(nw + 1u, sizeof *bm);
    double best = 0;
    for (i = 1; i < nw * 32u; i++) {
        int32_t d = abs(x[i] - x[i - 1u]);
        if (d > bm[i / 32u])
            bm[i / 32u] = d;
    }
    for (i = 1; i < nw * 32u; i++) {
        int32_t d = abs(x[i] - x[i - 1u]), ref = 1;
        uint32_t j;
        if (d < floor_)
            continue;
        w = i / 32u;
        for (j = w > span ? w - span : 0; j <= w + span && j < nw; j++)
            if (j + 1u < w || j > w + 1u)
                ref = bm[j] > ref ? bm[j] : ref;
        for (j = (w > 0 ? w - 1u : 0) * 32u; j < (w + 2u) * 32u && j < nw * 32u; j++)
            if (j && (j + 2u < i || j > i + 2u)) {
                int32_t e = abs(x[j] - x[j - 1u]);
                ref = e > ref ? e : ref;
            }
        if (d >= 4 * ref) {
            if (!cnt || i > last + 4u)
                cnt++;
            last = i;
            if ((double)d / ref > best) {
                best = (double)d / ref;
                *at = i;
                *dmax = d;
                *refmax = ref;
            }
        }
    }
    free(bm);
    return cnt;
}

static void analyse(uint32_t n, res_t *r)
{
    uint32_t nw = n / 32u, w, i;
    double *rms = calloc(nw + 64u, sizeof *rms);
    int32_t *pk = calloc(nw + 64u, sizeof *pk);
    int64_t acc = 0;
    r->njump = jumps(mx, n, FLOOR, &r->jump_at, &r->jump_d, &r->jump_ref);
    r->ojump = jumps(ol, n, FLOOR / 2, &r->ojump_at, &r->ojump_d, &r->ojump_ref);
    for (w = 0; w < nw; w++) {
        double s = 0;
        for (i = 0; i < 32u; i++) {
            int32_t v = mx[w * 32u + i];
            s += (double)v * v;
            pk[w] = abs(v) > pk[w] ? abs(v) : pk[w];
        }
        rms[w] = sqrt(s / 32.0);
    }
    {   /* the envelope: each window's peak held over the lowest note's period (a pulse wave's flat is not a dip) */
        uint32_t hold = lowest_period / 32u + 2u;
        for (w = 0; w < nw; w++) {
            int32_t e = 0;
            for (i = 0; i <= hold && i <= w; i++)
                e = pk[w - i] > e ? pk[w - i] : e;
            rms[w] = e;
        }
    }
    for (w = 2; w < nw; w++) {
        uint32_t back_w = lowest_period / 32u + 4u;
        if (w >= back_w && rms[w - back_w] > 300.0 && rms[w] < rms[w - back_w] / 4.0) {
            double back = 0;
            for (i = 1; i <= 80u && w + i < nw; i++)
                back = rms[w + i] > back ? rms[w + i] : back;
            if (back > rms[w - back_w] / 2.0) {
                if (!r->ndip++)
                    r->dip_at = w * 32u;
                w += 40u;
                continue;
            }
        }
        if (pk[w] == 0 && pk[w - 1u] > 300) {
            for (i = 1; i <= 8u && w + i < nw; i++)
                if (pk[w + i] > 300) {
                    if (!r->nhole++)
                        r->hole_at = w * 32u;
                    break;
                }
        }
    }
    for (i = 0; i < n; i++) {
        acc += mx[i];
        if (i >= 2048u) {
            acc -= mx[i - 2048u];
            if (llabs(acc) / 2048 > r->dc)
                r->dc = (int32_t)(llabs(acc) / 2048);
        }
    }
    free(rms);
    free(pk);
}

static const struct { const char *pr; int8_t trim; } TRIM[] = {{"BRASS 1", -10}, {"STRINGS 2", -6}, {"PIPE ORGAN 1", -12}};
static uint32_t preset_named(uint32_t eng, const char *name)
{
    const engine_t *e = ENGINES[eng];
    uint32_t k;
    for (k = 0; k < e->npresets; k++)
        if (!strcmp(e->presets[k].name, name))
            return k;
    return 0;
}

static void render(uint32_t eng, uint32_t pr, uint32_t s, uint32_t ci, int root, uint32_t vel, int bass, res_t *r)
{
    int32_t out[2 * CTL];
    uint32_t i, b, e = 0;
    memset(r, 0, sizeof *r);
    r->lim_min = 32768;
    scenario(s, ci, root, vel, bass);
    mx = calloc(nblk * CTL, sizeof *mx);
    ol = calloc(nblk * CTL, sizeof *ol);
    ost = calloc(nblk * CTL * 2u, sizeof *ost);
    host_tracks_init();
    fx_smooth = 1;
    voice_fade_steal = 1;
    song.master_q12 = 2047;
    song.g[G_BPM] = 120;
    host_preset(&trk[0], eng, pr);
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_LEVEL] = 92;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    if (eng == ENGI_CZ)
        for (i = 0; i < NELEM(TRIM); i++)
            if (!strcmp(ENGINES[eng]->presets[pr].name, TRIM[i].pr))
                trk[0].trim = TRIM[i].trim;
    for (i = 1; i < NPART; i++)
        trk[i].p[P_LEVEL] = 0;
    if (bass) {
        host_preset(&trk[1], ENGI_CZ, preset_named(ENGI_CZ, "SYNTH.BASS"));
        trk[1].p[P_VOICE] = V_MONO;
        trk[1].p[P_LEVEL] = 92;
        trk[1].p[P_CHOR] = trk[1].p[P_DLY] = trk[1].p[P_REV] = 0;
    }
    for (b = 0; b < nblk; b++) {
        /* (the note-offs of a block before its note-ons, as a chord change gives them: stable sort by construction) */
        for (; e < nev && ev[e].blk <= b; e++) {
            if (ev[e].on)
                trk_note_on(&trk[ev[e].part], ev[e].note, ev[e].vel);
            else
                trk_note_off(&trk[ev[e].part], ev[e].note);
        }
        trk[0].peak = 0;
        pb_blk = b + 1u;
        mix_block(out, CTL);
        r->part_pk = trk[0].peak > r->part_pk ? trk[0].peak : r->part_pk;
        r->lim_min = lim_g < r->lim_min ? lim_g : r->lim_min;
        for (i = 0; i < CTL; i++) {
            int32_t m = (mix_l[i] + mix_r[i]) / 2, o = out[2u * i];
            mx[b * CTL + i] = m;
            ol[b * CTL + i] = o;
            ost[2u * (b * CTL + i)] = (int16_t)clamp(out[2u * i], -32768, 32767);
            ost[2u * (b * CTL + i) + 1u] = (int16_t)clamp(out[2u * i + 1u], -32768, 32767);
            r->mix_pk = abs(m) > r->mix_pk ? abs(m) : r->mix_pk;
            r->out_pk = abs(o) > r->out_pk ? abs(o) : r->out_pk;
        }
    }
    r->line_pk = pb.line_pk;
    r->sum_pk = pb.sum_pk;
    r->voice_pk = pb.voice_pk;
    r->gstep = pb.gstep;
    r->gstep_on = pb.gstep_on;
    r->gjump = pb.gjump;
    r->gstep_at = (pb.gstep_blk - 1u) * CTL;
    r->dstep = pb.dstep;
    r->dstep_on = pb.dstep_on;
    r->kills = voice_kills;
    r->steals = voice_steals;
    analyse(nblk * CTL, r);
    {   /* the event nearest before the worst jump: its distance in samples, -1 none */
        int32_t best = -1;
        for (i = 0; i < nev; i++)
            if (ev[i].blk * CTL <= r->jump_at)
                best = (int32_t)(r->jump_at - ev[i].blk * CTL) * 2 + ev[i].on;
        r->jump_ev = best;
    }
}

static void wav16(const char *path, const int16_t *st, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    wav_hdr(f, frames);
    fwrite(st, 4, frames, f);
    fclose(f);
}

static void line(uint32_t eng, uint32_t pr, uint32_t s, uint32_t ci, int root, uint32_t vel, int bass, const res_t *r,
                 int wrap)
{
    printf("%u %u \"%s\" %s %s %d %u %d | part %d mix %d out %d lim %d dc %d | jump %u at %u d %d ref %d ev %d %s | ojump "
           "%u at %u d %d ref %d | dip %u at %u hole %u at %u | kills %u steals %u | line %d sum %d voice %d gstep %d %s at %u vjump %d dcwstep %d %s%s\n",
           eng, pr, ENGINES[eng]->presets[pr].name, S_NAME[s], C_NAME[ci], root, vel, bass, r->part_pk, r->mix_pk, r->out_pk,
           r->lim_min, r->dc, r->njump, r->jump_at, r->jump_d, r->jump_ref, r->jump_ev < 0 ? -1 : r->jump_ev / 2,
           r->jump_ev < 0 ? "-" : r->jump_ev & 1 ? "on" : "off", r->ojump, r->ojump_at, r->ojump_d, r->ojump_ref, r->ndip,
           r->dip_at, r->nhole, r->hole_at, r->kills, r->steals, r->line_pk, r->sum_pk, r->voice_pk, r->gstep,
           r->gstep_on ? "on" : "-", r->gstep_at, r->gjump, r->dstep, r->dstep_on ? "on" : "-", wrap ? " WRAP" : "");
}

/* a run in a child: its result through a pipe; a child that died (the sanitizer: a wrap) is reported as such */
static void run(uint32_t eng, uint32_t pr, uint32_t s, uint32_t ci, int root, uint32_t vel, int bass)
{
    res_t r;
    int fd[2], st = 0, ok;
    fflush(stdout);
    if (pipe(fd))
        exit(2);
    if (!fork()) {
        close(fd[0]);
        render(eng, pr, s, ci, root, vel, bass, &r);
        _exit(write(fd[1], &r, sizeof r) != sizeof r);
    }
    close(fd[1]);
    ok = read(fd[0], &r, sizeof r) == sizeof r;
    close(fd[0]);
    wait(&st);
    if (!ok)
        memset(&r, 0, sizeof r);
    line(eng, pr, s, ci, root, vel, bass, &r, !ok);
}

int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "all")) {
        uint32_t a = (uint32_t)atoi(argv[2]), z = (uint32_t)atoi(argv[3]), pr, s, ci, ri, v, bass;
        for (pr = a; pr <= z && pr < ENGINES[ENGI_CZ]->npresets; pr++)
            for (s = 0; s < S_N; s++)
                for (ci = 0; ci < 4u; ci++)
                    for (ri = 0; ri < 4u; ri++)
                        for (v = 0; v < 2u; v++)
                            for (bass = 0; bass < 2u; bass++)
                                run(ENGI_CZ, pr, s, ci, ROOTS[ri], v ? 127u : 100u, (int)bass);
        return 0;
    }
    if (argc >= 9 && !strcmp(argv[1], "one")) {
        uint32_t eng = (uint32_t)atoi(argv[2]), pr = (uint32_t)atoi(argv[3]), s = (uint32_t)atoi(argv[4]),
                 ci = (uint32_t)atoi(argv[5]), vel = (uint32_t)atoi(argv[7]);
        int root = atoi(argv[6]), bass = atoi(argv[8]);
        res_t r;
        render(eng, pr, s % S_N, ci % 4u, root, vel, bass, &r);
        line(eng, pr, s % S_N, ci % 4u, root, vel, bass, &r, 0);
        if (argc > 9)
            wav16(argv[9], ost, nblk * CTL);
        if (argc > 10) {                         /* the dry mix, a quarter of its level (it may pass full scale) */
            uint32_t i;
            for (i = 0; i < nblk * CTL; i++)
                ost[2u * i] = ost[2u * i + 1u] = (int16_t)clamp(mx[i] / 4, -32768, 32767);
            wav16(argv[10], ost, nblk * CTL);
        }
        if (r.njump) {
            uint32_t i, k = r.jump_at;
            printf("dry mix around the worst jump (sample %u = %.4f s):", k, (double)k / FS);
            for (i = k > 6u ? k - 6u : 0; i < k + 6u && i < nblk * CTL; i++)
                printf(" %d", mx[i]);
            printf("\n");
        }
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "level")) {
        /* the same held 6-note chord (D4 F#4 A4 B4 C#5 E5: chord 2 at root 62), velocity 100, LEVEL 92, no trim but
         * CB_TRIM's: the part's peak after its level, and the dry mix's RMS over 0.3 .. 2.9 s */
        static const struct { uint8_t e; const char *n; } REF[] = {
            {ENGI_FM6, "TINE EP"}, {ENGI_FM6, "PAD"}, {ENGI_FM6, "ORGAN"}, {ENGI_FM6, "BELL"}, {ENGI_FM6, "PLUCK"},
            {ENGI_VA, 0}, {ENGI_QUAD, 0}, {ENGI_CZ, 0}};
        uint32_t k, pr, i, v;
        printf("engine preset | vel 100: part peak dB, mix RMS dB | vel 127: part peak dB, mix RMS dB   (0 dB = 32768)\n");
        for (k = 0; k < NELEM(REF); k++) {
            const engine_t *e = ENGINES[REF[k].e];
            for (pr = REF[k].n ? preset_named(REF[k].e, REF[k].n) : 0; pr < e->npresets; pr++) {
                printf("%s \"%s\"", e->name, e->presets[pr].name);
                for (v = 0; v < 2u; v++) {
                    int fd[2];
                    double o[2] = {0, 0};
                    fflush(stdout);
                    if (pipe(fd))
                        exit(2);
                    if (!fork()) {
                        res_t r;
                        double sq = 0;
                        uint32_t a = (uint32_t)(0.3 * FS), z = (uint32_t)(2.9 * FS);
                        render(REF[k].e, pr, S_HOLD, 2, 62, v ? 127u : 100u, 0, &r);
                        for (i = a; i < z; i++)
                            sq += (double)mx[i] * mx[i];
                        o[0] = 20 * log10(r.part_pk / 32768.0 + 1e-9);
                        o[1] = 10 * log10(sq / (z - a) / (32768.0 * 32768.0) + 1e-12);
                        _exit(write(fd[1], o, sizeof o) != sizeof o);
                    }
                    close(fd[1]);
                    if (read(fd[0], o, sizeof o) != sizeof o)
                        o[0] = o[1] = 999;
                    close(fd[0]);
                    wait(0);
                    printf(" | %6.1f %6.1f", o[0], o[1]);
                }
                printf("\n");
                if (REF[k].n)
                    break;
            }
        }
        return 0;
    }
    printf("usage: cz_pops all A B | one E P S C R V B [out.wav [mix.wav]] | level\n");
    return 1;
}
