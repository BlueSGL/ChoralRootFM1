/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of Options > USB Rate (0.19, the owner 2026-10-12: "Make 48 kHz opt-in for now"; docs/USB-AUDIO.md
 * "48 kHz"): firmware/src/usb.c with FELUCCA_UAC=1 as tests/cr_usbaudio_test.c builds it. usb.c's UA_OFF_48 bit of
 * ua_off is the option's "44.1 kHz" (the default): what the host is then given must be ChoralRoot 0.18's device.
 *   0.18         USB Rate 44.1 kHz: the device and the configuration equal the stored copy of 0.18's bytes
 *                (tests/cr_usb018_desc.h, taken from the tag): bcdDevice 3.22, 208 bytes, one sampling frequency,
 *                wMaxPacketSize 640; the console's presentation (USB Record Off) equals 0.18's in both states
 *   two rates    USB Rate 44.1 + 48 kHz: 3.23, 211 bytes, two frequencies, 688: another bcdDevice than the one-rate
 *                device's, so a host that cached either reads the other; the identity and the MIDI function are the
 *                same bytes in both
 *   requests     44.1 kHz only: GET_CUR / MIN / MAX all 44100; SET_CUR 48000 is taken without a stall and changes
 *                nothing (the rate, the ring, a running stream), it is counted; ua.r48 is never set, so audio.c's
 *                test of it never calls the resampler
 *   stream       the 44.1 kHz stream's golden digest in both states, and in the 44.1-only state with a host that
 *                keeps asking for 48 kHz while it records: the same packets as without the requests
 *   replug       a change of the option leaves the bus once, 0.6 s after the last step; stepping there and back
 *                costs nothing; never while SysEx comes in or its last byte is under 1.5 s old (an update's
 *                handshake, a backup), then once; with the console presented the option changes no byte and does
 *                not leave the bus; USB Record's own replug as before
 *   cc -std=gnu11 -O2 -o build/host/cr_usbrate_test tests/cr_usbrate_test.c   (tests/run_cr_tests.sh) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 1
#define FELUCCA_UAC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
/* hal/fm1_usb.h's fm1_usb_off under another name (never called), ours under its own: usb_replug's leaving the bus is
 * counted, no register is written (tests/cr_restart_test.c's way) */
#define fm1_usb_off hw_usb_off
#include "../firmware/hal/fm1_usb.h"
#undef fm1_usb_off
static uint32_t t_offs;
static void fm1_usb_off(void) { t_offs++; }
#include "../firmware/src/usb.c"
#include "cr_usbaudio_golden.h"
#include "cr_usb018_desc.h"

static int fails, passes;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    if (ok)
        passes++;
    else
        fails++;
}

static void present(uint8_t off)                           /* as usb_start */
{
    ua_off = ua_off_want = off;
    usb_cdc_on = (uint8_t)((ua_off & UA_OFF_IN) != 0);
    ua_reset();
    ua_cfg_build();
}

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

