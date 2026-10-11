/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test: a restart asked over SysEx (72 RESTART, 77 STUB) does not leave the bus before the host has its reply
 * (firmware/src/cr_backup.c crb_leave, firmware/src/usb.c ota_wire_mark / ota_wire_gone).
 *   sh tests/run_cr_tests.sh
 * The firmware as the emulator builds it (tools/emu/emu_firmware.h), here with FELUCCA_OTA 1: the device's
 * ed_service, usb.c's SysEx out ring, ep1_rx / ep1_tx and usb_poll, all as on the unit. Modelled around them:
 *   the SIE    EP1 as usb.c uses it (hal/fm1_usb.h's functions replaced): INDEX, TXCSR1's TxPktRdy and the packet
 *              loaded with it, RXCSR1 / RXCOUNT and an OUT packet, INTRUSB's bus reset, a SIE that stops answering;
 *              fm1_usb_off (usb_detach) drops the packet that still waits in the endpoint
 *   the host   takes the IN packet when the scenario says (at once, late, one packet every so often, a number of
 *              packets, never) and keeps the SysEx bytes it got; sends its requests as EP1 OUT packets
 *   TIMER5     main.c fm1_timer5_irq: a tick every 100 us, fm1_ms every tenth, usb_poll every fifth (in every
 *              phase against the millisecond), MIDI out events as the audio ISR queues them (the clock)
 *   main loop  ed_service several times a tick (main.c's wait loop), or not at all for a while (a UI frame drawn)
 * CRB_STUB_GO / CRB_REBOOT are the detach: what the host has by then is what it will ever have.
 * Checks: a host that reads at once: the reply whole at the host, the restart 150 ms after the request, in every
 * phase of the request against the millisecond and the poll and across fm1_ms's wrap (to 0.16 the unit left with
 * the reply still in the endpoint for one request in four: fm1_ms | 1); a host that reads after 300 ms: the restart
 * waits for it, then 5 ms; a host that never reads, a host that has dropped the configuration, a SIE that is silent:
 * the restart at 500 ms; MIDI clock that never leaves the endpoint idle: the reply is waited for, the idle endpoint
 * is not; a long reply before it (LIST) read slowly: both whole; a reply queued behind it is not waited for; a bus
 * reset: nothing left to wait for; a request repeated: the first one's clock; 72 RESTART the same without a request
 * for the stub; nothing is read from the endpoint with nothing to send unless a restart is owed. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

/* ------------------------------------------------------------------ the SIE --- */
/* hal/fm1_usb.h's register functions under other names (never called), ours under theirs: usb.c includes the header
 * again (#pragma once: nothing) and runs on the model */
#define fm1_usb_reset hw_usb_reset
#define fm1_usb_attach hw_usb_attach
#define fm1_usb_off hw_usb_off
#define fm1_usb_sie_on hw_usb_sie_on
#define fm1_usb_sie_wr_start hw_usb_sie_wr_start
#define fm1_usb_sie_rd_start hw_usb_sie_rd_start
#define fm1_usb_sie_done hw_usb_sie_done
#define fm1_usb_sie_data hw_usb_sie_data
#define fm1_usb_ep0_buf hw_usb_ep0_buf
#define fm1_usb_ep_txbuf hw_usb_ep_txbuf
#define fm1_usb_ep_rxbuf hw_usb_ep_rxbuf
#define fm1_usb_ep0_send hw_usb_ep0_send
#define fm1_usb_ep_send hw_usb_ep_send
#define fm1_usb_ep4_txbuf hw_usb_ep4_txbuf
#define fm1_usb_ep4_send hw_usb_ep4_send
#define fm1_usb_rx_sync hw_usb_rx_sync
#define fm1_usb_ep_enable hw_usb_ep_enable
#define fm1_usb_sof_take hw_usb_sof_take
#include "../firmware/hal/fm1_usb.h"
#undef fm1_usb_reset
#undef fm1_usb_attach
#undef fm1_usb_off
#undef fm1_usb_sie_on
#undef fm1_usb_sie_wr_start
#undef fm1_usb_sie_rd_start
#undef fm1_usb_sie_done
#undef fm1_usb_sie_data
#undef fm1_usb_ep0_buf
#undef fm1_usb_ep_txbuf
#undef fm1_usb_ep_rxbuf
#undef fm1_usb_ep0_send
#undef fm1_usb_ep_send
#undef fm1_usb_ep4_txbuf
#undef fm1_usb_ep4_send
#undef fm1_usb_rx_sync
#undef fm1_usb_ep_enable
#undef fm1_usb_sof_take

enum { R_INTRTX1 = 2, R_INTRRX1 = 4, R_INTRUSB = 6, R_FRAME1 = 12, R_FRAME2 = 13, R_INDEX = 14, R_TXCSR1 = 17,
       R_RXCSR1 = 20, R_RXCOUNT1 = 22 };             /* usb.c's S_* (checked below) */
static uint32_t t_us;                                /* the time, in TIMER5 ticks of 100 us */
static struct {
    uint8_t on, dead, index, txcsr1, rxcsr1, intrusb, intrrx1, data;
    uint8_t tx[64], *rxbuf;                          /* EP1 IN: the packet loaded with TxPktRdy; EP1 OUT's buffer */
    const uint8_t *txp;                              /* fm1_usb_ep_send's */
    uint32_t txc, tx_n, rx_n;
    uint32_t csr_reads;                              /* reads of EP1's TXCSR1 (the cost of watching) */
    uint32_t dropped;                                /* bytes in the endpoint when the controller went off */
} sie;

static uint32_t fm1_usb_sie_on(void) { return sie.on ? 4u : 0u; }
static void fm1_usb_sie_wr_start(uint32_t r, uint32_t v)
{
    if (sie.dead)
        return;
    if (r == R_INDEX) {
        sie.index = (uint8_t)v;
    } else if (r == R_TXCSR1 && sie.index == 1u) {
        if (v & 0x08u)                               /* FlushFIFO */
            sie.txcsr1 &= (uint8_t)~1u;
        if ((v & 1u) && !(sie.txcsr1 & 1u)) {        /* TxPktRdy: the packet waits for the host's IN token */
            memcpy(sie.tx, sie.txp, sie.txc);
            sie.tx_n = sie.txc;
            sie.txcsr1 |= 1u;
        }
    } else if (r == R_RXCSR1 && sie.index == 1u) {
        if (v & 0x10u)                               /* FlushFIFO: the OUT packet taken */
            sie.rxcsr1 &= (uint8_t)~1u;
    }
}
static void fm1_usb_sie_rd_start(uint32_t r)
{
    uint32_t v = 0;
    if (r == R_INTRUSB) {
        v = sie.intrusb;
        sie.intrusb = 0;
    } else if (r == R_INTRRX1) {
        v = sie.intrrx1;
        sie.intrrx1 = 0;
    } else if (r == R_FRAME1) {
        v = (t_us / 1000u) & 0xFFu;                  /* the host's frames: the bus is alive */
    } else if (r == R_FRAME2) {
        v = ((t_us / 1000u) >> 8) & 7u;
    } else if (r == R_TXCSR1 && sie.index == 1u) {
        v = sie.txcsr1;
        sie.csr_reads++;
    } else if (r == R_RXCSR1 && sie.index == 1u) {
        v = sie.rxcsr1;
    } else if (r == R_RXCOUNT1 && sie.index == 1u) {
        v = sie.rx_n;
    }
    sie.data = (uint8_t)v;
}
static uint32_t fm1_usb_sie_done(void) { return sie.dead ? 0u : 0x8000u; }
static uint32_t fm1_usb_sie_data(void) { return sie.data; }
static void fm1_usb_reset(void) {}
static void fm1_usb_attach(void *ep0) { (void)ep0; sie.on = 1; }
static void fm1_usb_off(void)                        /* usb_detach: what waits in the endpoint goes with it */
{
    sie.dropped = (sie.txcsr1 & 1u) ? sie.tx_n : 0u;
    sie.txcsr1 = 0;
    sie.on = 0;
}
static void fm1_usb_ep0_buf(void *p) { (void)p; }
static void fm1_usb_ep_txbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
static void fm1_usb_ep_rxbuf(uint32_t ep, void *p) { if (ep == 1u) sie.rxbuf = p; }
static void fm1_usb_ep0_send(void *p, uint32_t n) { (void)p; (void)n; }
static void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n) { if (ep == 1u) { sie.txp = p; sie.txc = n; } }
static void fm1_usb_rx_sync(void) {}
static void fm1_usb_ep_enable(uint32_t eps) { (void)eps; }
static uint32_t fm1_usb_sof_take(void) { return 1; }

