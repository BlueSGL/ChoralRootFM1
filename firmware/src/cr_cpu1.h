/* SPDX-License-Identifier: GPL-3.0-only */
/* ChoralRoot on the FM-1's second core (docs/DUALCORE.md), and the audio ISR's stage profile (cr_dbg).
 * The core's bring-up is hal/fm1_cpu1.{h,S} (after fm1-x0x (GPL-3.0) and Melodee (GPL-3.0), following JieLi's AC79
 * SDK EnableOtherCpu (Apache-2.0)); what runs there, the budgets, the meter and the fail-safes are ChoralRoot's:
 *
 *   power-on (main.c cr_c1_boot, before audio_init; not in SAFE MODE): core 1 started, 20 ms to answer, else held
 *     for the session: cr_dbg.c1 = C1_FAILED, single core, the shared voice budget;
 *   each audio block (fx.c mix_block, cr_c1_split): core 0 does everything that touches shared state first (the
 *     events, part 1's voice allocation and stealing, both parts' matrix and LFO), hands part 1's render to core 1
 *     (one job: track_render of trk[1] into its own buffer), renders part 0, waits, then mixes every part in the
 *     order of one core, so the samples are one core's bit for bit;
 *   a job not done in 8 ms (fm1_cpu1_wait): core 1 held for the session, core 0 renders part 1 itself,
 *     cr_dbg.c1 = C1_GAVE_UP, the shared budget again.
 * Reliability (docs/DUALCORE.md, after Melodee PR 21 "fix(dual-core): raise the core supply and survive CPU1
 * misreads, faults and stalls", Kerem Kilic / Melodee, GPL-3.0): with Dual Core On the core rails go up at power-on
 * (hal/fm1_power.h: SYSVDD 14 = 1.35 V, VDC14 4 = 1.45 V) before core 1 starts; core 1 runs only a request it read
 * right (hal/fm1_cpu1.h fm1_c1_poll; a misread is counted, never run); a fault about core 1 holds it and core 0 goes
 * on (fm1_c1_fault_c). The escalation (cr_c1_assess): a first misread is counted and core 1 keeps working; a second
 * misread (a later block that sees new ones), or any fault or stall: C1_GAVE_UP, cr_dbg.c1_fallback = 1. The rails
 * are never raised past 14 / 4 (the user's decision after Charles Vestal's review of the PR: device safety).
 * While core 1 is alive (C1_OK) each of parts 0 and 1 has its own budget of VBUDGET units (voice.c voices_busy).
 * CR_CPU1 0 (felucca.c, tests/hostsim.c, tests/regress.c): none of this, the code as before. The emulator builds it
 * with 1 and a stub core that runs the job at once, when handed (before part 0: the split must not depend on the
 * order of the two renders). */
#pragma once
#include <stdint.h>
#ifndef CR_CPU1
#define CR_CPU1 0
#endif
#ifndef CR_CORE1_TEST
#define CR_CORE1_TEST 0                  /* 1: SysEx 75 CORE1_TEST injects core 1 failures (tools/core1_fault_test.py,
                                          * cr_backup.c); a test build only, never a release */
#endif

enum { C1_OFF, C1_OK, C1_FAILED, C1_GAVE_UP };
/* the audio ISR's stages: the engine tick (cr_out.c: the engine, the looper, the events, the voices' allocation),
 * part 0's render, part 1's (core 1's job time when it renders it), the rest of the mix (the parts' dist, level,
 * sends, parts 2..3, the FX buses), the master (limiter, the click, the USB tap), core 0's wait for core 1 */
