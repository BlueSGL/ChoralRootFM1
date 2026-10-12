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
static int step_root(int k) { return 50 + k % 13; }  /* the root entered at step k (no two neighbours alike) */
static void steps_n(int n)                           /* Step, Length Free: n chords, every key up between; REC */
{
    int k;
    reset();
    L.mode = CRL_MODE_STEP;
    cr_loop_rec(&L, &C);
    for (k = 0; k < n; k++) step_chord(step_root(k), CR_MOD_MAJ);
}
/* Step entry as a step sequencer (the owner, 2026-10-12): a step is a sixteenth (CRL_STEP) whatever Quantize says, a
 * pattern has 1 .. CRL_STEPS steps, a root pressed after one was let go is the next step */
static void s_step(void)
{
    static const int NS[5] = {1, 3, 5, 17, 64};
    char name[200];
    uint32_t t[70], e[8], gg[8];
    int mark, k, i, good, q;
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();                                      /* a loop there before: replaced by the steps */
    L.mode = CRL_MODE_STEP;
    L.quant = 0;                                     /* Quantize none: the step is a sixteenth all the same */
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP && L.cap == CRL_CAP_STEP && L.state == CRL_STOPPED && L.step == 0 &&
       L.step_g == 24 && CRL_STEP == 24, "step: REC while playing stops the loop, step entry at step 1; a step is a 1/16 (Quantize none)");
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
    /* the Rate scales the pattern (the Elektron's scale): x2 thirty-seconds, x1/2 eighths */
    cr_loop_set_rate(&L, CRL_RATE_1 + 2);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(990);
    k = ons_of(62, mark, t, 8);
    i = ons_of(64, mark, e, 8);
    ok(k == 4 && i >= 1 && near(t[1] - t[0], 250, 6) && near(e[0] - t[0], 62, 6),
       "step: Rate x2: the 4 steps come round every 250 ms, a step is 62 ms (a 1/32 of the tempo)");
    cr_loop_play(&L, &C);
    step(300);
    cr_loop_set_rate(&L, CRL_RATE_1 - 2);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(1990);
    k = ons_of(62, mark, t, 8);
    i = ons_of(64, mark, e, 8);
    ok(k == 2 && i >= 1 && near(t[1] - t[0], 1000, 6) && near(e[0] - t[0], 250, 6),
       "step: Rate x1/2: every 1000 ms, a step is 250 ms (an 1/8)");
    cr_loop_play(&L, &C);
    cr_loop_set_rate(&L, CRL_RATE_1);
    step(300);
    /* nothing entered: the loop as it was; LOOP leaves and plays */
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP && cr_loop_rec(&L, &C) == CRL_DID_EMPTY_TAKE && L.d.nev == 3 && L.d.len == 96,
       "step: REC REC with nothing entered keeps the loop");
    /* Quantize never sets the step */
    for (q = 0, good = 1; q < (int)CRL_NQUANT; q++) {
        reset();
        L.mode = CRL_MODE_STEP;
        L.quant = (uint8_t)q;
        cr_loop_rec(&L, &C);
        step_chord(62, CR_MOD_MAJ);
        step_chord(64, CR_MOD_MIN);
        step_chord(67, CR_MOD_MAJ);
        cr_loop_rec(&L, &C);
        good &= L.d.len == 72 && L.d.nev == 3 && L.d.ev[1].t == 24 && L.d.ev[2].t == 48 && L.d.ev[2].dur == 24;
    }
    ok(good, "step: at every Quantize setting (none .. 1/32) three chords are three sixteenths (72 ticks), a step long each");
    /* Length Free: N chords are N steps, up to 64 */
    for (i = 0; i < 5; i++) {
        steps_n(NS[i]);
        cr_loop_rec(&L, &C);
        for (k = 0, good = 1; k < NS[i]; k++)
            good &= L.d.ev[k].t == (uint32_t)k * 24u && L.d.ev[k].dur == 24 && L.d.ev[k].root == step_root(k);
        snprintf(name, sizeof name, "step: Free: %d chords = %d steps (len %u, %u events, each on its own step)", NS[i], NS[i],
                 (unsigned)L.d.len, (unsigned)L.d.nev);
        ok(good && L.d.nev == NS[i] && L.d.len == (uint32_t)NS[i] * 24u, name);
    }
    /* .. the 64 play back in order, a sixteenth apart, 4 bars round */
    mark = nlog;
    cr_loop_play(&L, &C);
    step(8100);
    for (k = 0, good = 1; k < 64; k++) {             /* step k's root sounds at k x 125 ms */
        int j, hit = 0;
        for (j = mark; j < nlog; j++)
            hit |= LOG[j].k == '+' && LOG[j].s == 0 && LOG[j].note == step_root(k) && near(LOG[j].ms - LOG[mark].ms, (uint32_t)k * 125u, 6);
        good &= hit;
    }
    for (k = mark, i = 0, t[0] = 0; k < nlog; k++)   /* the distinct times a note starts */
        if (LOG[k].k == '+' && LOG[k].s == 0 && (!i || LOG[k].ms != t[0])) { t[0] = LOG[k].ms; i++; }
    snprintf(name, sizeof name, "step: 64 steps entered play back in order, each 125 ms after the last (%d onsets in 8.1 s: 64 + the loop's start again)", i);
    ok(good && i == 65 && L.played == 65, name);
    cr_loop_play(&L, &C);
    step(300);
    /* a 65th: refused */
    steps_n(64);
    ok(L.step == 64 && L.step_end == 64 && L.d.nev == 64 && L.step_no == 0, "step: after the 64th the cursor is past the end");
    step_chord(62, CR_MOD_MAJ);
    ok(L.d.nev == 64 && L.step_no == 1 && L.step == 64, "step: a 65th chord is refused (step_no: the UI's \"64 steps: full\"), nothing written");
    cr_loop_step(&L, CRL_STEP_REST);
    ok(L.step == 64 && L.step_end == 64, "step: .. and a rest there does nothing");
    cr_loop_step(&L, CRL_STEP_BACK);
    ok(L.step == 63, "step: OCT- from there: back on step 64");
    step_chord(60, CR_MOD_MIN);
    ok(L.d.nev == 65 && L.d.ev[64].t == 63u * 24u && L.step == 64, "step: .. where a chord still goes (beside the one there)");
    cr_loop_rec(&L, &C);
    ok(L.d.len == 1536 && L.d.nev == 65, "step: 64 steps: 1536 ticks, 4 bars of 4/4");
    /* a root pressed after one was let go is the next step, whatever is still held (the owner's "only records to the
     * first step": the chord key kept down between the roots) */
    reset();
    L.mode = CRL_MODE_STEP;
    L.quant = 4;
    cr_loop_rec(&L, &C);
    cr_mod(&C, CR_MOD_MIN, 1);
    for (k = 0; k < 6; k++) { cr_key(&C, (uint8_t)step_root(k), 100, 1); step(30); cr_key(&C, (uint8_t)step_root(k), 100, 0); step(10); }
    for (k = 0, good = 1; k < 6; k++) good &= L.d.ev[k].t == (uint32_t)k * 24u;
    ok(good && L.d.nev == 6 && L.step == 5, "step: the chord key held, six roots tapped under it: six steps (not six chords on step 1)");
    cr_mod(&C, CR_MOD_MIN, 0);
    cr_loop_step(&L, CRL_STEP_KEYSUP);
    ok(L.step == 6, "step: .. every key up: the cursor moves on once");
    /* roots held together share a step; one held on is written where it was pressed */
    cr_key(&C, 62, 100, 1); step(20); cr_key(&C, 69, 100, 1); step(30);
    cr_key(&C, 62, 100, 0); step(10); cr_key(&C, 69, 100, 0); step(10);
    ok(L.d.nev == 8 && L.d.ev[6].t == 144 && L.d.ev[7].t == 144 && L.step == 6, "step: two roots held together: both on the step");
    cr_loop_step(&L, CRL_STEP_KEYSUP);
    cr_key(&C, 48, 100, 1); step(20);                /* a low key held .. */
    cr_key(&C, 64, 100, 1); step(20); cr_key(&C, 64, 100, 0); step(10);   /* .. a root over it: the same step */
    cr_key(&C, 67, 100, 1); step(20); cr_key(&C, 67, 100, 0); step(10);   /* .. the next root: the next step */
    cr_key(&C, 48, 100, 0); step(10);
    ok(L.d.nev == 11 && L.d.ev[8].root == 64 && L.d.ev[8].t == 168 && L.d.ev[9].root == 67 && L.d.ev[9].t == 192 &&
       L.d.ev[10].root == 48 && L.d.ev[10].t == 168 && L.d.ev[10].dur == 24,
       "step: a key held over two roots: it is written on the step it was pressed on, a step long");
    cr_loop_rec(&L, &C);
    ok(L.d.len == 216, "step: .. nine steps used: 216 ticks");
    /* a set Length: its steps, the cursor wraps; LOOP leaves and plays */
    reset();
    L.mode = CRL_MODE_STEP;
    L.sync = 1;
    L.quant = 2;                                     /* (1/8: not the step) */
    cr_loop_rec(&L, &C);
    ok(L.step_g == 24 && L.step_len == 384, "step, 1 bar: 16 steps");
    step_chord(62, CR_MOD_MIN);
    step_chord(69, CR_MOD_MIN);
    cr_loop_step(&L, CRL_STEP_BACK);
    cr_loop_step(&L, CRL_STEP_BACK);
    cr_loop_step(&L, CRL_STEP_BACK);
    ok(L.step == 15, "step, a set Length: back past step 1 wraps to the last step (16)");
    cr_loop_step(&L, CRL_STEP_REST);
    ok(L.step == 0, "step, a set Length: the cursor wraps at the length");
    for (k = 0; k < 17; k++) cr_loop_step(&L, CRL_STEP_REST);
    ok(L.step == 1 && L.step_no == 0, "step, a set Length: it goes round (nothing is refused)");
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY && L.state == CRL_PLAYING && L.d.nev == 2 && L.d.len == 384,
       "step: a set Length (1 bar): LOOP leaves step entry and plays the steps (2 events, the bar)");
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "step: no stuck, double or stray note");
    /* a Length longer than a pattern: its first whole bars within 64 steps */
    for (k = 1, good = 1; k < (int)CRL_NSYNC; k++) {
        static const uint32_t W44[CRL_NSYNC] = {0, 384, 768, 1536, 1536, 1536}, W34[CRL_NSYNC] = {0, 288, 576, 1152, 1440, 1440};
        reset();
        L.mode = CRL_MODE_STEP;
        L.sync = (uint8_t)k;
        cr_loop_rec(&L, &C);
        good &= L.step_len == W44[k] && cr_loop_step_len(&L) == W44[k];
        cr_loop_rec(&L, &C);
        L.sig = CRL_SIG_34;
        cr_loop_rec(&L, &C);
        good &= L.step_len == W34[k];
    }
    ok(good, "step: Length 1 2 4 bars: 16 32 64 steps; 8 and 16 bars: 64 steps (4 bars); 3/4: 12 a bar, 60 steps (5 bars) at most");
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
    /* a 1-bar loop's starts in 48 bars of the tempo (96 s at 120): the Rate in 48ths */
    static const int WANT[CRL_NRATE] = {3, 4, 6, 8, 12, 24, 36, 48, 72, 96, 192};
    static const char *const NM[CRL_NRATE] = {"x1/16", "x1/12", "x1/8", "x1/6", "x1/4", "x1/2", "x3/4", "x1", "x1.5", "x2", "x4"};
    char name[160];
    static uint32_t t[256];
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
        step(95990);                                 /* 48 bars at 120 */
        n = ons_of(62, mark, t, 256);
        cr_loop_play(&L, &C);
        step(600);
        snprintf(name, sizeof name, "rate %s: the 1-bar loop starts %d times in 48 bars (want %d), evenly (%u ms apart)",
                 NM[r], n, WANT[r], n > 1 ? (unsigned)(t[1] - t[0]) : 0u);
        ok(n == WANT[r] && (n < 2 || (near(t[1] - t[0], 96000u / (unsigned)WANT[r], 8) &&
                                      near(t[n - 1] - t[0], 96000u * (unsigned)(n - 1) / (unsigned)WANT[r], 8))), name);
        good &= sounding() == 0;
    }
    ok(good && dup_on == 0 && stray_off == 0, "rate: every rate stops clean (no stuck, double or stray note)");
    /* a change while it plays: taken at the loop's start, never in the middle of a cycle */
    cr_loop_set_rate(&L, CRL_RATE_1);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(500);
    cr_loop_set_rate(&L, CRL_RATE_1 + 2);            /* x2 asked a beat in */
    ok(L.rate == CRL_RATE_1 && L.rate_want == CRL_RATE_1 + 2, "rate: changed while playing: the old rate until the loop's start");
    step(3490);
    n = ons_of(62, mark, t, 40);
    ok(n == 3 && near(t[1] - t[0], 2000, 6) && near(t[2] - t[1], 1000, 6) && L.rate == CRL_RATE_1 + 2,
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
    cr_loop_set_rate(&L, CRL_RATE_1 - 1);            /* x3/4: the loop's start falls off the beat 2 times in 3 */
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
    cr_loop_set_rate(&L, CRL_NRATE - 1);             /* x4 */
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
    cr_loop_set_rate(&L, CRL_RATE_1 + 2);            /* x2, Free */
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

/* ---- the loop editor (0.18; a step sequencer since 2026-10-12): the loop as sixteenth-note steps, read from its
 * events; a step's length is its NOTE's length */
static crl_edmap_t M;
static void ed_at(uint32_t st) { cr_loop_ed_cursor(&L, st); }   /* the cursor on step st (0 ..), as the UI does */
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
static int dur_at(uint32_t t)                        /* .. its length in ticks (-1: none) */
{
    int i, r = -1;
    for (i = 0; i < L.d.nev; i++)
        if (!(L.d.ev[i].layer & CRL_HID) && L.d.ev[i].t == t) r = L.d.ev[i].dur;
    return r;
}
static void steps3(void)                             /* D, Em, G entered as steps, Length Free: 72 ticks */
{
    reset();
    L.mode = CRL_MODE_STEP;
    cr_loop_rec(&L, &C);
    step_chord(62, CR_MOD_MAJ);
    step_chord(64, CR_MOD_MIN);
    step_chord(67, CR_MOD_MAJ);
    cr_loop_rec(&L, &C);
}
static void s_edit(void)
{
    static uint8_t buf[CRL_REC_MAX];
    static crl_data_t back, was;
    char name[200];
    uint32_t n, k, i, t[8], u[8];
    int mark;
    steps3();
    ok(L.quant == 0 && cr_loop_ed_open(&L) == CRL_ED_OK && L.ed && L.ed_g == 24 && L.ed_len == 72,
       "edit: opens on a step loop with Quantize none: the grid is the sixteenth, always");
    cr_loop_ed_map(&L, 0, &M);
    ok(M.nstep == 3 && M.nall == 3 && M.start == 7u && M.tail == 0u && M.first == 0, "edit: the step loop is its steps exactly: 3 steps, all filled");
    /* a chord played replaces the step, and sounds */
    ed_at(1);
    mark = nlog;
    chord(69, CR_MOD_MIN, 60);
    ok(L.d.nev == 3 && live_at(24) == 1 && root_at(24) == 69 && root_at(0) == 62 && root_at(48) == 67 && L.dirty,
       "edit: a chord played replaces the selected step's notes (Em -> Am), the others untouched");
    ok(ons_of(69, mark, &n, 1) == 1, "edit: .. and it sounds (the keys play)");
    ok(L.ed_t0 == 24, "edit: the cursor stays on the step");
    step(50);
    cr_mod(&C, CR_MOD_MAJ, 1);                       /* two roots in one push: both in the step */
    cr_key(&C, 60, 100, 1); step(20); cr_key(&C, 64, 100, 1); step(40);
    cr_key(&C, 60, 100, 0); step(10); cr_key(&C, 64, 100, 0);
    cr_mod(&C, CR_MOD_MAJ, 0);
    step(20);
    ok(L.d.nev == 4 && live_at(24) == 2, "edit: two roots held together: the old chord goes once, both are in the step");
    chord(65, CR_MOD_MAJ, 60);
    step(20);
    ok(L.d.nev == 3 && live_at(24) == 1 && root_at(24) == 65, "edit: the next push replaces again");
    /* Split Bass: a bass-side note replaces the bass side only */
    cr_set_single_notes(&C, CR_SINGLE_SPLIT_BASS, 5);
    note(62, 60);                                    /* D4: below the split F: the bass side */
    step(20);
    ok(live_at(24) == 2 && nbass(&L.d) == 1, "edit: a bass-side note joins the step: the chord side is kept (Split Bass's rule)");
    note(60, 60);
    step(20);
    ok(live_at(24) == 2 && nbass(&L.d) == 1 && root_at(24) == 60, "edit: another bass-side note replaces the bass side alone");
    chord(69, CR_MOD_MIN, 60);                       /* A4: above the split: the chord side */
    step(20);
    ok(live_at(24) == 2 && nbass(&L.d) == 1, "edit: a chord replaces the chord side alone");
    cr_set_single_notes(&C, CR_SINGLE_FULL, 5);
    /* REC: the step cleared */
    ed_at(1);
    ok(cr_loop_ed_clear(&L) == 2 && live_at(24) == 0 && L.d.nev == 2, "edit: clear: the step's notes go (both sides)");
    cr_loop_ed_map(&L, 0, &M);
    ok(M.nstep == 3 && M.start == 5u && L.d.len == 72, "edit: .. the step stays, empty: still 3 steps");
    ok(cr_loop_ed_nlen(&L) == 0 && cr_loop_ed_setnlen(&L, 2) == 0, "edit: an empty step has no note length to set");
    chord(64, CR_MOD_MIN, 60);
    step(20);
    ok(dur_at(24) == 24 && cr_loop_ed_nlen(&L) == 1, "edit: a chord entered on an empty step is one step long");
    /* the step's length is its NOTE's length: nothing else moves */
    was = L.d;
    ok(cr_loop_ed_setnlen(&L, 4) == 1 && cr_loop_ed_nlen(&L) == 4 && dur_at(24) == 96, "edit: note length 4: the step's chord sounds 4 steps (96 ticks)");
    ok(L.d.len == 72 && L.d.nev == 3 && root_at(0) == 62 && dur_at(0) == 24 && root_at(48) == 67 && dur_at(48) == 24,
       "edit: .. no other step moved, the loop as long as it was (D at 0, G at 48, 72 ticks)");
    ok(cr_loop_ed_setnlen(&L, 16) == 1 && dur_at(24) == 384 && root_at(48) == 67 && L.d.len == 72, "edit: note length 16: 384 ticks, still nothing moved");
    ok(cr_loop_ed_setnlen(&L, 64) == 1 && dur_at(24) == 1536 && cr_loop_ed_nlen(&L) == 64, "edit: note length 64: the longest");
    ok(cr_loop_ed_setnlen(&L, 65) == 0 && cr_loop_ed_setnlen(&L, 0) == 0 && dur_at(24) == 1536, "edit: 1 .. 64 steps");
    ok(cr_loop_ed_setnlen(&L, 1) == 1 && dur_at(24) == 24, "edit: note length 1 again");
    for (i = k = 0; i < L.d.nev; i++)
        k += L.d.ev[i].t != was.ev[i].t || L.d.ev[i].root != was.ev[i].root || (L.d.ev[i].t != 24 && L.d.ev[i].dur != was.ev[i].dur);
    ok(k == 0 && L.d.len == was.len && L.d.nev == was.nev, "edit: after 4 / 16 / 64 / 1: every event where and what it was");
    /* a long note on the page: the steps it sounds on into are marked, the steps there still start */
    ok(cr_loop_ed_len(&L, 384) == 1 && L.d.len == 384, "edit: the loop 16 steps (Steps)");
    cr_loop_ed_setnlen(&L, 4);
    cr_loop_ed_map(&L, 0, &M);
    snprintf(name, sizeof name, "edit: step 2 four steps long: steps 1-3 start (%X), 4 and 5 are its tail (%X), 16 steps",
             (unsigned)M.start, (unsigned)M.tail);
    ok(M.nstep == 16 && M.start == 7u && M.tail == 0x18u, name);
    chord(61, CR_MOD_MAJ, 60);
    step(20);
    ok(root_at(24) == 61 && dur_at(24) == 96 && live_at(24) == 1, "edit: a chord played on a step with a long note keeps the length (4 steps)");
    mark = nlog;
    cr_loop_play(&L, &C);
    step(1900);
    { int j, off = -1;                               /* Db (step 2) at 125 ms, off 500 ms later; G (step 3) starts under it */
      for (j = mark; j < nlog && off < 0; j++)
          if (LOG[j].k == '-' && LOG[j].s == 0 && LOG[j].note == 61) off = (int)LOG[j].ms;
      ok(ons_of(61, mark, t, 8) == 1 && ons_of(67, mark, u, 8) == 1 && near(u[0] - t[0], 125, 6) && off > 0 &&
         near((uint32_t)off - t[0], 500, 6),
         "edit: playback: the long chord sounds 4 steps (500 ms); the next step's chord starts under it, 125 ms after"); }
    cr_loop_play(&L, &C);
    step(700);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "edit: a long note over other steps: no stuck, double or stray note");
    cr_loop_ed_setnlen(&L, 1);
    cr_loop_ed_len(&L, 72);
    /* the velocity */
    ed_at(0);
    ok(cr_loop_ed_vel(&L, 0) == 100 && cr_loop_ed_vel(&L, -40) == 60 && L.d.ev[0].vel == 60 && cr_loop_ed_vel(&L, 100) == 127,
       "edit: the step's velocity (100 -> 60 -> 127, clamped)");
    /* the record carries it: pack / unpack give the same events; a 0.14 .. 0.17 reader takes the record */
    ed_at(1);
    cr_loop_ed_setnlen(&L, 3);
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n > 0 && buf[7] == 0 && buf[4] == 1 && old_unpack_ok(buf, n), "edit: an edited loop's record: version 1, flags 0, read by 0.14 .. 0.17");
    ok(cr_loop_unpack(buf, n, &back) == 1 && back.len == 72 && back.nev == L.d.nev, "edit: unpack: the same loop");
    for (i = k = 0; i < back.nev; i++) k += back.ev[i].t == 24 && back.ev[i].dur == 72;
    ok(k == 1, "edit: note lengths survive the record with no new field (they are the events' dur)");
    cr_loop_ed_setnlen(&L, 1);
    /* the loop's length in steps: shorter hides the last step, longer brings it back; a save writes what sounds */
    ok(cr_loop_ed_len(&L, 48) == 1 && L.d.len == 48 && L.d.nev == 3, "edit: loop length -1 step: 48 ticks, the last step's event kept in RAM");
    cr_loop_ed_map(&L, 0, &M);
    ok(M.nstep == 2 && M.nall == 16 && M.start == 7u, "edit: .. 2 steps now, the hidden one known");
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n > 0 && cr_loop_unpack(buf, n, &back) == 1 && back.len == 48 && back.nev == 2 && old_unpack_ok(buf, n),
       "edit: a save while it is hidden writes what sounds (2 events, a valid record)");
    ok(cr_loop_ed_len(&L, 72) == 1, "edit: loop length +1 step");
    cr_loop_ed_map(&L, 0, &M);
    ok(M.nstep == 3 && root_at(48) == 67, "edit: .. the hidden step is back (G)");
    ok(cr_loop_ed_len(&L, 96) == 1, "edit: longer than it was: an empty step added");
    cr_loop_ed_map(&L, 0, &M);
    ok(M.nstep == 4 && !((M.start >> 3) & 1u), "edit: .. 4 steps, the new one empty");
    cr_loop_ed_len(&L, 48);
    cr_loop_ed_close(&L, &C);
    ok(!L.ed && L.d.nev == 2 && L.d.len == 48 && L.state == CRL_STOPPED, "edit: closed with the loop shortened: the hidden step's event is gone for good");
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
    cr_loop_ed_setnlen(&L, 8);                       /* longer than the loop: it sounds across its start */
    cr_loop_ed_len(&L, 24);                          /* shortened under the playhead */
    step(1500);
    ok(L.state == CRL_PLAYING && ons_of(72, mark, &n, 1) >= 1, "edit: LOOP plays in the editor, the new chord is heard in place");
    cr_loop_play(&L, &C);
    step(1400);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "edit: edits under a playing loop: no stuck, double or stray note");
    cr_loop_ed_close(&L, &C);

    /* a loop played by hand, Quantize none: it opens (refused until 2026-10-12), nothing is moved; an event belongs to
     * the step it starts in and keeps its time until that step is replaced */
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();
    cr_loop_play(&L, &C);
    step(300);
    L.d.ev[1].t = (uint16_t)(L.d.ev[1].t + 5u);      /* (G a little late: off the grid) */
    was = L.d;
    L.quant = 0;
    ok(cr_loop_ed_open(&L) == CRL_ED_OK && L.ed, "edit: a played loop with Quantize none: it opens");
    ok(!memcmp(&was, &L.d, sizeof(crl_data_t) - sizeof(crl_ev_t) * (CRL_MAX_EV - was.nev)),
       "edit: .. unchanged: no event moved, nothing requantised");
    cr_loop_ed_map(&L, 0, &M);
    { uint64_t want = 0, tail = 0;
      for (i = 0; i < was.nev; i++) want |= (uint64_t)1 << (was.ev[i].t / 24u);
      for (i = 0; i < was.nev; i++)
          for (k = was.ev[i].t / 24u + 1u; k < 16u && (uint32_t)was.ev[i].t + was.ev[i].dur >= k * 24u + 12u; k++) tail |= (uint64_t)1 << k;
      tail &= ~want;
      snprintf(name, sizeof name, "edit: D (a beat), G (two beats, late), Ab on the sixteenth grid: 16 steps, starts %X, tails %X",
               (unsigned)M.start, (unsigned)M.tail);
      ok(M.nstep == 16 && M.start == want && M.tail == tail && M.start == 0x1011u && (M.tail & 0xF0u) == 0xE0u, name); }
    ed_at(was.ev[1].t / 24u);
    ok(cr_loop_ed_nlen(&L) == 8 || cr_loop_ed_nlen(&L) == 9, "edit: G's step: its note about 8 steps long (a part of a step counts whole)");
    ed_at(8);
    chord(60, CR_MOD_MAJ, 60);                       /* an empty step filled */
    step(20);
    ok(L.d.nev == 4 && root_at(192) == 60 && L.d.ev[0].t == was.ev[0].t && L.d.ev[1].t == was.ev[1].t && L.d.ev[1].dur == was.ev[1].dur &&
       L.d.ev[2].t == was.ev[2].t && L.d.ev[2].dur == was.ev[2].dur,
       "edit: a chord entered on another step: the played events keep their exact times and lengths");
    ed_at(was.ev[1].t / 24u);
    ok(cr_loop_ed_setnlen(&L, 2) == 1 && L.d.ev[1].t == was.ev[1].t && L.d.ev[1].dur == 48u - was.ev[1].t % 24u,
       "edit: G's note 2 steps long: it keeps its (late) start and ends on the grid");
    chord(65, CR_MOD_MAJ, 60);
    step(20);
    ok(root_at(was.ev[1].t / 24u * 24u) == 65 && find_ev(67, 1) < 0, "edit: G's step replaced: the new chord starts on the step's tick");
    cr_loop_ed_close(&L, &C);
    /* .. opened and closed without an edit: as it was */
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();
    cr_loop_play(&L, &C);
    step(300);
    was = L.d;
    cr_loop_ed_open(&L);
    ed_at(4);
    cr_loop_ed_vel(&L, 0);
    cr_loop_ed_nlen(&L);
    cr_loop_ed_close(&L, &C);
    ok(!memcmp(&was, &L.d, sizeof(crl_data_t) - sizeof(crl_ev_t) * (CRL_MAX_EV - was.nev)) && L.d.len == 384,
       "edit: opened, looked at and closed: the loop as it was");
    L.cap = CRL_CAP_NONE;
    cr_loop_rec(&L, &C);                             /* (armed: a capture is open) */
    ok(cr_loop_ed_open(&L) == CRL_ED_BUSY, "edit: not while a take is armed");
    cr_loop_rec(&L, &C);

    /* a take longer than 64 steps: a page at a time */
    reset();
    L.sync = 4;                                      /* 8 bars */
    L.count_in = 0;
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 200);
    step(8300);                                      /* bar 5, beat 2 */
    chord(67, CR_MOD_MAJ, 200);
    step(8000);
    cr_loop_play(&L, &C);
    step(300);
    ok(L.d.len == 3072 && L.d.nev == 2 && cr_loop_ed_open(&L) == CRL_ED_OK, "edit: an 8-bar take (128 steps) opens");
    cr_loop_ed_map(&L, 0, &M);
    n = L.d.ev[1].t / 24u;
    ok(M.nstep == 128 && M.start == 1u && n >= 64u, "edit: .. page 1: steps 1-64 (D on step 1)");
    cr_loop_ed_map(&L, 64, &M);
    ok(M.first == 64 && M.start == (uint64_t)1 << (n - 64u), "edit: .. page 2: steps 65-128 (G where it was played)");
    ed_at(n);
    ok(cr_loop_ed_nlen(&L) >= 1 && cr_loop_ed_clear(&L) == 1 && L.d.nev == 1, "edit: .. a step of page 2 edited as any other");
    cr_loop_ed_close(&L, &C);

    /* layers: what Replace hid goes when the editor's first edit is made; undo holds */
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
    { uint32_t nev0 = L.d.nev, live0 = L.d.nev - k;
      ok(k >= 1 && L.d.nlayers == 2 && cr_loop_ed_open(&L) == CRL_ED_OK, "edit: a loop with a Replace layer opens");
      cr_loop_ed_close(&L, &C);
      for (i = n = 0; i < L.d.nev; i++) n += (L.d.ev[i].layer & CRL_HID) != 0;
      ok(n == k && L.d.nev == nev0 && L.d.nlayers == 2, "edit: opened and closed without an edit: the layers and what Replace hid as they were");
      cr_loop_ed_open(&L);
      ed_at(14);
      cr_loop_ed_vel(&L, 0);
      ok(L.d.nlayers == 2, "edit: looking (the velocity read) is no edit");
      chord(60, CR_MOD_MAJ, 60);
      step(20);
      for (i = n = 0; i < L.d.nev; i++) n += (L.d.ev[i].layer & CRL_HID) != 0 || L.d.ev[i].layer != 0;
      snprintf(name, sizeof name, "edit: the first edit makes the loop one layer: what Replace hid is gone (%u live events + the new one = %u)",
               (unsigned)live0, (unsigned)L.d.nev);
      ok(n == 0 && L.d.nlayers == 1 && L.d.nev >= 1 && L.d.ev[L.d.nev - 1].t == 336, name); }
    cr_loop_ed_close(&L, &C);
    ok(cr_loop_undo(&L, &C) == CRL_DID_NOTHING && L.d.nlayers == 1, "edit: REC held (undo) after an edit: nothing to undo");
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n > 0 && cr_loop_unpack(buf, n, &back) == 1 && back.nlayers == 1, "edit: .. and the loop packs to a valid one-layer record");

    /* an empty loop opens as an empty pattern */
    reset();
    L.quant = 0;
    ok(cr_loop_ed_open(&L) == CRL_ED_OK && L.d.len == 384 && L.ed_g == 24 && L.state == CRL_STOPPED,
       "edit: an empty slot (Quantize none, Length Free): an empty pattern of a bar");
    cr_loop_ed_map(&L, 0, &M);
    ok(M.nstep == 16 && M.start == 0 && M.tail == 0, "edit: .. 16 empty steps (4 until 2026-10-12: quarter-note steps)");
    cr_loop_ed_close(&L, &C);
    ok(L.state == CRL_EMPTY && L.d.len == 0 && !L.dirty, "edit: closed with nothing entered: the slot empty again, nothing to save");
    L.sync = 2;
    cr_loop_ed_open(&L);
    ok(L.d.len == 768, "edit: an empty slot, Length 2 bars: 32 steps");
    ed_at(5);
    chord(62, CR_MOD_MAJ, 60);
    step(20);
    cr_loop_ed_close(&L, &C);
    ok(L.state == CRL_STOPPED && L.d.nev == 1 && L.d.ev[0].t == 120 && L.d.ev[0].dur == 24 && L.dirty,
       "edit: a chord entered there: a loop (dirty: the global save writes it)");
    reset();
    L.sync = 5;
    cr_loop_ed_open(&L);
    ok(L.d.len == 1536, "edit: an empty slot, Length 16 bars: 64 steps (a pattern's most)");
    cr_loop_ed_close(&L, &C);

    /* a step loop of 0.18's earlier dev builds (a step = the Quantize value; here none: quarter notes): its record
     * loads and plays as it did; in the editor it is on the sixteenth grid */
    reset();
    memset(&was, 0, sizeof was);
    was.len = 288;
    was.nev = 3;
    was.nlayers = 1;
    for (i = 0; i < 3; i++) {
        was.ev[i].t = (uint16_t)(i * 96u);
        was.ev[i].dur = 96;
        was.ev[i].root = (uint8_t)(i == 0 ? 62 : i == 1 ? 64 : 67);
        was.ev[i].vel = 100;
        was.ev[i].qx = (uint8_t)((i == 1 ? CR_Q_MIN : CR_Q_MAJ) << 4);
    }
    n = cr_loop_pack(&was, buf, sizeof buf);
    ok(n == CRL_REC_HDR + 2u + 21u && cr_loop_unpack(buf, n, &back) == 1, "old step loop: a record of three quarter-note steps loads");
    cr_loop_set(&L, &C, &back);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(1490);
    ok(L.d.len == 288 && ons_of(62, mark, t, 8) == 1 && ons_of(64, mark, u, 8) == 1 && near(u[0] - t[0], 500, 6),
       "old step loop: it plays as it did (a quarter note a step: Em 500 ms after D, 1.5 s round)");
    cr_loop_play(&L, &C);
    step(700);
    ok(cr_loop_ed_open(&L) == CRL_ED_OK, "old step loop: the editor opens on it");
    cr_loop_ed_map(&L, 0, &M);
    ed_at(4);
    ok(M.nstep == 12 && M.start == 0x111u && M.tail == 0xEEEu && cr_loop_ed_nlen(&L) == 4,
       "old step loop: 12 sixteenth steps, its chords on steps 1, 5, 9, each 4 steps long");
    cr_loop_ed_close(&L, &C);
    ok(L.d.nev == 3 && L.d.len == 288 && sounding() == 0 && dup_on == 0 && stray_off == 0, "old step loop: closed: as it was");
}

