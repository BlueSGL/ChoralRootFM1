/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) <mcgeisler@gmail.com> */
/* ChoralRoot on the M-Vave SMK-37, the store owner tag (storage.c st_owner_check, build with -DBOARD_SMK37=1): a
 * simulated flash with another firmware's store, an untagged one and ChoralRoot's own; only ChoralRoot's data sectors
 * (ST_SMK_REGIONS) and the tag sector are ever erased, ours is kept, the staging area, the calibration and the rest
 * are never touched. The family is FELUCCA_DATA_FAMILY (build.py: CHORAL). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif
#include "fm1_flash_map.h"
#define FELUCCA_CZ 1
#define FELUCCA_QUAD 1
#ifndef FELUCCA_DATA_FAMILY
#define FELUCCA_DATA_FAMILY "CHORAL"
#endif

static uint8_t nor[0x100000];
static int bad_write;
static int in_regions(uint32_t s);
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || !(in_regions(off) || off == 0xAF000u))
        bad_write++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off != 0xAF000u)
        bad_write++;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

static int in_regions(uint32_t s)
{
    uint32_t r;
    for (r = 0; r < sizeof ST_SMK_REGIONS / sizeof ST_SMK_REGIONS[0]; r++)
        if (s >= ST_SMK_REGIONS[r][0] && s < ST_SMK_REGIONS[r][0] + ST_SMK_REGIONS[r][1] * ST_SECTOR)
            return 1;
    return 0;
}

static int bad;
static void check(const char *what, int ok) { printf("smk owner: %-78s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static void fill(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(nor + 0x9D000, 0x11, 0x12000);          /* objects below the tag */
    memset(nor + 0xB0000, 0x22, 0x4000);           /* the CZ-1 store's second half, the QUAD store */
    memset(nor + 0xBB000, 0x55, 0x5000);           /* the staging (the loader the stub hands over to): never */
    memset(nor + 0xC1000, 0x33, 0x1000);           /* USRTRIM: never touched */
    memset(nor + 0xC2000, 0x44, 0x24000);          /* the CZ-1 banks, the loop slots */
    memset(nor + 0xE6000, 0x66, 0x4000);           /* the rest of USRFLASH: not ChoralRoot's, never touched */
}
static int all(uint32_t lo, uint32_t hi, uint8_t v)
{
    uint32_t i;
    for (i = lo; i < hi; i++)
        if (nor[i] != v)
            return 0;
    return 1;
}
static int store_erased(void)
{
    return all(0x9D000, 0xAF000, 0xFF) && all(0xB0000, 0xB4000, 0xFF) && all(0xC2000, 0xE6000, 0xFF);
}
static int others_kept(void)
{
    return all(0xBB000, 0xC0000, 0x55) && all(0xC1000, 0xC2000, 0x33) && all(0xE6000, 0xEA000, 0x66) &&
           all(0xB4000, 0xBB000, 0xFF);
}
static int tagged(void)
{
    static const char FAM[] = FELUCCA_DATA_FAMILY;
    uint8_t want[16];
    uint32_t i;
    for (i = 0; i < 16u; i++)                     /* (16 bytes: a 7-letter family fills the tag, no NUL) */
        want[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
    return !memcmp(nor + 0xAF000, want, 16) && all(0xAF010, 0xB0000, 0xFF);
}

int main(void)
{
    fill();
    memcpy(nor + 0xAF000, "SMK37DATAFELUCCA", 16);
    st_owner_check();
    check("another family's store (FELUCCA): every ChoralRoot data sector cleared, tagged ours",
          store_erased() && st_owner_cleared && tagged());
    check("... the staging, the calibration (USRTRIM) and the rest of USRFLASH untouched", others_kept());
    fill();
    memcpy(nor + 0xAF000, "SMK37DATASLOOP\0", 16);
    st_owner_cleared = 0;
    st_owner_check();
    check("another family's store (SLOOP): cleared, tagged ours", store_erased() && st_owner_cleared && tagged() &&
          others_kept());
    fill();
    st_owner_check();                              /* tag the store ours (an untagged one: cleared first) */
    memset(nor + 0x9D000, 0x11, 0x12000);
    memset(nor + 0xC2000, 0x44, 0x24000);
    st_owner_cleared = 0;
    st_owner_check();
    check("our own store: kept as it is", all(0x9D000, 0xAF000, 0x11) && all(0xC2000, 0xE6000, 0x44) &&
          !st_owner_cleared && tagged());
    fill();
    st_owner_check();
    check("an untagged store (FELUCCA_DATA_UNTAGGED_OK 0): cleared, tagged", store_erased() && tagged() &&
          others_kept());
    check("nothing written outside ChoralRoot's data sectors and the tag", bad_write == 0);
    printf("%s\n", bad ? "SMK OWNER TEST FAILED" : "smk owner test passed");
    return bad != 0;
}