enum { CRP_TICK, CRP_PART0, CRP_PART1, CRP_FX, CRP_MASTER, CRP_WAIT, CRP_N };
typedef struct {
    uint8_t c1;                      /* C1_*: off (CR_CPU1 0, SAFE MODE, Options > Dual Core Off), ok, failed at start, gave up on a job */
    uint8_t win_reset;               /* audio.c cpu_window took the window: the ISR starts a new one */
    uint8_t prof_on;                 /* the stage counters run (device: always; emulator: EMU_STAGES=1) */
    uint32_t c1_jobs;                /* jobs core 1 rendered */
    uint32_t c1_stack;               /* bytes of core 1's stack used at most (its "C1MK" fill, cr_c1_stack_used) */
    uint32_t c1_start_us;            /* how long core 1 took to answer at power-on */
    uint8_t c1_fallback;             /* 1: core 1 was given up this session (a second misread, a fault, a stall); RAM only */
    uint8_t c1_misreads;             /* audio blocks that found new misreads (cr_c1_assess): the first is forgiven */
    uint8_t rail_boot[2], rail_now[2];   /* SYSVDD, VDC14 levels: the boot loader's, now (hal/fm1_power.h; host 11, 3) */
    uint32_t mark;                   /* the last stage mark (prof units) */
    uint32_t cur[CRP_N];             /* this half buffer's stages (prof units, its blocks summed) */
    uint32_t win_sum[CRP_N], win_max[CRP_N], win_n;   /* the window being filled */
    uint32_t avg_us[CRP_N], max_us[CRP_N];             /* the last full second (console `cpu`, GEEK OUT) */
    uint64_t tot_sum[CRP_N];         /* since power-on (the emulator's summary, tools/emu/perf.sh) */
    uint32_t tot_max[CRP_N], tot_n;
} cr_dbg_t;
static cr_dbg_t cr_dbg;
/* Options > Dual Core Off (settings v11, read by cr_settings_boot before cr_c1_boot): core 1 is never started this
 * power-on, cr_dbg.c1 stays C1_OFF ("c1 off"), the shared budget. A change in Options waits for the next power-on */
static uint8_t cr_c1_off;
static const char *const CRP_NAME[CRP_N] = {"tick", "part0", "part1", "fx", "master", "wait"};

#if CR_CPU1
/* ------------------------------------------------ the platform: the core and the clock --- */
#ifdef FM1_HAVE_CPU1                 /* the device (hal/fm1_cpu1.h, included before core.h by choralroot.c) */
#define CR_PROF_PER_US FM1_TICKS_PER_US   /* TIMER4 ticks (24 MHz) */
static inline uint32_t cr_prof_now(void) { return fm1_ticks(); }
static int cr_c1_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
static int cr_c1_wait_core(void) { return fm1_cpu1_wait(); }
static void cr_c1_hold(void) { fm1_cpu1_hold(); }
static inline uint32_t cr_c1_rejected(void) { return fm1_c1_rejected; }
static inline uint32_t cr_c1_faults(void) { return fm1_c1_fault.count; }
static void cr_rails_record(void)                   /* the boot loader's levels (P33 reads only) */
{
    fm1_power_record();
    cr_dbg.rail_boot[0] = fm1_power.boot[FM1_RAIL_SYSVDD];
    cr_dbg.rail_boot[1] = fm1_power.boot[FM1_RAIL_VDC14];
}
static void cr_rails_now(void)
{
    cr_dbg.rail_now[0] = fm1_power.now[FM1_RAIL_SYSVDD];
    cr_dbg.rail_now[1] = fm1_power.now[FM1_RAIL_VDC14];
}
static void cr_rails_init(void) { fm1_power_init(); cr_rails_now(); }       /* IRQs off: power-on */
static uint32_t cr_c1_stack_used(void)              /* the C1MK fill: the deepest word core 1's usp reached */
{
    uint32_t i = 0;
    while (i < FM1_C1_STACK_WORDS && _c1_ustack[i] == FM1_C1_MARK)
        i++;
    return (FM1_C1_STACK_WORDS - i) * 4u;
}
#else                                /* the host (the emulator, the tests): the job runs when handed */
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif
#define CR_PROF_PER_US 259u          /* host instructions per device us (tools/emu/emu.c DEV_INSTR_PER_US) */
static uint8_t cr_c1_test_hang;      /* tests: the next jobs never answer (cr_c1_wait times out) */
static uint8_t cr_c1_test_nostart;   /* tests: core 1 does not answer at start */
static uint8_t cr_c1_test_late;      /* tests: the job runs at the wait (after part 0), not when handed (before it) */
static uint32_t cr_c1_test_rejected; /* tests: core 1's misread count (the device's fm1_c1_rejected) */
static uint32_t cr_c1_test_faults;   /* tests: faults about core 1 (the device's fm1_c1_fault.count) */
static void (*cr_c1_fn)(uint32_t);
static uint32_t cr_c1_arg;
static inline uint32_t cr_prof_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return (uint32_t)ri.ri_instructions;
#endif
    return 0;
}
static int cr_c1_run(void (*fn)(uint32_t), uint32_t arg)
{
    cr_c1_fn = fn;
    cr_c1_arg = arg;
    if (!cr_c1_test_hang && !cr_c1_test_late)
        fn(arg);
    return 1;
}
static int cr_c1_wait_core(void)
{
    if (cr_c1_test_faults)
        return -2;
    if (cr_c1_test_hang)
        return -1;
    if (cr_c1_test_late)
        cr_c1_fn(cr_c1_arg);
    return 0;
}
static void cr_c1_hold(void) {}
static inline uint32_t cr_c1_rejected(void) { return cr_c1_test_rejected; }
static inline uint32_t cr_c1_faults(void) { return cr_c1_test_faults; }
static uint32_t cr_c1_stack_used(void) { return 0; }
/* the rails on the host: P33 reads the boot loader's defaults (SYSVDD 11, VDC14 3) */
static void cr_rails_record(void) { cr_dbg.rail_boot[0] = cr_dbg.rail_now[0] = 11; cr_dbg.rail_boot[1] = cr_dbg.rail_now[1] = 3; }
static void cr_rails_init(void)
{
    if (cr_dbg.rail_now[0] < 14u) cr_dbg.rail_now[0] = 14;
    if (cr_dbg.rail_now[1] < 4u) cr_dbg.rail_now[1] = 4;
}
#endif

