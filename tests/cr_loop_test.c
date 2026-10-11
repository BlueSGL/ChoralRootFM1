/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests of the ChoralRoot looper (firmware/src/cr_loop.c) on the real engine (cr_engine.c):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -o build/host/cr_loop_test tests/cr_loop_test.c \
 *      firmware/src/cr_loop.c firmware/src/cr_engine.c
 * Time runs in 1 ms steps: cr_tick then cr_loop_tick, as cr_out.c's audio block does. One line per check. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/cr_loop.h"

static cr_t C;
static cr_loop_t L;
static uint32_t NOW;
static int passed, failed;
static uint8_t SND[3][128];
static int dup_on, stray_off;
typedef struct { char k; uint8_t s, note, vel; uint32_t ms; } ev_t;
static ev_t LOG[20000];
static int nlog;
static int clicks[3];

static void cb_on(void *ud, cr_stream_t s, uint8_t n, uint8_t v)
{
    (void)ud;
    if (SND[s][n]) dup_on++;
    SND[s][n] = 1;
    if (nlog < 20000) { LOG[nlog].k = '+'; LOG[nlog].s = (uint8_t)s; LOG[nlog].note = n; LOG[nlog].vel = v; LOG[nlog].ms = NOW; nlog++; }
}
static void cb_off(void *ud, cr_stream_t s, uint8_t n)
{
    (void)ud;
    if (!SND[s][n]) stray_off++;
    SND[s][n] = 0;
    if (nlog < 20000) { LOG[nlog].k = '-'; LOG[nlog].s = (uint8_t)s; LOG[nlog].note = n; LOG[nlog].vel = 0; LOG[nlog].ms = NOW; nlog++; }
}
static void cb_all(void *ud, cr_stream_t s) { (void)ud; memset(SND[s], 0, sizeof SND[s]); }
static void cb_gesture(void *ud, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    (void)ud;
    cr_loop_gesture(&L, gid, root, q, ext, vel, on);
}
static const cr_out_t OUT = { cb_on, cb_off, cb_all, NULL, cb_gesture };

static void ok(int c, const char *name)
{
    if (c) passed++; else failed++;
    printf("%s %s\n", c ? "ok  " : "FAIL", name);
}
static void step(uint32_t ms)
{
    while (ms--) {
        NOW++;
        cr_tick(&C, NOW);
        cr_loop_tick(&L, &C, NOW);
        if (L.click) { clicks[L.click]++; L.click = 0; }
    }
}
static int sounding(void)
{
    int s, n, k = 0;
    for (s = 0; s < 3; s++) for (n = 0; n < 128; n++) k += SND[s][n];
    return k;
}
static void reset(void)
{
    memset(SND, 0, sizeof SND);
    dup_on = stray_off = nlog = 0;
    NOW = 1000;
    cr_init(&C, &OUT);
    cr_loop_init(&L);
    L.mode = CRL_MODE_OVERDUB;                       /* (the scenarios before the record modes: REC over a loop dubs) */
    cr_tick(&C, NOW);
    cr_loop_tick(&L, &C, NOW);
    memset(clicks, 0, sizeof clicks);
}
static void chord(int note, cr_mod_t m, uint32_t hold)   /* hold a chord type + a root for hold ms */
{
    cr_mod(&C, m, 1); cr_key(&C, (uint8_t)note, 100, 1);
    step(hold);
    cr_key(&C, (uint8_t)note, 100, 0); cr_mod(&C, m, 0);
}
/* the times (ms) of the note-ons of `note` on MAIN since log entry from */
static int ons_of(int note, int from, uint32_t *t, int max)
{
    int k, n = 0;
    for (k = from; k < nlog && n < max; k++)
        if (LOG[k].k == '+' && LOG[k].s == 0 && LOG[k].note == note) t[n++] = LOG[k].ms;
    return n;
}
static int near(uint32_t a, uint32_t b, uint32_t tol) { return a > b ? a - b <= tol : b - a <= tol; }

static void s_free(void)
{
    uint32_t t[8];
    int n, mark;
    char name[128];
    reset();
    ok(cr_loop_rec(&L, &C) == CRL_DID_ARM && L.cap == CRL_CAP_ARMED, "free: REC arms (nothing runs until a chord)");
    step(100);
    ok(L.cap == CRL_CAP_ARMED, "free: still armed after 100 ms of silence");
    chord(62, CR_MOD_MAJ, 500);                      /* D at bar 1 beat 1 */
    ok(L.cap == CRL_CAP_REC, "free: the first chord started the take");
    step(500);
    chord(64, CR_MOD_MIN, 500);                      /* Em at beat 3 */
    step(500);
    mark = nlog;
    ok(cr_loop_rec(&L, &C) == CRL_DID_COMMIT, "free: REC again commits");
    ok(L.state == CRL_PLAYING && L.d.nlayers == 1 && L.d.nev == 2, "free: playing, 1 layer, 2 events");
    snprintf(name, sizeof name, "free: 2 s at 120 BPM = one 4/4 bar (len %u ticks)", (unsigned)L.d.len);
    ok(L.d.len >= 383 && L.d.len <= 385, name);
    ok(L.d.ev[0].t == 0 && L.d.ev[1].t >= 191 && L.d.ev[1].t <= 193 && (L.d.ev[0].qx >> 4) == CR_Q_MAJ &&
       (L.d.ev[1].qx >> 4) == CR_Q_MIN && L.d.ev[0].root == 62 && L.d.ev[1].root == 64,
       "free: events D at 0, Em at beat 3 (resolved roots and qualities)");
    ok(L.d.ev[0].dur >= 95 && L.d.ev[0].dur <= 97, "free: a 500 ms chord lasts a beat (96 ticks)");
    step(3990);
    n = ons_of(62, mark, t, 8);
    snprintf(name, sizeof name, "playback: D re-triggers every 2 s (%d ons: %u %u)", n, n > 0 ? (unsigned)t[0] : 0u,
             n > 1 ? (unsigned)t[1] : 0u);
    ok(n == 2 && near(t[1] - t[0], 2000, 6), name);
    {
        uint32_t e[8];
        int m = ons_of(64, mark, e, 8);
        ok(m == 2 && near(e[0] - t[0], 1000, 6) && near(e[1] - t[1], 1000, 6), "playback: Em 1 s after each D");
    }
    ok(cr_loop_ring(&L) < 256, "the ring position is a fraction of the cycle");
    /* voicing applies live */
    cr_voicing_step(&C, 1);
    mark = nlog;
    step(2000);
    n = ons_of(74, mark, t, 8);
    ok(n >= 1 && ons_of(62, mark, t, 8) == 0, "voicing +1 at playback: D's lowest note moved up (D5, no D4)");
    cr_voicing_step(&C, -1);
    /* tempo: 240 BPM halves the cycle */
    cr_set_tempo(&C, 240);
    step(1500);
    mark = nlog;
    step(3000);
    n = ons_of(62, mark, t, 8);
    snprintf(name, sizeof name, "240 BPM: the loop cycles in 1 s (%d ons)", n);
    ok(n >= 2 && near(t[1] - t[0], 1000, 6), name);
    cr_set_tempo(&C, 120);
    /* overdub, undo */
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_ARM && L.cap == CRL_CAP_OD_ARMED, "overdub: REC while playing arms it");
    step(300);
    chord(60, CR_MOD_MAJ, 300);
    ok(L.cap == CRL_CAP_OD, "overdub: the chord opened a layer");
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_END && L.d.nlayers == 2 && L.d.nev == 3, "overdub: REC ends it: 2 layers, 3 events");
    mark = nlog;
    step(2000);
    ok(ons_of(60, mark, t, 8) == 1, "overdub: C plays in the next cycle");
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nlayers == 1 && L.d.nev == 2, "undo: the overdub layer is gone");
    step(500);
    mark = nlog;
    step(2000);
    ok(ons_of(60, mark, t, 8) == 0 && ons_of(62, mark, t, 8) == 1, "undo: C no longer plays, D still does");
    ok(cr_loop_undo(&L, &C) == CRL_DID_NOTHING, "undo: the first layer stays (CLEAR empties)");
    ok(cr_loop_play(&L, &C) == CRL_DID_STOP && L.state == CRL_STOPPED, "LOOP tap: stop");
    step(1500);
    ok(sounding() == 0, "stop: nothing left sounding");
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY && L.state == CRL_PLAYING, "LOOP tap: play again from the start");
    step(10);
    ok(SND[0][62] == 1, "play: the event at 0 sounds at once");
    ok(cr_loop_clear(&L, &C) == CRL_DID_CLEAR && L.state == CRL_EMPTY && L.d.nev == 0, "clear: empty");
    step(100);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "clear: no stuck, double or stray note");
}

