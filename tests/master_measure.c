/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* MEASUREMENT ONLY (GitHub issue 3, "muffled / harsh"; docs/AUDIO-QUALITY.md): what the output chain (fx.c: Options >
 * Output Classic = master_block, 0.18's DC blocker, limiter and soft clip; Clean = clean_block and clean_post, 0.19's)
 * does to a note or a chord as ChoralRoot plays it. Not a test, not run by any suite; nothing in the firmware is
 * changed. The firmware as the emulator builds it (tools/emu/emu_fw.c: the whole instrument, its UI, a fresh RAM
 * flash), driven as the headless emulator drives it (the 1 ms tick, a UI frame every 15 ms, the audio blocks due),
 * keys through the matrix (emu_hal.keys). The audio is rendered by the ISR's block (emu_firmware.h's mix_block: the
 * engine's tick, fx.c's mix, the click) called directly, so that after each block the mix can be read back from
 * fx.c's own buffers: raw = mix + wet, what out_master's first lines turn into the chain's input.
 * The captured mix is then put through the firmware's OWN chain functions a frame at a time (master_block(o, 1) /
 * clean_block(o, 1), states at rest), which gives the limiter's gain for every sample; run at the render's own
 * MASTER this must equal what the firmware put out, bit for bit, the 7 bits under the Q15 sample included
 * ("chain: exact"). The same is run for each MASTER setting asked for (--att: dB under the knob's top), in both chains.
 *
 *   cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/master_measure tests/master_measure.c -lm
 *   build/host/master_measure list
 *   build/host/master_measure render PREFIX [--eng N --name PRESET] [--fx 0|1] [--vel V] [--master ADC]
 *        [--mods k,k,..] [--root k] [--bass 0|1] [--hold MS] [--att dB,dB,..] [--send rev,cho,dly]
 *        [--out classic|clean]        (the chain the firmware itself runs in the render: the exactness check)
 * Files: PREFIX.raw.i32 (int32 L R: mix + wet), and PREFIX.classic.aNN.i32 / PREFIX.clean.aNN.i32 for each MASTER
 * setting NN dB down: int32 x 5 a frame:
 *   refL refR   the chain's input as it would be with no arithmetic lost: raw * gain (Classic: MASTER; Clean:
 *               the trim), DC removed in floating point, in 1/64 of a Q15 unit
 *   gain        the limiter's, Q15
 *   outL outR   the DAC word: Q15 << 7 (Classic), Q22 (Clean)
 * tools/master_analyze.py reads them; tools/master_measure_run.sh renders the set. */
#include "../tools/emu/emu_fw.c"

static int32_t *cap;                              /* mix + wet, L R */
static int32_t *fwout;                            /* the firmware's DAC words, L R */
static uint32_t cap_n, cap_max;
static uint64_t frames_done;

static void audio_due(uint32_t ms)
{
    int32_t out[2 * CTL];
    uint32_t i;
    while (frames_done + CTL <= (uint64_t)ms * FS / 1000u) {
        mix_block(out, CTL);                      /* emu_firmware.h's: cr_audio_block, fx.c's mix, the click */
        for (i = 0; i < CTL && cap_n < cap_max; i++, cap_n++) {
            cap[2u * cap_n] = mix_l[i] + wet[i];
            cap[2u * cap_n + 1u] = mix_r[i] + wet[i];
            fwout[2u * cap_n] = out[2u * i] * 128 + (fx_out_blk ? out_frac[2u * i] : 0);
            fwout[2u * cap_n + 1u] = out[2u * i + 1u] * 128 + (fx_out_blk ? out_frac[2u * i + 1u] : 0);
        }
        frames_done += CTL;
    }
}

/* the chain's states as at power-on */
static void chain_rest(void)
{
    dc_l = dc_r = dce_l = dce_r = 0;
    lim_env = LIM_T;
    lim_g = 32768;
    cln_env = CLN_T4;
    cln_gt = cln_g = 32768;
    cln_hold = 0;
}