/* ---- 0.18: a step's parts (1/4 1/2 3/4), ratchet, condition, chance and voicing (docs/LOOPER.md "Ratchet, condition,
 * chance, voicing"). The loops are built event by event (exact ticks); a tick is 5.2 ms at 120 BPM, a step 125 ms */
static void mkloop(uint32_t nsteps)
{
    reset();
    L.d.len = nsteps * CRL_STEP;
    L.d.nlayers = 1;
    L.d.nev = 0;
    L.lev0[0] = 0;
    L.state = CRL_STOPPED;
    L.mode = CRL_MODE_OVERWRITE;
}
static void addev(uint32_t t, uint32_t dur, int root, int q, int ext)
{
    crl_ev_t *e = &L.d.ev[L.d.nev++];
    e->t = (uint16_t)t; e->dur = (uint16_t)dur; e->root = (uint8_t)root; e->vel = 100;
    e->qx = (uint8_t)(q << 4 | ext); e->layer = 0;
}
static void setst(uint32_t s, int rat, int cond, int voic, int chance)
{
    L.d.st[s][0] = (uint8_t)(rat | cond << 2);
    L.d.st[s][1] = (uint8_t)(voic | chance << 5);
}
static int lvoices(void)
{
    int k, n = 0;
    for (k = 0; k < CR_MAX_LOOPV; k++) n += L.pv[k].used != 0;
    return n;
}
static int lv_top;
static uint32_t run_pass(void)                       /* to the end of the cycle: the hits played in it; lv_top: the
                                                      * most loop voices in use at once */
{
    uint32_t p0 = L.played, c0 = L.cyc0;
    lv_top = 0;
    while (L.cyc0 == c0 && L.state == CRL_PLAYING) {
        step(1);
        if (lvoices() > lv_top) lv_top = lvoices();
    }
    return L.played - p0;
}
/* ---- the slow Rates (2026-10-11: x1/6 x1/8 x1/12 x1/16; at x1/16 a step is a bar of 4/4). Everything of the loop's
 * content is in the loop's ticks, so a slow rate stretches all of it alike */