static void s_sync(void)
{
    char name[128];
    uint32_t t[8];
    int mark;
    reset();
    L.sync = 2;                                      /* 2 bars */
    L.count_in = 1;
    cr_loop_metro(&L, 0);
    ok(cr_loop_rec(&L, &C) == CRL_DID_COUNTIN && L.cap == CRL_CAP_COUNTIN, "sync: REC starts a one-bar count-in");
    step(1000);
    {
        uint32_t bar, beat;
        cr_loop_where(&L, &bar, &beat);
        ok(bar == 0 && beat == 2, "sync: half-way through the count-in, 2 beats left");
    }
    step(1000);
    ok(L.cap == CRL_CAP_REC, "sync: recording after one bar");
    snprintf(name, sizeof name, "sync: the count-in clicked 4 beats, the first accented (%d + %d)", clicks[2], clicks[1]);
    ok(clicks[2] == 1 && clicks[1] == 3, name);
    step(30);
    chord(62, CR_MOD_MAJ, 400);                      /* 30 ms late: 1/16 quantize puts it on 0 */
    step(1570);
    chord(69, CR_MOD_MIN, 1000);                     /* bar 2 */
    ok(L.cap == CRL_CAP_REC, "sync: still recording in bar 2");
    step(2000);
    ok(L.cap == CRL_CAP_NONE && L.state == CRL_PLAYING && L.d.len == 768,
       "sync: committed by itself after 2 bars (768 ticks), playing");
    ok(L.d.nev == 2 && L.d.ev[0].t >= 5 && L.d.ev[0].t <= 6, "sync: no quantize: D 30 ms (5.76 ticks) late as played");
    mark = nlog;
    step(4000);
    ok(ons_of(62, mark, t, 8) == 1, "sync: D plays once per 4 s cycle");
    cr_loop_play(&L, &C);
    cr_loop_clear(&L, &C);
    L.quant = 4;                                     /* 1/16 */
    cr_loop_rec(&L, &C);
    step(2000 + 30);
    chord(62, CR_MOD_MAJ, 400);
    step(4000);
    ok(L.d.nev == 1 && L.d.ev[0].t == 0, "quantize 1/16: a chord 6 ticks late lands on 0");
    ok(L.d.ev[0].dur >= 81 && L.d.ev[0].dur <= 84, "quantize: its note-off kept (the length grows by the shift)");
    /* panic while playing: stopped, content kept */
    cr_panic(&C);
    cr_loop_panic(&L);
    ok(L.state == CRL_STOPPED && L.d.nev == 1 && sounding() == 0, "panic: stopped, loop kept, silence");
    step(500);
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY, "after panic: plays again");
    step(100);
    cr_loop_play(&L, &C);
    step(1500);
    ok(sounding() == 0, "stop after panic: silence");
    /* panic during a fresh take drops it */
    cr_loop_clear(&L, &C);
    cr_loop_rec(&L, &C);
    step(2100);
    chord(62, CR_MOD_MAJ, 200);
    cr_panic(&C);
    cr_loop_panic(&L);
    ok(L.state == CRL_EMPTY && L.d.nev == 0, "panic during a take: dropped");
}

static void s_slots(void)
{
    static crl_data_t a, b;
    static uint8_t buf[CRL_REC_MAX];
    uint32_t n, t[8];
    int mark;
    reset();
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 300);
    step(700);
    cr_loop_rec(&L, &C);                             /* a 1 s loop of D */
    cr_loop_rec(&L, &C);
    step(200);
    chord(65, CR_MOD_MIN, 200);
    cr_loop_rec(&L, &C);                             /* + an overdub of Fm */
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n == CRL_REC_HDR + 2u * 2u + 7u * 2u, "pack: header + 2 layer counts + 2 x 7 bytes");
    ok(cr_loop_unpack(buf, n, &a) == 1 && a.len == L.d.len && a.nev == 2 && a.nlayers == 2 &&
       memcmp(a.ev, L.d.ev, sizeof a.ev[0] * 2) == 0, "unpack: the same loop back (layers kept)");
    buf[n - 2] = 200;                                /* a velocity out of range */
    ok(cr_loop_unpack(buf, n, &b) == 0 && b.nev == 0, "unpack: a bad event: refused, empty");
    ok(cr_loop_unpack(buf, 10, &b) == 0, "unpack: short: refused");
    /* a queued slot switches at the cycle's end */
    memset(&b, 0, sizeof b);
    b.len = 384; b.nev = 1; b.nlayers = 1; b.ev[0].t = 0; b.ev[0].dur = 96; b.ev[0].root = 60; b.ev[0].vel = 90;
    b.ev[0].qx = CR_Q_MAJ << 4;
    step(2000 - (NOW % 1000));
    cr_loop_queue(&L, &C, &b);
    ok(L.next_on == 1 && L.d.nev == 2, "queue while playing: waits for the cycle's end");
    mark = nlog;
    step(2500);
    ok(L.next_on == 0 && L.d.len == 384 && ons_of(60, mark, t, 8) >= 1, "the next cycle is the queued slot's (C plays)");
    cr_loop_play(&L, &C);
    cr_loop_set(&L, &C, &a);
    ok(L.state == CRL_STOPPED && L.d.nev == 2 && !L.dirty, "set when stopped: loaded at once, not dirty");
    cr_loop_set(&L, &C, 0);
    ok(L.state == CRL_EMPTY, "set(0): empty");
    step(1500);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "slots: no stuck, double or stray note");
}

/* a queued switch is taken at the cycle's end (loads + 1) or cancelled by an edit of the loop still playing, a
 * stop (loads unchanged): the UI keeps its slot number right by it (cr_ui.c cu_loop_frame) */
static void s_switch(void)
{
    static crl_data_t b;
    uint8_t n0;
    reset();
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 300);
    step(700);
    cr_loop_rec(&L, &C);                             /* a 1 s loop of D, playing */
    cr_loop_rec(&L, &C);
    step(200);
    chord(65, CR_MOD_MIN, 200);
    cr_loop_rec(&L, &C);                             /* + an overdub of Fm: 2 layers */
    ok(L.dirty && L.d.nlayers == 2 && L.state == CRL_PLAYING, "a take and an overdub: dirty (the UI saves it on leaving)");
    memset(&b, 0, sizeof b);
    b.len = 384; b.nev = 1; b.nlayers = 1; b.ev[0].t = 0; b.ev[0].dur = 96; b.ev[0].root = 60; b.ev[0].vel = 90;
    b.ev[0].qx = CR_Q_MAJ << 4;
    n0 = L.loads;
    cr_loop_queue(&L, &C, &b);
    step(100);
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_ARM && !L.next_on, "queued, then an overdub armed: the switch cancelled");
    step(1500);
    ok(L.loads == n0 && L.d.nlayers == 2 && L.d.nev == 2 && L.d.len != 384, "cancelled: the loop stays (no load)");
    cr_loop_rec(&L, &C);                             /* (the armed overdub: cancelled) */
    cr_loop_queue(&L, &C, &b);
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && !L.next_on && L.d.nlayers == 1 && L.loads == n0,
       "queued, then an undo: the switch cancelled, the undo done");
    cr_loop_queue(&L, &C, &b);
    cr_loop_play(&L, &C);                            /* LOOP tap before the cycle's end: stop */
    ok(!L.next_on && L.state == CRL_STOPPED && L.loads == n0 && L.d.nev == 1, "queued, then a stop: cancelled, kept");
    cr_loop_play(&L, &C);
    cr_loop_queue(&L, &C, &b);
    step(1100);
    ok(!L.next_on && L.loads == (uint8_t)(n0 + 1u) && L.d.len == 384 && !L.dirty && L.state == CRL_PLAYING,
       "queued, the cycle's end: taken (loads + 1), not dirty, playing on");
    cr_loop_set(&L, &C, 0);
    ok(L.loads == (uint8_t)(n0 + 2u) && L.state == CRL_EMPTY, "set: a load (loads + 1)");
    step(500);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "switches: no stuck, double or stray note");
}

static void s_limits(void)
{
    int i;
    reset();
    L.sync = 0;
    cr_loop_rec(&L, &C);
    for (i = 0; i < 600 && (L.cap == CRL_CAP_ARMED || L.cap == CRL_CAP_REC); i++) {
        cr_key(&C, 62, 100, 1);
        step(3);
        cr_key(&C, 62, 100, 0);
        step(2);
    }
    ok(L.state == CRL_PLAYING && L.d.nev == CRL_MAX_EV, "the event cap ends the take by itself (512 events)");
    step(3000);
    cr_loop_play(&L, &C);
    step(500);
    ok(sounding() == 0, "a full loop played and stopped: silence");
    /* determinism: the same take twice gives the same events */
    {
        static crl_data_t first;
        int run;
        for (run = 0; run < 2; run++) {
            reset();
            cr_loop_rec(&L, &C);
            chord(62, CR_MOD_MAJ, 333);
            step(171);
            chord(66, CR_MOD_SUS, 222);
            step(400);
            cr_loop_rec(&L, &C);
            if (!run) first = L.d;
        }
        ok(memcmp(&first, &L.d, sizeof first) == 0, "determinism: the same take, the same loop");
    }
    /* loop level scales the velocity; 0 mutes */
    L.level = 50;
    cr_loop_play(&L, &C);
    nlog = 0;
    cr_loop_play(&L, &C);
    step(5);
    ok(nlog > 0 && LOG[0].vel == 50, "loop level 50%: velocity 100 plays at 50");
    cr_loop_play(&L, &C);
    L.level = 0;
    step(500);
    nlog = 0;
    cr_loop_play(&L, &C);
    step(1500);
    ok(nlog == 0, "loop level 0: silent");
}