/* ------------------------------------------------------------------ descriptors --- */
static void test_descriptors(void)
{
    static uint8_t dev44[18], cfg44[256], dev48[18], cfg48[256];
    const uint8_t *d, *c;
    uint16_t dl, n, n44, n48;
    uint32_t i, o, diff = 0, first = 0, mp = 0, fmt = 0, cur_if = 0;
    char m[200];

    present(UA_OFF_48);                                    /* Options > USB Rate 44.1 kHz, USB Record On */
    get_desc(0x0100, &d, &dl);
    get_desc(0x0200, &c, &n);
    memcpy(dev44, d, 18);
    memcpy(cfg44, c, n);
    n44 = n;
    check("USB Rate 44.1 kHz: the device descriptor is 0.18's, byte for byte (bcdDevice 3.22)",
          dl == sizeof USB018_DEV && !memcmp(d, USB018_DEV, sizeof USB018_DEV) && d[12] == 0x22 && d[13] == 3);
    snprintf(m, sizeof m, "USB Rate 44.1 kHz: the configuration is 0.18's, byte for byte (%u bytes)", n);
    check(m, n == sizeof USB018_CFG && !memcmp(c, USB018_CFG, sizeof USB018_CFG) && le16(c + 2) == 208 &&
             ua_cfg_len == 208 && ua_nif == 4 && ua_if_cap == 3);
    for (o = 0; o < n; o += c[o]) {
        if (c[o + 1] == 4)
            cur_if = c[o + 2];
        if (c[o + 1] == 0x24 && c[o + 2] == 2 && cur_if == 3)
            fmt = c[o] == 11 && c[o + 7] == 1 && (c[o + 8] | c[o + 9] << 8 | (uint32_t)c[o + 10] << 16) == 44100;
        if (c[o + 1] == 5 && c[o + 2] == 0x82)
            mp = le16(c + o + 4);
    }
    check("  one sampling frequency, 44100 Hz (an 11-byte format); wMaxPacketSize 640 >= 45 frames x 12 x 7 / 6",
          fmt && mp == 640u && mp == UA_EP_MAXP44 && mp >= 45u * 12u * 7u / 6u);

    present(0);                                            /* USB Rate 44.1 + 48 kHz */
    get_desc(0x0100, &d, &dl);
    get_desc(0x0200, &c, &n);
    memcpy(dev48, d, 18);
    memcpy(cfg48, c, n);
    n48 = n;
    fmt = mp = cur_if = 0;
    for (o = 0; o < n; o += c[o]) {
        if (c[o + 1] == 4)
            cur_if = c[o + 2];
        if (c[o + 1] == 0x24 && c[o + 2] == 2 && cur_if == 3)
            fmt = c[o] == 14 && c[o + 7] == 2 && (c[o + 8] | c[o + 9] << 8 | (uint32_t)c[o + 10] << 16) == 44100 &&
                  (c[o + 11] | c[o + 12] << 8 | (uint32_t)c[o + 13] << 16) == 48000;
        if (c[o + 1] == 5 && c[o + 2] == 0x82)
            mp = le16(c + o + 4);
    }
    check("USB Rate 44.1 + 48 kHz: bcdDevice 3.23, 211 bytes, two frequencies (44100, 48000), wMaxPacketSize 688",
          dev48[12] == 0x23 && dev48[13] == 3 && n48 == 211 && le16(c + 2) == 211 && fmt && mp == 688u);
    for (i = 0; i < 18; i++)
        diff += dev44[i] != dev48[i];
    check("the two devices differ in bcdDevice and in nothing else (VID 1209, PID 0001, class, strings): hosts re-read",
          diff == 1 && dev44[12] != dev48[12] && dev44[8] == 0x09 && dev44[9] == 0x12 && dev44[10] == 0x01 &&
          dev44[11] == 0x00);
    for (first = 9; first < n44 && cfg44[first] == cfg48[first]; first++)
        ;
    snprintf(m, sizeof m, "the configurations are the same up to the format (byte %u): MIDI and the control interface", first);
    check(m, first > 180 && cfg44[4] == cfg48[4] && !memcmp(cfg44 + 9, cfg48 + 9, 101 - 9));

    for (i = 0; i < 2; i++) {                              /* the console (USB Record Off), either rate option */
        present((uint8_t)(UA_OFF_IN | (i ? UA_OFF_48 : 0u)));
        get_desc(0x0100, &d, &dl);
        get_desc(0x0200, &c, &n);
        snprintf(m, sizeof m, "USB Record Off, USB Rate %s: the console's device and configuration are 0.18's (3.21, 175 bytes)",
                 i ? "44.1 kHz" : "44.1 + 48 kHz");
        check(m, dl == 18 && !memcmp(d, USB018_DEV_CDC, 18) && n == sizeof USB018_CFG_CDC &&
                 !memcmp(c, USB018_CFG_CDC, sizeof USB018_CFG_CDC));
    }
    present(UA_OFF_48);
    check("strings: the same in both states (the product, the device, six channel names)", ({
        const uint8_t *s;
        uint16_t l;
        int ok = 1;
        static uint8_t keep[10][40];
        uint16_t kl[10];
        for (i = 1; i < 10; i++) {
            ok &= get_desc(0x0300u | i, &s, &l);
            memcpy(keep[i], s, l);
            kl[i] = l;
        }
        present(0);
        for (i = 1; i < 10; i++)
            ok &= get_desc(0x0300u | i, &s, &l) && l == kl[i] && !memcmp(keep[i], s, l);
        ok; }));
}

