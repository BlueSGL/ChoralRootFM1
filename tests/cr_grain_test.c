/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of GRAIN's source generator (firmware/src/grain_src.c, docs/GRAIN.md step 1), no HAL: the generator on
 * core.h, dsp.c and felucca_tables.h.
 *   sh tests/run_cr_tests.sh          (or: cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -Ibuild/gen
 *                                      -o build/host/cr_grain_test tests/cr_grain_test.c -lm && build/host/cr_grain_test)
 * Checks: every family at a grid of TONE x MOTION x SEED (800 renders) renders a whole buffer whose peak is GS_PEAK
 * (within the Q12 gain's rounding, or below it when the gain capped at 8), the mean under 1 % of full scale (the
 * finish pass's high-pass leaves what drifts slower than 20 Hz: VOWEL's formants settling, up to 0.6 %), RMS above 1 %
 * (not silent), the same bytes
 * twice (deterministic), and the same bytes in slices of 512 as in one call; slices of one sample for one setting a
 * family; the root pitch a family reports; the knobs' masking (TYPE wraps, TONE / MOTION / SEED take seven bits);
 * eight seeds a family give eight different buffers; the 24 sketch settings (tools/grain_sketch.c SRC_SET) hash to
 * the goldens of tests/cr_grain_golden.txt (GOLDEN_UPDATE=1 rewrites it: on purpose, review the diff) and, where
 * build/grain-sketch/src-*.wav exists, equal the WAV heard byte for byte; the host time a render (information). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpedantic"
#define __attribute__(x)
#define FELUCCA_VA 1
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#include "felucca_tables.h"
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/grain_src.c"

static int fails, checks;
#define CHECK(c, ...)                                   \
    do {                                                \
        checks++;                                       \
        if (!(c)) {                                     \
            fails++;                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static int16_t bufa[GS_N], bufb[GS_N];

static uint64_t fnv(const int16_t *b, uint32_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint8_t)b[i];
        h *= 0x100000001b3ull;
        h ^= (uint8_t)(b[i] >> 8);
        h *= 0x100000001b3ull;
    }
    return h;
}
/* a whole render, in one call or in slices of `slice` samples; returns gs_finish's gain */
static int32_t render(int16_t *buf, gs_t *g, uint32_t type, uint32_t tone, uint32_t motion, uint32_t seed, uint32_t slice)
{
    gs_begin(g, type, tone, motion, seed);
    while (gs_step(g, buf, slice) < GS_N)
        ;
    return gs_finish(buf);
}
static void stats(const int16_t *b, int32_t *peak, double *dc, double *rms)
{
    uint32_t i;
    double sum = 0, sq = 0;
    int32_t pk = 0;
    for (i = 0; i < GS_N; i++) {
        int32_t a = b[i] < 0 ? -b[i] : b[i];
        pk = a > pk ? a : pk;
        sum += b[i];
        sq += (double)b[i] * b[i];
    }
    *peak = pk;
    *dc = sum / GS_N;
    *rms = sqrt(sq / GS_N);
}

/* ---- 1. the grid: whole, deterministic, sliced, the levels */
static void t_grid(void)
{
    static const uint8_t TONES[5] = {0, 28, 64, 104, 127}, MOTIONS[5] = {0, 20, 64, 104, 127}, SEEDS[4] = {0, 3, 11, 127};
    uint32_t ty, a, b, c, n = 0, capped = 0;
    gs_t g;
    for (ty = 0; ty < GS_NTYPE; ty++)
        for (a = 0; a < 5; a++)
            for (b = 0; b < 5; b++)
                for (c = 0; c < 4; c++) {
                    int32_t gain = render(bufa, &g, ty, TONES[a], MOTIONS[b], SEEDS[c], GS_N), peak;
                    double dc, rms;
                    stats(bufa, &peak, &dc, &rms);
                    n++;
                    if (gain == 8 * 4096) {
                        capped++;
                        CHECK(peak <= GS_PEAK, "%s %u/%u/%u: capped gain, peak %d", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c], peak);
                    } else
                        CHECK(peak >= GS_PEAK - 8 && peak <= GS_PEAK, "%s %u/%u/%u: peak %d (gain %d)", GS_TYPE_NAMES[ty],
                              TONES[a], MOTIONS[b], SEEDS[c], peak, gain);
                    CHECK(fabs(dc) < 328.0, "%s %u/%u/%u: DC %.1f", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c], dc);
                    CHECK(rms > 328.0, "%s %u/%u/%u: RMS %.1f (silent?)", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c], rms);
                    render(bufb, &g, ty, TONES[a], MOTIONS[b], SEEDS[c], GS_N);
                    CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s %u/%u/%u: not deterministic", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c]);
                    render(bufb, &g, ty, TONES[a], MOTIONS[b], SEEDS[c], 512u);
                    CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s %u/%u/%u: slices of 512 differ", GS_TYPE_NAMES[ty], TONES[a], MOTIONS[b], SEEDS[c]);
                }
    printf("grid: %u renders, %u with the gain capped at 8\n", n, capped);
    for (ty = 0; ty < GS_NTYPE; ty++) {                      /* slices of one sample */
        render(bufa, &g, ty, 64, 64, 0, GS_N);
        render(bufb, &g, ty, 64, 64, 0, 1u);
        CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s: slices of 1 differ", GS_TYPE_NAMES[ty]);
        CHECK(g.pos == GS_N && gs_step(&g, bufb, 100u) == GS_N, "%s: a step past the end", GS_TYPE_NAMES[ty]);
    }
}

