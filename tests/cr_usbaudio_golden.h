/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The 44.1 kHz stream as a digest: every byte and every length of every packet ChoralRoot In sends over three
 * simulated runs (the I2S fast, slow, and fast with the host not reading for 50 ms), with noise in all six channels.
 * It uses only what the stream had before 48 kHz came (ua_reset, ua_audio, ua_sof, ua_transmit, the ua struct), so
 * the same file builds against the tree of 0.18 / f402608: the digest in tests/cr_usbaudio_test.c ("golden") was
 * taken there, and the test fails if a 44.1 kHz packet ever differs by a bit. Included after usb.c. */
static uint64_t ua_gold_hash;
static uint32_t ua_gold_packets, ua_gold_bytes, ua_gold_lcg;

static void ua_gold_byte(uint8_t b)
{
    ua_gold_hash = (ua_gold_hash ^ b) * 0x100000001B3ull;   /* FNV-1a, 64 bit */
}

static void ua_gold_run(double fs, uint32_t ms_total, int stop_ms)
{
    static int16_t st[32 * UA_CAP_CHANNELS];
    static uint8_t pk[1024];
    double next_half = 0, half_us = 128e6 / fs;
    uint32_t ms, i, n, bytes;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.cap_alt = 1;
    for (ms = 0; ms < ms_total; ms++) {
        while (next_half <= ms * 1000.0) {                 /* a half: four blocks of 32 frames */
            for (n = 0; n < 4; n++) {
                for (i = 0; i < 32 * UA_CAP_CHANNELS; i++) {
                    ua_gold_lcg = ua_gold_lcg * 1664525u + 1013904223u;
                    st[i] = (int16_t)(ua_gold_lcg >> 16);
                }
                ua_audio(st, 32);
            }
            next_half += half_us;
        }
        ua_sof();
        if (stop_ms >= 0 && ms >= (uint32_t)stop_ms && ms < (uint32_t)stop_ms + 50u)
            continue;                                      /* the host does not read: the ring runs over */
        bytes = ua_transmit(pk);
        ua_gold_byte((uint8_t)bytes);
        ua_gold_byte((uint8_t)(bytes >> 8));
        for (i = 0; i < bytes; i++)
            ua_gold_byte(pk[i]);
        ua_gold_packets++;
        ua_gold_bytes += bytes;
    }
    ua_gold_byte((uint8_t)ua.cap_underruns);
    ua_gold_byte((uint8_t)ua.cap_overruns);
    ua_gold_byte((uint8_t)ua.cap_nudges_up);
    ua_gold_byte((uint8_t)ua.cap_nudges_down);
}

static void ua_gold(void)
{
    ua_gold_hash = 0xCBF29CE484222325ull;
    ua_gold_packets = ua_gold_bytes = 0;
    ua_gold_lcg = 20261012u;
    ua_gold_run(44117.647, 20000, -1);
    ua_gold_run(44056.0, 20000, -1);
    ua_gold_run(44117.647, 6000, 3000);
}