/* ------------------------------------------------------------------ requests --- */
static int set_cur(uint32_t hz)                            /* SET_CUR's data stage as usb.c hands it over */
{
    uint8_t p[4] = {(uint8_t)hz, (uint8_t)(hz >> 8), (uint8_t)(hz >> 16), 0};
    ua_rate_pending = 0x82;                                /* (ua_control_setup's SETUP stage: SIE, not run here) */
    return ua_control_data(p, 3);
}

static void test_requests(void)
{
    uint8_t s[8] = {0xA2, 0x81, 0, 1, 0x82, 0, 3, 0};
    uint32_t rate = 1, cur, mn, mx;
    memset(&ua, 0, sizeof ua);
    memset(&ua_dbg, 0, sizeof ua_dbg);
    present(UA_OFF_48);
    usb.config = 1;
    ua_control_get(s, &cur);
    s[1] = 0x82;
    ua_control_get(s, &mn);
    s[1] = 0x83;
    ua_control_get(s, &mx);
    s[1] = 0x81;
    check("44.1 kHz only: GET_CUR, GET_MIN and GET_MAX all answer 44100 (0.18's answers)", cur == 44100 && mn == 44100 &&
          mx == 44100);
    ua.cap_alt = 1;
    ua.cw = 300;
    ua.cr = 100;
    ua.cap_ready = 1;
    check("  SET_CUR 48000 is taken (no stall) and changes nothing: the rate, the ring, the stream",
          set_cur(48000) == 1 && ua.r48 == 0 && ua.cw == 300 && ua.cr == 100 && ua.cap_ready == 1 &&
          ua.rate_changes == 0 && ua_control_get(s, &rate) && rate == 44100);
    check("  it is counted (79 USB: ua_48k_refused); SET_CUR 44100, 32000, 96000: 44100 too",
          ua_dbg.rate_refused == 1 && set_cur(44100) == 1 && set_cur(32000) == 1 && set_cur(96000) == 1 &&
          ua.r48 == 0 && ua.rate_changes == 0 && ua.cw == 300 && ua_dbg.rate_refused == 2 && ua.rate_sets == 4);
    present(0);
    usb.config = 1;
    s[1] = 0x83;
    ua_control_get(s, &mx);
    s[1] = 0x81;
    check("44.1 + 48 kHz: GET_MAX 48000, SET_CUR 48000 sets it, GET_CUR follows", mx == 48000 && set_cur(48000) == 1 &&
          ua.r48 == 1 && ua_control_get(s, &rate) && rate == 48000);
    present(UA_OFF_48);                                    /* the option back to 44.1: usb_start (the replug) */
    check("  the option set back (the replug's usb_start): 44100 again, whatever the host had chosen", ua.r48 == 0);
    usb.config = 0;
}

/* ------------------------------------------------------------------ the stream --- */
/* a host that records at 44.1 kHz and asks for 48 kHz every 250 ms (ask) or never: every packet's digest */
static uint64_t run_stream(int ask, uint32_t *r48_seen)
{
    static int16_t st[32 * UA_CAP_CHANNELS];
    static uint8_t pk[1024];
    double next_half = 0, half_us = 128e6 / 44117.647;
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t ms, i, n, bytes, lcg = 4711u;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    usb.config = 1;
    ua.cap_alt = 1;
    *r48_seen = 0;
    for (ms = 0; ms < 20000u; ms++) {
        if (ask && ms % 250u == 100u)
            set_cur(48000);
        while (next_half <= ms * 1000.0) {
            for (n = 0; n < 4; n++) {
                const int16_t *src = st;
                uint32_t fn = 32, r48 = ua.r48;            /* audio.c audio_block's lines */
                for (i = 0; i < 32 * UA_CAP_CHANNELS; i++) {
                    lcg = lcg * 1664525u + 1013904223u;
                    st[i] = (int16_t)(lcg >> 16);
                }
                if (r48) {
                    fn = ua_block48(st, 32);
                    src = ua.rs_out;
                    (*r48_seen)++;
                }
                ua_feed(src, fn, r48);
            }
            next_half += half_us;
        }
        ua_sof();
        bytes = ua_transmit(pk);
        h = (h ^ (bytes & 0xFFu)) * 0x100000001B3ull;
        h = (h ^ (bytes >> 8)) * 0x100000001B3ull;
        for (i = 0; i < bytes; i++)
            h = (h ^ pk[i]) * 0x100000001B3ull;
    }
    usb.config = 0;
    return h;
}