/* ------------------------------------------------------------------ the firmware --- */
static void t_left(uint32_t w);
#define FELUCCA_OTA 1                                /* the device's SysEx transport and ed_service */
#define CRB_STUB_VER() 2u
#define CRB_STUB_GO(w) t_left(w)
#define CRB_REBOOT() t_left(0u)
#include "../tools/emu/emu_firmware.h"
#include "../firmware/stub/stub_core.h"            /* the request words */
_Static_assert(R_INTRTX1 == S_INTRTX1 && R_INTRRX1 == S_INTRRX1 && R_INTRUSB == S_INTRUSB && R_FRAME1 == S_FRAME1 &&
               R_FRAME2 == S_FRAME2 && R_INDEX == S_INDEX && R_TXCSR1 == S_TXCSR1 && R_RXCSR1 == S_RXCSR1 &&
               R_RXCOUNT1 == S_RXCOUNT1, "the model's registers are usb.c's");

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

/* ------------------------------------------------------------------ the host --- */
#define NEVER 0xFFFFFFFFu
static struct {
    uint32_t from_us;                                /* it reads EP1 IN from this time on (NEVER) */
    uint32_t every_us;                               /* .. a packet every so often at most (0: any tick) */
    uint32_t max_pkts;                               /* .. and stops after this many (0: no limit) */
    uint32_t pkts, last_us;
    uint8_t sx[2048];                                /* the SysEx bytes it got */
    uint32_t sx_n, sx_f7, sx_done_us;                /* .. how many, complete messages, when the last one ended */
    uint32_t other;                                  /* other events (clock, notes) */
} host;

