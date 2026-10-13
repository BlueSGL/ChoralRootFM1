/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot's P5 (Melodee's Prophet-5 engine, docs/PROPHET.md; the code's names: PROPHET, p5_) on the
 * firmware as the emulator builds it (tools/emu/emu_firmware.h: hostsim's sound side with voice.c, the ChoralRoot UI,
 * storage.c on the RAM NOR):
 *   sh tests/run_cr_tests.sh
 *   build/host/cr_prophet_test               the checks
 *   build/host/cr_prophet_test --pcm FILE    a digest of every sample of a fixed render of every shipped program
 *                                            (a line names the program by its Sequential number, not its place) and
 *                                            of stress programs (sync, Poly-Mod, the wheel, both filters):
 *                                            tests/run_prophet_test.sh builds this file with P5_STAGE36 0, 1, 2, 4
 *                                            and 7 and compares the five outputs (the remix's optimisations are
 *                                            bit-exact), and with two shipped lists (a program renders the same
 *                                            samples wherever it stands)
 *   build/host/cr_prophet_test --id-out FILE the session's record of the chord part on every shipped program, as
 *                                            cr_settings.c css_part_get writes it
 *   build/host/cr_prophet_test --id-in FILE  .. read by THIS build (css_part_put), which may ship another list: every
 *                                            record brings its own program back, at the program's place here or, when
 *                                            it is not shipped here, at no place (run_prophet_test.sh)
 * Checks: the engine's place (16, after CZ-1 and before WHEEL, six engines shown, named P5); Sequential's bank (the
 * generated packed form of the shipped list = the .syx's frames of those numbers decoded; decode / encode round trip;
 * bad frames refused); the PRESETS names; the blob (138 bytes) round trip, a bad blob = INIT; every program rendered
 * through voice.c (a four-note chord held and released): sounds, no sample beyond the voices' sum, no DC after the
 * blocker, every voice free after its release, exact silence then; note-off frees the voices; a struck note again (no
 * click beyond the wave's own steps); the four-voice cap, the budget's units and which keys of a six-key chord sound;
 * UNISON (the editor's switch; A LOAD TAKES THE PROGRAM'S: one note of a chord on the stacked voices, which note, the
 * units; poly again on the next program; another engine; the bass part; the session's record; no hanging note) and
 * GLIDE; the pitch bend through the part's MIDI (cr_out.c midi_event): +2 semitones measured; the deep page table (19
 * pages: every column sets the byte it names and only that one, clamps, reads back; every one of the 56 bytes on
 * exactly one column); the pool (INIT + the shipped list) and a load through it; SAVE refused with a message and no
 * record written. With -DFELUCCA_P5_FACTORY=0: the engine with INIT alone builds and plays. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)
