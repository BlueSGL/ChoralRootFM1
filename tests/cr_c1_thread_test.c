/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The bass part's job on a REAL second thread (HISTORY 3y45, docs/DUALCORE.md "Two threads on the host"): the host's
 * stub core runs core 1's job inline, so until this test nothing on the host could show the two cores writing one
 * thing. Here fx.c's mix_parts_split hands part 1's render to a second thread (cr_cpu1.h cr_c1_test_thread) that runs
 * it while this one renders part 0, as the unit's two cores do, on the firmware as the emulator builds it (ALL 200 P5
 * programs when built with -Ibuild/host/p5_all first).
 *   build/host/cr_c1_thread_test          every pair of engines on the two parts, a phrase with steals, a UNISON bass,
 *                                         the wheel, the bend and a sound change under held notes: the output with
 *                                         the job on a thread must be the inline job's, every sample (each run in a
 *                                         child process: the same start)
 *   build/host/cr_c1_thread_tsan --tsan   the same phrases with the job on the thread, in this process, built with
 *                                         -fsanitize=thread: the sanitizer names every word both threads touch
 *                                         without a lock between them (tests/run_cr_tests.sh fails on any report)
 * What it cannot show: the unit's timing, its caches and its flash (a core that reads a word wrong: cr_cpu1.h's
 * validation), an interrupt on core 0 during the job, the main loop writing a parameter under both. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
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
#ifdef C1_PLANT                 /* the proof that the sanitizer sees this test's two threads: a word that both parts'
                                 * P5 renders write (eng_prophet_test.c's tap), planted; run_cr_tests.sh wants a report */
static volatile int c1_plant;
#define P5_TAP(k, x) (c1_plant += (int)(x))
#endif
#include "../tools/emu/emu_firmware.h"

static int fails;
typedef struct { uint8_t ce, be; uint16_t cpos, bpos; const char *what; } pair_t;
static uint32_t rs, vmax[2];
static uint32_t rnd(uint32_t n)
{
    rs = rs * 1664525u + 1013904223u;
    return (rs >> 8) % n;
}
/* a phrase on both parts: chords of five and six notes on the chord part (more than P5's four voices: steals, the
 * fade and the deferred note), bass notes under them on part 1 (a UNISON program: the stack), the wheel, the
 * pressure, a bend on both, a sound change on each part under held notes. Returns the digest of every sample */
static uint64_t phrase(const pair_t *p, uint32_t nb, int thread)
{
    int32_t out[2u * CTL];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i, k;
    uint8_t held[6] = {0}, bass = 0;
    rs = 0x1234567u + p->ce * 131u + p->be * 17u + p->cpos;
    vmax[0] = vmax[1] = 0;
    cr_dbg.c1 = C1_OK;
    cr_c1_test_thread = (uint8_t)thread;
    cu_pool_load(0, p->ce, p->cpos);
    cu_pool_load(1, p->be, p->bpos);
    for (b = 0; b < nb; b++) {
        if (!(b % 37u)) {                               /* a new chord over the old one's notes */
            for (i = 0; i < 6u; i++)
                if (held[i] && rnd(3)) {
                    trk_note_off(&trk[0], held[i]);
                    held[i] = 0;
                }
            for (i = 0; i < 6u; i++)
                if (!held[i] && rnd(4)) {
                    held[i] = (uint8_t)(48u + rnd(30));
                    trk_note_on(&trk[0], held[i], 60u + rnd(67));
                }
        }
        if (!(b % 53u)) {
            if (bass)
                trk_note_off(&trk[1], bass);
            bass = rnd(5) ? (uint8_t)(28u + rnd(20)) : 0u;
            if (bass)
                trk_note_on(&trk[1], bass, 60u + rnd(67));
        }
        if (!(b % 11u)) {
            trk[0].mw = (uint8_t)rnd(128);
            trk[1].mw = (uint8_t)rnd(128);
            trk[rnd(2)].at = (uint8_t)rnd(128);
            midi_bend_target[rnd(2)] = (int32_t)rnd(512) - 256;
        }
        if (b == nb / 2u)                               /* another sound of each engine under the held notes */
            for (k = 0; k < 2u; k++)
                cu_pool_load(k, k ? p->be : p->ce, 1u + rnd(pool_count(k ? p->be : p->ce) - 1u));
        mix_block(out, CTL);
        for (k = 0; k < 2u; k++) {
            uint32_t n = 0;
            for (i = 0; i < NVOICE; i++)
                n += trk[k].v[i].active;
            if (n > vmax[k])
                vmax[k] = n;
        }
        for (i = 0; i < 2u * CTL; i++)
            for (k = 0; k < 4u; k++)
                h = (h ^ (((uint32_t)out[i] >> (8u * k)) & 255u)) * 1099511628211ull;
    }
    return h;
}
static uint64_t fork_phrase(const pair_t *p, uint32_t nb, int thread)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    pid = fork();
    if (!pid) {
        h = phrase(p, nb, thread);
        (void)!write(fd[1], &h, sizeof h);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

int main(int argc, char **argv)
{
    int tsan = argc >= 2 && !strcmp(argv[1], "--tsan");
    uint32_t i, uni = 0, k, nb = tsan ? 900u : 3000u;
    pair_t P[16];
    uint32_t np = 0;
    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    for (k = 1; k < pool_count(ENGI_PROPHET) && !uni; k++) {   /* a P5 program with the UNISON switch: the bass's stack */
        p5_patch_t q;
        p5_preset_patch(pool_entry(ENGI_PROPHET, k).fk, &q);
        if (q.raw[P5_UNISON])
            uni = k;
    }
#define PAIR(c, cp, b, bp, w) (P[np].ce = (uint8_t)(c), P[np].cpos = (uint16_t)(cp), P[np].be = (uint8_t)(b), P[np].bpos = (uint16_t)(bp), P[np++].what = (w))
    PAIR(ENGI_PROPHET, 1, ENGI_PROPHET, uni ? uni : 2u, "P5 chord, P5 bass (a UNISON program)");
    PAIR(ENGI_PROPHET, 2, ENGI_PROPHET, 3, "P5 chord, P5 bass");
    PAIR(ENGI_PROPHET, 1, ENGI_VA, 3, "P5 chord, VA bass");
    PAIR(ENGI_PROPHET, 3, ENGI_FM6, 2, "P5 chord, FM6 bass");
    PAIR(ENGI_FM6, 2, ENGI_PROPHET, uni ? uni : 2u, "FM6 chord, P5 bass");
    PAIR(ENGI_VA, 2, ENGI_PROPHET, 4, "VA chord, P5 bass");
    PAIR(ENGI_FM6, 5, ENGI_FM6, 3, "FM6 chord, FM6 bass");
    PAIR(ENGI_VA, 4, ENGI_VA, 2, "VA chord, VA bass");
    PAIR(ENGI_CZ, 3, ENGI_CZ, 5, "CZ-1 chord, CZ-1 bass");
    PAIR(ENGI_QUAD, 2, ENGI_QUAD, 4, "FM TONE chord, FM TONE bass");
    PAIR(ENGI_FM6, 1, ENGI_VA, 1, "FM6 chord, VA bass (the power-on pair's engines)");
    PAIR(ENGI_CZ, 2, ENGI_QUAD, 3, "CZ-1 chord, FM TONE bass");
    printf("cr_c1_thread: the bass part's job on a second thread (%u pairs, %u blocks each%s)\n", (unsigned)np, (unsigned)nb,
           tsan ? "; the sanitizer's run" : "");
    for (i = 0; i < np; i++) {
        if (tsan) {
            uint64_t h = phrase(&P[i], nb, 1);
            int ok = vmax[0] >= 3u && vmax[1] >= 1u && (i || !uni || vmax[1] == P5_POLY) && cr_c1_th_done == (i + 1u) * nb;
            fails += !ok;
            printf("  %s %s: %016llx (voices: chord %u, bass %u; jobs on the thread: %u)\n", ok ? "ran " : "FAIL", P[i].what,
                   (unsigned long long)h, (unsigned)vmax[0], (unsigned)vmax[1], (unsigned)cr_c1_th_done);
        } else {
            uint64_t a = fork_phrase(&P[i], nb, 0), b = fork_phrase(&P[i], nb, 1), c = fork_phrase(&P[i], nb, 1);
            int ok = a && a == b && a == c;
            fails += !ok;
            printf("  %s %s: inline %016llx, on a thread %016llx, again %016llx\n", ok ? "ok  " : "FAIL", P[i].what,
                   (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
        }
    }
    if (fails) {
        printf("cr_c1_thread: %d FAILED\n", fails);
        return 1;
    }
    printf(tsan ? "cr_c1_thread: ran (the sanitizer's verdict is its own: no report above, exit 0)\n"
                : "cr_c1_thread: all tests passed (the job on a thread renders the inline job's samples)\n");
    return 0;
}