static void host_step(void)                          /* the IN token: the packet that waits is taken */
{
    uint32_t i, k;
    if (!sie.on || !(sie.txcsr1 & 1u) || t_us < host.from_us || (host.max_pkts && host.pkts >= host.max_pkts) ||
        (host.pkts && t_us - host.last_us < host.every_us))
        return;
    for (i = 0; i + 3u < sie.tx_n; i += 4u) {
        uint32_t cin = sie.tx[i] & 15u, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : cin == 5u ? 1u : 0u;
        if (!nb)
            host.other++;
        for (k = 0; k < nb && host.sx_n < sizeof host.sx; k++) {
            host.sx[host.sx_n++] = sie.tx[i + 1u + k];
            if (sie.tx[i + 1u + k] == 0xF7u) {
                host.sx_f7++;
                host.sx_done_us = t_us;
            }
        }
    }
    sie.txcsr1 &= (uint8_t)~1u;                      /* acknowledged: the SIE clears TxPktRdy */
    host.pkts++;
    host.last_us = t_us;
}
static void host_send(const uint8_t *m, uint32_t n) /* one SysEx message as an EP1 OUT packet (<= 48 bytes) */
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t k = n - i >= 3u ? 3u : n - i, cin = i + k < n ? 4u : 4u + k;
        sie.rxbuf[o] = (uint8_t)cin;
        sie.rxbuf[o + 1u] = m[i];
        sie.rxbuf[o + 2u] = k > 1u ? m[i + 1u] : 0;
        sie.rxbuf[o + 3u] = k > 2u ? m[i + 2u] : 0;
        o += 4u;
        i += k;
    }
    sie.rx_n = o;
    sie.rxcsr1 |= 1u;
    sie.intrrx1 |= 2u;
}