#define OK(c, ...)                                                      \
    do {                                                                \
        int ok_ = (c);                                                  \
        CHECK(ok_, __VA_ARGS__);                                        \
        if (ok_) {                                                      \
            printf("ok   ");                                            \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static uint32_t rs = 0x1234567u;
static uint32_t rnd(uint32_t n)
{
    rs = rs * 1664525u + 1013904223u;
    return (rs >> 8) % n;
}

/* part 0 on PROPHET with program p (as a load leaves it), its engine state as at an engine switch */
static void part_set(track_t *t, const p5_patch_t *p)
{
    uint32_t i;
    memset(t->v, 0, sizeof t->v);
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    t->engine = t->eng_req = (uint8_t)ENGI_PROPHET;
    t->preset = 0;
    t->user = 0;
    t->nmono = t->mono_note = 0;
    t->mw = t->at = 0;
    t->xf_on = t->xf = 0;
    eng_state_clear((uint32_t)(t - trk));
    midi_bend_q8[t - trk] = midi_bend_target[t - trk] = 0;
    p5_track_loaded(t);                               /* (preset 0: INIT, the part's voice mode noted) */
    if (p) {
        p5_patch_put(t, p);
        p5_track_accept(t);
    }
}
static uint32_t busy(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}
static uint64_t fnv(uint64_t h, const int32_t *x, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < 4u; k++)
            h = (h ^ (((uint32_t)x[i] >> (8u * k)) & 255u)) * 1099511628211ull;
    return h;
}

/* a program's fixed render: a chord, held hb blocks, released, rb blocks more. *pk the largest |sample|, *sum2 / *sum
 * of the held part after its first quarter (the level, the DC); returns the digest of every sample */
typedef struct { int32_t pk; double rms, dc; uint32_t left; int32_t tail; uint64_t h; } rr_t;
static rr_t render(track_t *t, const p5_patch_t *p, const uint8_t *notes, uint32_t nn, uint32_t vel, uint32_t hb, uint32_t rb)
{
    rr_t r;
    int32_t out[CTL];
    double s2 = 0, s1 = 0;
    uint32_t b, i, n = 0;
    memset(&r, 0, sizeof r);
    r.h = 1469598103934665603ull;
    part_set(t, p);
    for (i = 0; i < nn; i++)
        trk_note_on(t, notes[i], vel);
    for (b = 0; b < hb + rb; b++) {
        if (b == hb)
            for (i = 0; i < nn; i++)
                trk_note_off(t, notes[i]);
        track_render(t, out, CTL);
        r.h = fnv(r.h, out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            if (a > r.pk)
                r.pk = a;
            if (b >= hb / 4u && b < hb) {
                s2 += (double)out[i] * out[i];
                s1 += out[i];
                n++;
            }
            if (b + 1u == hb + rb && a > r.tail)
                r.tail = a;
        }
    }
    r.rms = n ? sqrt(s2 / n) : 0;
    r.dc = n ? s1 / n : 0;
    r.left = busy(t);
    return r;
}

/* the stress programs of --pcm (and of the overflow check): every path of the sample loop the factory bank may not
 * reach at its extremes */
static void stress(uint32_t k, p5_patch_t *p)
{
    p5_patch_init(p);
    p->raw[P5_LEVEL_B] = 127;
    p->raw[P5_SAW_B] = 1;
    p->raw[P5_PULSE_A] = p->raw[P5_PULSE_B] = 1;
    p->raw[P5_TRI_B] = (uint8_t)(k & 1u);
    p->raw[P5_SYNC] = (uint8_t)((k >> 1) & 1u);
    p->raw[P5_POLY_FREQ] = (uint8_t)((k >> 2) & 1u);
    p->raw[P5_POLY_PW] = p->raw[P5_POLY_FILTER] = (uint8_t)((k >> 3) & 1u);
    p->raw[P5_FILTER_REV] = (uint8_t)((k >> 4) & 1u);
    p->raw[P5_POLY_ENV] = 127;
    p->raw[P5_POLY_B] = 127;
    p->raw[P5_FREQ_B] = (uint8_t)(k & 32u ? 96 : 31);
    p->raw[P5_FINE_B] = 77;
    p->raw[P5_RESONANCE] = (uint8_t)(k & 64u ? 127 : 60);
    p->raw[P5_CUTOFF] = (uint8_t)(k & 64u ? 70 : 100);
    p->raw[P5_NOISE] = (uint8_t)(k & 128u ? 127 : 0);
    p->raw[P5_ENV_FILTER] = 127;
    p->raw[P5_LFO_RATE] = 127;
    p->raw[P5_LFO_INITIAL] = 127;
    p->raw[P5_LFO_SAW] = p->raw[P5_LFO_TRI] = p->raw[P5_LFO_PULSE] = 1;
    p->raw[P5_WHEEL_MIX] = 64;
    p->raw[P5_WHEEL_FREQ_A] = p->raw[P5_WHEEL_FREQ_B] = p->raw[P5_WHEEL_PW_A] = p->raw[P5_WHEEL_PW_B] =
        p->raw[P5_WHEEL_FILTER] = (uint8_t)((k >> 8) & 1u);
    p->raw[P5_LOW_B] = (uint8_t)((k >> 9) & 1u);
    p->raw[P5_KEY_B] = (uint8_t)!((k >> 10) & 1u);
    p->raw[P5_VINTAGE] = 127;
    p->raw[P5_DECAY_FILTER] = 40;
    p->raw[P5_SUSTAIN_FILTER] = 40;
}
#define NSTRESS 2048u

static int pcm_file(const char *path)
{
    static const uint8_t CH[4] = {36, 55, 64, 96};
    track_t *t = &trk[0];
    FILE *f = fopen(path, "w");
    p5_patch_t p;
    uint32_t k;
    if (!f)
        return 1;
    for (k = 0; k < NELEM(P5_TEST_PRESETS); k++) {       /* (named by the program's number: 000 INIT) */
        p5_preset_patch(k, &p);
        vage = 0;                                      /* (a render of its own: the voices' ages seed their noise, and
                                                        * the same samples whatever was rendered before: the lists) */
        rng_state = 0x1234567u;
        fprintf(f, "program %03u %016llx\n", (unsigned)p5_preset_id(k), (unsigned long long)render(t, &p, CH, 4, 100, 300, 150).h);
    }
    for (k = 0; k < NSTRESS; k += 3u) {
        stress(k, &p);
        fprintf(f, "stress %u %016llx\n", (unsigned)k, (unsigned long long)render(t, &p, CH, 4, 127, 60, 30).h);
    }
    fclose(f);
    return 0;
}

/* --id-out: the session's record of the chord part on every shipped program (cr_settings.c css_part_get) */
static int id_out(const char *path)
{
    FILE *f = fopen(path, "wb");
    css_part_t r;
    uint32_t k;
    if (!f)
        return 1;
    for (k = 1; k < NELEM(P5_TEST_PRESETS); k++) {
        cu_pool_load(0, ENGI_PROPHET, k);
        memset(&r, 0, sizeof r);
        css_part_get(0, &r);
        fwrite(&r, sizeof r, 1, f);
    }
    fclose(f);
    printf("id-out: %u records (INIT + %u shipped programs in this build)\n", (unsigned)k - 1u, (unsigned)k - 1u);
    return 0;
}
/* --id-in: records another build wrote, put into the chord part by this one (css_part_put). Every record must bring
 * its own program (the blob's 138 bytes, its name, its voice mode), and its place must be the place of the program
 * of that NUMBER in this build's pool (not the place the writer had it at); a program this build does not ship plays
 * all the same, at no place (the pool's INIT position, edited) */
static int id_in(const char *path)
{
    FILE *f = fopen(path, "rb");
    css_part_t r;
    track_t *t = &trk[CR_PART_CHORD];
    uint32_t n = 0, here = 0, moved = 0, away = 0, bad = 0;
    if (!f)
        return 1;
    while (fread(&r, sizeof r, 1, f) == 1) {
        p5_patch_t want, got, fac;
        uint32_t id, pos;
        memcpy(&want, r.blob, sizeof want);
        id = p5_patch_id(&want);
        pos = p5_preset_of(id);
        cu_pool_load(0, ENGI_FM6, 1);                  /* (something else first) */
        n++;
        if (!css_part_put(0, &r)) {
            bad++;
            printf("  record %u (program %u): refused\n", (unsigned)n, (unsigned)id);
            continue;
        }
        got = *p5_patch_of(t);
        if (memcmp(&got, &want, sizeof want) || strcmp(psnd[0].name, r.name) || t->p[P_VOICE] != r.p[P_VOICE] ||
            t->eng_req != ENGI_PROPHET || !id) {
            bad++;
            printf("  record %u (program %u): not the record's program\n", (unsigned)n, (unsigned)id);
        }
        if (pos) {
            p5_preset_patch(pos, &fac);
            here++;
            moved += pos != r.fk;
            if (memcmp(&fac, &want, sizeof want) || t->preset != pos || psnd[0].kind != PK_FACTORY || psnd[0].fk != pos ||
                cu_part_pos(0) != pos || psnd[0].edited != r.edited) {
                bad++;
                printf("  record %u (program %u): place %u, want %u\n", (unsigned)n, (unsigned)id, (unsigned)cu_part_pos(0), (unsigned)pos);
            }
        } else {
            away++;
            if (t->preset != 0 || psnd[0].kind != PK_INIT || !psnd[0].edited || cu_part_pos(0) != 0u) {
                bad++;
                printf("  record %u (program %u): not shipped here, place %u\n", (unsigned)n, (unsigned)id, (unsigned)cu_part_pos(0));
            }
        }
    }
    fclose(f);
    printf("id-in: %u records, %u wrong; %u of the programs are shipped here and stand at their own place (%u of them at "
           "another place than the writer's), %u are not shipped here and play from the record at no place\n",
           (unsigned)n, (unsigned)bad, (unsigned)here, (unsigned)moved, (unsigned)away);
    return bad != 0 || !n;
}

/* the part's frequency: the rising zero crossings of nb blocks (a plain wave) */
static double freq(track_t *t, uint32_t nb)
{
    int32_t out[CTL], prev = 0;
    uint32_t b, i, n = 0, first = 0, last = 0, s = 0;
    for (b = 0; b < nb; b++) {
        track_render(t, out, CTL);
        for (i = 0; i < CTL; i++, s++) {
            if (prev < 0 && out[i] >= 0) {
                if (!n)
                    first = s;
                last = s;
                n++;
            }
            prev = out[i];
        }
    }
    return n > 1u ? (double)(n - 1u) * FS / (double)(last - first) : 0;
}

int main(int argc, char **argv)
{
    static const uint8_t CHORD[4] = {48, 60, 64, 67};
    track_t *t = &trk[0];
    p5_patch_t p, q;
    uint32_t k, i, bad;
    rr_t r;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    if (argc == 3 && !strcmp(argv[1], "--pcm"))
        return pcm_file(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--id-out"))
        return id_out(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--id-in"))
        return id_in(argv[2]);

    /* ---- the engine's place ---- */
    for (k = 0, bad = 1; k + 2u < NENG_SHOWN + 1u; k++)
        if (ENGINE_ORDER[k] == ENGI_CZ)
            bad = !(k + 2u < NENG_SHOWN && ENGINE_ORDER[k + 1u] == ENGI_PROPHET && ENGINE_ORDER[k + 2u] == 7u);
    OK(ENGI_PROPHET == 16u && NENGINES == 17u && ENGINES[ENGI_PROPHET] == &ENG_P5_TEST && !strcmp(ENG_P5_TEST.name, "P5") &&
       eng_ok(ENGI_PROPHET) && !bad && NENG_SHOWN == 6 && cu_engine_melodic(ENGI_PROPHET) && cu_engine_rank(ENGI_PROPHET) == 4,
       "engine: P5 (the code's PROPHET) is 16 (appended after FM TONE's 15), shown after CZ-1 and before WHEEL, six engines to pick");
    OK(sizeof(p5_patch_t) == 138u && sizeof(p5_part_t) <= sizeof eng_state[0] && sizeof eng_state[0] == sizeof(fm6_part_t) &&
       ENG_P5_TEST.poly == 4u && P5_POLY == 4u && p5_units(t) == 4u && ENG_P5_TEST.ownenv && ENG_P5_TEST.deep == &P5_DEEP,
       "state: p5_part_t %u B in the parts' engine state (%u B, FM6's size: no growth); four voices of four units (the unit's reading)",
       (unsigned)sizeof(p5_part_t), (unsigned)sizeof eng_state[0]);

    /* ---- INIT PROPHET, the wire format ---- */
    {
        uint8_t f[P5_FRAME_MAX], f2[P5_FRAME_MAX];
        uint32_t n, n2;
        p5_patch_init(&p);
        n = p5_patch_encode(&p, f, sizeof f);
        OK(p5_patch_valid(&p) && n == 157u && p5_patch_decode(&q, f, n) && !memcmp(&p, &q, sizeof p) &&
           !memcmp(p.raw + P5_NAME, "INIT PROPHET        ", 20) && !strcmp(P5_TEST_PRESETS[0].name, "INIT P5") && !p5_patch_id(&p),
           "INIT: valid, an edit-buffer dump of 157 bytes that decodes to the same 138; no program number");
        memcpy(f2, f, n);
        f2[4] |= 0x80u;                                /* a mask byte over 127 */
        bad = (uint32_t)p5_patch_decode(&q, f2, n);
        memcpy(f2, f, n);
        f2[2] = 0x33;
        bad += (uint32_t)p5_patch_decode(&q, f2, n);
        bad += (uint32_t)p5_patch_decode(&q, f, n - 1u);
        memcpy(f2, f, n);
        f2[21] = 0x80;                                 /* a data byte over 127 */
        bad += (uint32_t)p5_patch_decode(&q, f2, n);
        p.command = 2;
        p.group = 4;
        p.program = 39;
        n2 = p5_patch_encode(&p, f2, sizeof f2);
        p.size = 100;
        OK(!bad && n2 == 159u && p5_patch_decode(&q, f2, n2) && q.group == 4 && q.program == 39 && !p5_patch_valid(&p),
           "wire: a bad mask, another model, a short frame and a data byte over 127 are refused; a program dump is 159");
    }

#if FELUCCA_P5_FACTORY
    /* ---- Sequential's bank ---- */
    {
        static uint8_t syx[200 * 159 + 1];
        uint8_t f[P5_FRAME_MAX];
        char nm[21];
        FILE *fh = fopen("assets/prophet5-factory/prophet5-v1.03.syx", "rb");
        size_t got = fh ? fread(syx, 1, sizeof syx, fh) : 0;
        uint32_t uni = 0, ssi = 0, sync = 0, over = 0, suni = 0, seen[201] = {0};
        if (fh)
            fclose(fh);
        for (k = 0, bad = 0; k < 200u && got == 200u * 159u; k++) {     /* the whole bank, from the file */
            bad += !p5_patch_decode(&q, syx + k * 159u, 159u) || p5_patch_id(&q) != k + 1u;
            uni += q.raw[P5_UNISON] != 0;
            ssi += !q.raw[P5_FILTER_REV];
            sync += q.raw[P5_SYNC] != 0;
            for (i = 0; i < 55u; i++)
                over += q.raw[i] > 127u;
        }
        OK(got == 200u * 159u && !bad && uni == 40u && ssi == 80u && sync == 25u && !over && P5_FACTORY_ALL == 200u,
           "bank: Sequential's 200 programs: 40 with UNISON, 80 on the SSI filter, 25 with sync; no knob over 127");
        for (k = 0, bad = 0; k < P5_FACTORY_N && got == 200u * 159u; k++) {
            uint32_t id = P5_FACTORY_ID[k];
            bad += id < 1u || id > 200u || seen[id <= 200u ? id : 0]++;
            if (id < 1u || id > 200u)
                continue;
            p5_factory(k, &p);
            bad += !p5_patch_decode(&q, syx + (id - 1u) * 159u, 159u) || memcmp(&p, &q, sizeof p) != 0;
            bad += p5_patch_encode(&p, f, sizeof f) != 159u || memcmp(f, syx + (id - 1u) * 159u, 159u) != 0;
            bad += !p5_patch_valid(&p) || p5_patch_id(&p) != id || p5_preset_of(id) != k + 1u || p5_preset_id(k + 1u) != id;
            suni += p.raw[P5_UNISON] != 0;
        }
        OK(P5_FACTORY_N >= 1u && P5_FACTORY_N <= 200u && !bad && NELEM(P5_TEST_PRESETS) == P5_FACTORY_N + 1u &&
           ENG_P5_TEST.npresets == P5_FACTORY_N + 1u && !p5_preset_of(0) && !p5_preset_of(201) && !p5_preset_id(0),
           "shipped: %u of the 200 (%u with UNISON, %u B a program packed): each unpacks to the .syx's frame of its own "
           "number, encodes back to its bytes, and is found by that number", (unsigned)P5_FACTORY_N, (unsigned)suni,
           (unsigned)P5_FACTORY_BYTES);
        for (k = 1, bad = 0; k <= P5_FACTORY_N; k++) {
            uint32_t l = (uint32_t)strlen(P5_TEST_PRESETS[k].name);
            bad += !l || l > 12u;
            for (i = 1; i < k; i++)
                bad += !strcmp(P5_TEST_PRESETS[k].name, P5_TEST_PRESETS[i].name);
        }
        if (p5_preset_of(1)) {
            p5_factory(p5_preset_of(1) - 1u, &p);
            p5_patch_name(nm, &p);
            bad += strcmp(nm, "It's a Prophet 5") || strcmp(P5_TEST_PRESETS[p5_preset_of(1)].name, "ITS PROPHET5");
        }
        OK(!bad, "names: PRESETS shows 1..12 characters, all different; the program keeps its 20 (program 1: ITS PROPHET5)");
    }
#else
    OK(NELEM(P5_TEST_PRESETS) == 1u && ENG_P5_TEST.npresets == 1u && pool_count(ENGI_PROPHET) == 1u,
       "FELUCCA_P5_FACTORY 0: INIT alone (no factory data in the build), the pool is INIT");
#endif

    /* ---- the blob ---- */
    {
        uint8_t b[ENG_BLOB_MAX], b2[ENG_BLOB_MAX];
        t->eng_req = t->engine = (uint8_t)ENGI_PROPHET;
        for (k = 0, bad = 0; k < NELEM(P5_TEST_PRESETS); k++) {
            p5_blob_preset(t, k);
            p5_blob_get(t, b);
            p5_preset_patch(k, &p);
            bad += memcmp(b, &p, sizeof p) != 0;
            p5_blob_preset(t, (k + 7u) % NELEM(P5_TEST_PRESETS));
            p5_blob_set(t, b);
            bad += memcmp(p5_patch_of(t), &p, sizeof p) != 0;
        }
        memcpy(b2, b, sizeof b2);
        b2[133] = 99;                                  /* a size that is neither 128 nor 133 */
        p5_blob_set(t, b2);
        p5_patch_init(&p);
        OK(!bad && P5_DEEP.blob_size == 138u && !memcmp(p5_patch_of(t), &p, sizeof p),
           "blob: the 138-byte program, every preset round trip; a bad blob -> INIT");
        p5_blob_set(t, 0);
        OK(!memcmp(p5_patch_of(t), &p, sizeof p), "blob: none -> INIT");
    }

    /* ---- every program through voice.c: a chord held 1.5 s, released, until its voices are free ---- */
    {
        uint32_t silent = 0, loud = 0, dc = 0, stuck = 0, nz = 0, worst = 0, quiet = 0, wk = 0;
        int32_t maxpk = 0;
        double maxdc = 0;
        for (k = 0; k < NELEM(P5_TEST_PRESETS); k++) {
            int32_t out[CTL];
            uint32_t b, cap;
            p5_preset_patch(k, &p);
            r = render(t, &p, CHORD, 4, 100, 3u * FS / (2u * CTL), 0);
            /* four voices of at most 2 x VOICE_FS each after the filter's knee (p5_samples: the amplifier x 2) */
            if (r.pk > 4 * 2 * VOICE_FS + 4 * VOICE_FS)
                loud++, printf("  program %u: peak %d\n", (unsigned)k, (int)r.pk);
            if (r.pk > maxpk)
                maxpk = r.pk, wk = k;
            if (r.rms < 20.0)
                quiet++;
            if (r.pk == 0)
                silent++;
            /* the DC after the blocker, against the part's level (a slow LFO is not DC: the mean of 1.1 s) */
            if (fabs(r.dc) > maxdc)
                maxdc = fabs(r.dc);
            if (fabs(r.dc) > 0.25 * r.rms + 300.0)
                dc++, printf("  program %u: dc %.0f rms %.0f\n", (unsigned)k, r.dc, r.rms);
            /* the release: 3 time constants past -60 dB, 100 s at most (P5_ENV_EXP: tau = 10000^(raw/127) ms) */
            cap = (uint32_t)(FS / CTL * (0.5 + 9.0 * pow(10000.0, p.raw[P5_RELEASE_AMP] / 127.0) / 1000.0));
            for (i = 0; i < 4u; i++)
                trk_note_off(t, CHORD[i]);
            for (b = 0; b < cap && busy(t); b++)
                track_render(t, out, CTL);
            if (b > worst)
                worst = b;
            if (busy(t))
                stuck++, printf("  program %u: %u voices after %u blocks\n", (unsigned)k, (unsigned)busy(t), (unsigned)b);
            for (b = 0; b < 4u; b++) {
                track_render(t, out, CTL);
                for (i = 0; i < CTL; i++)
                    nz += out[i] != 0;
            }
        }
        OK(!loud && !silent, "render: %u programs, a four-note chord at velocity 100: none silent, the largest sample %d "
           "(program %u; four voices' sum is at most %d)", (unsigned)NELEM(P5_TEST_PRESETS), (int)maxpk, (unsigned)wk,
           8 * VOICE_FS);
        OK(!dc, "render: no DC in a held chord (the part's blocker; the largest mean %.0f)", maxdc);
        OK(!stuck && !nz, "render: every voice free after its release (the longest %.1f s), exact silence after",
           worst * (double)CTL / FS);
        printf("     (%u programs under 20 rms in the held chord: percussive or self-ending)\n", (unsigned)quiet);
    }

    /* ---- the stress programs: no sample beyond the voices' sum ---- */
    {
        static const uint8_t HI[5] = {24, 43, 60, 79, 108};
        int32_t maxpk = 0;
        for (k = 0, bad = 0; k < NSTRESS; k += 5u) {
            stress(k, &p);
            r = render(t, &p, HI, 5, 127, 40, 20);
            bad += r.pk > 5 * 2 * VOICE_FS + 5 * VOICE_FS;
            if (r.pk > maxpk)
                maxpk = r.pk;
        }
        OK(!bad, "stress: sync, Poly-Mod at 127, the wheel at 127 on every destination, resonance 127, both filters, five "
           "keys at velocity 127: the largest sample %d (the voices' sum is at most %d)", (int)maxpk, 10 * VOICE_FS);
    }

    /* ---- voices: the cap, the units, note-off, a note struck again ---- */
    {
        int32_t out[CTL], last = 0, in = 0, edge = 0, d;
        uint32_t b;
        p5_patch_init(&p);
        p.raw[P5_RELEASE_AMP] = 10;
        part_set(t, &p);
        for (k = 0; k < 7u; k++)
            trk_note_on(t, 48 + 3 * k, 100);
        for (b = 0; b < 8u; b++)
            track_render(t, out, CTL);
        OK(busy(t) == 4u && trk_nvoice(t) == 4u && voice_units(t) == 4u && voices_busy_of(&trk[1]) == 16u,
           "voices: seven keys sound four voices (the cap), four budget units each: the 16 of one core");
        for (k = 0; k < 7u; k++)
            trk_note_off(t, 48 + 3 * k);
        for (b = 0; b < FS / CTL && busy(t); b++)
            track_render(t, out, CTL);
        /* which keys of a five- and a six-key chord sound: the chord's notes as cr_out.c brings them (ascending), a
         * block rendered and events_block run between and after (a note that takes a sounding voice waits for that
         * voice's fade: voice.c voice_fade_steal, vsq) */
        {
            static const uint8_t SIX[6] = {50, 54, 57, 61, 64, 66};   /* D3 F#3 A3 C#4 E4 F#4 */
            uint32_t nk, m5 = 0, m6 = 0;
            events_block(CTL);                         /* (a release the loads above asked for) */
            for (nk = 5; nk <= 6u; nk++) {
                uint32_t m = 0;
                for (k = 0; k < nk; k++)
                    trk_note_on(t, SIX[k], 100);
                for (b = 0; b < 12u; b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
                for (i = 0; i < NVOICE; i++)
                    if (t->v[i].active && t->v[i].gate && t->v[i].stage != 4u)
                        for (k = 0; k < nk; k++)
                            if (t->v[i].note == SIX[k])
                                m |= 1u << k;
                if (nk == 5u)
                    m5 = m;
                else
                    m6 = m;
                for (k = 0; k < nk; k++)
                    trk_note_off(t, SIX[k]);
                for (b = 0; b < FS / CTL && busy(t); b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
            }
            /* bit k: the chord's k-th note from the bottom sounds */
            OK(m5 == 0x1Du && m6 == 0x39u,
               "voices: of five keys the bottom note and the top three sound (the second from the bottom gives way; "
               "mask %02X), of six the bottom and the top three (the second and third give way; mask %02X): the oldest "
               "held voice goes, never the lowest note", (unsigned)m5, (unsigned)m6);
        }
        for (k = 0; k < 7u; k++)
            trk_note_on(t, 48 + 3 * k, 100);
        for (b = 0; b < 8u; b++)
            track_render(t, out, CTL);
        /* one core's shared budget (16 units; Dual Core Off, the default): a bass note takes two (the VA's) of them,
         * the chord part keeps three voices of its four */
        cu_pool_load(1, ENGI_VA, 1);
        trk[1].engine = trk[1].eng_req;
        trk[1].xf_on = trk[1].xf = 0;
        trk_note_on(&trk[1], 36, 100);
        for (b = 0; b < 8u; b++) {
            track_render(t, out, CTL);
            track_render(&trk[1], out, CTL);
        }
        OK(!cr_dualcore_active() && busy(t) == 3u && busy(&trk[1]) == 1u,
           "voices: with the bass sounding on one core the chord part has three (3 x 4 + the bass's 2 of 16 units)");
        trk_note_off(&trk[1], 36);
        for (b = 0; b < 4u * FS / CTL && busy(&trk[1]); b++)
            track_render(&trk[1], out, CTL);
        for (k = 0; k < 7u; k++)
            trk_note_off(t, 48 + 3 * k);
        for (b = 0; b < FS / CTL && busy(t); b++)
            track_render(t, out, CTL);
        OK(!busy(t) && b < FS / CTL / 2u, "voices: note-off frees them (release 10: %u ms)", (unsigned)(b * CTL * 1000u / FS));
        /* a struck note again: a low-passed saw, the largest step at the first sample of the restrike's block against
         * the largest inside the blocks */
        p5_patch_init(&p);
        p.raw[P5_CUTOFF] = 40;
        p.raw[P5_ATTACK_AMP] = 30;
        p.raw[P5_SUSTAIN_AMP] = 100;
        part_set(t, &p);
        trk_note_on(t, 57, 100);
        for (b = 0; b < 200u; b++) {
            if (b == 120u)
                trk_note_on(t, 57, 100);
            track_render(t, out, CTL);
            for (i = 0; i < CTL; i++) {
                d = out[i] - (i ? out[i - 1u] : last);
                d = d < 0 ? -d : d;
                if (b >= 100u && i && d > in)
                    in = d;
                if (b == 120u && !i)
                    edge = d;
            }
            last = out[CTL - 1u];
        }
        OK(busy(t) == 1u && edge <= in, "restrike: the same key again takes its voice; the step at the restrike %d, inside "
           "the blocks at most %d", (int)edge, (int)in);
    }

    /* ---- UNISON and GLIDE (the editor's columns) ---- */
    {
        int32_t out[CTL];
        uint32_t b, mid = 0;
        p5_patch_init(&p);
        part_set(t, &p);
        P5_DEEP.set(t, 17, 1, 3);                      /* VOICE: VOICES 3 */
        P5_DEEP.set(t, 17, 2, 7);                      /* DETUNE 7 */
        bad = t->p[P_VOICE] != V_POLY;
        P5_DEEP.set(t, 17, 0, 1);                      /* UNISON on */
        bad += t->p[P_VOICE] != V_UNISON || t->p[P_DETUNE] != 127 || p5_cap(t) != 3u;
        trk_note_on(t, 60, 100);
        for (b = 0; b < 4u; b++)
            track_render(t, out, CTL);
        bad += busy(t) != 3u;
        trk_note_off(t, 60);
        P5_DEEP.set(t, 17, 0, 0);
        OK(!bad && t->p[P_VOICE] == V_POLY, "unison: the editor's switch makes the part UNISON (three voices of one key, "
           "DETUNE 7), off: POLY again");
        t->p[P_VOICE] = V_MONO;                        /* a bass part's load of a program without UNISON */
        p5_track_loaded(t);
        bad = t->p[P_VOICE] != V_MONO || p5_own[0] != V_MONO;
        P5_DEEP.set(t, 17, 0, 1);
        bad += t->p[P_VOICE] != V_UNISON;
        P5_DEEP.set(t, 17, 0, 0);
        OK(!bad && t->p[P_VOICE] == V_MONO, "unison: on a MONO part the switch goes to UNISON and back to MONO");
#if FELUCCA_P5_FACTORY
        /* ---- A LOAD TAKES THE PROGRAM'S UNISON (the owner, 2026-10-13) ---- */
        {
            static const uint8_t CH4[4] = {62, 66, 69, 73};          /* D4 F#4 A4 C#5, as the chord engine sends them */
            uint32_t un = 0, po = 0, lowp = 0, lastp = 0, nuni = 0, wrong = 0, hang = 0, g;
            css_part_t rec;
            for (k = 1; k <= P5_FACTORY_N; k++) {
                p5_preset_patch(k, &p);
                if (p.raw[P5_UNISON]) {
                    uint32_t cnt = (uint32_t)clamp(p.raw[P5_UNISON_COUNT] ? p.raw[P5_UNISON_COUNT] : 4, 1, 4);
                    nuni++;
                    if (!un)
                        un = k;
                    if (cnt == 4u && p.raw[P5_RETRIGGER] < 2u && !lowp)
                        lowp = k;
                    if (cnt == 4u && p.raw[P5_RETRIGGER] >= 2u && !lastp)
                        lastp = k;
                } else if (!po)
                    po = k;
                /* every program through the pool, on both parts: UNISON exactly when the program says so */
                cu_pool_load(0, ENGI_PROPHET, k);
                cu_pool_load(1, ENGI_PROPHET, k);
                wrong += trk[0].p[P_VOICE] != (p.raw[P5_UNISON] ? V_UNISON : V_POLY) || p5_own[0] != V_POLY;
                wrong += trk[1].p[P_VOICE] != (p.raw[P5_UNISON] ? V_UNISON : V_MONO) || p5_own[1] != V_MONO;
            }
            cu_pool_load(1, ENGI_VA, 1);
            cu_pool_load(0, ENGI_PROPHET, 0);
            OK(!wrong && trk[0].p[P_VOICE] == V_POLY,
               "unison on load: each of the %u shipped programs makes the part UNISON exactly when its switch is on (%u "
               "of them), on the chord part (else POLY) and on the bass part (else MONO); INIT is POLY",
               (unsigned)P5_FACTORY_N, (unsigned)nuni);
            if (un && po) {
                /* a four-note chord on a UNISON program: ONE note on the stacked voices */
                for (g = 0; g < 2u; g++) {
                    uint32_t pr = g ? lastp : lowp, nv = 0, note = 0, mixed = 0;
                    if (!pr)
                        continue;
                    cu_pool_load(0, ENGI_PROPHET, pr);
                    t->engine = t->eng_req;
                    t->xf_on = t->xf = 0;
                    events_block(CTL);
                    for (i = 0; i < 4u; i++)
                        trk_note_on(t, CH4[i], 100);
                    for (b = 0; b < 8u; b++) {
                        track_render(t, out, CTL);
                        events_block(CTL);
                    }
                    for (i = 0; i < NVOICE; i++)
                        if (t->v[i].active && t->v[i].gate) {
                            nv++;
                            mixed += note && t->v[i].note != note;
                            note = t->v[i].note;
                        }
                    OK(nv == 4u && !mixed && note == (g ? 73u : 62u) && voices_busy_of(&trk[1]) == 16u && p5_cap(t) == 4u,
                       "unison on load: a 4-note chord (D4 F#4 A4 C#5) on %s: %u voices, all on %s (the program's key "
                       "priority: %s), %u of the 16 budget units", P5_TEST_PRESETS[pr].name, (unsigned)nv,
                       note == 62u ? "D4, the chord's lowest note" : note == 73u ? "C#5, the last note sent" : "another note",
                       g ? "LAST" : "LOW", (unsigned)voices_busy_of(&trk[1]));
                    if (!g) {                          /* with a bass note on one core: three of the four stack */
                        trk[1].engine = trk[1].eng_req;
                        trk[1].xf_on = trk[1].xf = 0;
                        for (i = 0; i < 4u; i++)
                            trk_note_off(t, CH4[i]);
                        for (b = 0; b < 2u * FS / CTL && busy(t); b++)
                            track_render(t, out, CTL);
                        trk_note_on(&trk[1], 38, 100);
                        for (i = 0; i < 4u; i++)
                            trk_note_on(t, CH4[i], 100);
                        for (b = 0; b < 8u; b++) {
                            track_render(t, out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        for (i = 0, nv = 0; i < NVOICE; i++)
                            nv += t->v[i].active && t->v[i].gate && t->v[i].stage != 4u;
                        OK(nv == 3u && busy(&trk[1]) == 1u,
                           "unison on load: with a VA bass note sounding (2 units) the chord's one note has three voices");
                        trk_note_off(&trk[1], 38);
                        for (b = 0; b < 4u * FS / CTL && busy(&trk[1]); b++)
                            track_render(&trk[1], out, CTL);
                    }
                    /* held keys, then the next program (poly): the load releases them, nothing hangs */
                    cu_pool_load(0, ENGI_PROPHET, po);
                    events_block(CTL);
                    for (i = 0; i < NVOICE; i++)
                        hang += t->v[i].gate;
                    hang += t->p[P_VOICE] != V_POLY || t->nmono || t->mono_note;
                    for (i = 0; i < 4u; i++)
                        trk_note_off(t, CH4[i]);      /* (the keys come up after the load) */
                    for (b = 0; b < 100u * FS / CTL && busy(t); b++) {
                        track_render(t, out, CTL);
                        events_block(CTL);
                    }
                    hang += busy(t);
                }
                /* .. and the other way: a poly chord held, a UNISON program loaded */
                cu_pool_load(0, ENGI_PROPHET, po);
                events_block(CTL);
                for (i = 0; i < 4u; i++)
                    trk_note_on(t, CH4[i], 100);
                for (b = 0; b < 8u; b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
                bad = busy(t) != 4u;
                cu_pool_load(0, ENGI_PROPHET, un);
                events_block(CTL);
                for (i = 0; i < NVOICE; i++)
                    hang += t->v[i].gate;
                for (i = 0; i < 4u; i++)
                    trk_note_off(t, CH4[i]);
                for (b = 0; b < 100u * FS / CTL && busy(t); b++) {
                    track_render(t, out, CTL);
                    events_block(CTL);
                }
                hang += busy(t);
                OK(!bad && !hang && t->p[P_VOICE] == V_UNISON,
                   "unison on load: a load over held keys releases them both ways (UNISON -> POLY: the part is POLY, no "
                   "mono stack left; POLY -> UNISON): no voice hangs");
                /* the editor's cell on a loaded UNISON program, and back */
                P5_DEEP.set(t, 17, 0, 0);
                bad = t->p[P_VOICE] != V_POLY;
                P5_DEEP.set(t, 17, 0, 1);
                OK(!bad && t->p[P_VOICE] == V_UNISON, "unison on load: the editor's UNISON cell turns it off (POLY) and on again");
                /* the engine changes: a load of another engine's sound, and the editor's engine switch (the mix kept) */
                cu_pool_load(0, ENGI_VA, 1);
                bad = trk[0].p[P_VOICE] != V_POLY;
                cu_pool_load(0, ENGI_PROPHET, un);
                cu_engine_switch(0, ENGI_VA);
                bad += trk[0].p[P_VOICE] != V_POLY || trk[0].eng_req != ENGI_VA;
                cu_pool_load(1, ENGI_PROPHET, un);
                cu_engine_switch(1, ENGI_VA);
                bad += trk[1].p[P_VOICE] != V_MONO;
                cu_pool_load(0, ENGI_PROPHET, po);
                trk[0].p[P_VOICE] = V_UNISON;          /* (the MIX page's VOICE on a program without the switch) */
                cu_engine_switch(0, ENGI_VA);
                OK(!bad && trk[0].p[P_VOICE] == V_UNISON,
                   "unison on load: another engine gets the part as it expects it (a pool load: POLY; the editor's engine "
                   "switch from a UNISON program: POLY, the bass MONO); a UNISON set on the MIX page stays, as between any two engines");
                trk[0].p[P_VOICE] = V_POLY;
                /* a UNISON program on the BASS part, one core: its note's stack and a chord share the 16 units. The bass
                 * note first, then the chord's four notes; then the chord first, then the bass note */
                if (lowp) {
                    uint32_t cb[2], cc[2];
                    for (g = 0; g < 2u; g++) {
                        cu_pool_load(0, ENGI_PROPHET, po);
                        cu_pool_load(1, ENGI_PROPHET, lowp);
                        trk[0].engine = trk[0].eng_req;
                        trk[1].engine = trk[1].eng_req;
                        trk[0].xf_on = trk[0].xf = trk[1].xf_on = trk[1].xf = 0;
                        events_block(CTL);
                        if (!g)
                            trk_note_on(&trk[1], 38, 100);
                        for (b = 0; b < 4u; b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        for (i = 0; i < 4u; i++)
                            trk_note_on(&trk[0], CH4[i], 100);
                        for (b = 0; b < 4u; b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        if (g)
                            trk_note_on(&trk[1], 38, 100);
                        for (b = 0; b < 12u; b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                        for (i = 0, cc[g] = cb[g] = 0; i < NVOICE; i++) {
                            cc[g] += trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].stage != 4u;
                            cb[g] += trk[1].v[i].active && trk[1].v[i].gate && trk[1].v[i].stage != 4u;
                        }
                        trk_note_off(&trk[1], 38);
                        for (i = 0; i < 4u; i++)
                            trk_note_off(&trk[0], CH4[i]);
                        for (b = 0; b < 100u * FS / CTL && (busy(&trk[0]) || busy(&trk[1])); b++) {
                            track_render(&trk[0], out, CTL);
                            track_render(&trk[1], out, CTL);
                            events_block(CTL);
                        }
                    }
                    OK(cc[0] + cb[0] == 4u && cc[1] + cb[1] == 4u && cb[0] >= 1u && cb[1] >= 1u,
                       "unison on load: a UNISON bass (%s) with a 4-note chord on one core: four voices between the two "
                       "parts. The bass note first: the bass has %u voices, the chord %u of its notes; the chord first: "
                       "the chord %u, the bass %u", P5_TEST_PRESETS[lowp].name, (unsigned)cb[0], (unsigned)cc[0],
                       (unsigned)cc[1], (unsigned)cb[1]);
                    cu_pool_load(1, ENGI_VA, 1);
                    trk[1].engine = trk[1].eng_req;
                }
                /* the session's record: the mode as it was left */
                cu_pool_load(0, ENGI_PROPHET, un);
                memset(&rec, 0, sizeof rec);
                css_part_get(0, &rec);
                cu_pool_load(0, ENGI_FM6, 1);
                bad = !css_part_put(0, &rec) || trk[0].p[P_VOICE] != V_UNISON || p5_own[0] != V_POLY || cu_part_pos(0) != un;
                P5_DEEP.set(&trk[0], 17, 0, 0);        /* UNISON turned off in the editor, then the power cycle */
                memset(&rec, 0, sizeof rec);
                css_part_get(0, &rec);
                cu_pool_load(0, ENGI_FM6, 1);
                bad += !css_part_put(0, &rec) || trk[0].p[P_VOICE] != V_POLY || p5_patch_of(&trk[0])->raw[P5_UNISON] != 0;
                cu_pool_load(1, ENGI_PROPHET, un);
                memset(&rec, 0, sizeof rec);
                css_part_get(1, &rec);
                cu_pool_load(1, ENGI_VA, 1);
                bad += !css_part_put(1, &rec) || trk[1].p[P_VOICE] != V_UNISON || p5_own[1] != V_MONO;
                P5_DEEP.set(&trk[1], 17, 0, 0);
                bad += trk[1].p[P_VOICE] != V_MONO;
                cu_pool_load(1, ENGI_VA, 1);
                trk[1].engine = trk[1].eng_req;
                OK(!bad, "unison on load: the session's record brings the part back as it was left (a UNISON program: "
                   "UNISON; one with UNISON turned off in the editor: POLY; the bass part: UNISON over MONO)");
            } else
                printf("     (the shipped list has no UNISON program or no poly one: the chord checks are skipped)\n");
            part_set(t, 0);
        }
#endif
        /* glide */
        p5_patch_init(&p);
        p.raw[P5_GLIDE] = 90;
        part_set(t, &p);
        trk_note_on(t, 48, 100);
        for (b = 0; b < 400u; b++)
            track_render(t, out, CTL);
        trk_note_off(t, 48);
        trk_note_on(t, 72, 100);
        for (b = 0; b < 4000u; b++) {
            track_render(t, out, CTL);
            for (i = 0; i < P5_POLY; i++)
                if (t->v[i].gate && b == 20u)
                    mid = (uint32_t)p5_part(0)->voice[i].glide_pitch;
        }
        for (i = 0, k = 0; i < P5_POLY; i++)
            if (t->v[i].gate)
                k = (uint32_t)p5_part(0)->voice[i].glide_pitch;
        OK(mid > 48u * 16u && mid < 72u * 16u && k == 72u * 16u,
           "glide: GLIDE 90, C2 then C4: the new voice starts at the last key's pitch (%.1f after 15 ms) and arrives", mid / 16.0);
    }

    /* ---- the pitch bend, as the part's MIDI brings it (cr_out.c midi_event -> voice.c -> vmod_t) ---- */
    {
        double f0, f1, f2;
        p5_patch_init(&p);
        p.raw[P5_CUTOFF] = 30;                         /* (a saw's fundamental: one rising crossing a cycle) */
        part_set(t, &p);
        trk_note_on(t, 69, 100);
        freq(t, 200);
        f0 = freq(t, 1400);
        midi_event(0xE0, 0, 0x7F, 0x7F);               /* bend full up: the channel's range, +2 semitones */
        freq(t, 200);
        f1 = freq(t, 1400);
        midi_event(0xE0, 0, 0x00, 0x00);               /* full down */
        freq(t, 200);
        f2 = freq(t, 1400);
        midi_event(0xE0, 0, 0x00, 0x40);
        OK(fabs(f0 - 440.0) < 1.0 && fabs(f1 - 440.0 * pow(2, 2.0 / 12)) < 1.2 && fabs(f2 - 440.0 * pow(2, -2.0 / 12)) < 1.2,
           "bend: A4 %.2f Hz, bent full up %.2f (+2 semitones: 493.88), full down %.2f (391.99)", f0, f1, f2);
        freq(t, 200);
        f0 = freq(t, 700);
        OK(fabs(f0 - 440.0) < 1.0, "bend: back at the centre %.2f Hz", f0);
    }

    /* ---- the deep pages ---- */
    {
        uint8_t seen[P5_RAW_MAX] = {0};
        uint32_t pg, c, ncol = 0, only = 0, clampb = 0, rd = 0;
        const eng_deep_t *d = &P5_DEEP;
        p5_patch_init(&p);
        part_set(t, &p);
        for (pg = 0, bad = 0; pg < d->npages; pg++)
            for (c = 0; c < 4u; c++) {
                const param_desc_t *cd = &d->pages[pg].col[c];
                int32_t id = p5_dref(pg, c), v[5], j;
                if (!cd->label) {
                    bad += id >= 0 || d->get(t, pg, c) != 0;
                    p5_patch_put(t, &p);
                    d->set(t, pg, c, 1);
                    bad += memcmp(p5_patch_of(t), &p, sizeof p) != 0;
                    continue;
                }
                ncol++;
                bad += id < 0 || id >= (int32_t)P5_RAW_MAX || strlen(cd->label) > 7u || cd->min > cd->max ||
                       cd->def < cd->min || cd->def > cd->max;
                if (id < 0)
                    continue;
                seen[id]++;
                v[0] = cd->min;
                v[1] = cd->max;
                v[2] = (cd->min + cd->max) / 2;
                v[3] = cd->min - 5;
                v[4] = cd->max + 300;
                for (j = 0; j < 5; j++) {
                    int32_t want = clamp(v[j], cd->min, cd->max);
                    p5_patch_put(t, &p);
                    d->set(t, pg, c, v[j]);
                    q = *p5_patch_of(t);
                    clampb += q.raw[id] != (uint8_t)want;
                    rd += d->get(t, pg, c) != want;
                    q.raw[id] = p.raw[id];
                    only += memcmp(&q, &p, sizeof p) != 0;
                }
            }
        for (k = 0, i = 0; k < P5_RAW_MAX; k++) {
            i += seen[k] > 1u;
            bad += (k < 55u || k == P5_RETRIGGER) ? seen[k] != 1u : seen[k] != 0u;
        }
        OK(d->npages == 19u && ncol == 56u && !bad && !i && d->section[0] == 0 && d->section[1] == 5 && d->section[2] == 7 &&
           d->section[3] == 11 && d->section[4] == 15 && d->section[5] == 0xFF,
           "pages: 19 pages in five sections, 56 columns: each of the 55 panel bytes and PRIORITY on exactly one (BEND on none)");
        OK(!only && !clampb && !rd, "pages: a column sets its byte and no other, clamped to its range, and reads it back");
        /* the names the editor's rules read (cr_edit.c): the filter's curve, the two ADSRs, the + pages */
        OK(!strcmp(d->pages[5].title, "FILTER") && cp_dcol(&d->pages[5], "CUT") == 0 && cp_dcol(&d->pages[5], "RES") == 1 &&
           !strcmp(d->pages[7].title, "ENV 1") && cp_dcol(&d->pages[7], "ATK") == 0 && cp_dcol(&d->pages[7], "REL") == 3 &&
           P5_DKEY[7][0] == P5_ATTACK_AMP && !strcmp(d->pages[9].title, "ENV 2") && P5_DKEY[9][3] == P5_RELEASE_FILTER &&
           ce_plus_of(d, 0, 1) && ce_plus_of(d, 2, 3) && ce_plus_of(d, 11, 12) && ce_plus_of(d, 15, 16) && ce_deep(t) == d,
           "pages: FILTER has CUT and RES (the curve), ENV 1 is the amplifier's ADSR and ENV 2 the filter's, the + pages pair");
        /* a factory program's switch bytes and VOICES 0 read as what plays */
        p5_patch_init(&p);
        p.raw[P5_SAW_A] = 7;
        p.raw[P5_UNISON_COUNT] = 0;
        p.raw[P5_KEY_FILTER] = 9;
        p5_patch_put(t, &p);
        OK(d->get(t, 0, 1) == 1 && d->get(t, 17, 1) == 4 && d->get(t, 5, 3) == 2 && p5_patch_of(t)->raw[P5_SAW_A] == 7,
           "pages: a switch byte other than 0 reads on, VOICES 0 reads 4, a value past its names reads the last; the bytes stay");
        OK(P5_DEEP.mod_dst(t, 5, 0) == -1, "pages: no matrix destination of its own");
    }

    /* ---- the pool, a load through it, SAVE ---- */
    {
        char nm[13];
        uint32_t n = pool_count(ENGI_PROPHET), used = 0;
        cu_pool_load(0, ENGI_PROPHET, 0);
        p5_patch_init(&p);
        bad = trk[CR_PART_CHORD].eng_req != ENGI_PROPHET || memcmp(p5_patch_of(&trk[CR_PART_CHORD]), &p, sizeof p) != 0 ||
              psnd[0].kind != PK_INIT;
        pool_name(ENGI_PROPHET, 0, nm);
        OK(!bad && pool_f0(ENGI_PROPHET) == 1u && n == NELEM(P5_TEST_PRESETS) && !strcmp(nm, "INIT"),
           "pool: INIT and the %u shipped factory programs", (unsigned)n - 1u);
#if FELUCCA_P5_FACTORY
        {
            uint32_t pos = P5_FACTORY_N >= 3u ? 3u : 1u, last = P5_FACTORY_N;
            cu_pool_load(0, ENGI_PROPHET, pos);
            p5_factory(pos - 1u, &p);
            pool_name(ENGI_PROPHET, pos, nm);
            OK(!memcmp(p5_patch_of(&trk[CR_PART_CHORD]), &p, sizeof p) && !strcmp(nm, P5_TEST_PRESETS[pos].name) &&
               psnd[0].kind == PK_FACTORY && trk[CR_PART_CHORD].preset == pos && cu_part_pos(0) == pos &&
               p5_patch_id(p5_patch_of(&trk[CR_PART_CHORD])) == P5_FACTORY_ID[pos - 1u],
               "pool: position %u loads the list's program there (%s, Sequential's %u) into the chord part", (unsigned)pos,
               nm, (unsigned)P5_FACTORY_ID[pos - 1u]);
            cu_pool_load(1, ENGI_PROPHET, last);
            p5_factory(last - 1u, &p);
            OK(!memcmp(p5_patch_of(&trk[CR_PART_BASS]), &p, sizeof p) && trk[CR_PART_BASS].preset == last &&
               cu_part_pos(1) == last && pool_count(ENGI_PROPHET) == last + 1u,
               "pool: the last position, %u, loads the list's last program (%s, Sequential's %u) into the bass part",
               (unsigned)last, P5_TEST_PRESETS[last].name, (unsigned)P5_FACTORY_ID[last - 1u]);
        }
        cu_pool_load(1, ENGI_VA, 1);
#endif
        cu.page = PG_NONE;
        cu.msg.text[0] = 0;
        cu_save_open(0);
        for (k = 0; k < UP_SLOTS; k++)
            used += (uint32_t)up_used(k);
        OK(cu.page == PG_NONE && !strcmp(cu.msg.text, "P5: no save yet") && !used,
           "save: refused on a P5 part with a message (\"P5: no save yet\"), no record written (docs/PROPHET.md \"Saving\")");
        cu_pool_load(0, ENGI_FM6, 1);
        cu_save_open(0);
        OK(cu.page == PG_SAVE, "save: another engine's part opens the dialog as before");
        cu.page = PG_NONE;
    }

    printf("%s: %d checks, %d failed\n", fails ? "cr_prophet_test FAILED" : "cr_prophet_test ok", checks, fails);
    return fails != 0;
}