/* the record modes (docs/LOOPER-MODES.md) */
static void until_pos(uint32_t p)                    /* step until the cycle position reaches p (ticks) */
{
    int i;
    for (i = 0; i < 10000 && cr_loop_pos(&L) != p; i++) step(1);
}
static int find_ev(int root, int live)               /* the first event of root (live: not hidden), -1 */
{
    int i;
    for (i = 0; i < L.d.nev; i++)
        if (L.d.ev[i].root == root && (!live || !(L.d.ev[i].layer & CRL_HID))) return i;
    return -1;
}
static void take_dga(void)                           /* Free: D (a beat), G (two beats), Ab (a beat): one bar
                                                      * (no chord holds another's root: ons_of counts roots) */
{
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 500);
    chord(67, CR_MOD_MAJ, 1000);
    chord(68, CR_MOD_MAJ, 500);
    cr_loop_rec(&L, &C);
}
static void s_overwrite(void)
{
    char name[128];
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();
    ok(L.state == CRL_PLAYING && L.d.nev == 3 && L.d.len == 384, "overwrite: a 1-bar take of 3 events plays");
    step(700);
    ok(cr_loop_rec(&L, &C) == CRL_DID_ARM && L.state == CRL_STOPPED && L.d.nev == 3 && L.ow,
       "overwrite: REC while playing stops and arms; the loop is still there");
    step(300);
    ok(sounding() == 0, "overwrite: armed, the loop silent");
    ok(cr_loop_rec(&L, &C) == CRL_DID_CANCEL && L.d.nev == 3 && L.d.len == 384 && !L.ow && L.state == CRL_STOPPED,
       "overwrite: REC again before the first chord cancels, the loop intact");
    ok(cr_loop_rec(&L, &C) == CRL_DID_ARM && L.d.nev == 3, "overwrite: armed again (stopped, a loop)");
    cr_mod(&C, CR_MOD_MIN, 1); cr_key(&C, 64, 100, 1);
    step(1);
    ok(L.cap == CRL_CAP_REC && L.d.nev == 0 && L.d.len == 0 && L.dirty, "overwrite: the first chord starts the take: the loop goes now");
    step(500);
    cr_key(&C, 64, 100, 0); cr_mod(&C, CR_MOD_MIN, 0);
    step(500);
    ok(cr_loop_rec(&L, &C) == CRL_DID_COMMIT && L.d.nev == 1 && L.d.ev[0].root == 64 && L.d.nlayers == 1 &&
       L.state == CRL_PLAYING, "overwrite: the new take replaced it (Em alone, 1 layer, playing)");
    /* synced: the count-in keeps the loop; its end clears it */
    L.sync = 1;
    L.count_in = 1;
    ok(cr_loop_rec(&L, &C) == CRL_DID_COUNTIN && L.d.nev == 1, "overwrite, 1 bar synced: REC starts the count-in, the loop kept");
    step(1000);
    ok(L.cap == CRL_CAP_COUNTIN && L.d.nev == 1, "overwrite: halfway through the count-in, still kept");
    step(1010);
    snprintf(name, sizeof name, "overwrite: the count-in's end starts the take, the loop cleared (cap %u nev %u)",
             (unsigned)L.cap, (unsigned)L.d.nev);
    ok(L.cap == CRL_CAP_REC && L.d.nev == 0, name);
    chord(62, CR_MOD_MAJ, 400);
    step(2000);
    ok(L.cap == CRL_CAP_NONE && L.state == CRL_PLAYING && L.d.len == 384 && L.d.nev == 1 && L.d.ev[0].root == 62,
       "overwrite: the synced take committed by itself (D, 1 bar)");
    /* Advance is Overwrite in the engine (the UI loads the next slot first): the slot math */
    ok(CRL_NEXT_SLOT(2u) == 3u && CRL_NEXT_SLOT(9u) == 0u && CRL_NEXT_SLOT(0u) == 1u, "advance: slot 3 -> 4, 10 wraps to 1");
    L.mode = CRL_MODE_ADVANCE;
    ok(cr_loop_rec(&L, &C) == CRL_DID_COUNTIN && L.ow, "advance: over a loop (the slot jumped to), as Overwrite");
    cr_loop_play(&L, &C);
    ok(L.cap == CRL_CAP_NONE && L.d.nev == 1 && !L.ow, "advance: LOOP during the count-in cancels, the loop kept");
    cr_loop_panic(&L);
    L.mode = CRL_MODE_OVERWRITE;
    L.sync = 0;
    cr_loop_rec(&L, &C);
    cr_loop_panic(&L);
    ok(L.d.nev == 1 && L.state == CRL_STOPPED, "overwrite: a panic while armed keeps the loop");
    step(200);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "overwrite: no stuck, double or stray note");
}
static void s_replace(void)
{
    char name[160];
    static uint8_t buf[CRL_REC_MAX];
    static crl_data_t back;
    uint32_t t[8], n;
    int g, a, mark, gc;
    reset();
    L.mode = CRL_MODE_REPLACE;
    take_dga();
    g = find_ev(67, 1);
    a = find_ev(68, 1);
    snprintf(name, sizeof name, "replace: the take D G Ab (G at %u for %u, A at %u)", g >= 0 ? (unsigned)L.d.ev[g].t : 0u,
             g >= 0 ? (unsigned)L.d.ev[g].dur : 0u, a >= 0 ? (unsigned)L.d.ev[a].t : 0u);
    ok(L.d.nev == 3 && g == 1 && a == 2 && L.d.ev[g].t < 144 && L.d.ev[g].t + L.d.ev[g].dur > 160 &&
       L.d.ev[a].t > 150 && L.d.ev[a].t < 300, name);
    ok(cr_loop_rec(&L, &C) == CRL_DID_REP_ARM && L.cap == CRL_CAP_OD_ARMED && L.rep, "replace: REC while playing arms it");
    until_pos(144);
    cr_mod(&C, CR_MOD_MAJ, 1); cr_key(&C, 65, 100, 1);  /* F held from tick 144 (G sounds there) */
    step(1);
    ok(L.cap == CRL_CAP_OD && L.rep_n == 1 && (L.d.ev[g].layer & CRL_HID), "replace: the chord opened the layer, G (sounding) hidden");
    gc = find_ev(67, 1);
    ok(gc >= 3 && L.d.ev[gc].t == L.d.ev[g].t && L.d.ev[gc].t + L.d.ev[gc].dur == 144 && L.d.ev[gc].layer == 1,
       "replace: G cut at 144 (a copy in the new layer, ending there)");
    until_pos(310);
    ok(L.d.ev[a].layer == (CRL_HID | 1u), "replace: A, starting inside the held span, hidden by layer 2");
    ok(!(L.d.ev[0].layer & CRL_HID), "replace: D, before the span, untouched");
    cr_key(&C, 65, 100, 0); cr_mod(&C, CR_MOD_MAJ, 0);
    step(1);
    ok(L.rep_n == 0, "replace: released, nothing more is erased");
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_END && L.d.nlayers == 2 && !L.rep, "replace: REC ends it: 2 layers");
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n == CRL_REC_HDR + 2u * 2u + 7u * 3u && cr_loop_unpack(buf, n, &back) && back.nev == 3,
       "replace: the record holds the live events only (D, G cut, F)");
    until_pos(380);
    mark = nlog;
    step(2000);
    snprintf(name, sizeof name, "replace: the next cycle plays D, G (cut), F; not Ab (%d %d %d %d)", ons_of(62, mark, t, 8),
             ons_of(67, mark, t, 8), ons_of(65, mark, t, 8), ons_of(68, mark, t, 8));
    ok(ons_of(62, mark, t, 8) == 1 && ons_of(65, mark, t, 8) == 1 && ons_of(68, mark, t, 8) == 0 &&
       ons_of(67, mark, t, 8) == 1, name);
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nlayers == 1 && L.d.nev == 3 && !(L.d.ev[g].layer & CRL_HID) &&
       !(L.d.ev[a].layer & CRL_HID) && L.d.ev[g].t + L.d.ev[g].dur > 160, "undo: the layer goes, G (whole) and Ab come back");
    until_pos(380);
    mark = nlog;
    step(2000);
    ok(ons_of(68, mark, t, 8) == 1 && ons_of(65, mark, t, 8) == 0, "undo: Ab plays again, F no longer");
    /* undo of a replace in progress restores too */
    cr_loop_rec(&L, &C);
    until_pos(10);
    cr_mod(&C, CR_MOD_MAJ, 1); cr_key(&C, 65, 100, 1);
    until_pos(200);
    ok((L.d.ev[g].layer & CRL_HID) && (L.d.ev[0].layer & CRL_HID), "replace again: D and G hidden while C is held");
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nev == 3 && !(L.d.ev[0].layer & CRL_HID) && !(L.d.ev[g].layer & CRL_HID),
       "undo while replacing: the layer in progress goes, D and G back");
    cr_key(&C, 65, 100, 0); cr_mod(&C, CR_MOD_MAJ, 0);
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "replace: no stuck, double or stray note");
}
static void step_chord(int note, cr_mod_t m)      /* a chord pressed and released, then every key up */
{
    chord(note, m, 60);
    step(5);
    cr_loop_step(&L, CRL_STEP_KEYSUP);
}
static void s_step(void)
{
    char name[128];
    uint32_t t[8], e[8], gg[8];
    int mark;
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();                                      /* a loop there before: replaced by the steps */
    L.mode = CRL_MODE_STEP;
    L.quant = 4;                                     /* 1/16 steps (24 ticks) */
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP && L.cap == CRL_CAP_STEP && L.state == CRL_STOPPED && L.step == 0 &&
       L.step_g == 24, "step: REC while playing stops the loop, step entry at step 1 (1/16)");
    step(200);
    ok(sounding() == 0, "step: the loop stopped");
    step_chord(62, CR_MOD_MAJ);
    ok(L.step == 1 && L.d.nev == 4 && L.d.ev[3].t == 0 && L.d.ev[3].dur == 24, "step: D written at step 1 (one step long), the cursor on 2");
    cr_loop_step(&L, CRL_STEP_KEYSUP);
    ok(L.step == 1, "step: keys up again with nothing written: the cursor stays");
    step_chord(64, CR_MOD_MIN);
    step_chord(68, CR_MOD_MAJ);
    ok(L.step == 3 && L.d.ev[4].t == 24 && L.d.ev[5].t == 48, "step: Em at step 2, Ab at step 3");
    cr_loop_step(&L, CRL_STEP_REST);
    ok(L.step == 4, "step: OCT+ a rest, the cursor on 5");
    cr_loop_step(&L, CRL_STEP_BACK);
    ok(L.step == 3 && L.d.nev == 6, "step: OCT- back to 4 (nothing erased)");
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP_DONE && L.d.nev == 3 && L.d.nlayers == 1 && L.state == CRL_STOPPED,
       "step: REC commits the steps as the loop (the old one gone), stopped");
    snprintf(name, sizeof name, "step: Free, 4 steps used (3 chords, a rest): the loop is 4 steps long, no rounding to "
             "a bar (len %u)", (unsigned)L.d.len);
    ok(L.d.len == 96, name);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(490);
    ok(ons_of(62, mark, t, 8) == 1 && ons_of(64, mark, e, 8) == 1 && ons_of(68, mark, gg, 8) == 1 &&
       near(e[0] - t[0], 125, 6) && near(gg[0] - t[0], 250, 6), "step: playback D, Em 125 ms later, Ab 250 ms (1/16s at 120)");
    step(520);
    ok(ons_of(62, mark, t, 8) == 3 && near(t[1] - t[0], 500, 6) && near(t[2] - t[1], 500, 6),
       "step: the 4-step loop comes round every 4 steps (500 ms)");
    cr_loop_play(&L, &C);
    step(300);
    /* nothing entered: the loop as it was; LOOP leaves and plays */
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP && cr_loop_rec(&L, &C) == CRL_DID_EMPTY_TAKE && L.d.nev == 3 && L.d.len == 96,
       "step: REC REC with nothing entered keeps the loop");
    /* three chords, Length Free: three steps (the owner, 2026-10-11); Quantize none: a step is a quarter note */
    L.quant = 0;
    cr_loop_rec(&L, &C);
    ok(L.step_g == CRL_STEP_NONE && CRL_STEP_NONE == 96, "step, Quantize none: a step is a quarter note");
    step_chord(62, CR_MOD_MAJ);
    step_chord(64, CR_MOD_MIN);
    step_chord(67, CR_MOD_MAJ);
    cr_loop_rec(&L, &C);
    snprintf(name, sizeof name, "step: 3 chords, Free: the loop is 3 steps long (len %u = 3 x 96), events at 0 96 192, "
             "a step long each", (unsigned)L.d.len);
    ok(L.d.len == 288 && L.d.nev == 3 && L.d.ev[0].t == 0 && L.d.ev[1].t == 96 && L.d.ev[2].t == 192 &&
       L.d.ev[0].dur == 96 && L.d.ev[2].dur == 96, name);
    L.quant = 2;
    cr_loop_rec(&L, &C);
    step_chord(62, CR_MOD_MAJ);
    step_chord(64, CR_MOD_MIN);
    step_chord(67, CR_MOD_MAJ);
    cr_loop_rec(&L, &C);
    ok(L.d.len == 144 && L.d.ev[2].t == 96, "step: 3 chords at Quantize 1/8: 3 eighths (len 144): the step follows Quantize");
    /* synced 1 bar: the cursor wraps; two gestures at one step; LOOP leaves and plays */
    L.sync = 1;
    L.quant = 2;                                     /* 1/8: 8 steps a bar */
    cr_loop_rec(&L, &C);
    ok(L.step_g == 48 && L.step_len == 384, "step, 1 bar at 1/8: 8 steps");
    cr_mod(&C, CR_MOD_MIN, 1);
    cr_key(&C, 62, 100, 1); step(30); cr_key(&C, 62, 100, 0); step(10);
    cr_key(&C, 69, 100, 1); step(30); cr_key(&C, 69, 100, 0); step(10);
    cr_mod(&C, CR_MOD_MIN, 0);
    cr_loop_step(&L, CRL_STEP_KEYSUP);
    ok(L.d.nev == 5 && L.d.ev[3].t == 0 && L.d.ev[4].t == 0 && L.step == 1, "step: two presses before every key is up: both at step 1");
    cr_loop_step(&L, CRL_STEP_BACK);
    cr_loop_step(&L, CRL_STEP_BACK);
    ok(L.step == 7, "step, synced: back past step 1 wraps to the bar's last step");
    cr_loop_step(&L, CRL_STEP_REST);
    ok(L.step == 0, "step, synced: the cursor wraps at the length");
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY && L.state == CRL_PLAYING && L.d.nev == 2 && L.d.len == 384,
       "step: a set Length (1 bar) as before: LOOP leaves step entry and plays the steps (2 events, the bar)");
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "step: no stuck, double or stray note");
}

