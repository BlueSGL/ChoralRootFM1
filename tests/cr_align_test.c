/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* What does not pass the master, lined up with it, and MASTER at power-on (0.19; docs/AUDIO-QUALITY.md "Round 3"),
 * on the firmware as the emulator builds it (tools/emu/emu_fw.c: the whole instrument on a fresh RAM flash), the
 * audio ISR's block called directly:
 *   build/host/cr_align_test                  (sh tests/run_cr_tests.sh)
 *  1. the click: an impulse put into the mix (fx.c FX_MIX_HOOK) and the metronome's click of the same block leave
 *     the DAC on the same sample in Classic and in Clean; Clean's is Classic's CLN_LA samples later, sample for sample
 *     (the click is not touched by the chain: never through the limiter);
 *  2. the USB recording's stems (fx.c's staging without USB: FX_UA_STAGE): the CHORD pair against the master pair of a
 *     dry note: no lag in Classic, none in Clean; the part pairs' line starts empty when a recording begins;
 *  3. the chorus's test switch: with the mono return asked for (cho_mono) both sides get the same wet;
 *  4. MASTER at power-on (the program runs itself again: "boot ADC classic|clean BLOCK [old]"): a key struck 2 ms after
 *     power-on (the earliest note there can be), the pot low, half and at its top: the first sounding block is
 *     already at the pot's level, in both chains, as the note is 100 ms later; "old" puts back what the unit did
 *     before (MASTER 2048, the pot's smoothing started at mid-scale) and the same check must fail on it. */
#include <stdint.h>
#define FX_UA_STAGE 1                   /* fx.c: the capture staging without USB audio */
static int32_t hook_imp;                /* a sample added to the mix at the start of the next block (both sides) */
#define FX_MIX_HOOK(l, r, n) do { (l)[0] += hook_imp; (r)[0] += hook_imp; hook_imp = 0; (void)(n); } while (0)
#include "../tools/emu/emu_fw.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("cr_align: %-108s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define NB 400u                         /* blocks kept */
static int32_t rec[NB * CTL * 2];       /* the DAC's Q15 samples, L R */
static int16_t stage[NB * CTL * 6];     /* the capture frames */
static uint32_t rec_n;
static int click_at = -1, stage_on;
static uint32_t ms_now, frame_ms;
static uint64_t frames_done;

/* the ISR's block as choralroot.c's mix_block shim runs it (the engine, fx.c's mix, the USB master pair, the click) */
static void block(void)
{
    int32_t out[2 * CTL];
    uint32_t i;
    cr_audio_block(CTL);
    ua_stage_on = (uint8_t)stage_on;
    fx_mix_block(out, CTL);
    if (ua_stage_on)
        for (i = 0; i < CTL; i++) {
            ua_stage[i * UA_CAP_CHANNELS] = ua_sat(out[2u * i]);
            ua_stage[i * UA_CAP_CHANNELS + 1u] = ua_sat(out[2u * i + 1u]);
        }
    if (click_at == (int)rec_n) {
        crl.metro_vol = 100;
        crl.click = 1;                  /* (as cr_loop_tick sets it in the block of a beat) */
    }
    cr_click_mix(out, CTL);
    if (rec_n < NB) {
        memcpy(rec + rec_n * CTL * 2u, out, sizeof out);
        memcpy(stage + rec_n * CTL * 6u, ua_stage, sizeof ua_stage);
    }
    rec_n++;
}
/* ms of the instrument's time: the 1 ms tick, a UI frame every 15 ms, the audio blocks due */
static void run(uint32_t ms)
{
    uint32_t end = ms_now + ms;
    for (; ms_now < end; ms_now++) {
        emu_fw_tick(ms_now);
        if (ms_now == 0 || ms_now - frame_ms >= 15u) {
            frame_ms = ms_now;
            emu_fw_frame();
        } else {
            emu_fw_idle();
        }
        while (frames_done + CTL <= (uint64_t)ms_now * FS / 1000u) {
            block();
            frames_done += CTL;
        }
    }
}
static void output(uint32_t clean)
{
    cs.out_clean = (uint8_t)clean;
    fx_output_set(clean, 1);
    dc_l = dc_r = dce_l = dce_r = 0;
    lim_env = LIM_T;
    lim_g = 32768;
}
static int first_nz(const int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (x[2u * i] || x[2u * i + 1u])
            return (int)i;
    return -1;
}

static void t_click(void)
{
    static int32_t ck[2][NB * CTL];
    int at[2][2];
    uint32_t mode, i, same = 1, moved = 0;
    for (mode = 0; mode < 2u; mode++) {
        output(mode);
        run(100);                                       /* silence: the chain settled */
        rec_n = 0;
        click_at = 3;                                   /* the click of block 3 .. */
        run(120);
        click_at = -1;
        at[mode][0] = first_nz(rec, NB * CTL);
        for (i = 0; i < NB * CTL; i++)
            ck[mode][i] = rec[2u * i];
        run(200);
        rec_n = 0;
        for (i = 0; i < 3u; i++)
            block();
        hook_imp = 8000;                                /* .. and a sample put into the mix of block 3 */
        for (i = 0; i < 40u; i++)
            block();
        at[mode][1] = first_nz(rec, NB * CTL);
        printf("cr_align: %s: the click of block 3 leaves at sample %d, a sample in that block's mix at %d\n",
               mode ? "Clean" : "Classic", at[mode][0], at[mode][1]);
        run(100);
    }
    check("the click and the mix of the same block leave the DAC on the same sample in Classic (no delay: sample 96)",
          at[0][0] == 3 * (int)CTL && at[0][1] == at[0][0]);
    check("the click and the mix of the same block leave the DAC on the same sample in Clean (both CLN_LA later)",
          at[1][0] == 3 * (int)CTL + (int)CLN_LA && at[1][1] == at[1][0]);
    for (i = 0; i + CLN_LA < 100u * CTL; i++) {
        if (ck[1][i + CLN_LA] != ck[0][i])
            same = 0;
        if (ck[0][i])
            moved++;
    }
    check("Clean's click is Classic's, sample for sample, CLN_LA samples later (made late, not passed through the limiter)",
          same && moved > 2000u);
}

/* the lag (samples, -64..64) at which b best follows a, and the correlation there */
static int lag_of(const int16_t *a, const int16_t *b, uint32_t n, double *corr)
{
    int d, best = 0;
    double bc = -2;
    for (d = -64; d <= 64; d++) {
        double sab = 0, saa = 0, sbb = 0;
        uint32_t i;
        for (i = 64; i + 64 < n; i++) {
            double x = a[i * 6u], y = b[(uint32_t)((int)i + d) * 6u];
            sab += x * y;
            saa += x * x;
            sbb += y * y;
        }
        sab /= sqrt(saa * sbb) + 1e-9;
        if (sab > bc) {
            bc = sab;
            best = d;
        }
    }
    *corr = bc;
    return best;
}
static void t_stems(void)
{
    int lag[2];
    double corr[2];
    uint32_t mode, i, empty = 1, late = 0;
    cs.fx_on = 0;                                       /* a dry note: the master pair is the part's, scaled */
    cu_fx_apply();
    for (mode = 0; mode < 2u; mode++) {
        output(mode);
        run(100);
        emu_hal.keys |= 1u << 9;                        /* a root key: the chord part's note */
        run(150);                                       /* (the note sounds; no recording yet) */
        rec_n = 0;
        stage_on = 1;                                   /* the computer starts to record, mid-note */
        run(250);
        stage_on = 0;
        emu_hal.keys = 0;
        /* the master's left against the CHORD's left, over what was recorded */
        lag[mode] = lag_of(stage + 2, stage, (rec_n < NB ? rec_n : NB) * CTL, &corr[mode]);
        if (mode) {
            for (i = 0; i < CLN_LA; i++)
                if (stage[i * 6u + 2u] || stage[i * 6u + 3u] || stage[i * 6u + 4u] || stage[i * 6u + 5u])
                    empty = 0;
            for (i = CLN_LA; i < 2u * CLN_LA; i++)
                late |= stage[i * 6u + 2u] != 0;
        }
        printf("cr_align: %s: USB's master pair follows the CHORD pair by %d samples (correlation %.4f)\n",
               mode ? "Clean" : "Classic", lag[mode], corr[mode]);
        run(600);
    }
    check("USB stems, Classic: the master pair and the CHORD pair of a dry note line up to the sample",
          lag[0] == 0 && corr[0] > 0.999);
    check("USB stems, Clean: the part pairs wait the limiter's look-ahead too: lined up to the sample",
          lag[1] == 0 && corr[1] > 0.999);
    check("USB stems, Clean: a recording begun mid-note starts the part pairs from an empty line (CLN_LA frames of silence, then the note)",
          empty && late);
    cs.fx_on = 1;
    cu_fx_apply();
}

static void t_chorus_switch(void)
{
    uint32_t pass, i, differs[2] = {0, 0}, sound[2] = {0, 0};
    for (pass = 0; pass < 2u; pass++) {                 /* the default sends (chorus 45), TINE EP, one note */
        cho_mono = (uint8_t)pass;
        output(1);
        run(700);                                       /* (the last pass's tails out) */
        emu_hal.keys |= 1u << 9;
        rec_n = 0;
        cs.fx_amt[0] = cs.fx_amt[2] = 0;                /* (reverb and delay sends off: they are stereo; at once: */
        cu_fx_apply();                                  /* a send's ramp to 0 would put a block of the note into them) */
        fx_sg[0][1] = fx_sg[0][2] = 0;
        run(280);
        emu_hal.keys = 0;
        for (i = 0; i < 380u * CTL; i++) {              /* (what this pass recorded) */
            differs[pass] += rec[2u * i] != rec[2u * i + 1u];
            sound[pass] += rec[2u * i] != 0;
        }
        run(1500);
    }
    cho_mono = 0;
    printf("cr_align: a TINE EP note with only its chorus send: left differs from right on %u of %u samples (stereo), %u "
           "(the mono switch)\n", differs[0], 380u * CTL, differs[1]);
    check("the chorus: left and right differ as built; with the test's mono switch they are the same sample (0.18's return)",
          differs[0] > 380u * CTL / 2u && !differs[1] && sound[0] > 1000u && sound[1] > 1000u);
}

/* "boot ADC classic|clean BLOCK [old]": power on with the pot at ADC and a key held; prints the first sounding block's
 * level and the level 100 ms on, as parts of what MASTER at ADC gives (the same note with the pot's level forced) */
static int boot(int adc, const char *chain, int old, int at)
{
    static int32_t ref[NB * CTL * 2];
    uint32_t i, b0, q = ((uint32_t)adc * (uint32_t)adc) >> 8;
    double e0 = 0, e1 = 0, r0 = 0, r1 = 0;
    setenv("EMU_OUTPUT", chain, 1);
    emu_hal.ready = 2;
    emu_flash_path[0] = 0;
    emu_hal.master = adc ? adc : 1;                     /* (0 means "not set" to emu_fw_init) */
    emu_fw_init(0);
    if (old) {                                          /* 0.18: felucca_init's 2048, the smoothing from mid-scale */
        master_knob = 512 * 16;
        fx_master_now(2048);
    }
    run(2);                                             /* (a key already down at the first scan is not a press) */
    emu_hal.keys |= 1u << 9;                            /* the earliest note there can be: 2 ms after power-on */
    rec_n = 0;
    run(260);
    memcpy(ref, rec, sizeof ref);
    for (b0 = 0; b0 < NB && first_nz(rec + b0 * CTL * 2u, CTL) < 0; b0++)
        ;
    if (b0 + 160u > NB) {
        printf("boot: no sound (first block %u of %u)\n", b0, rec_n);
        return 2;
    }
    /* the energy of the note's first four blocks and of four 100 ms on, from block `at` (the first sounding block of
     * the run at full MASTER: a quiet run's first non-zero sample comes a little later in the same attack); the
     * caller divides by the full run's: the note's own shape goes out and MASTER is left */
    if (at >= 0)
        b0 = (uint32_t)at;
    for (i = 0; i < 4u * CTL; i++) {
        e0 += (double)ref[(b0 * CTL + i) * 2u] * ref[(b0 * CTL + i) * 2u];
        e1 += (double)ref[((b0 + 140u) * CTL + i) * 2u] * ref[((b0 + 140u) * CTL + i) * 2u];
    }
    (void)r0;
    (void)r1;
    printf("boot: adc %d q12 %u chain %s first-block %u e0 %.6e e1 %.6e master_q12 %u\n", adc, q, chain, b0, e0, e1,
           (unsigned)song.master_q12);
    return 0;
}
static int boot_read(const char *self, int adc, const char *chain, int old, int at, uint32_t *b0, double *e0, double *e1)
{
    char cmd[1200], line[400];
    FILE *p;
    int ok = 0, a;
    unsigned q, mq;
    char ch[16];
    snprintf(cmd, sizeof cmd, "\"%s\" boot %d %s %d%s 2>/dev/null", self, adc, chain, at, old ? " old" : "");
    p = popen(cmd, "r");
    if (!p)
        return 0;
    while (fgets(line, sizeof line, p))
        if (sscanf(line, "boot: adc %d q12 %u chain %15s first-block %u e0 %lf e1 %lf master_q12 %u", &a, &q, ch, b0, e0, e1, &mq) == 7)
            ok = 1;
    pclose(p);
    return ok;
}
static void t_boot(const char *self)
{
    static const int ADC[3] = {160, 512, 1023};
    static const char *const CH[2] = {"classic", "clean"};
    uint32_t c, k, b0, bt;
    double e0, e1, t0, t1;
    for (c = 0; c < 2u; c++) {
        int ok = 1, got, old_seen;
        if (!boot_read(self, 1023, CH[c], 0, -1, &bt, &t0, &t1)) {
            check("MASTER at power-on: the test's own runs", 0);
            return;
        }
        for (k = 0; k < 3u; k++) {
            double want = pow((double)((ADC[k] * ADC[k]) >> 8) / (double)((1023 * 1023) >> 8), 2.0), r0, r1;
            got = boot_read(self, ADC[k], CH[c], 0, (int)bt, &b0, &e0, &e1);
            r0 = e0 / t0 / want;
            r1 = e1 / t1 / want;
            printf("cr_align: %s, the pot at %d (MASTER %d / 4096): the first sounding block (%u) at %.2f dB of the pot's "
                   "level, 100 ms later %.2f dB\n", CH[c], ADC[k], (ADC[k] * ADC[k]) >> 8, b0, 10 * log10(r0), 10 * log10(r1));
            /* (Classic's master is not linear, its tanh bends the full run a little: 0.3 dB) */
            if (!got || fabs(10 * log10(r0)) > 0.3 || fabs(10 * log10(r1)) > 0.3)
                ok = 0;
        }
        check(c ? "MASTER at power-on, Clean: the first note after power-on sounds at the pot's level from its first block (low, half, top)"
                : "MASTER at power-on, Classic: the first note after power-on sounds at the pot's level from its first block (low, half, top)",
              ok);
        /* the old behaviour on the same check: the pot low, MASTER 2048 until the smoothing gets there */
        got = boot_read(self, 160, CH[c], 1, (int)bt, &b0, &e0, &e1);
        {
            double want = pow((double)((160 * 160) >> 8) / (double)((1023 * 1023) >> 8), 2.0);
            old_seen = got && 10 * log10(e0 / t0 / want) > 6.0;
            printf("cr_align: %s, the pot at 160, as 0.18 started (MASTER 2048, the smoothing from mid-scale): the first block "
                   "%.1f dB over the pot's level, 100 ms later %.1f dB\n", CH[c], 10 * log10(e0 / t0 / want),
                   10 * log10(e1 / t1 / want));
        }
        check(c ? "MASTER at power-on, Clean: (the test sees it) 0.18's start is more than 6 dB over the pot's level in the first block"
                : "MASTER at power-on, Classic: (the test sees it) 0.18's start is more than 6 dB over the pot's level in the first block",
              old_seen);
    }
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "boot"))
        return boot(atoi(argv[2]), argv[3], argc >= 6 && !strcmp(argv[5], "old"), atoi(argv[4]));
    emu_hal.ready = 2;                                  /* simulated time */
    emu_flash_path[0] = 0;                              /* a fresh RAM flash: the defaults */
    emu_hal.master = 1023;
    emu_fw_init(0);
    run(60);
    printf("cr_align: look-ahead %u samples; MASTER %u / 4096\n", (unsigned)CLN_LA, (unsigned)song.master_q12);
    t_click();
    t_stems();
    t_chorus_switch();
    t_boot(argv[0]);
    printf(bad ? "cr_align_test: %d FAILED\n" : "cr_align_test: all ok\n", bad);
    return bad != 0;
}