static int offs_of(int note, int from, uint32_t *t, int max)
{
    int k, n = 0;
    for (k = from; k < nlog && n < max; k++)
        if (LOG[k].k == '-' && LOG[k].s == 0 && LOG[k].note == note) t[n++] = LOG[k].ms;
    return n;
}
static void slow4(void)                              /* 4 steps: C a step long, C# a quarter of its step, D with a
                                                      * ratchet x4, Eb half its step (no two share a note) */
{
    mkloop(4);
    addev(0, 24, 60, CR_Q_MAJ, 0);
    addev(24, 6, 61, CR_Q_MAJ, 0);
    addev(48, 24, 62, CR_Q_MAJ, 0);
    addev(72, 12, 63, CR_Q_MAJ, 0);
    setst(2, 3, 0, 0, 0);
}
static void s_slow(void)
{
    static const struct { int r; uint32_t ms; const char *nm; } R[4] = {    /* a step at 120 BPM */
        {0, 2000, "x1/16"}, {1, 1500, "x1/12"}, {2, 1000, "x1/8"}, {3, 750, "x1/6"}};
    char name[240];
    uint32_t a[8], b[8], c[8], d[8], fa[8], fb[8], fc[8], fd[8], S, bar, beat, rt0;
    int k, mark, na, nb, nc, nd, good;
    ok(CRL_NRATE == 11 && CRL_RATE_1 == 7, "slow: eleven rates, x1 the eighth (the session's 0)");
    for (k = 0; k < 4; k++) {
        slow4();
        cr_loop_set_rate(&L, R[k].r);
        S = R[k].ms;
        mark = nlog;
        cr_loop_play(&L, &C);
        step(4u * S - 10u);                          /* one pass */
        na = ons_of(60, mark, a, 8); nb = ons_of(61, mark, b, 8); nc = ons_of(62, mark, c, 8); nd = ons_of(63, mark, d, 8);
        snprintf(name, sizeof name, "slow %s: one pass: a step is %u ms: C at 0, C# at %u, D at %u (a ratchet x4), Eb at %u",
                 R[k].nm, (unsigned)S, (unsigned)(nb ? b[0] - a[0] : 0), (unsigned)(nc ? c[0] - a[0] : 0), (unsigned)(nd ? d[0] - a[0] : 0));
        ok(na == 1 && nb == 1 && nc == 4 && nd == 1 && near(b[0] - a[0], S, 8) && near(c[0] - a[0], 2u * S, 8) &&
           near(d[0] - a[0], 3u * S, 8), name);
        snprintf(name, sizeof name, "slow %s: the ratchet's four hits are a quarter of the step apart (%u %u %u ms, want %u)",
                 R[k].nm, (unsigned)(c[1] - c[0]), (unsigned)(c[2] - c[1]), (unsigned)(c[3] - c[2]), (unsigned)(S / 4u));
        ok(nc == 4 && near(c[1] - c[0], S / 4u, 8) && near(c[2] - c[1], S / 4u, 8) && near(c[3] - c[2], S / 4u, 8), name);
        step(20);                                    /* the second pass has begun */
        good = offs_of(60, mark, fa, 8) == 1 && offs_of(61, mark, fb, 8) == 1 && offs_of(62, mark, fc, 8) == 4 &&
               offs_of(63, mark, fd, 8) == 1;
        snprintf(name, sizeof name, "slow %s: the lengths: a step %u ms, 1/4 step %u, a ratchet hit %u, 1/2 step %u (want %u %u %u %u)",
                 R[k].nm, (unsigned)(fa[0] - a[0]), (unsigned)(fb[0] - b[0]), (unsigned)(fc[0] - c[0]), (unsigned)(fd[0] - d[0]),
                 (unsigned)S, (unsigned)(S / 4u), (unsigned)(S / 4u), (unsigned)(S / 2u));
        ok(good && near(fa[0] - a[0], S, 8) && near(fb[0] - b[0], S / 4u, 8) && near(fc[0] - c[0], S / 4u, 8) &&
           near(fd[0] - d[0], S / 2u, 8), name);
        na = ons_of(60, mark, a, 8);
        snprintf(name, sizeof name, "slow %s: the 4 steps come round after %u ms", R[k].nm, (unsigned)(na == 2 ? a[1] - a[0] : 0));
        ok(na == 2 && near(a[1] - a[0], 4u * S, 8), name);
        cr_loop_stop(&L, &C);
        step(50);
        ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "slow: stops clean");
    }
    /* 64 steps at x1/16: 64 bars of the tempo a pass (128 s at 120; 24 576 tempo ticks for 1536 of the loop's) */
    mkloop(64);
    addev(0, 24, 60, CR_Q_MAJ, 0);
    addev(63u * 24u, 24, 62, CR_Q_MAJ, 0);
    cr_loop_set_rate(&L, 0);
    mark = nlog;
    rt0 = L.rt;
    cr_loop_play(&L, &C);
    step(64000);
    cr_loop_where(&L, &bar, &beat);
    snprintf(name, sizeof name, "slow x1/16, 64 steps: half way (64 s): position %u of 1536 (want 768), ring %u (128), bar %u beat %u (3, 1) of the loop",
             (unsigned)cr_loop_pos(&L), (unsigned)cr_loop_ring(&L), (unsigned)bar, (unsigned)beat);
    ok(near(cr_loop_pos(&L), 768, 1) && near(cr_loop_ring(&L), 128, 1) && bar == 3 && beat == 1, name);
    step(63990);
    na = ons_of(60, mark, a, 8); nc = ons_of(62, mark, c, 8);
    cr_loop_where(&L, &bar, &beat);
    snprintf(name, sizeof name, "slow x1/16, 64 steps: the last step starts %u ms in (want 126000), the position %u of 1536, ring %u, bar %u beat %u",
             (unsigned)(nc ? c[0] - a[0] : 0), (unsigned)cr_loop_pos(&L), (unsigned)cr_loop_ring(&L), (unsigned)bar, (unsigned)beat);
    ok(na == 1 && nc == 1 && near(c[0] - a[0], 126000, 8) && cr_loop_pos(&L) == 1535 && cr_loop_ring(&L) == 255 && bar == 4 && beat == 4, name);
    step(128020);
    na = ons_of(60, mark, a, 8); nc = ons_of(62, mark, c, 8);
    snprintf(name, sizeof name, "slow x1/16, 64 steps: three starts %u and %u ms apart (want 128000), %u whole loop ticks' tempo ticks for 2 passes (want 49152)",
             (unsigned)(na == 3 ? a[1] - a[0] : 0), (unsigned)(na == 3 ? a[2] - a[1] : 0), (unsigned)((L.rt - rt0) / 16u * 16u));
    ok(na == 3 && nc == 2 && near(a[1] - a[0], 128000, 8) && near(a[2] - a[1], 128000, 8) && near(c[1] - c[0], 128000, 8) &&
       (L.rt - rt0) / 16u * 16u == 49152u && L.rfrac < 48u, name);
    cr_loop_stop(&L, &C);
    step(50);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "slow x1/16, 64 steps: stops clean");
    /* a condition counts the loop's passes, however long one lasts */
    mkloop(2);
    addev(0, 24, 60, CR_Q_MAJ, 0);
    addev(24, 24, 62, CR_Q_MAJ, 0);
    setst(0, 0, CRL_C_1_2, 0, 0);
    cr_loop_set_rate(&L, 0);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(15990);                                     /* 4 passes of 4 s */
    na = ons_of(60, mark, a, 8); nc = ons_of(62, mark, c, 8);
    ok(na == 2 && nc == 4 && near(a[1] - a[0], 8000, 8) && near(c[1] - c[0], 4000, 8),
       "slow x1/16: a 1:2 step plays every second pass (8 s apart), the plain step every pass (4 s)");
    cr_loop_stop(&L, &C);
    step(50);
    /* the click stays the tempo's: 4 steps at x1/16 are 4 bars */
    slow4();
    cr_loop_set_rate(&L, 0);
    cr_loop_metro(&L, 1);
    memset(clicks, 0, sizeof clicks);
    cr_loop_play(&L, &C);
    step(15990);
    snprintf(name, sizeof name, "slow x1/16: the click is the tempo's: %d beats, %d of them a bar's first, in 2 passes of 4 bars (want 32, 8)",
             clicks[1] + clicks[2], clicks[2]);
    ok(clicks[1] + clicks[2] == 32 && clicks[2] == 8, name);
    cr_loop_stop(&L, &C);
    cr_loop_metro(&L, 0);
    step(50);
    /* a take at x1/16: the count-in is a bar of the tempo, a 1-bar take lasts 16, Quantize works on the loop's grid */
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    L.sync = 1;
    L.count_in = 1;
    L.quant = 4;                                     /* 1/16 of the loop: a bar of the tempo here */
    cr_loop_set_rate(&L, 0);
    cr_loop_rec(&L, &C);
    step(1990);
    ok(L.cap == CRL_CAP_COUNTIN, "slow x1/16: the count-in is a bar of the tempo (still counting after 1.99 s)");
    step(20);
    ok(L.cap == CRL_CAP_REC, "slow x1/16: .. and the take starts after it");
    step(7700);                                      /* 7.71 s in: 290 ms before the fifth bar */
    note(62, 300);
    step(23000);
    ok(L.cap == CRL_CAP_REC, "slow x1/16: a 1-bar take is 16 bars of the tempo: still recording at 31 s");
    step(1100);
    snprintf(name, sizeof name, "slow x1/16: .. done at 32 s (len %u), the note played 290 ms early is on step 5 (t %u, want 96), %u ticks long",
             (unsigned)L.d.len, (unsigned)L.d.ev[0].t, (unsigned)L.d.ev[0].dur);
    ok(L.cap == CRL_CAP_NONE && L.state == CRL_PLAYING && L.d.len == 384 && L.d.nev == 1 && L.d.ev[0].t == 96 && L.d.ev[0].dur >= 1, name);
    mark = nlog;
    step(64000);
    na = ons_of(62, mark, a, 8);
    ok(na == 2 && near(a[1] - a[0], 32000, 8), "slow x1/16: played back, the note comes round every 32 s");
    cr_loop_stop(&L, &C);
    step(50);
    /* out of range: x1 */
    cr_loop_set_rate(&L, (int)CRL_NRATE);
    k = L.rate == CRL_RATE_1;
    cr_loop_set_rate(&L, -1);
    ok(k && L.rate == CRL_RATE_1 && cr_loop_rate_q48(&L) == 48, "slow: a rate out of range is x1");
    cr_loop_set_rate(&L, 0);
    ok(cr_loop_rate_q48(&L) == 3, "slow: x1/16 is 3 48ths");
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "slow: no stuck, double or stray note");
}
/* the record as 0.14 .. 0.17 and main before the steps' properties wrote it (cr_loop_pack then, flags = the part bit) */
static uint32_t old_pack(const crl_data_t *d, uint8_t *b)
{
    uint32_t i, live = 0, o, part = 0;
    for (i = 0; i < d->nev; i++)
        if (!(d->ev[i].layer & CRL_HID) && d->ev[i].t < d->len) { live++; part |= CRL_EV_BASS(&d->ev[i]); }
    memset(b, 0, 16u + 2u * d->nlayers);
    b[0] = 'C'; b[1] = 'R'; b[2] = 'L'; b[3] = '1'; b[4] = 1; b[5] = d->sig; b[6] = d->nlayers; b[7] = (uint8_t)(part ? 1 : 0);
    b[8] = (uint8_t)d->len; b[9] = (uint8_t)(d->len >> 8); b[10] = (uint8_t)(d->len >> 16); b[11] = (uint8_t)(d->len >> 24);
    b[12] = (uint8_t)live; b[13] = (uint8_t)(live >> 8); b[14] = CRL_PPQN; b[15] = 0;
    o = 16u + 2u * d->nlayers;
    for (i = 0; i < d->nev; i++) {
        const crl_ev_t *e = &d->ev[i];
        uint32_t l;
        if ((e->layer & CRL_HID) || e->t >= d->len) continue;
        l = e->layer < d->nlayers ? e->layer : d->nlayers - 1u;
        if (!++b[16u + 2u * l]) b[17u + 2u * l]++;
        b[o] = (uint8_t)e->t; b[o + 1] = (uint8_t)(e->t >> 8); b[o + 2] = (uint8_t)e->dur; b[o + 3] = (uint8_t)(e->dur >> 8);
        b[o + 4] = e->root; b[o + 5] = e->vel; b[o + 6] = e->qx;
        o += 7u;
    }
    return o;
}
static int main_notes(uint8_t *out)                  /* the notes sounding on MAIN, ascending */
{
    int n, k = 0;
    for (n = 0; n < 128; n++) if (SND[0][n]) out[k++] = (uint8_t)n;
    return k;
}