/* 0.18: a Split Bass bass-side note is recorded as one and plays on the bass part (crl_ev_t.qx: CRL_X_BASS; the
 * record's flags byte CRL_RF_PART) */
static int ons_s(int s, int note, int from)          /* note-ons of note on stream s since log entry from (-1: any) */
{
    int k, n = 0;
    for (k = from; k < nlog; k++)
        n += LOG[k].k == '+' && LOG[k].s == s && (note < 0 || LOG[k].note == note);
    return n;
}
static void note(int n, uint32_t hold)               /* a key alone */
{
    cr_key(&C, (uint8_t)n, 100, 1);
    step(hold);
    cr_key(&C, (uint8_t)n, 100, 0);
}
static int nbass(const crl_data_t *d)
{
    int i, n = 0;
    for (i = 0; i < d->nev; i++) n += !(d->ev[i].layer & CRL_HID) && CRL_EV_BASS(&d->ev[i]);
    return n;
}
/* what 0.14 .. 0.17's cr_loop_unpack accepts (its checks, as released): a 0.18 record must pass them */
static int old_unpack_ok(const uint8_t *b, uint32_t n)
{
    uint32_t nl, nev, len, l, cnt = 0, o, i;
    if (n < 16u || b[0] != 'C' || b[1] != 'R' || b[2] != 'L' || b[3] != '1' || b[4] != 1u || b[5] >= CRL_NSIG ||
        (b[14] | (uint32_t)b[15] << 8) != CRL_PPQN)
        return 0;
    nl = b[6];
    len = b[8] | (uint32_t)b[9] << 8 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 24;
    nev = b[12] | (uint32_t)b[13] << 8;
    if (!nl || nl > CRL_MAX_LAYERS || !nev || nev > CRL_MAX_EV || !len || len > CRL_MAX_LEN || n < 16u + 2u * nl + 7u * nev)
        return 0;
    for (l = 0; l < nl; l++) cnt += b[16u + 2u * l] | (uint32_t)b[17u + 2u * l] << 8;
    if (cnt != nev) return 0;
    for (i = 0, o = 16u + 2u * nl; i < nev; i++, o += 7u)
        if ((b[o] | (uint32_t)b[o + 1] << 8) >= len || !(b[o + 2] | b[o + 3]) || b[o + 4] > 127u || !b[o + 5] ||
            b[o + 5] > 127u || (b[o + 6] >> 4) >= CR_Q_COUNT)
            return 0;
    return 1;
}
static void s_bass(void)
{
    char name[200];
    static uint8_t buf[CRL_REC_MAX];
    static crl_data_t back;
    uint32_t n, i, o;
    int mark, c, k;
    /* the Full keyboard, the split at F4: F3 and G3 on the bass side, C5 above */
    reset();
    cr_set_keyboard(&C, 1, 65);
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    mark = nlog;
    cr_loop_rec(&L, &C);
    note(53, 500);
    note(72, 500);
    note(55, 500);
    step(500);
    ok(ons_s(1, 53, mark) == 1 && ons_s(0, 53, mark) == 0 && ons_s(0, 72, mark) == 1,
       "split bass, live: F3 on the bass stream alone, C5 on the main one");
    mark = nlog;
    ok(cr_loop_rec(&L, &C) == CRL_DID_COMMIT && L.d.nev == 3, "split bass: a take of 3 notes");
    ok(CRL_EV_BASS(&L.d.ev[0]) && L.d.ev[0].root == 53 && L.d.ev[1].qx == 0 && L.d.ev[1].root == 72 &&
       CRL_EV_BASS(&L.d.ev[2]) && L.d.ev[2].root == 55, "split bass: F3 and G3 recorded as bass-side notes, C5 (above the split) not");
    step(1990);
    snprintf(name, sizeof name, "playback: F3 and G3 on the bass stream alone, C5 on the main one (bass %d %d, main %d %d %d)",
             ons_s(1, 53, mark), ons_s(1, 55, mark), ons_s(0, 53, mark), ons_s(0, 55, mark), ons_s(0, 72, mark));
    ok(ons_s(1, 53, mark) == 1 && ons_s(1, 55, mark) == 1 && ons_s(1, -1, mark) == 2 && ons_s(0, -1, mark) == 1 &&
       ons_s(0, 72, mark) == 1, name);
    /* what was recorded stays on its part, whatever is set at playback */
    cr_set_single_notes(&C, CR_SINGLE_FULL, 5);
    mark = nlog;
    step(2000);
    ok(ons_s(1, 53, mark) == 1 && ons_s(1, 55, mark) == 1 && ons_s(0, -1, mark) == 1,
       "Single = Full at playback: the bass-side notes still on the bass part");
    cr_set_single_notes(&C, CR_SINGLE_SPLIT, 5);
    cr_set_keyboard(&C, 0, 65);
    cr_set_bass(&C, 0);
    mark = nlog;
    step(2000);
    ok(ons_s(1, 53, mark) == 1 && ons_s(1, 55, mark) == 1 && ons_s(0, -1, mark) == 1 && ons_s(0, 72, mark) == 1,
       "Split Oct, the Chords layout, Bass off at playback: the same");
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    cr_set_keyboard(&C, 1, 80);                      /* the split moved above C5 */
    mark = nlog;
    step(2000);
    ok(ons_s(0, 72, mark) == 1 && ons_s(1, 72, mark) == 0,
       "the split moved above a recorded chord-side note: it stays on the chord part");
    cr_set_keyboard(&C, 1, 65);
    /* the bass register of now moves it (as a live bass-side note); the voicing and transpose do not */
    cr_bass_voicing_step(&C, 1);
    step(2000);
    mark = nlog;
    step(2000);
    ok(ons_s(1, 65, mark) == 1 && ons_s(1, 67, mark) == 1 && ons_s(1, -1, mark) == 2 && ons_s(0, 65, mark) == 0,
       "the bass register + 1 at playback: the looped bass notes an octave up");
    cr_bass_voicing_step(&C, -1);
    cr_voicing_step(&C, 1);
    cr_set_transpose(&C, 5);
    step(2000);
    mark = nlog;
    step(2000);
    ok(ons_s(1, 53, mark) == 1 && ons_s(1, 55, mark) == 1 && ons_s(1, -1, mark) == 2,
       "voicing + 1 and transpose + 5 at playback: the bass notes as recorded");
    cr_voicing_step(&C, -1);
    cr_set_transpose(&C, 0);
    /* live over the loop, on the one bass part: no stuck note when either ends */
    until_pos(380);
    until_pos(10);
    ok(SND[1][53] == 1, "the looped F3 sounds on the bass stream");
    cr_key(&C, 50, 100, 1);
    step(50);
    ok(SND[1][50] == 1 && SND[1][53] == 1, "a live bass-side D3 over it: both notes on (the part's voice mode decides)");
    cr_key(&C, 50, 100, 0);
    step(5);
    ok(SND[1][50] == 0 && SND[1][53] == 1, "the live note's end ends the live note only");
    cr_key(&C, 53, 100, 1);                          /* the same pitch: shared (re-struck), the last owner ends it */
    step(5);
    ok(cr_sounding(&C, CR_STREAM_BASS, 53) == 2, "a live F3 over the looped F3: one note, two owners");
    step(600);
    ok(SND[1][53] == 1 && cr_sounding(&C, CR_STREAM_BASS, 53) == 1, "the loop's F3 ended: the held live one still sounds");
    cr_key(&C, 53, 100, 0);
    step(5);
    ok(SND[1][53] == 0, "the live F3 let go: off");
    cr_key(&C, 55, 100, 1);                          /* held live across the looped G3's whole length */
    until_pos(380);
    ok(SND[1][55] == 1, "a live G3 held through the looped G3: still sounding after the loop's ended");
    cr_key(&C, 55, 100, 0);
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "live over a looped bass: no stuck, double or stray note");
    /* the other way: a note recorded with Split Bass off is a chord-side note, with Split Bass on at playback too */
    cr_loop_clear(&L, &C);
    cr_set_single_notes(&C, CR_SINGLE_FULL, 5);
    cr_loop_rec(&L, &C);
    note(53, 500);
    step(500);
    cr_loop_rec(&L, &C);
    ok(L.d.nev == 1 && L.d.ev[0].qx == 0, "Single = Full: F3 recorded as a chord-side note");
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n == CRL_REC_HDR + 2u + 7u && buf[7] == 0, "pack: no bass-side note: the flags byte 0 (the record as 0.17 wrote it)");
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    mark = nlog;
    step(2000);
    ok(ons_s(0, 53, mark) >= 1 && ons_s(1, -1, mark) == 0, "Split Bass on at playback: it stays on the chord part");
    cr_loop_play(&L, &C);
    step(300);

    /* the Chords layout: the split by pitch class (F): G a chord, D on the bass side */
    reset();
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    cr_loop_rec(&L, &C);
    chord(67, CR_MOD_MAJ, 500);
    note(62, 500);
    step(1000);
    mark = nlog;
    cr_loop_rec(&L, &C);
    ok(L.d.nev == 2 && (L.d.ev[0].qx >> 4) == CR_Q_MAJ && !CRL_EV_BASS(&L.d.ev[0]) && CRL_EV_BASS(&L.d.ev[1]) &&
       L.d.ev[1].root == 62, "the Chords layout: G major a chord, D4 (below the split point) a bass-side note");
    step(1990);
    ok(ons_s(1, 62, mark) == 1 && ons_s(1, -1, mark) == 1 && ons_s(0, 62, mark) == 0 && ons_s(0, 67, mark) == 1,
       "playback: D on the bass stream, G major on the main one, nothing follows the chord (Split Bass)");
    /* Split Bass off at playback, Bass on: the bass follows the looped chord AND the recorded bass notes play */
    cr_set_single_notes(&C, CR_SINGLE_FULL, 5);
    cr_set_bass(&C, 1);
    mark = nlog;
    step(2000);
    snprintf(name, sizeof name,
             "Single = Full + Bass on at playback: the follower's G1 and the recorded D4, both on the bass stream (%d %d of %d)",
             ons_s(1, 43, mark), ons_s(1, 62, mark), ons_s(1, -1, mark));
    ok(ons_s(1, 43, mark) == 1 && ons_s(1, 62, mark) == 1 && ons_s(1, -1, mark) == 2 && ons_s(0, 62, mark) == 0, name);
    cr_set_bass(&C, 0);
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    /* overdub (quantized), undo */
    L.quant = 2;                                     /* 1/8: 48 ticks */
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_ARM, "overdub armed");
    until_pos(300);
    note(60, 100);
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_END && L.d.nlayers == 2 && L.d.nev == 3 && CRL_EV_BASS(&L.d.ev[2]) &&
       L.d.ev[2].root == 60 && L.d.ev[2].t == 288 && L.d.ev[2].layer == 1,
       "overdub: C4 a bass-side note of layer 2, quantized to the 1/8");
    L.quant = 0;
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nev == 2 && CRL_EV_BASS(&L.d.ev[1]) && !CRL_EV_BASS(&L.d.ev[0]),
       "undo: the layer goes, the first layer's parts kept");
    /* Replace: the looped D cut by a bass-side C; the cut copy and the new note are bass-side */
    L.mode = CRL_MODE_REPLACE;
    cr_loop_rec(&L, &C);
    until_pos((uint32_t)L.d.ev[1].t + 40u);
    note(60, 100);
    c = -1;
    for (k = 2; k < L.d.nev; k++) if (L.d.ev[k].root == 62) c = k;
    ok((L.d.ev[1].layer & CRL_HID) && c >= 2 && CRL_EV_BASS(&L.d.ev[c]) && L.d.ev[c].dur == 40,
       "replace: the looped D hidden, its cut copy a bass-side note");
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_END && L.d.nlayers == 2 && nbass(&L.d) == 2,
       "replace: 2 layers, the cut D and the new C on the bass side");
    until_pos(380);
    mark = nlog;
    step(2000);
    ok(ons_s(1, 62, mark) == 1 && ons_s(1, 60, mark) == 1 && ons_s(1, -1, mark) == 2 && ons_s(0, 62, mark) == 0 &&
       ons_s(0, 60, mark) == 0, "replace: the next cycle plays both on the bass stream");
    /* the record: the part of every event kept; 0.17 reads it */
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n == CRL_REC_HDR + 2u * 2u + 7u * 3u && buf[4] == 1 && buf[7] == CRL_RF_PART,
       "pack: version 1, the flags byte says single notes carry their part");
    ok(old_unpack_ok(buf, n), "pack: 0.14 .. 0.17's unpack accepts the record (it reads neither byte 7 nor a single note's nibble)");
    ok(cr_loop_unpack(buf, n, &back) == 1 && back.nev == 3 && nbass(&back) == 2, "unpack: 3 events, 2 of them bass-side");
    for (i = 0, k = 0, c = 1; i < L.d.nev; i++) {
        if (L.d.ev[i].layer & CRL_HID) continue;
        c = c && back.ev[k].root == L.d.ev[i].root && back.ev[k].qx == L.d.ev[i].qx && back.ev[k].t == L.d.ev[i].t &&
            back.ev[k].dur == L.d.ev[i].dur;
        k++;
    }
    ok(c && k == 3, "unpack: every event's root, time, length and part as packed");
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nev == 2 && !(L.d.ev[1].layer & CRL_HID) && CRL_EV_BASS(&L.d.ev[1]),
       "undo: the whole D back, a bass-side note");
    cr_loop_play(&L, &C);
    step(300);
    /* a slot loaded: plays as recorded, Single = Full now */
    cr_set_single_notes(&C, CR_SINGLE_FULL, 5);
    cr_loop_set(&L, &C, &back);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(1990);
    ok(ons_s(1, 62, mark) == 1 && ons_s(1, 60, mark) == 1 && ons_s(0, 67, mark) == 1 && ons_s(0, 62, mark) == 0,
       "a loaded slot: its bass-side notes on the bass stream");
    cr_loop_play(&L, &C);
    step(300);
    /* an older record (flags 0): a single note's nibble was the extension keys held, never heard: not a part */
    buf[7] = 0;
    ok(cr_loop_unpack(buf, n, &back) == 1 && back.nev == 3 && nbass(&back) == 0 && (back.ev[0].qx >> 4) == CR_Q_MAJ,
       "unpack, a 0.17 record (flags 0) with the nibble set on single notes: no bass-side note, the chord as it was");
    cr_loop_set(&L, &C, &back);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(1990);
    ok(ons_s(1, -1, mark) == 0 && ons_s(0, 62, mark) == 1 && ons_s(0, 60, mark) == 1,
       "it plays as 0.17 played it: single notes of the chord part");
    cr_loop_play(&L, &C);
    step(300);
    /* a chord's extensions are not the flag's; a single note's other nibble bits are dropped */
    buf[7] = CRL_RF_PART;
    o = CRL_REC_HDR + 2u * 2u;
    buf[o + 6] = (uint8_t)(CR_Q_MIN << 4 | 0x0B);
    buf[o + 7 + 6] = 0x0E;
    buf[o + 14 + 6] = 0x0F;
    ok(cr_loop_unpack(buf, n, &back) == 1 && back.ev[0].qx == (CR_Q_MIN << 4 | 0x0B) && back.ev[1].qx == 0 &&
       back.ev[2].qx == CRL_X_BASS, "unpack: a chord's extensions kept; a single note: the part bit alone");

    /* step entry: a bass-side key is a bass-side step */
    reset();
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    L.mode = CRL_MODE_STEP;
    L.quant = 4;                                     /* (1/16 steps in a 1-bar loop) */
    L.sync = 1;
    cr_loop_rec(&L, &C);
    note(62, 60); step(5); cr_loop_step(&L, CRL_STEP_KEYSUP);
    step_chord(67, CR_MOD_MAJ);
    note(60, 60); step(5); cr_loop_step(&L, CRL_STEP_KEYSUP);
    mark = nlog;
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY && L.d.nev == 3 && CRL_EV_BASS(&L.d.ev[0]) && L.d.ev[0].t == 0 &&
       !CRL_EV_BASS(&L.d.ev[1]) && CRL_EV_BASS(&L.d.ev[2]) && L.d.ev[2].t == 48,
       "step: D and C entered as bass-side steps, G major a chord step");
    step(1990);
    ok(ons_s(1, 62, mark) == 1 && ons_s(1, 60, mark) == 1 && ons_s(1, -1, mark) == 2 && ons_s(0, 67, mark) == 1 &&
       ons_s(0, 62, mark) == 0, "step: the loop plays them on the bass stream");
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "split bass loops: no stuck, double or stray note");
}