/* ---- 2. the roots, the knobs' masking, the seeds */
static void t_knobs(void)
{
    uint32_t ty, s;
    gs_t g;
    for (ty = 0; ty < GS_NTYPE; ty++) {
        uint64_t h[8];
        render(bufa, &g, ty, 64, 64, 0, GS_N);
        CHECK(g.root16 == (ty == GT_PLUCK ? GS_PLUCK_ROOT16 : GS_ROOT16), "%s: root16 %d", GS_TYPE_NAMES[ty], g.root16);
        render(bufb, &g, ty + GS_NTYPE, 64 + 128, 64 + 128, 0 + 128, GS_N);   /* TYPE wraps, the others take 7 bits */
        CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s: the knobs' masking", GS_TYPE_NAMES[ty]);
        for (s = 0; s < 8; s++) {
            uint32_t k;
            render(bufb, &g, ty, 64, 64, s, GS_N);
            h[s] = fnv(bufb, GS_N);
            for (k = 0; k < s; k++)
                CHECK(h[k] != h[s], "%s: seeds %u and %u render the same", GS_TYPE_NAMES[ty], k, s);
        }
    }
    gs_begin(&g, 0, 0, 0, 0);
    CHECK(g.pos == 0 && gs_step(&g, bufa, 0) == 0, "an empty step");
}

/* ---- 3. the sketch settings' goldens, and the WAVs heard */
static const struct { const char *name; uint8_t tone, motion, seed; } SRC_SET[3] = {
    {"mid", 64, 64, 0}, {"dark-slow", 28, 20, 3}, {"bright-fast", 104, 104, 11}};
static int wav_data(const char *path, int16_t *out, uint32_t n)   /* the WAV's samples, 1 = read */
{
    FILE *f = fopen(path, "rb");
    uint8_t hdr[44];
    if (!f)
        return 0;
    if (fread(hdr, 1, 44, f) != 44 || memcmp(hdr, "RIFF", 4) || fread(out, 2, n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 1;
}
static void t_golden(void)
{
    const char *gpath = "tests/cr_grain_golden.txt";
    int update = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    char line[128], key[64], gold[25 * 64][64];
    uint64_t gh[25 * 64];
    uint32_t ng = 0, ty, s, i, heard = 0;
    gs_t g;
    FILE *f = update ? fopen(gpath, "w") : fopen(gpath, "r");
    CHECK(f != 0, "%s: cannot open", gpath);
    if (!f)
        return;
    if (!update)
        while (fgets(line, sizeof line, f) && ng < 25 * 64)
            if (sscanf(line, "%63s %llx", gold[ng], (unsigned long long *)&gh[ng]) == 2)
                ng++;
    for (ty = 0; ty < GS_NTYPE; ty++)
        for (s = 0; s < 3; s++) {
            uint64_t h;
            char fam[16], path[128];
            render(bufa, &g, ty, SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed, 512u);
            h = fnv(bufa, GS_N);
            snprintf(key, sizeof key, "%s/%u/%u/%u", GS_TYPE_NAMES[ty], SRC_SET[s].tone, SRC_SET[s].motion, SRC_SET[s].seed);
            if (update)
                fprintf(f, "%s %016llx\n", key, (unsigned long long)h);
            else {
                for (i = 0; i < ng && strcmp(gold[i], key); i++)
                    ;
                CHECK(i < ng, "%s: no golden", key);
                if (i < ng)
                    CHECK(gh[i] == h, "%s: %016llx, golden %016llx", key, (unsigned long long)h, (unsigned long long)gh[i]);
            }
            for (i = 0; GS_TYPE_NAMES[ty][i]; i++)
                fam[i] = (char)(GS_TYPE_NAMES[ty][i] | 32);
            fam[i] = 0;
            snprintf(path, sizeof path, "build/grain-sketch/src-%s-%s.wav", fam, SRC_SET[s].name);
            if (wav_data(path, bufb, GS_N) == 1) {
                heard++;
                CHECK(!memcmp(bufa, bufb, sizeof bufa), "%s: not the WAV heard (%s)", key, path);
            }
        }
    fclose(f);
    printf("goldens: %s%s; %u of 24 sketch WAVs present and compared\n", update ? "written to " : "", gpath, heard);
}

/* ---- 4. the host time a render (information) */
static void t_time(void)
{
    uint32_t ty, k;
    gs_t g;
    printf("host ms a render:");
    for (ty = 0; ty < GS_NTYPE; ty++) {
        clock_t t0 = clock();
        for (k = 0; k < 20; k++)
            render(bufa, &g, ty, 64, 64, k, 1024u);
        printf(" %s %.2f", GS_TYPE_NAMES[ty], 1000.0 * (clock() - t0) / CLOCKS_PER_SEC / 20);
    }
    printf("\n");
}

int main(void)
{
    t_grid();
    t_knobs();
    t_golden();
    t_time();
    printf("cr_grain_test: %d checks, %d failed%s\n", checks, fails, fails ? "" : " ok");
    return fails ? 1 : 0;
}