/* raw through one chain at MASTER m (Q12): the firmware's functions, a frame a call */
static void chain(const int32_t *raw, uint32_t n, int clean, int32_t m, FILE *f, uint32_t *mismatch)
{
    double dcl = 0, dcr = 0, k = (clean ? CLN_TRIM : m) / 4096.0;
    uint32_t i;
    chain_rest();
    for (i = 0; i < n; i++) {
        int32_t o[2], rec[5];
        double xl = raw[2u * i] * k, xr = raw[2u * i + 1u] * k;
        dcl += (xl - dcl) / 4096.0;               /* dc_block's corner (2 Hz), without its arithmetic */
        dcr += (xr - dcr) / 4096.0;
        rec[0] = (int32_t)lrint((xl - dcl) * 64.0);
        rec[1] = (int32_t)lrint((xr - dcr) * 64.0);
        if (clean) {
            o[0] = (raw[2u * i] * (CLN_TRIM >> 4)) >> 6;   /* fx.c out_master's lines */
            o[1] = (raw[2u * i + 1u] * (CLN_TRIM >> 4)) >> 6;
            clean_block(o, 1u);
            rec[2] = cln_g;
            rec[3] = (o[0] * (m * CLN_MAKEUP >> 12)) >> 7;   /* clean_post's line at a resting knob */
            rec[4] = (o[1] * (m * CLN_MAKEUP >> 12)) >> 7;
        } else {
            o[0] = ((raw[2u * i] >> 2) * m) >> 10;
            o[1] = ((raw[2u * i + 1u] >> 2) * m) >> 10;
            master_block(o, 1u);
            rec[2] = lim_g;
            rec[3] = o[0] * 128;
            rec[4] = o[1] * 128;
        }
        if (mismatch && (rec[3] != fwout[2u * i] || rec[4] != fwout[2u * i + 1u]))
            (*mismatch)++;
        if (f)
            fwrite(rec, sizeof rec, 1, f);
    }
}

static void list(void)
{
    uint32_t e, p;
    for (e = 0; e < NENGINES; e++) {
        uint32_t n = pool_count(e);
        printf("engine %u %s: %u in the pool (factory presets %u)\n", e, ENGINES[e]->name, n, (unsigned)ENGINES[e]->npresets);
        for (p = 0; p < n; p++) {
            char b[16];
            pool_name(e, p, b);
            printf("   %2u %s\n", p, b);
        }
    }
}