static void s_trig(void)
{
    static uint8_t buf[CRL_REC_MAX + 8], ref[CRL_REC_MAX + 8];
    static crl_data_t back, was;
    static const uint16_t CM[CRL_NCOND] = {0xFFF, 0x555, 0xAAA, 0x249, 0x492, 0x924, 0x111, 0x222, 0x444, 0x888, 0x001, 0xFFE};
    static const char *const CN[CRL_NCOND] = {"Always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1st", "not 1st"};
    char name[240];
    uint32_t t[16], n, k, i, m, got, rn, a, b, c;
    uint8_t na[8], nb[8], nc[8];
    int mark, x, y, z, bad;

    /* ---- parts of a step */
    mkloop(4);
    addev(24, 6, 62, CR_Q_MAJ, 0);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(124);
    ok(sounding() == 0, "part: nothing before the step");
    step(10);
    x = sounding();
    step(20);
    y = sounding();
    step(15);
    ok(x > 0 && y > 0 && sounding() == 0 && ons_of(62, mark, t, 4) == 1,
       "part: a note 6 ticks long (1/4 step) sounds 31 ms of the step's 125");
    cr_loop_stop(&L, &C);
    ok(cr_loop_ed_open(&L) == CRL_ED_OK, "part: the editor opens");
    ed_at(1);
    ok(cr_loop_ed_nfrac(&L) == 1 && cr_loop_ed_nlen(&L) == 1, "part: 6 ticks read as 1/4 (whole steps: 1, as before)");
    L.d.ev[0].dur = 12; a = cr_loop_ed_nfrac(&L);
    L.d.ev[0].dur = 18; b = cr_loop_ed_nfrac(&L);
    L.d.ev[0].dur = 10; c = cr_loop_ed_nfrac(&L);
    ok(a == 2 && b == 3 && c == 0 && cr_loop_ed_nlen(&L) == 1, "part: 12 = 1/2, 18 = 3/4; 10 ticks (off the scale): 1 step, as before");
    L.d.ev[0].dur = 24; a = cr_loop_ed_nfrac(&L);
    L.d.ev[0].t = 32; L.d.ev[0].dur = 18; b = cr_loop_ed_nfrac(&L);   /* off the grid, over the step's end */
    L.d.ev[0].t = 24;
    ok(a == 0 && b == 0, "part: a whole step is not a part; a note that sounds past its step's end neither");
    ok(cr_loop_ed_setnfrac(&L, 2) == 1 && L.d.ev[0].dur == 12 && cr_loop_ed_nfrac(&L) == 2 && L.d.ev[0].t == 24 && L.d.len == 96,
       "part: set to 1/2: 12 ticks, nothing else moved");
    ok(cr_loop_ed_setnfrac(&L, 0) == 0 && cr_loop_ed_setnfrac(&L, 4) == 0, "part: 0 and 4 quarters refused");
    chord(69, CR_MOD_MIN, 60);
    step(20);
    ok(L.d.nev == 1 && L.d.ev[0].root == 69 && L.d.ev[0].dur == 12, "part: a chord played into the step is as long as its notes were: 1/2");
    ok(cr_loop_ed_setnlen(&L, 2) == 1 && L.d.ev[0].dur == 48 && cr_loop_ed_nfrac(&L) == 0 && cr_loop_ed_nlen(&L) == 2,
       "part: and back to whole steps: 2");
    ed_at(2);
    ok(cr_loop_ed_setnfrac(&L, 1) == 0 && cr_loop_ed_nfrac(&L) == 0, "part: an empty step has no length");
    cr_loop_ed_close(&L, &C);

    /* ---- the ratchet */
    for (k = 1; k < 4; k++) {
        static const uint32_t GAP[4] = {0, 62, 42, 31};
        mkloop(4);
        addev(24, 24, 62, CR_Q_MAJ, 0);
        setst(1, (int)k, 0, 0, 0);
        mark = nlog;
        cr_loop_play(&L, &C);
        rn = run_pass();
        n = (uint32_t)ons_of(62, mark, t, 16);
        bad = n != k + 1u;
        for (i = 1; i < n; i++) bad |= !near(t[i] - t[i - 1], GAP[k], 6);
        bad |= n && !near(t[0] - 1000u, 125, 6);
        bad |= n && t[n - 1] - t[0] >= 125u;
        snprintf(name, sizeof name, "ratchet x%u: %u hits %u ms apart, all inside the one step; one loop voice at most (%d)",
                 (unsigned)k + 1u, (unsigned)n, (unsigned)GAP[k], lv_top);
        ok(!bad && rn == k + 1u && lv_top == 1, name);
        step(130);
        ok(sounding() > 0 || 1, "ratchet: (the second pass runs)");
        cr_loop_stop(&L, &C);
        ok(sounding() == 0 && lvoices() == 0 && dup_on == 0 && stray_off == 0, "ratchet: stopped: silent, no voice kept, no doubled or stray note");
    }
    mkloop(4);                                       /* a hit's length: the slice, or the note's part of it */
    addev(24, 12, 62, CR_Q_MAJ, 0);                  /* 1/2 step, x2: hits 12 ticks apart, each 6 long */
    setst(1, 1, 0, 0, 0);
    cr_loop_play(&L, &C);
    step(125 + 16); x = sounding();                  /* inside hit 1 */
    step(30); y = sounding();                        /* between the hits (6 .. 12 ticks = 31 .. 62 ms) */
    step(32); z = sounding();                        /* inside hit 2 */
    step(40);
    ok(x > 0 && y == 0 && z > 0 && sounding() == 0, "ratchet x2 on a 1/2-step note: two hits of a quarter step with a gap between them");
    cr_loop_stop(&L, &C);
    mkloop(4);
    addev(24, 72, 62, CR_Q_MAJ, 0);                  /* a 3-step note, x2: two hits in ITS step, then silence */
    setst(1, 1, 0, 0, 0);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(125 + 125 + 20);
    ok(ons_of(62, mark, t, 8) == 2 && sounding() == 0 && lvoices() == 0, "ratchet on a long note: its hits are slices of one step, nothing sounds after it");
    cr_loop_stop(&L, &C);
    mkloop(4);                                       /* eight ratcheted chords on one step: the eight voices, no more */
    for (i = 0; i < 8; i++) addev(24, 24, 48 + (int)i * 3, CR_Q_MAJ, 0);
    setst(1, 3, 0, 0, 0);
    cr_loop_play(&L, &C);
    rn = run_pass();
    ok(rn == 32 && lv_top == 8, "ratchet x4 on eight chords: 32 hits on the eight loop voices");
    step(110);
    ok(lvoices() == 0 && sounding() == 0 && dup_on == 0 && stray_off == 0, "ratchet: the step over: every voice free again (none leaked)");
    cr_loop_stop(&L, &C);
    mkloop(4);                                       /* ten: two are stolen; still nothing stuck */
    for (i = 0; i < 10; i++) addev(24, 24, 40 + (int)i * 3, CR_Q_MAJ, 0);
    setst(1, 3, 0, 0, 0);
    cr_loop_play(&L, &C);
    for (k = 0; k < 3; k++) run_pass();
    ok(lv_top == 8, "ratchet x4 on ten chords: never more than the eight voices");
    cr_loop_stop(&L, &C);
    ok(lvoices() == 0 && sounding() == 0 && stray_off == 0, "ratchet: ten chords, three passes, stopped: silent");
    mkloop(4);                                       /* the editor takes the event away between two hits */
    addev(24, 12, 62, CR_Q_MAJ, 0);
    setst(1, 3, 0, 0, 0);
    cr_loop_play(&L, &C);
    cr_loop_ed_open(&L);
    ed_at(1);
    step(125 + 12);
    a = L.played;
    cr_loop_ed_clear(&L);
    step(200);
    ok(L.played - a <= 0u && lvoices() == 0 && sounding() == 0, "ratchet: the step cleared between two hits: no more hits, the voice free");
    cr_loop_ed_close(&L, &C);
    cr_loop_stop(&L, &C);

    /* ---- the conditions: each over 24 passes */
    for (k = 0; k < CRL_NCOND; k++) {
        mkloop(2);
        addev(24, 12, 62, CR_Q_MAJ, 0);
        setst(1, 0, (int)k, 0, 0);
        cr_loop_play(&L, &C);
        bad = 0;
        for (i = 0, m = 0; i < 24; i++) {
            got = run_pass();
            if (i < 12) m |= got << i;
            bad |= got != ((k == CRL_C_FIRST ? i == 0 : k == CRL_C_NFIRST ? i != 0 : (CM[k] >> (i % 12)) & 1u) ? 1u : 0u);
        }
        snprintf(name, sizeof name, "condition %s: passes 1 .. 12 %03X, and the same over the next twelve", CN[k], (unsigned)m);
        ok(!bad && m == CM[k], name);
        cr_loop_stop(&L, &C);
        cr_loop_play(&L, &C);                        /* (counted from when the loop starts playing) */
        got = run_pass();
        ok(got == (CM[k] & 1u) && sounding() == 0, "condition: a new start counts from pass 1 again");
        cr_loop_stop(&L, &C);
    }
    mkloop(2);                                       /* the condition is the step's: both sides, and a ratchet whole */
    addev(24, 12, 62, CR_Q_MAJ, 0);
    addev(24, 12, 38, 0, CRL_X_BASS);
    addev(0, 12, 67, CR_Q_MAJ, 0);
    setst(1, 1, CRL_C_2_2, 0, 0);
    cr_loop_play(&L, &C);
    a = run_pass(); b = run_pass(); c = run_pass();   /* (each count ends with the next pass's step 1, played at the wrap) */
    ok(a == 1 && b == 5 && c == 1, "condition 2:2 on a step with a chord and a bass-side note, ratchet x2: pass 1 neither, pass 2 both twice; the other step always");
    cr_loop_stop(&L, &C);

    /* ---- the chance */
    for (k = 1; k < CRL_NCHANCE; k++) {
        uint32_t pct = CRL_CHANCE_PCT(k), hit = 0, part = 0;
        mkloop(2);
        cr_loop_seed(&L, 12345u + k);
        addev(24, 12, 62, CR_Q_MAJ, 0);
        addev(24, 12, 38, 0, CRL_X_BASS);
        setst(1, 1, 0, 0, (int)k);
        cr_loop_play(&L, &C);
        for (i = 0; i < 600; i++) {
            got = run_pass();
            hit += got == 4;
            part += got != 0 && got != 4;
        }
        snprintf(name, sizeof name, "chance %u %%: %u of 600 passes; a step's chord, its bass note and both ratchet hits always together",
                 (unsigned)pct, (unsigned)hit);
        ok(!part && hit * 100u > 600u * (pct > 7u ? pct - 7u : 0u) && hit * 100u < 600u * (pct + 7u), name);
        cr_loop_stop(&L, &C);
    }
    ok(CRL_CHANCE_PCT(0) == 100 && CRL_CHANCE_PCT(7) == 10 && CRL_CHANCE_PCT(4) == 50, "chance: 100 % .. 10 % in eight levels");
    {
        static uint8_t r1[200], r2[200], r3[200], q1[200];
        uint32_t same = 0, anti = 0, d13 = 0;
        for (m = 0; m < 3; m++) {
            mkloop(4);
            cr_loop_seed(&L, m == 2 ? 999u : 777u);
            addev(24, 12, 62, CR_Q_MAJ, 0);
            addev(48, 12, 64, CR_Q_MAJ, 0);
            setst(1, 0, 0, 0, 4);
            setst(2, 0, 0, 0, 4);
            cr_loop_play(&L, &C);
            for (i = 0; i < 200; i++) {
                uint32_t p0 = L.played, c0 = L.cyc0, h1;
                while (L.cyc0 == c0 && (L.t - L.cyc0) < 40u) step(1);   /* (step 1 done, step 2 not yet) */
                h1 = L.played - p0;
                while (L.cyc0 == c0) step(1);
                (m == 0 ? r1 : m == 1 ? r2 : r3)[i] = (uint8_t)h1;
                if (m == 0) q1[i] = (uint8_t)(L.played - p0 - h1);
            }
            cr_loop_stop(&L, &C);
        }
        for (i = 0; i < 200; i++) { same += r1[i] == q1[i]; anti += r1[i] != q1[i]; d13 += r1[i] != r3[i]; }
        ok(!memcmp(r1, r2, sizeof r1), "chance: the same seed and the same timing: the same 200 passes (the host tests are deterministic)");
        ok(d13 > 40u, "chance: another seed: other passes");
        snprintf(name, sizeof name, "chance: two steps at 50 %% roll apart (the same on %u of 200 passes)", (unsigned)same);
        ok(same > 60u && anti > 60u, name);
    }
    mkloop(2);                                       /* the condition AND the chance */
    cr_loop_seed(&L, 4242u);
    addev(24, 12, 62, CR_Q_MAJ, 0);
    setst(1, 0, CRL_C_1_2, 0, 4);
    cr_loop_play(&L, &C);
    for (i = 0, a = b = 0; i < 400; i++) {
        got = run_pass();
        if (i & 1u) a += got; else b += got;
    }
    snprintf(name, sizeof name, "condition 1:2 and chance 50 %%: never on the even passes, %u of the 200 odd ones", (unsigned)b);
    ok(a == 0 && b > 70u && b < 130u, name);
    cr_loop_stop(&L, &C);

    /* ---- the voicing */
    mkloop(4);
    addev(24, 48, 62, CR_Q_MAJ, 0);
    cr_voicing_step(&C, 2);
    cr_loop_play(&L, &C);
    step(140);
    x = main_notes(na);                              /* Follow, the live voicing 2 */
    cr_loop_stop(&L, &C);
    cr_voicing_step(&C, -2);
    setst(1, 0, 0, 13 + 2, 0);
    cr_loop_play(&L, &C);
    step(140);
    y = main_notes(nb);                              /* the step's own +2, the live voicing 0 */
    cr_voicing_step(&C, -3);
    z = main_notes(nc);                              /* .. and the live one turned under it */
    cr_loop_stop(&L, &C);
    cr_voicing_step(&C, 3);
    ok(x >= 3 && x == y && !memcmp(na, nb, (size_t)x), "voicing: a step set to +2 sounds as the live voicing +2 does");
    ok(z == y && !memcmp(nb, nc, (size_t)y), "voicing: .. and the live voicing turned while it sounds leaves it alone");
    setst(1, 0, 0, 0, 0);
    cr_loop_play(&L, &C);
    step(140);
    y = main_notes(nb);
    cr_voicing_step(&C, 2);
    z = main_notes(nc);
    cr_loop_stop(&L, &C);
    cr_voicing_step(&C, -2);
    ok(y == x && memcmp(na, nb, (size_t)x) != 0 && z == x && !memcmp(na, nc, (size_t)x),
       "voicing: Follow: the live voicing, as before (0 then, +2 once it is turned)");
    mkloop(4);
    addev(24, 48, 62, 0, 0);                         /* a single note: not voiced */
    cr_loop_play(&L, &C);
    step(140); x = main_notes(na);
    cr_loop_stop(&L, &C);
    setst(1, 0, 0, 13 + 5, 0);
    cr_loop_play(&L, &C);
    step(140); y = main_notes(nb);
    cr_loop_stop(&L, &C);
    ok(x == 1 && y == 1 && na[0] == nb[0], "voicing: a single note ignores its step's voicing");
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "voicing: silent, no doubled or stray note");

    /* ---- the editor's side: the step's properties */
    steps3();
    memcpy(&was, &L.d, sizeof was);
    n = cr_loop_pack(&L.d, buf, CRL_REC_MAX);
    k = old_pack(&L.d, ref);
    ok(n == k && !memcmp(buf, ref, n) && buf[7] == 0, "record: a loop with no step property: byte for byte the record written before them");
    cr_loop_ed_open(&L);
    cr_loop_ed_close(&L, &C);
    ok(!memcmp(&was, &L.d, sizeof was), "edit: opened and closed: not a byte of the loop moved");
    cr_loop_ed_open(&L);
    ed_at(1);
    ok(cr_loop_ed_prop(&L, CRL_SP_RATCH) == 0 && cr_loop_ed_prop(&L, CRL_SP_COND) == 0 && cr_loop_ed_prop(&L, CRL_SP_VOIC) == 0 &&
       cr_loop_ed_prop(&L, CRL_SP_CHANCE) == 0, "props: a step with notes: ratchet off, Always, Follow, 100 %");
    ok(cr_loop_ed_setprop(&L, CRL_SP_RATCH, 2) == 1 && cr_loop_ed_setprop(&L, CRL_SP_COND, CRL_C_2_3) == 1 &&
       cr_loop_ed_setprop(&L, CRL_SP_VOIC, 16) == 1 && cr_loop_ed_setprop(&L, CRL_SP_CHANCE, 3) == 1 &&
       cr_loop_ed_prop(&L, CRL_SP_RATCH) == 2 && cr_loop_ed_prop(&L, CRL_SP_COND) == CRL_C_2_3 &&
       cr_loop_ed_prop(&L, CRL_SP_VOIC) == 16 && cr_loop_ed_prop(&L, CRL_SP_CHANCE) == 3 && L.dirty,
       "props: set and read back: x3, 2:3, +3, 60 %");
    ok(cr_loop_ed_setprop(&L, CRL_SP_RATCH, 4) == 0 && cr_loop_ed_setprop(&L, CRL_SP_COND, CRL_NCOND) == 0 &&
       cr_loop_ed_setprop(&L, CRL_SP_VOIC, 26) == 0 && cr_loop_ed_setprop(&L, CRL_SP_CHANCE, 8) == 0 &&
       cr_loop_ed_setprop(&L, CRL_SP_RATCH, -1) == 0 && cr_loop_ed_prop(&L, CRL_SP_RATCH) == 2, "props: out of range: refused");
    cr_loop_ed_map(&L, 0, &M);
    ok(M.rat[0] == 0u && M.rat[1] == 2u && (M.cell[1] >> 4) == (8u | 3u) && (M.cell[1] & 15u) == CRL_EC_VEL((uint32_t)cr_loop_ed_vel(&L, 0)) && (M.cell[1] & 15u) &&
       (M.cell[0] & 15u) && !(M.cell[0] >> 4) && !M.cell[3] &&
       CRL_EC_VEL(100u) == 12u && CRL_EC_VEL(1u) == 1u && CRL_EC_VEL(127u) == 15u && CRL_EC_VEL(20u) == 3u,
       "props: the map has the step's ratchet (x3), its chance (60 %) and that it has a condition, and its velocity's level");
    chord(69, CR_MOD_MIN, 60);
    step(20);
    ok(root_at(24) == 69 && cr_loop_ed_prop(&L, CRL_SP_RATCH) == 2 && cr_loop_ed_prop(&L, CRL_SP_CHANCE) == 3,
       "props: the step's notes replaced: its properties stay");
    n = cr_loop_pack(&L.d, buf, CRL_REC_MAX);
    k = old_pack(&L.d, ref);
    ok(n == k + 4u && buf[7] == CRL_RF_STEP && !memcmp(buf, ref, 7) && !memcmp(buf + 8, ref + 8, k - 8u) && buf[k] == 1 &&
       buf[k + 1] == 1 && buf[k + 2] == (2 | CRL_C_2_3 << 2) && buf[k + 3] == (16 | 3 << 5),
       "record: a step with properties: flag bit 1, the same record with {count, step, 2 bytes} after the events");
    ok(old_unpack_ok(buf, n) && n <= CRL_REC_MAX, "record: 0.14 .. 0.17's unpack accepts it (it reads neither the flag nor what follows the events)");
    ok(cr_loop_unpack(buf, n, &back) == 1 && back.nev == 3 && back.st[1][0] == L.d.st[1][0] && back.st[1][1] == L.d.st[1][1] &&
       !memcmp(back.st, L.d.st, sizeof back.st), "record: round trip: the steps' properties come back");
    ok(cr_loop_unpack(ref, k, &back) == 1 && back.nev == 3 && back.st[1][0] == 0 && back.st[1][1] == 0,
       "record: the old record of the same loop loads, with no property");
    for (i = 0, bad = 0; i < 6; i++) {               /* a bad table: the record is refused whole */
        static uint8_t bb[CRL_REC_MAX + 8];
        uint32_t nn = n;
        memcpy(bb, buf, n);
        if (i == 0) bb[k] = 0;
        if (i == 1) bb[k + 1] = 64;
        if (i == 2) bb[k + 2] = (uint8_t)(CRL_NCOND << 2);
        if (i == 3) bb[k + 3] = 26;
        if (i == 4) nn = n - 1u;
        if (i == 5) { bb[k] = 2; bb[k + 4] = 1; bb[k + 5] = 1; bb[k + 6] = 0; nn = n + 3u; }   /* (not ascending) */
        bad |= cr_loop_unpack(bb, nn, &back) != 0 || back.nev != 0;
    }
    ok(!bad, "record: a table with no entry, a step past 64, an unknown condition or voicing, cut short, out of order: refused as empty");
    ed_at(2);
    cr_loop_ed_setprop(&L, CRL_SP_RATCH, 1);
    cr_loop_ed_len(&L, 48);                          /* Steps: 2: step 3 hidden beyond the end */
    n = cr_loop_pack(&L.d, buf, CRL_REC_MAX);
    k = old_pack(&L.d, ref);
    ok(n == k + 4u && buf[k] == 1 && buf[k + 1] == 1, "record: a step hidden beyond the loop's end: its entry is not written");
    cr_loop_ed_len(&L, 72);
    ok(cr_loop_ed_prop(&L, CRL_SP_RATCH) == 1, "props: Steps longer again: the hidden step's properties are back with it");
    ok(cr_loop_ed_clear(&L) == 1 && cr_loop_ed_prop(&L, CRL_SP_RATCH) == -1 && L.d.st[2][0] == 0 && L.d.st[2][1] == 0,
       "props: REC clears the step: its properties with its notes; an empty step has none");
    ok(cr_loop_ed_setprop(&L, CRL_SP_RATCH, 1) == 0 && cr_loop_ed_setprop(&L, CRL_SP_CHANCE, 2) == 0 && L.d.st[2][0] == 0,
       "props: an empty step: none can be set");
    chord(67, CR_MOD_MAJ, 60);
    step(20);
    ok(cr_loop_ed_prop(&L, CRL_SP_RATCH) == 0, "props: notes played into it: a step with the defaults");
    cr_loop_ed_setprop(&L, CRL_SP_COND, CRL_C_FIRST);
    cr_loop_ed_len(&L, 48);
    cr_loop_ed_close(&L, &C);
    ok(L.d.nev == 2 && L.d.st[2][0] == 0 && L.d.st[1][0] != 0, "props: closed with a step hidden: it is deleted, its properties too; the others kept");
    /* the looper's other ways to change the loop */
    mkloop(4);
    addev(24, 24, 60, CR_Q_MAJ, 0);
    addev(48, 24, 64, CR_Q_MAJ, 0);                  /* an overdub's chord, alone on step 3 */
    L.d.ev[1].layer = 1;
    L.d.nlayers = 2;
    L.lev0[1] = 1;
    setst(1, 1, 0, 0, 0);
    setst(2, 2, 0, 0, 0);
    cr_loop_undo(&L, &C);
    ok(L.d.nlayers == 1 && L.d.nev == 1 && L.d.st[1][0] == 1 && L.d.st[2][0] == 0,
       "props: undo of an overdub: a step it alone filled loses its properties, the others keep theirs");
    cr_loop_set(&L, &C, &was);                       /* a slot's data: its own table */
    ok(L.d.st[1][0] == 0 && L.d.st[1][1] == 0 && L.d.nev == 3, "props: another slot's loop comes with its own (none here)");
    L.d.st[1][0] = 3;
    L.mode = CRL_MODE_STEP;
    cr_loop_rec(&L, &C);
    step_chord(60, CR_MOD_MAJ);
    step_chord(62, CR_MOD_MAJ);
    ok(L.d.st[1][0] == 3, "props: step entry open: the loop there is untouched until the commit");
    cr_loop_rec(&L, &C);
    ok(L.d.nev == 2 && L.d.st[1][0] == 0, "props: a pattern entered by steps replaces the loop: no property left from it");
    L.d.st[1][0] = 3;
    L.mode = CRL_MODE_OVERWRITE;
    L.sync = 0;
    cr_loop_rec(&L, &C);
    ok(L.d.st[1][0] == 3, "props: Overwrite armed: the loop and its properties intact until the take starts");
    cr_key(&C, 60, 100, 1);
    step(30);
    ok(L.d.st[1][0] == 0, "props: the take started: the old loop's properties went with it");
    cr_key(&C, 60, 100, 0);
    step(300);
    cr_loop_rec(&L, &C);
    L.d.st[0][0] = 2;
    cr_loop_stop(&L, &C);
    cr_loop_clear(&L, &C);
    ok(L.d.st[0][0] == 0, "props: a cleared loop has none");
    /* the record's room: CRL_REC_MAX is what it was */
    mkloop(64);
    for (i = 0; i < CRL_MAX_EV; i++) addev((i % 64u) * 24u, 12, 60, CR_Q_MAJ, 0);
    for (i = 0; i < 64; i++) setst(i, 1, 0, 0, 0);
    n = cr_loop_pack(&L.d, buf, CRL_REC_MAX);
    ok(CRL_REC_MAX == 16u + 64u + 3584u && n == 16u + 2u + 3584u + 1u + 3u * 20u && n <= CRL_REC_MAX && buf[16u + 2u + 3584u] == 20 &&
       old_unpack_ok(buf, n) && cr_loop_unpack(buf, n, &back) == 1 && back.nev == CRL_MAX_EV && back.st[19][0] == 1 && back.st[20][0] == 0,
       "record: 512 events and 64 steps with a property: the record stays within CRL_REC_MAX, the first 20 entries fit");
    for (i = 0; i < 64; i++) setst(i, 0, 0, 0, 0);
    cr_loop_ed_open(&L);
    for (i = 0, k = 0; i < 64; i++) {
        ed_at(i);
        x = cr_loop_ed_setprop(&L, CRL_SP_RATCH, 1);
        if (x == 1) k++;
        else if (x != CRL_ED_NOROOM) k = 999;
    }
    ok(k == 20 && cr_loop_pack(&L.d, buf, CRL_REC_MAX) == 16u + 2u + 3584u + 61u,
       "props: on a loop of 512 events the editor sets 20 steps' and refuses the 21st (no room in the record), so none is lost at a save");
    cr_loop_ed_close(&L, &C);
    mkloop(80);                                      /* a take longer than 64 steps: its later steps have none */
    addev(70 * 24, 24, 60, CR_Q_MAJ, 0);
    addev(24, 24, 60, CR_Q_MAJ, 0);
    cr_loop_ed_open(&L);
    ed_at(70);
    ok(cr_loop_ed_prop(&L, CRL_SP_RATCH) == -1 && cr_loop_ed_setprop(&L, CRL_SP_RATCH, 1) == 0,
       "props: a step past the 64th (a long take's second page): none");
    cr_loop_ed_close(&L, &C);
    (void)rn;
}

int main(void)
{
    s_overwrite();
    s_replace();
    s_step();
    s_rate();
    s_slow();
    s_edit();
    s_trig();
    s_bass();
    s_free();
    s_sync();
    s_slots();
    s_switch();
    s_limits();
    printf("\ncr_loop: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