/* a stage's time without the reading of the clock (the host's counter costs ~6.6 k instructions a reading: measured
 * once, cr_prof_calibrate; the device's timer read costs nothing worth it) */
static uint32_t cr_prof_cost;
static inline uint32_t cr_prof_less(uint32_t d) { return d > cr_prof_cost ? d - cr_prof_cost : 0u; }
static void cr_prof_calibrate(void)
{
    uint32_t i, a, b, best = 0xFFFFFFFFu;
    for (i = 0; i < 16u; i++) {
        a = cr_prof_now();
        b = cr_prof_now();
        if (b - a < best)
            best = b - a;
    }
    cr_prof_cost = best == 0xFFFFFFFFu ? 0u : best;
}
static inline int cr_dualcore_active(void) { return cr_dbg.c1 == C1_OK; }

/* ------------------------------------------------ the escalation (after Melodee PR 21, no boost) --- */
static uint32_t cr_c1_seen;          /* core 1's misread count at the last look */
/* core 0 (the audio ISR): what core 1's counters say. 0 = keep core 1, -1 = give it up. New misreads in a first block:
 * counted, core 1 kept (one block's burst is one event); new misreads in a later block, or any fault or stall: give up */
static int cr_c1_assess(uint32_t rejected, uint32_t faults, int stalled)
{
    if (faults || stalled)
        return -1;
    if (rejected == cr_c1_seen)
        return 0;
    cr_c1_seen = rejected;
    return ++cr_dbg.c1_misreads > 1u ? -1 : 0;
}
static void cr_c1_give_up(void)
{
    cr_c1_hold();
    cr_dbg.c1 = C1_GAVE_UP;
    cr_dbg.c1_fallback = 1;
}
/* each block before the hand-over (fx.c): 1 = core 1 may take it */
static int cr_c1_check(void)
{
    if (cr_dbg.c1 != C1_OK)
        return 0;
    if (cr_c1_assess(cr_c1_rejected(), cr_c1_faults(), 0) < 0) {
        cr_c1_give_up();
        return 0;
    }
    return 1;
}
/* the wait (fx.c): 0 = done; else core 1 is held for good (a stall or a fault), C1_GAVE_UP */
static int cr_c1_wait(void)
{
    int rc = cr_c1_wait_core();
    if (rc) {
        (void)cr_c1_assess(cr_c1_rejected(), cr_c1_faults(), rc == -1);
        cr_c1_give_up();
    }
    return rc;
}
/* this block: part 1 renders as core 1's job (its own scratch where an engine has shared scratch: fm6_core.c) */
static volatile uint8_t cr_c1_split;
#define CR_C1_IDX(t) (cr_c1_split && (t) == &trk[1])
#define CR_PROF_START() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)
#define CR_PROF_SKIP() do { if (cr_dbg.prof_on) cr_dbg.mark = cr_prof_now(); } while (0)   /* (not counted) */
#define CR_PROF(k) do { if (cr_dbg.prof_on) { uint32_t t_ = cr_prof_now(); cr_dbg.cur[k] += cr_prof_less(t_ - cr_dbg.mark); \
                                              cr_dbg.mark = t_; } } while (0)