static void voices(const char *when)
{
    uint32_t p, i;
    printf("%s:", when);
    for (p = 0; p < 2u; p++) {
        printf(" part %u (%s %d, level %d, sends dist %d cho %d dly %d rev %d):", p, ENGINES[trk[p].engine]->name,
               (int)trk[p].preset, (int)trk[p].p[P_LEVEL], (int)trk[p].p[P_DIST], (int)trk[p].p[P_CHOR],
               (int)trk[p].p[P_DLY], (int)trk[p].p[P_REV]);
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active && trk[p].v[i].gate)
                printf(" %d", (int)trk[p].v[i].note);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *prefix, *name = NULL, *mods = "5,4,7,0", *atts = "0,6,12", *outm = "clean";
    int eng = -1, fx = 1, vel = -1, master = 1023, root = 9, bass = 1, hold = 4000, a, send[3] = {-1, -1, -1};
    uint32_t ms, last_frame = 0, end, modmask = 0, t_on = 1000u, mism = 0;
    int clean;
    char path[1024];
    emu_hal.ready = 2;                            /* simulated time */
    emu_flash_path[0] = 0;                        /* RAM flash, fresh: the defaults */
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        emu_fw_init(0);
        list();
        return 0;
    }
    if (argc < 3 || strcmp(argv[1], "render")) {
        printf("usage: master_measure list | render PREFIX [options]\n");
        return 2;
    }
    prefix = argv[2];
    for (a = 3; a + 1 < argc; a += 2) {
        if (!strcmp(argv[a], "--eng")) eng = atoi(argv[a + 1]);
        else if (!strcmp(argv[a], "--name")) name = argv[a + 1];
        else if (!strcmp(argv[a], "--fx")) fx = atoi(argv[a + 1]);
        else if (!strcmp(argv[a], "--vel")) vel = atoi(argv[a + 1]);
        else if (!strcmp(argv[a], "--master")) master = atoi(argv[a + 1]);
        else if (!strcmp(argv[a], "--mods")) mods = argv[a + 1];
        else if (!strcmp(argv[a], "--root")) root = atoi(argv[a + 1]);
        else if (!strcmp(argv[a], "--bass")) bass = atoi(argv[a + 1]);
        else if (!strcmp(argv[a], "--hold")) hold = atoi(argv[a + 1]);
        else if (!strcmp(argv[a], "--att")) atts = argv[a + 1];
        else if (!strcmp(argv[a], "--out")) outm = argv[a + 1];
        else if (!strcmp(argv[a], "--send")) sscanf(argv[a + 1], "%d,%d,%d", &send[0], &send[1], &send[2]);
        else { printf("unknown option %s\n", argv[a]); return 2; }
    }
    clean = strcmp(outm, "classic") != 0;
    {
        const char *q = mods;
        while (*q) {
            char *e;
            long k = strtol(q, &e, 10);
            if (e == q) break;
            if (k >= 0 && k < 27) modmask |= 1u << k;
            q = *e ? e + 1 : e;
        }
    }
    emu_hal.master = master;
    emu_fw_init(0);
    cs.out_clean = (uint8_t)clean;                /* Options > Output, before the first block */
    fx_output_set((uint32_t)clean, 1);
    end = t_on + 60u + (uint32_t)hold + 1500u;
    cap_max = (uint32_t)((uint64_t)(end + 10u) * FS / 1000u);
    cap = calloc(cap_max, 2 * sizeof *cap);
    fwout = calloc(cap_max, 2 * sizeof *fwout);
    for (ms = 0; ms <= end; ms++) {
        emu_fw_tick(ms);
        if (ms == 200u) {                         /* the sounds and the session, as the knobs' handlers set them */
            if (eng >= 0) {
                cu_pool_load(0, (uint32_t)eng, name ? cu_pool_find((uint32_t)eng, name) : 1u);
                cu_sends_to_fx();
            }
            if (send[0] >= 0) {
                cs.fx_amt[0] = (uint8_t)send[0];
                cs.fx_amt[1] = (uint8_t)send[1];
                cs.fx_amt[2] = (uint8_t)send[2];
            }
            cs.fx_on = (uint8_t)(fx != 0);
            cu_fx_apply();
            if (vel >= 0)
                cs.vel = (uint8_t)vel;
            if (bass) {                           /* the BASS tap at ALGORITHM OFF (cr_ui.c BT_BASS): DEEP SUB */
                cs.bass_sound = (uint16_t)(cu_part_pos(1) + 1u);
                cs.bass_on = 1;
                cr_post(CRE_BASS, 0, 0, 1);
            }
        }
        if (ms == t_on)
            emu_hal.keys |= modmask;
        if (ms == t_on + 60u)
            emu_hal.keys |= 1u << root;
        if (ms == t_on + 60u + 500u)
            voices("held (500 ms in)");
        if (ms == t_on + 60u + (uint32_t)hold)
            emu_hal.keys = 0;
        if (ms == 0 || ms - last_frame >= 15u) {
            last_frame = ms;
            emu_fw_frame();
        } else {
            emu_fw_idle();
        }
        audio_due(ms);
    }
    printf("render: %u frames, note-on at frame %u, release at frame %u; master_q12 %u, vel %u, fx_on %u, fx_smooth %u, "
           "lowcut %u, bass_on %u, output %s; LIM_T %d, CLN_T %d, CLN_TRIM %d, CLN_K %d, hold %u, release >> %d\n", cap_n,
           (unsigned)((uint64_t)(t_on + 60u) * FS / 1000u),
           (unsigned)((uint64_t)(t_on + 60u + (uint32_t)hold) * FS / 1000u), (unsigned)song.master_q12, (unsigned)cs.vel,
           (unsigned)cs.fx_on, (unsigned)fx_smooth, (unsigned)fx_lowcut, (unsigned)cs.bass_on, clean ? "Clean" : "Classic",
           LIM_T, CLN_T, CLN_TRIM, CLN_K, (unsigned)CLN_HOLD, CLN_REL);
    snprintf(path, sizeof path, "%s.raw.i32", prefix);
    {
        FILE *f = fopen(path, "wb");
        if (!f) { printf("cannot write %s\n", path); return 1; }
        fwrite(cap, 2 * sizeof *cap, cap_n, f);
        fclose(f);
    }
    chain(cap, cap_n, clean, (int32_t)song.master_q12, NULL, &mism);
    printf("chain: %s (%u frames differ from the firmware's own output, %s)\n", mism ? "DIFFERS" : "exact", mism,
           clean ? "Clean" : "Classic");
    for (a = 0; a < 2; a++) {
        const char *q = atts;
        while (*q) {
            char *e;
            double db = strtod(q, &e);
            FILE *f;
            if (e == q) break;
            snprintf(path, sizeof path, "%s.%s.a%02d.i32", prefix, a ? "clean" : "classic", (int)lrint(db));
            f = fopen(path, "wb");
            if (!f) { printf("cannot write %s\n", path); return 1; }
            chain(cap, cap_n, a, (int32_t)lrint(4088.0 * pow(10.0, -db / 20.0)), f, NULL);
            fclose(f);
            q = *e ? e + 1 : e;
        }
    }
    return mism ? 1 : 0;
}