/* ---- the Rate (0.18): the loop's own time against the tempo */
static void s_rate(void)
{
    static const int WANT[CRL_NRATE] = {1, 2, 3, 4, 6, 8, 16};      /* a 1-bar loop's starts in 4 bars */
    static const char *const NM[CRL_NRATE] = {"x1/4", "x1/2", "x3/4", "x1", "x1.5", "x2", "x4"};
    char name[160];
    uint32_t t[40];
    int r, n, mark, good = 1;
    reset();
    L.sync = 1;
    L.count_in = 0;
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 200);                      /* D on the bar's first beat */
    step(1900);
    ok(L.state == CRL_PLAYING && L.d.len == 384 && L.d.nev == 1 && L.rate == CRL_RATE_1, "rate: a 1-bar loop, x1 by default");
    cr_loop_play(&L, &C);
    step(400);
    for (r = 0; r < (int)CRL_NRATE; r++) {
        cr_loop_set_rate(&L, r);
        mark = nlog;
        cr_loop_play(&L, &C);
        step(7990);                                  /* 4 bars at 120 */
        n = ons_of(62, mark, t, 40);
        cr_loop_play(&L, &C);
        step(600);
        snprintf(name, sizeof name, "rate %s: the 1-bar loop starts %d times in 4 bars (want %d), evenly (%u ms apart)",
                 NM[r], n, WANT[r], n > 1 ? (unsigned)(t[1] - t[0]) : 0u);
        ok(n == WANT[r] && (n < 2 || (near(t[1] - t[0], 8000u / (unsigned)WANT[r], 8) &&
                                      near(t[n - 1] - t[0], 8000u / (unsigned)WANT[r] * (unsigned)(n - 1), 8))), name);
        good &= sounding() == 0;
    }
    ok(good && dup_on == 0 && stray_off == 0, "rate: every rate stops clean (no stuck, double or stray note)");
    /* a change while it plays: taken at the loop's start, never in the middle of a cycle */
    cr_loop_set_rate(&L, CRL_RATE_1);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(500);
    cr_loop_set_rate(&L, 5);                         /* x2 asked a beat in */
    ok(L.rate == CRL_RATE_1 && L.rate_want == 5, "rate: changed while playing: the old rate until the loop's start");
    step(3490);
    n = ons_of(62, mark, t, 40);
    ok(n == 3 && near(t[1] - t[0], 2000, 6) && near(t[2] - t[1], 1000, 6) && L.rate == 5,
       "rate: x1 -> x2 while playing: the cycle in progress ends at x1 (2 s), the next ones last 1 s: no jump, on the beat");
    cr_loop_play(&L, &C);
    step(400);
    /* the click and the count-in are the tempo's */
    cr_loop_metro(&L, 1);
    memset(clicks, 0, sizeof clicks);
    cr_loop_play(&L, &C);
    step(3990);
    ok(clicks[1] + clicks[2] == 8 && clicks[2] == 4, "rate x2: the click stays the tempo's (8 beats in 2 bars; the accent on each loop start)");
    cr_loop_play(&L, &C);
    cr_loop_metro(&L, 0);
    step(300);
    cr_loop_set_rate(&L, 2);                         /* x3/4: the loop's start falls off the beat 2 times in 3 */
    cr_loop_metro(&L, 1);
    memset(clicks, 0, sizeof clicks);
    cr_loop_play(&L, &C);
    step(7990);
    ok(clicks[1] + clicks[2] == 16, "rate x3/4: 16 clicks in 4 bars: the click keeps the tempo where the loop's start is off the beat");
    cr_loop_play(&L, &C);
    cr_loop_metro(&L, 0);
    step(300);
    L.mode = CRL_MODE_OVERWRITE;
    L.count_in = 1;
    cr_loop_set_rate(&L, 6);                         /* x4 */
    cr_loop_rec(&L, &C);
    step(1990);
    ok(L.cap == CRL_CAP_COUNTIN, "rate x4: the count-in is a bar of the tempo (still counting after 1.99 s)");
    step(20);
    ok(L.cap == CRL_CAP_REC, "rate x4: .. and the take starts after it");
    cr_key(&C, 64, 100, 1);
    step(100);
    cr_key(&C, 64, 100, 0);
    step(370);
    ok(L.cap == CRL_CAP_REC, "rate x4: a 1-bar take is a bar of the LOOP's time: still recording at 490 ms");
    step(30);
    ok(L.cap == CRL_CAP_NONE && L.state == CRL_PLAYING && L.d.len == 384, "rate x4: .. and done at 500 ms (len 384 loop ticks)");
    cr_loop_play(&L, &C);
    step(300);
    /* recorded at a rate, played at that rate: what was played */
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    cr_loop_set_rate(&L, 5);                         /* x2, Free */
    cr_loop_rec(&L, &C);
    note(62, 200);
    step(300);
    note(65, 200);
    step(300);
    cr_loop_rec(&L, &C);                             /* 1 s played */
    ok(L.d.len == 384 && L.d.nev == 2 && L.d.ev[1].t == 192, "rate x2: a 1 s take is 384 loop ticks, the second note at 192: the events are in loop time");
    step(900);
    mark = nlog;
    step(2000);
    { uint32_t a[8], b[8];
      n = ons_of(62, mark, a, 8);
      ok(n == 2 && ons_of(65, mark, b, 8) == 2 && near(a[1] - a[0], 1000, 6) && near(b[0] - a[0], 500, 6),
         "rate x2: played back at x2 it is what was played (D, F 500 ms later, round every second)"); }
    cr_loop_play(&L, &C);
    step(300);
    cr_loop_set_rate(&L, CRL_RATE_1);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(1990);
    { uint32_t a[8], b[8];
      ok(ons_of(62, mark, a, 8) == 1 && ons_of(65, mark, b, 8) == 1 && near(b[0] - a[0], 1000, 6),
         "rate: the same loop at x1: half the speed (F a second after D)"); }
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "rate: no stuck, double or stray note");
}