/* power-on (main.c fm1_main, before audio_init and the timers; not in SAFE MODE): start core 1. It boots through the
 * chip's ROM, which the PC limits refuse, and its entry word is in the top of RAM: both open while it starts.
 * Not answering in 20 ms: held for the session (C1_FAILED), single core, the shared budget */
static void cr_c1_boot(void)
{
    int rc;
    cr_rails_record();                                  /* (the boot loader's levels, reported either way) */
    if (cr_c1_off) {                                    /* Options > Dual Core Off: one core this session, the boot
                                                         * loader's rails (the stock behaviour) */
        cr_dbg.c1 = C1_OFF;
        return;
    }
    cr_rails_init();                                    /* the core rails up before core 1 starts (IRQs off) */
#ifdef FM1_HAVE_CPU1
    {
    uint32_t t0 = fm1_ticks();
    fm1_guard_unlock_top();                             /* (fm1_main locks it after the IRQs are attached) */
    fm1_guard_pc_open();
    rc = fm1_cpu1_start();
    fm1_guard_enable(FM1_GUARD_PC);
    cr_dbg.c1_start_us = (fm1_ticks() - t0) / CR_PROF_PER_US;
    }
#else
    rc = cr_c1_test_nostart ? -1 : 0;
#endif
    cr_dbg.c1 = rc ? C1_FAILED : C1_OK;
}

/* end of a half buffer (audio.c): the stages into the window and the totals */
static void cr_prof_half(void)
{
    uint32_t k;
    if (!cr_dbg.prof_on)
        return;
    if (cr_dbg.win_reset) {
        for (k = 0; k < CRP_N; k++)
            cr_dbg.win_sum[k] = cr_dbg.win_max[k] = 0;
        cr_dbg.win_n = 0;
        cr_dbg.win_reset = 0;
    }
    for (k = 0; k < CRP_N; k++) {
        uint32_t c = cr_dbg.cur[k];
        cr_dbg.win_sum[k] += c;
        if (c > cr_dbg.win_max[k])
            cr_dbg.win_max[k] = c;
        cr_dbg.tot_sum[k] += c;
        if (c > cr_dbg.tot_max[k])
            cr_dbg.tot_max[k] = c;
        cr_dbg.cur[k] = 0;
    }
    cr_dbg.win_n++;
    cr_dbg.tot_n++;
}
/* the main loop, once a second (audio.c cpu_window): the window closed into avg_us / max_us */
static void cr_prof_window(void)
{
    uint32_t k;
    if (cr_dbg.win_reset || !cr_dbg.win_n)
        return;
    for (k = 0; k < CRP_N; k++) {
        cr_dbg.avg_us[k] = cr_dbg.win_sum[k] / cr_dbg.win_n / CR_PROF_PER_US;
        cr_dbg.max_us[k] = cr_dbg.win_max[k] / CR_PROF_PER_US;
    }
    cr_dbg.win_reset = 1;
}
#else
#define cr_dualcore_active() 0
#define CR_C1_IDX(t) 0
#define CR_PROF_START() ((void)0)
#define CR_PROF(k) ((void)0)
#define CR_PROF_SKIP() ((void)0)
#define cr_prof_half() ((void)0)
#define cr_prof_window() ((void)0)
#define cr_c1_boot() ((void)0)
#define cr_c1_stack_used() 0u
#define cr_c1_rejected() 0u
#define cr_c1_faults() 0u
#endif

/* GEEK OUT's / the console's word for core 1 */
static const char *cr_c1_name(void)
{
    static const char *const N[4] = {"off", "ok", "failed", "gave up"};
    return N[cr_dbg.c1 & 3u];
}