#define UA_GOLD_44K1 0x3C1F6F60F9E08D4Dull                 /* tests/cr_usbaudio_test.c's: the stream before 48 kHz */
static void test_stream(void)
{
    uint64_t a, b, c;
    uint32_t ra, rb, rc;
    char m[200];
    present(UA_OFF_48);
    ua_gold();
    snprintf(m, sizeof m, "USB Rate 44.1 kHz: the 44.1 kHz stream's digest %016llX = before 48 kHz (0.18's)",
             (unsigned long long)ua_gold_hash);
    check(m, ua_gold_hash == UA_GOLD_44K1 && ua_gold_packets == 45950u && ua_gold_bytes == 24310980u);
    present(0);
    ua_gold();
    check("USB Rate 44.1 + 48 kHz: the 44.1 kHz stream's digest is the same", ua_gold_hash == UA_GOLD_44K1 &&
          ua_gold_packets == 45950u);
    present(UA_OFF_48);
    a = run_stream(0, &ra);
    b = run_stream(1, &rb);
    check("44.1 kHz only, a host that asks for 48 kHz 80 times while it records: the same packets, bit for bit", a == b);
    check("  the resampler never ran (ua.r48 never set: audio.c's test of it is all of 48 kHz that executes)",
          ra == 0 && rb == 0 && ua.rs_blocks == 0 && ua.rate_changes == 0);
    present(0);
    c = run_stream(1, &rc);
    check("44.1 + 48 kHz, the same host: it gets 48 kHz (other packets, the resampler runs)", c != a && rc > 0 &&
          ua.rs_blocks > 0);
}

/* ------------------------------------------------------------------ the replug --- */
static uint32_t run_loop(uint32_t from, uint32_t to)       /* main.c's loop, a pass a ms: the replugs it made */
{
    uint32_t t, n = 0;
    for (t = from; t <= to; t++) {
        fm1_ms = t;
        n += (uint32_t)ua_off_apply(t);
    }
    return n;
}

static void sysex(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        sysex_byte(p[i]);
}