/* ---- the loop editor (0.18): the loop as steps, read from its events */
static crl_edmap_t M;
static void ed_at(uint32_t cell)                     /* the cursor on the step cell is in (as the UI does) */
{
    uint32_t c0, n;
    cr_loop_ed_map(&L, &M);
    cr_loop_ed_step(&M, cell, &c0, &n);
    cr_loop_ed_cursor(&L, c0, n);
}
static int live_at(uint32_t t)                       /* live events starting at tick t */
{
    int i, n = 0;
    for (i = 0; i < L.d.nev; i++) n += !(L.d.ev[i].layer & CRL_HID) && L.d.ev[i].t == t;
    return n;
}
static int root_at(uint32_t t)                       /* the root of the last live event at tick t (-1: none) */
{
    int i, r = -1;
    for (i = 0; i < L.d.nev; i++)
        if (!(L.d.ev[i].layer & CRL_HID) && L.d.ev[i].t == t) r = L.d.ev[i].root;
    return r;
}
static void steps3(void)                             /* D, Em, G entered as quarter-note steps, Length Free */
{
    reset();
    L.mode = CRL_MODE_STEP;
    L.quant = 1;
    cr_loop_rec(&L, &C);
    step_chord(62, CR_MOD_MAJ);
    step_chord(64, CR_MOD_MIN);
    step_chord(67, CR_MOD_MAJ);
    cr_loop_rec(&L, &C);
}
static void s_edit(void)
{
    static uint8_t buf[CRL_REC_MAX];
    static crl_data_t back;
    static cr_loop_t K;
    char name[200];
    uint32_t n, c0, k, i;
    int mark;
    steps3();
    ok(cr_loop_ed_open(&L) == CRL_ED_OK && L.ed && L.ed_g == 96 && L.ed_len == 288, "edit: opens on a step loop, the grid the Quantize value (1/4)");
    cr_loop_ed_map(&L, &M);
    ok(M.ncell == 3 && M.nstep == 3 && M.start[0] == 7u && M.cont[0] == 0u, "edit: the step loop is its steps exactly: 3 cells, 3 steps");
    /* a chord played replaces the step, and sounds */
    ed_at(1);
    mark = nlog;
    chord(69, CR_MOD_MIN, 60);
    ok(L.d.nev == 3 && live_at(96) == 1 && root_at(96) == 69 && root_at(0) == 62 && root_at(192) == 67 && L.dirty,
       "edit: a chord played replaces the selected step's notes (Em -> Am), the others untouched");
    ok(ons_of(69, mark, &n, 1) == 1, "edit: .. and it sounds (the keys play)");
    ok(L.ed_t0 == 96, "edit: the cursor stays on the step");
    step(50);
    cr_mod(&C, CR_MOD_MAJ, 1);                       /* two roots in one push: both in the step */
    cr_key(&C, 60, 100, 1); step(20); cr_key(&C, 64, 100, 1); step(40);
    cr_key(&C, 60, 100, 0); step(10); cr_key(&C, 64, 100, 0);
    cr_mod(&C, CR_MOD_MAJ, 0);
    step(20);
    ok(L.d.nev == 4 && live_at(96) == 2, "edit: two roots held together: the old chord goes once, both are in the step");
    chord(65, CR_MOD_MAJ, 60);
    step(20);
    ok(L.d.nev == 3 && live_at(96) == 1 && root_at(96) == 65, "edit: the next push replaces again");
    /* Split Bass: a bass-side note replaces the bass side only */
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    note(62, 60);                                    /* D4: below the split F: the bass side */
    step(20);
    ok(live_at(96) == 2 && nbass(&L.d) == 1, "edit: a bass-side note joins the step: the chord side is kept (Split Bass's rule)");
    note(60, 60);
    step(20);
    ok(live_at(96) == 2 && nbass(&L.d) == 1 && root_at(96) == 60, "edit: another bass-side note replaces the bass side alone");
    chord(69, CR_MOD_MIN, 60);                       /* A4: above the split: the chord side */
    step(20);
    ok(live_at(96) == 2 && nbass(&L.d) == 1, "edit: a chord replaces the chord side alone");
    cr_set_single_notes(&C, CR_SINGLE_FULL, 5);
    /* REC: the step cleared */
    ed_at(1);
    ok(cr_loop_ed_clear(&L) == 2 && live_at(96) == 0 && L.d.nev == 2, "edit: clear: the step's notes go (both sides)");
    cr_loop_ed_map(&L, &M);
    ok(M.nstep == 3 && M.start[0] == 5u, "edit: .. the step stays, empty: still 3 steps");
    ok(cr_loop_ed_steplen(&L, 2) == 0, "edit: an empty step is one cell: its length is refused");
    chord(64, CR_MOD_MIN, 60);
    step(20);
    /* the step's length: the later events move, Free: the loop's length follows */
    ed_at(1);
    ok(cr_loop_ed_steplen(&L, 3) == 1 && L.d.len == 480 && root_at(384) == 67 && live_at(192) == 0,
       "edit: step 2 three cells long: the later step moves (G from 192 to 384), Length Free: the loop grows (480)");
    cr_loop_ed_map(&L, &M);
    cr_loop_ed_step(&M, 2, &c0, &n);
    ok(M.ncell == 5 && M.nstep == 3 && c0 == 1 && n == 3 && M.cont[0] == 0xCu, "edit: .. still 3 steps: cells 2 and 3 continue step 2");
    for (i = k = 0; i < L.d.nev; i++) k += L.d.ev[i].t == 96 && L.d.ev[i].dur == 288;
    ok(k == 1, "edit: .. its chord sounds to its end (dur 288)");
    ok(cr_loop_ed_index(&M, 4) == 2, "edit: the step after it is number 3");
    ed_at(1);
    ok(cr_loop_ed_steplen(&L, 2) == 1 && L.d.len == 384 && root_at(192 + 96) == 67, "edit: shorter again (2 cells): G back at 288, the loop 384");
    ok(cr_loop_ed_steplen(&L, 17) == 0 && cr_loop_ed_steplen(&L, 0) == 0, "edit: 1 .. 16 cells");
    /* the velocity */
    ed_at(0);
    ok(cr_loop_ed_vel(&L, 0) == 100 && cr_loop_ed_vel(&L, -40) == 60 && L.d.ev[0].vel == 60 && cr_loop_ed_vel(&L, 100) == 127,
       "edit: the step's velocity (100 -> 60 -> 127, clamped)");
    /* the record carries it: pack / unpack give the same steps; a 0.14 .. 0.17 reader takes the record */
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n > 0 && buf[7] == 0 && buf[4] == 1 && old_unpack_ok(buf, n), "edit: an edited loop's record: version 1, flags 0, read by 0.14 .. 0.17");
    ok(cr_loop_unpack(buf, n, &back) == 1 && back.len == 384 && back.nev == L.d.nev, "edit: unpack: the same loop");
    memset(&K, 0, sizeof K);
    K.d = back;
    K.ed_g = 96;
    K.ed_len = back.len;
    { static crl_edmap_t M2;
      cr_loop_ed_map(&L, &M);
      cr_loop_ed_map(&K, &M2);
      ok(M2.ncell == 4 && M2.nstep == 3 && !memcmp(&M, &M2, sizeof M),
         "edit: per-step lengths survive the record with no new field (the steps are read from t and dur)"); }
    /* the loop's length in steps: shorter hides the last step, longer brings it back; a save writes what sounds */
    cr_loop_ed_map(&L, &M);
    cr_loop_ed_step(&M, M.ncell - 1u, &c0, &n);
    ok(c0 == 3 && cr_loop_ed_len(&L, c0 * 96u) == 1 && L.d.len == 288 && L.d.nev == 3, "edit: loop length -1 step: 288 ticks, the last step's event kept in RAM");
    cr_loop_ed_map(&L, &M);
    ok(M.ncell == 3 && M.nall == 4 && M.nstep == 2, "edit: .. 2 steps now, the hidden one known (4 cells in all)");
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n > 0 && cr_loop_unpack(buf, n, &back) == 1 && back.len == 288 && back.nev == 2 && old_unpack_ok(buf, n),
       "edit: a save while it is hidden writes what sounds (2 events, a valid record)");
    ok(cr_loop_ed_len(&L, 384) == 1, "edit: loop length +1 step");
    cr_loop_ed_map(&L, &M);
    ok(M.nstep == 3 && root_at(288) == 67, "edit: .. the hidden step is back (G)");
    ok(cr_loop_ed_len(&L, 480) == 1, "edit: longer than it was: an empty step added");
    cr_loop_ed_map(&L, &M);
    ok(M.nstep == 4 && M.ncell == 5 && !((M.start[0] >> 4) & 1u), "edit: .. 4 steps, the new one empty");
    cr_loop_ed_len(&L, 288);
    cr_loop_ed_close(&L, &C);
    ok(!L.ed && L.d.nev == 2 && L.d.len == 288 && L.state == CRL_STOPPED, "edit: closed with the loop shortened: the hidden step's event is gone for good");
    for (i = k = 0; i < L.d.nev; i++) k += L.d.ev[i].t >= L.d.len;
    ok(k == 0, "edit: no event beyond the end outside the editor");
    /* playing while editing: heard in place, nothing stuck */
    cr_loop_ed_open(&L);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(700);
    ed_at(0);
    chord(72, CR_MOD_MAJ, 80);
    step(100);
    ed_at(1);
    cr_loop_ed_steplen(&L, 1);
    cr_loop_ed_len(&L, 96);                          /* shortened under the playhead */
    step(1500);
    ok(L.state == CRL_PLAYING && ons_of(72, mark, &n, 1) >= 1, "edit: LOOP plays in the editor, the new chord is heard in place");
    cr_loop_play(&L, &C);
    step(400);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "edit: edits under a playing loop: no stuck, double or stray note");
    cr_loop_ed_close(&L, &C);

    /* a set Length: the loop keeps it; a step may not push a note past the end */
    reset();
    L.mode = CRL_MODE_STEP;
    L.quant = 1;
    L.sync = 1;
    cr_loop_rec(&L, &C);
    step_chord(62, CR_MOD_MAJ);
    step_chord(64, CR_MOD_MIN);
    step_chord(65, CR_MOD_MAJ);
    step_chord(67, CR_MOD_MAJ);
    cr_loop_rec(&L, &C);
    cr_loop_ed_open(&L);
    ed_at(0);
    ok(L.d.len == 384 && cr_loop_ed_steplen(&L, 2) == 0 && root_at(288) == 67 && L.d.len == 384,
       "edit, Length 1 bar: a longer step would push the last chord past the end: refused, nothing moved");
    ed_at(3);
    cr_loop_ed_clear(&L);
    ed_at(0);
    ok(cr_loop_ed_steplen(&L, 2) == 1 && L.d.len == 384 && root_at(192) == 64 && root_at(288) == 65,
       "edit, Length 1 bar: with the last step empty it fits: the others move, the loop is still a bar");
    cr_loop_ed_close(&L, &C);

    /* a loop played free: the grid is Quantize; none: it does not open, nothing is requantised */
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();
    cr_loop_play(&L, &C);
    step(300);
    { uint16_t t0 = L.d.ev[0].t, t1 = L.d.ev[1].t, t2 = L.d.ev[2].t, d1 = L.d.ev[1].dur;
      L.quant = 0;
      ok(cr_loop_ed_open(&L) == CRL_ED_NOQUANT && !L.ed, "edit: a played loop with Quantize none: refused (no grid)");
      L.quant = 1;
      ok(cr_loop_ed_open(&L) == CRL_ED_OK && L.d.ev[0].t == t0 && L.d.ev[1].t == t1 && L.d.ev[2].t == t2 && L.d.ev[1].dur == d1,
         "edit: with Quantize 1/4 it opens; no event is moved (nothing requantised)");
      cr_loop_ed_map(&L, &M);
      cr_loop_ed_step(&M, 1, &c0, &n);
      snprintf(name, sizeof name, "edit: D (a beat), G (two beats), Ab: 4 cells, 3 steps, G's step 2 cells (map %X / %X, step %u+%u)",
               (unsigned)M.start[0], (unsigned)M.cont[0], (unsigned)c0, (unsigned)n);
      ok(M.ncell == 4 && M.nstep == 3 && M.start[0] == 0xBu && M.cont[0] == 4u && c0 == 1 && n == 2, name); }
    cr_loop_ed_close(&L, &C);
    ok(L.d.nev == 3 && L.d.len == 384, "edit: opened and closed: the loop as it was");
    L.cap = CRL_CAP_NONE;
    cr_loop_rec(&L, &C);                             /* (armed: a capture is open) */
    ok(cr_loop_ed_open(&L) == CRL_ED_BUSY, "edit: not while a take is armed");
    cr_loop_rec(&L, &C);

    /* layers: what Replace hid goes when the editor opens; the last layer takes the editor's notes; undo holds */
    reset();
    take_dga();
    L.mode = CRL_MODE_REPLACE;
    cr_loop_rec(&L, &C);
    until_pos(380);
    chord(72, CR_MOD_MAJ, 700);                      /* over D: hidden */
    cr_loop_rec(&L, &C);
    cr_loop_play(&L, &C);
    step(300);
    for (i = k = 0; i < L.d.nev; i++) k += (L.d.ev[i].layer & CRL_HID) != 0;
    L.quant = 1;
    { uint32_t nev0 = L.d.nev, live0 = L.d.nev - k;
      ok(k >= 1 && L.d.nlayers == 2 && cr_loop_ed_open(&L) == CRL_ED_OK, "edit: a loop with a Replace layer opens");
      cr_loop_ed_close(&L, &C);
      for (i = n = 0; i < L.d.nev; i++) n += (L.d.ev[i].layer & CRL_HID) != 0;
      ok(n == k && L.d.nev == nev0 && L.d.nlayers == 2, "edit: opened and closed without an edit: the layers and what Replace hid as they were");
      cr_loop_ed_open(&L);
      ed_at(3);
      cr_loop_ed_vel(&L, 0);
      ok(L.d.nlayers == 2, "edit: looking (the velocity read) is no edit");
      chord(60, CR_MOD_MAJ, 60);
      step(20);
      for (i = n = 0; i < L.d.nev; i++) n += (L.d.ev[i].layer & CRL_HID) != 0 || L.d.ev[i].layer != 0;
      snprintf(name, sizeof name, "edit: the first edit makes the loop one layer: what Replace hid is gone (%u live events + the new one = %u)",
               (unsigned)live0, (unsigned)L.d.nev);
      ok(n == 0 && L.d.nlayers == 1 && L.d.nev >= 1 && L.d.ev[L.d.nev - 1].t == 288, name); }
    cr_loop_ed_close(&L, &C);
    ok(cr_loop_undo(&L, &C) == CRL_DID_NOTHING && L.d.nlayers == 1, "edit: REC held (undo) after an edit: nothing to undo");
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n > 0 && cr_loop_unpack(buf, n, &back) == 1 && back.nlayers == 1, "edit: .. and the loop packs to a valid one-layer record");

    /* an empty loop opens as an empty step loop */
    reset();
    L.quant = 0;
    ok(cr_loop_ed_open(&L) == CRL_ED_OK && L.d.len == 384 && L.ed_g == 96 && L.state == CRL_STOPPED,
       "edit: an empty slot (Quantize none, Length Free): an empty loop of a bar, quarter-note steps");
    cr_loop_ed_map(&L, &M);
    ok(M.ncell == 4 && M.nstep == 4 && M.start[0] == 0, "edit: .. 4 empty steps");
    cr_loop_ed_close(&L, &C);
    ok(L.state == CRL_EMPTY && L.d.len == 0 && !L.dirty, "edit: closed with nothing entered: the slot empty again, nothing to save");
    L.sync = 2;
    cr_loop_ed_open(&L);
    ok(L.d.len == 768, "edit: an empty slot, Length 2 bars: 2 bars");
    ed_at(5);
    chord(62, CR_MOD_MAJ, 60);
    step(20);
    cr_loop_ed_close(&L, &C);
    ok(L.state == CRL_STOPPED && L.d.nev == 1 && L.d.ev[0].t == 480 && L.dirty, "edit: a chord entered there: a loop (dirty: the global save writes it)");
}

int main(void)
{
    s_overwrite();
    s_replace();
    s_step();
    s_rate();
    s_edit();
    s_bass();
    s_free();
    s_sync();
    s_slots();
    s_switch();
    s_limits();
    printf("\ncr_loop: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