/* ------------------------------------------------------------------ TIMER5 and the main loop --- */
static struct {
    uint32_t sub, msacc;                             /* fm1_timer5_irq's counters: the poll's phase, the millisecond's */
    uint32_t out_every;                              /* a MIDI clock byte queued every so many ticks (0: none) */
    uint32_t draw_until;                             /* the main loop is in a UI frame until then: no ed_service */
    uint32_t req_us, req_seen;                       /* when the firmware took the restart request */
} tm;
static struct {
    uint32_t n, go, us, stub, sx_n, sx_f7, waiting;  /* the detach: when, the stub's word, what the host had, the endpoint */
} left;

static void t_left(uint32_t w)
{
    if (w)
        left.go++;
    if (left.n++)
        return;                                      /* (CRB_STUB_GO returns here: CRB_REBOOT follows it) */
    left.us = t_us;
    left.stub = w;
    left.sx_n = host.sx_n;
    left.sx_f7 = host.sx_f7;
    left.waiting = sie.txcsr1 & 1u;
    usb_detach();
}
static void t_tick(void)                             /* one TIMER5 interrupt, with what happened on the bus before it */
{
    t_us += 100u;
    host_step();
    if (++tm.msacc >= 10u) {
        tm.msacc = 0;
        fm1_ms++;
    }
    if (tm.out_every && (t_us / 100u) % tm.out_every == 0u)
        midi_out_event(0x0Fu | 0xF8u << 8);          /* (the audio ISR's: MIDI Clock Out) */
    if (tm.sub % 5u == 0u)
        usb_poll();
    if (++tm.sub == 10u)
        tm.sub = 0;
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { t_tick(); }            /* (ota_wire_send waits for room in the ring) */
static void t_loop(void)                             /* the main loop between two ticks */
{
    uint32_t k;
    for (k = 0; k < 3u && t_us >= tm.draw_until; k++) {
        ed_service();
        if (crb.reboot && !tm.req_seen) {
            tm.req_seen = 1;
            tm.req_us = t_us;
        }
    }
}
static void t_run(uint32_t us)
{
    uint32_t end = t_us + us;
    while (t_us != end) {
        t_tick();
        t_loop();
    }
}
static void t_until_left(uint32_t ms)                /* .. until the detach, ms at most */
{
    uint32_t end = t_us + ms * 1000u;
    while (t_us != end && !left.n) {
        t_tick();
        t_loop();
    }
}

/* a unit just started, enumerated and configured, a host that reads at once; ms0: fm1_ms, phase: of the poll
 * against the millisecond (0..9 ticks) */
static void t_boot(uint32_t ms0, uint32_t phase)
{
    memset(&sie, 0, sizeof sie);
    memset(&host, 0, sizeof host);
    memset(&tm, 0, sizeof tm);
    memset(&left, 0, sizeof left);
    memset(&usb, 0, sizeof usb);
    crb.reboot = crb.stub = 0;
    mo_r = mo_w;
    so_r = so_gone = so_w;                           /* (the counters keep running: marks anywhere in their range) */
    sx_watch = sx_ready = sx_collect = sx_busy = 0;
    t_us -= t_us % 1000u;
    fm1_ms = ms0;
    tm.sub = phase % 10u;
    sie.on = 1;
    usb.up = usb.config = 1;
    ep1_config();
}
static const uint8_t STUB_TEST[] = {0xF0, 0x7D, 0x46, 0x4C, 77, 1, 0xF7}, STUB_RESCUE[] = {0xF0, 0x7D, 0x46, 0x4C, 77, 2, 0xF7};
static const uint8_t RESTART[] = {0xF0, 0x7D, 0x46, 0x4C, 72, 0xF7}, LIST[] = {0xF0, 0x7D, 0x46, 0x4C, 65, 0xF7};
static const uint8_t INFO[] = {0xF0, 0x7D, 0x46, 0x4C, 1, 0xF7};
static const uint8_t STUB_OK[] = {0xF0, 0x7D, 0x46, 0x4C, 77, 0, 0xF7}, RESTART_OK[] = {0xF0, 0x7D, 0x46, 0x4C, 72, 0, 0xF7};

/* the request sent, the firmware has taken it (its time: tm.req_us) */
static void t_request(const uint8_t *m, uint32_t n)
{
    uint32_t k;
    tm.req_seen = 0;
    host_send(m, n);
    for (k = 0; k < 500u && !tm.req_seen; k++) {
        t_tick();
        t_loop();
    }
}
/* a request that asks for no restart (or a second one): sent, the request's time stays the first one's */
static void t_other(const uint8_t *m, uint32_t n)
{
    tm.req_seen = 1;
    host_send(m, n);
}
static int32_t since_req(uint32_t us) { return (int32_t)(us - tm.req_us); }   /* us after the request */
/* the host had all of `want` as the last SysEx bytes it got by the detach */
static int host_had(const uint8_t *want, uint32_t n)
{
    return left.sx_n >= n && !memcmp(host.sx + left.sx_n - n, want, n);
}

int main(void)
{
    uint32_t phase, pre, par, k, lost, early, late;
    int32_t dmin, dmax;

    emu_flash_open();                                /* (no file: RAM only, erased) */
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();

    /* ---- a host that reads at once, every phase: the request against the millisecond (an even one, an odd one),
     * the poll against the millisecond, and fm1_ms across its wrap ---- */
    lost = early = late = 0;
    dmin = 0x7FFFFFFF;
    dmax = 0;
    for (par = 0; par < 4u; par++)
        for (phase = 0; phase < 10u; phase++)
            for (pre = 0; pre < 20u; pre++) {
                int32_t d;
                t_boot(par < 2u ? 5000u + par : 0xFFFFFFF0u + par, phase);
                t_run(100u * pre + 100u);
                t_request(STUB_TEST, sizeof STUB_TEST);
                t_until_left(1000);
                d = since_req(left.us);
                lost += !(left.n && host_had(STUB_OK, sizeof STUB_OK) && !left.waiting && !sie.dropped);
                early += d < 149000;
                late += d > 151000;
                dmin = d < dmin ? d : dmin;
                dmax = d > dmax ? d : dmax;
            }
    CHECK(!lost, "a host that reads at once, 800 phases: the reply was not whole at the host in %u", (unsigned)lost);
    CHECK(!early && !late, "a host that reads at once: the restart 150 ms after the request (%u early, %u late; %.1f .. %.1f ms)",
          (unsigned)early, (unsigned)late, dmin / 1000.0, dmax / 1000.0);
    printf("a host that reads at once, 800 phases of the request (even / odd ms, the poll, fm1_ms's wrap): the reply whole "
           "at the host in all, the detach %.1f .. %.1f ms after the request\n", dmin / 1000.0, dmax / 1000.0);

    /* ---- 77 STUB op 1 / op 2 and 72 RESTART: what the restart leaves for the stub ---- */
    t_boot(1000, 0);
    t_run(700);
    t_request(STUB_TEST, sizeof STUB_TEST);
    CHECK(tm.req_seen && crb.stub == STUB_REQ_TEST && !left.n, "STUB 1: taken, nothing leaves with the request");
    t_run(100000);
    CHECK(!left.n && host.sx_n == sizeof STUB_OK && !memcmp(host.sx, STUB_OK, sizeof STUB_OK) && since_req(host.sx_done_us) < 1500,
          "STUB 1: the reply at the host %.1f ms after the request, the unit still there at 100 ms", since_req(host.sx_done_us) / 1000.0);
    t_until_left(1000);
    CHECK(left.n == 2 && left.go == 1 && left.stub == STUB_REQ_TEST && usb.detached && !sie.on,
          "STUB 1: CRB_STUB_GO with the test request, once (then CRB_REBOOT: never reached on the unit)");
    t_run(700000);
    CHECK(left.n == 2 && !crb.reboot, "STUB 1: no second restart");
    t_boot(1000, 3);
    t_request(STUB_RESCUE, sizeof STUB_RESCUE);
    t_until_left(1000);
    CHECK(left.n == 2 && left.stub == STUB_REQ_RESCUE && host_had(STUB_OK, sizeof STUB_OK), "STUB 2: the rescue request");
    t_boot(1000, 7);
    t_request(RESTART, sizeof RESTART);
    t_until_left(1000);
    CHECK(left.n == 1 && !left.go && !left.stub && host_had(RESTART_OK, sizeof RESTART_OK) && !left.waiting &&
          since_req(left.us) >= 149000 && since_req(left.us) <= 151000,
          "RESTART: CRB_REBOOT alone, the reply whole at the host, %.1f ms after the request", since_req(left.us) / 1000.0);

    /* ---- the main loop busy with a UI frame for 12 ms right after the request ---- */
    t_boot(2000, 2);
    t_request(STUB_TEST, sizeof STUB_TEST);
    tm.draw_until = t_us + 12000u;
    t_until_left(1000);
    CHECK(left.n && host_had(STUB_OK, sizeof STUB_OK) && since_req(left.us) >= 149000 && since_req(left.us) <= 151000,
          "a UI frame after the request: the same (%.1f ms)", since_req(left.us) / 1000.0);

    /* ---- a host that takes the packet late: 300 ms after the request ---- */
    for (k = 0; k < 2u; k++) {
        t_boot(3000u + k, 4);
        host.from_us = NEVER;
        t_request(k ? RESTART : STUB_TEST, k ? sizeof RESTART : sizeof STUB_TEST);
        host.from_us = tm.req_us + 300000u;
        t_run(299000u - (t_us - tm.req_us));
        CHECK(!left.n && (sie.txcsr1 & 1u) && !host.sx_n && crb.reboot == 2u,
              "a late host (%s): at 299 ms the reply waits in the endpoint, the unit waits with it", k ? "RESTART" : "STUB");
        t_until_left(1000);
        CHECK(left.n && host_had(k ? RESTART_OK : STUB_OK, 7) && !left.waiting && !sie.dropped,
              "a late host: the reply whole at the host before the detach");
        CHECK(since_req(host.sx_done_us) >= 300000 && since_req(host.sx_done_us) < 300200 &&
              left.us - host.sx_done_us >= 4000u && left.us - host.sx_done_us <= 6000u,
              "a late host: taken at %.1f ms, the detach %.1f ms after that (the grace: 4 whole ms at least)",
              since_req(host.sx_done_us) / 1000.0, (left.us - host.sx_done_us) / 1000.0);
        if (!k)
            printf("a host that takes the packet 300 ms after the request: taken at %.1f ms, the detach at %.1f ms\n",
                   since_req(host.sx_done_us) / 1000.0, since_req(left.us) / 1000.0);
    }

    /* ---- a host that never takes it; one that has dropped the configuration; a SIE that stops answering ---- */
    t_boot(4000, 6);
    host.from_us = NEVER;
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_run(498000u - (t_us - tm.req_us));
    CHECK(!left.n && (sie.txcsr1 & 1u), "a host that never reads: at 498 ms the unit still waits");
    t_until_left(1000);
    CHECK(left.n == 2 && left.stub == STUB_REQ_TEST && since_req(left.us) >= 499000 && since_req(left.us) <= 501000,
          "a host that never reads: the restart at the bound (%.1f ms)", since_req(left.us) / 1000.0);
    CHECK(left.waiting && sie.dropped == 12u && !host.sx_n && so_r == so_w,
          "a host that never reads: the reply was whole in the endpoint (12 bytes, the ring empty), not taken: it goes with the detach");
    printf("a host that never takes the packet: the detach at %.1f ms, the reply still in the endpoint\n", since_req(left.us) / 1000.0);
    t_boot(4000, 1);
    host.from_us = NEVER;
    t_request(RESTART, sizeof RESTART);
    usb.config = 0;                                  /* (SET_CONFIGURATION 0: ep1_tx is no longer called) */
    t_until_left(1000);
    CHECK(left.n == 1 && since_req(left.us) >= 499000 && since_req(left.us) <= 501000,
          "no configuration: the restart at the bound (%.1f ms)", since_req(left.us) / 1000.0);
    t_boot(4000, 8);
    host.from_us = NEVER;
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_run(2000);
    sie.dead = 1;                                    /* every SIE access times out: usb.c gives the controller up */
    sie.txcsr1 = 0;                                  /* (and what it would read is "free") */
    t_until_left(1000);
    CHECK(left.n && !usb.up && crb.mark != so_gone && since_req(left.us) >= 499000 && since_req(left.us) <= 501000,
          "a SIE that stops answering: not taken for \"the host has it\", the restart at the bound (%.1f ms)", since_req(left.us) / 1000.0);

    /* ---- MIDI clock that never leaves the endpoint idle (an event a tick, the host takes a packet a ms) ---- */
    t_boot(6000, 5);
    tm.out_every = 1;
    host.every_us = 2000;
    t_run(50000);
    CHECK(mo_w - mo_r > 16u && (sie.txcsr1 & 1u), "clock flood: the MIDI out ring backed up, the endpoint busy");
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_until_left(1000);
    CHECK(left.n && host_had(STUB_OK, sizeof STUB_OK) && since_req(host.sx_done_us) < 6000 && left.waiting &&
          since_req(left.us) >= 149000 && since_req(left.us) <= 151000,
          "clock flood: the reply goes first (at the host after %.1f ms), the restart at %.1f ms with the endpoint busy",
          since_req(host.sx_done_us) / 1000.0, since_req(left.us) / 1000.0);
    t_boot(6000, 9);
    tm.out_every = 1;
    host.from_us = NEVER;
    t_run(20000);                                    /* (the endpoint holds a packet of clock nobody takes) */
    t_request(STUB_TEST, sizeof STUB_TEST);
    host.from_us = tm.req_us + 300000u;
    host.every_us = 1000;
    t_until_left(1000);
    CHECK(left.n && host_had(STUB_OK, sizeof STUB_OK) && left.waiting && host.other > 16u &&
          since_req(host.sx_done_us) >= 300000 && since_req(host.sx_done_us) <= 302000 &&
          left.us - host.sx_done_us >= 4000u && left.us - host.sx_done_us <= 6500u,
          "clock flood and a late host: the reply behind a packet of clock, at the host at %.1f ms, the detach %.1f ms "
          "later: the reply's packet is waited for, not an idle endpoint",
          since_req(host.sx_done_us) / 1000.0, (left.us - host.sx_done_us) / 1000.0);
    printf("MIDI clock in every packet, the host late: the reply at the host at %.1f ms, the detach at %.1f ms, the endpoint "
           "busy then (%u clock events taken)\n", since_req(host.sx_done_us) / 1000.0, since_req(left.us) / 1000.0, (unsigned)host.other);

    /* ---- a long reply (LIST: more events than the ring holds) still on its way when the restart is asked, the host
     * slow from then on ---- */
    t_boot(7000, 0);
    host.every_us = 1000;
    t_other(LIST, sizeof LIST);
    for (k = 0; k < 40u && !host.sx_n; k++) {        /* (ed_service queues it: ota_wire_send waits for the host) */
        t_tick();
        t_loop();
    }
    t_run(1000);
    CHECK(so_w - so_r > 16u && !host.sx_f7 && host.sx_n, "a long reply: LIST's on its way (%u events still in the ring)", (unsigned)(so_w - so_r));
    host.every_us = 40000;
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_until_left(1000);
    CHECK(left.n && left.sx_f7 == 2u && host_had(STUB_OK, sizeof STUB_OK) && host.sx[0] == 0xF0 && host.sx[4] == 65 &&
          left.sx_n == 9u + 11u * host.sx[7] + sizeof STUB_OK && host.sx[left.sx_n - 8u] == 0xF7 && !left.waiting,
          "a long reply before it, read slowly: both whole at the host (%u bytes)", (unsigned)left.sx_n);
    CHECK(since_req(host.sx_done_us) > 150000 && left.us - host.sx_done_us >= 4000u && left.us - host.sx_done_us <= 6000u,
          "a long reply before it: the last packet at %.1f ms, the detach %.1f ms later",
          since_req(host.sx_done_us) / 1000.0, (left.us - host.sx_done_us) / 1000.0);

    /* ---- a reply queued behind the restart's is not waited for ---- */
    t_boot(8000, 3);
    host.from_us = NEVER;
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_run(2000);
    t_other(INFO, sizeof INFO);
    t_run(3000);
    CHECK((sie.txcsr1 & 1u) && sie.tx_n == 12u && so_w != so_r, "a request after it is answered: INFO's reply in the ring");
    host.from_us = tm.req_us + 300000u;
    host.max_pkts = 1;                               /* (the host takes the one packet and stops) */
    t_until_left(1000);
    CHECK(left.n && left.sx_n == sizeof STUB_OK && host_had(STUB_OK, sizeof STUB_OK) && left.waiting &&
          since_req(left.us) >= 304000 && since_req(left.us) <= 306500,
          "a reply queued behind the restart's is not waited for: the detach at %.1f ms, INFO's in the endpoint", since_req(left.us) / 1000.0);

    /* ---- a bus reset while the reply waits: dropped, nothing left to wait for ---- */
    t_boot(9000, 2);
    host.from_us = NEVER;
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_run(200000u - (t_us - tm.req_us));
    sie.intrusb = 0x04;
    t_until_left(1000);
    CHECK(left.n && usb.resets == 1u && !usb.config && since_req(left.us) >= 204000 && since_req(left.us) <= 206500,
          "a bus reset at 200 ms: the restart %.1f ms after the request, not at the bound", since_req(left.us) / 1000.0);

    /* ---- the request repeated while a restart is owed: its request for the stub, the first one's clock ---- */
    t_boot(10000, 6);
    host.from_us = NEVER;
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_run(100000);
    t_other(STUB_RESCUE, sizeof STUB_RESCUE);
    t_run(5000);
    CHECK(crb.stub == STUB_REQ_RESCUE && crb.reboot == 2u, "a second request at 100 ms: taken");
    host.from_us = tm.req_us + 120000u;
    t_until_left(1000);
    CHECK(left.n && left.stub == STUB_REQ_RESCUE && left.sx_f7 == 2u && host_had(STUB_OK, sizeof STUB_OK) &&
          since_req(left.us) >= 149000 && since_req(left.us) <= 151000,
          "a second request: both replies at the host, the newer request for the stub, the restart %.1f ms after the "
          "FIRST request", since_req(left.us) / 1000.0);
    t_boot(10000, 6);
    host.from_us = NEVER;
    t_request(STUB_TEST, sizeof STUB_TEST);
    t_run(400000);
    t_other(STUB_TEST, sizeof STUB_TEST);
    t_until_left(1000);
    CHECK(left.n && since_req(left.us) >= 499000 && since_req(left.us) <= 501000,
          "a second request at 400 ms, the host not reading: the bound is not pushed back (%.1f ms)", since_req(left.us) / 1000.0);

    /* ---- the endpoint is not read with nothing to send unless a restart is owed ---- */
    t_boot(11000, 0);
    t_other(INFO, sizeof INFO);
    t_run(20000);
    k = sie.csr_reads;
    t_run(200000);
    CHECK(host.sx_f7 == 1u && sie.csr_reads == k && !sx_watch && k <= 3u,
          "no restart owed: INFO answered with %u reads of TXCSR1, none while nothing is to be sent (ep1_tx as before)", (unsigned)k);
    t_request(STUB_TEST, sizeof STUB_TEST);
    k = sie.csr_reads;
    t_until_left(1000);
    CHECK(left.n && sx_watch && sie.csr_reads - k <= 4u,
          "a restart owed: %u reads of TXCSR1 until the reply is seen taken, none after", (unsigned)(sie.csr_reads - k));

    printf("%s: %d checks, %d failed\n", fails ? "CR RESTART TESTS FAILED" : "cr restart tests passed", checks, fails);
    return fails ? 1 : 0;
}