static void test_replug(void)
{
    static const uint8_t INFO[] = {0xF0, 0x7D, 0x46, 0x4C, 1, 0xF7};
    uint32_t n, t = 10000, offs0;
    char m[200];
    memset(&usb, 0, sizeof usb);
    present(UA_OFF_48);                                    /* a unit as it powers on: 44.1 kHz, USB Record On */
    usb.up = usb.config = 1;
    t_offs = 0;
    ua_replugs = 0;
    check("nothing changed: the main loop never leaves the bus", run_loop(t, t + 3000) == 0 && t_offs == 0);
    t += 3000;
    fm1_ms = t;
    ua_off_set(UA_OFF_48, 1, t);                           /* Options > USB Rate: 44.1 + 48 kHz (cr_ui.c cu_usb_apply) */
    n = run_loop(t, t + 599);
    check("USB Rate changed: nothing for the first 0.6 s (the setting rests)", n == 0 && t_offs == 0 && (ua_off & UA_OFF_48));
    n = run_loop(t + 600, t + 5000);
    check("  then the FM-1 leaves the bus once (USB RECONNECTING), and once only", n == 1 && t_offs == 1 &&
          ua_replugs == 1 && !usb.up && !usb.config && ua_off == 0 && usb.retry_ms == t + 600);
    usb_cdc_on = 0;
    ua_reset();
    ua_cfg_build();                                        /* usb_retry's usb_start, a second later */
    check("  what it comes back with: 3.23, 211 bytes", ({
        const uint8_t *d, *c;
        uint16_t l, cl;
        get_desc(0x0100, &d, &l);
        get_desc(0x0200, &c, &cl);
        d[12] == 0x23 && cl == 211; }));
    usb.up = usb.config = 1;
    t += 6000;
    ua_off_set(UA_OFF_48, 1, t);                           /* (the same value again, as cu_usb_apply sends both bits) */
    ua_off_set(UA_OFF_IN, 1, t);
    check("the same values set again (every Options turn sends both): no replug", run_loop(t, t + 3000) == 0 &&
          t_offs == 1);
    t += 3000;
    ua_off_set(UA_OFF_48, 0, t);                           /* stepped to 44.1 and back within 0.6 s */
    run_loop(t, t + 300);
    ua_off_set(UA_OFF_48, 1, t + 300);
    check("stepped to 44.1 kHz and back within 0.6 s: no replug", run_loop(t + 300, t + 4000) == 0 && t_offs == 1 &&
          ua_off == 0);
    t += 4000;

    /* a SysEx transfer (an update's handshake, a backup's GETs): the change waits for it */
    ua_off_set(UA_OFF_48, 0, t);
    offs0 = t_offs;
    n = 0;
    {
        uint32_t k;
        for (k = 0; k < 40; k++) {                         /* a request every 100 ms for 4 s */
            fm1_ms = t + k * 100u;
            sysex(INFO, sizeof INFO);
            n += run_loop(t + k * 100u, t + k * 100u + 99u);
        }
    }
    check("USB Rate changed while SysEx requests come (4 s of them): the FM-1 stays on the bus", n == 0 &&
          t_offs == offs0 && ua_off == 0 && ua_off_want == UA_OFF_48);
    {
        uint32_t k;                                        /* a long frame: a byte every 500 ms for 5 s */
        fm1_ms = t + 4000;
        sysex(INFO, 3);
        n = 0;
        for (k = 0; k < 10; k++) {
            n += run_loop(t + 4000 + k * 500u, t + 4499 + k * 500u);
            fm1_ms = t + 4500 + k * 500u;
            sysex(INFO + 3, 1);
        }
    }
    check("  nor while a long SysEx frame is still coming in (5 s)", n == 0 && t_offs == offs0 && usb.sx_on);
    fm1_ms = t + 9000;
    sysex(INFO + 5, 1);                                    /* its end */
    n = run_loop(t + 9000, t + 9000 + UA_REPLUG_QUIET - 1u);
    snprintf(m, sizeof m, "  nor in the %u ms after its last byte", UA_REPLUG_QUIET);
    check(m, n == 0 && t_offs == offs0);
    n = run_loop(t + 9000 + UA_REPLUG_QUIET, t + 20000);
    check("  then once: the change was kept, not lost", n == 1 && t_offs == offs0 + 1 && ua_off == UA_OFF_48 &&
          ua_replugs == 2);
    usb.up = usb.config = 1;
    t += 30000;

    /* USB Record Off (the console): the rate option changes nothing the host is given */
    fm1_ms = t;
    ua_off_set(UA_OFF_IN, 0, t);
    n = run_loop(t, t + 3000);
    check("USB Record Off: its own replug, once (as before)", n == 1 && ua_off == (UA_OFF_IN | UA_OFF_48));
    usb.up = usb.config = 1;
    usb_cdc_on = 1;
    t += 3000;
    offs0 = t_offs;
    ua_off_set(UA_OFF_48, 1, t);
    n = run_loop(t, t + 3000);
    check("  USB Rate changed with the console presented: taken, the bus not left (no byte of it changes)", n == 0 &&
          t_offs == offs0 && ua_off == UA_OFF_IN);
    t += 3000;
    ua_off_set(UA_OFF_IN, 1, t);
    n = run_loop(t, t + 3000);
    check("  USB Record On again: one replug, into the rate option's state of now (3.23)", n == 1 && ua_off == 0);
    usb.up = usb.config = 0;
}

int main(void)
{
    test_descriptors();
    test_requests();
    test_stream();
    test_replug();
    printf("cr_usbrate: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
