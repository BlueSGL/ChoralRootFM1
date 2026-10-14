/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* ChoralRoot on the M-Vave SMK-37, the store owner tag (storage.c st_owner_check, build with -DBOARD_SMK37=1): a
 * simulated flash with another firmware's store, an untagged one and ChoralRoot's own; only ChoralRoot's data sectors
 * (ST_SMK_REGIONS) and the tag sector are ever erased, ours is kept, the staging area, the calibration and the rest
 * are never touched. The family is FELUCCA_DATA_FAMILY (build.py: CHORAL). 0.24: the regions take in the favourites
 * and the sound pool's runs (0xB4000..0xBAFFF, 0xE6000..0xE9FFF, 0xEB000..0xF1FFF); USRFLASH's last sector 0xEA000,
 * the free tail's rest (0xF2000..0xF3FFF), the stock resources and key_mac stay as they are. */
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
static int tag_erases, usr_before_tag;
static int in_regions(uint32_t s);
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || !(in_regions(off) || off == 0xAF000u || off == 0xEA000u))
        bad_write++;
    if (off == 0xEA000u)
        tag_erases++;
    else if (off >= 0xC2000u && off < 0xEA000u && nor[0xEAFF8] == 'u')
        usr_before_tag++;                          /* a USRFLASH erase while the stock marker is still there */
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
    memset(nor + 0xB0000, 0x22, 0xB000);           /* the CZ-1 store's second half, the QUAD store, the favourites,
                                                    * the pool's first run */
    memset(nor + 0xBB000, 0x55, 0x5000);           /* the staging (the loader the stub hands over to): never */
    memset(nor + 0xC1000, 0x33, 0x1000);           /* USRTRIM: never touched */
    memset(nor + 0xC2000, 0x44, 0x28000);          /* the CZ-1 banks, the loop slots, the pool's second run */
    memset(nor + 0xEA000, 0x99, 0x1000);           /* USRFLASH's last sector (the stock marker's): not ChoralRoot's */
    memset(nor + 0xEB000, 0x44, 0x7000);           /* the pool's third run, in FL_TAIL */
    memset(nor + 0xF2000, 0x66, 0x2000);           /* the rest of the free tail: not ChoralRoot's, never touched */
    memset(nor + 0xF4000, 0x77, 0xA000);           /* USR, the stock resources: never touched */
    memset(nor + 0xFF000, 0x88, 0x1000);           /* key_mac: never touched */
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
    return all(0x9D000, 0xAF000, 0xFF) && all(0xB0000, 0xBB000, 0xFF) && all(0xC2000, 0xEA000, 0xFF) &&
           all(0xEB000, 0xF2000, 0xFF);
}
static int others_kept(void)
{
    return all(0xBB000, 0xC0000, 0x55) && all(0xC1000, 0xC2000, 0x33) && all(0xEA000, 0xEB000, 0x99) &&
           all(0xF2000, 0xF4000, 0x66) &&
           all(0xF4000, 0xFE000, 0x77) && all(0xFF000, 0x100000, 0x88);
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
    check("... the staging, USRTRIM, 0xEA000, the free tail's rest, USR and key_mac untouched", others_kept());
    fill();
    memcpy(nor + 0xAF000, "SMK37DATASLOOP\0", 16);
    st_owner_cleared = 0;
    st_owner_check();
    check("another family's store (SLOOP): cleared, tagged ours", store_erased() && st_owner_cleared && tagged() &&
          others_kept());
    fill();
    st_owner_check();                              /* tag the store ours (an untagged one: cleared first) */
    memset(nor + 0x9D000, 0x11, 0x12000);
    memset(nor + 0xC2000, 0x44, 0x28000);
    memset(nor + 0xEB000, 0x44, 0x7000);
    st_owner_cleared = 0;
    st_owner_check();
    check("our own store: kept as it is", all(0x9D000, 0xAF000, 0x11) && all(0xC2000, 0xEA000, 0x44) &&
          all(0xEB000, 0xF2000, 0x44) &&
          !st_owner_cleared && tagged());
    fill();
    st_owner_check();
    check("an untagged store (FELUCCA_DATA_UNTAGGED_OK 0): cleared, tagged", store_erased() && tagged() &&
          others_kept());
    /* the stock firmware's mapping in USRFLASH (marker "usrdata" at 0xEAFF8, A3 3A at 0xC2000) */
    fill();
    memset(nor + 0xC2000, 0x00, 0x28000);          /* (made-up presets) */
    nor[0xC2000] = 0xA3; nor[0xC2001] = 0x3A;
    memset(nor + 0xEA000, 0xFF, 0x1000);
    memcpy(nor + 0xEAFF8, "usrdata", 8);
    memcpy(nor + 0xAF000, "SMK37DATASLOOP\0", 16);
    tag_erases = usr_before_tag = 0;
    st_owner_cleared = 0;
    st_owner_check();
    check("owner change over the intact stock mapping: the store cleared, USRFLASH and its marker kept",
          all(0x9D000, 0xAF000, 0xFF) && all(0xB0000, 0xB4000, 0xFF) && nor[0xC2000] == 0xA3 && all(0xC2002, 0xEA000, 0x00) &&
          !memcmp(nor + 0xEAFF8, "usrdata", 8) && tag_erases == 0 && st_stock_tagged() && st_stock_intact() && tagged());
    st_stock_release();                            /* ChoralRoot's first write into USRFLASH (storage_hw.c st_erase) */
    check("first write into USRFLASH: the marker sector erased, once", all(0xEA000, 0xEB000, 0xFF) && tag_erases == 1 &&
          !st_stock_tagged());
    st_stock_release();
    check("... and only once", tag_erases == 1);
    fill();
    memset(nor + 0xEA000, 0xFF, 0x1000);
    memcpy(nor + 0xEAFF8, "usrdata", 8);           /* the marker over another firmware's data (0x44): broken stock */
    memcpy(nor + 0xAF000, "SMK37DATASLOOP\0", 16);
    tag_erases = usr_before_tag = 0;
    st_owner_check();
    check("owner change, marker over another firmware's data: marker released first, then the store cleared",
          store_erased() && all(0xEA000, 0xEB000, 0xFF) && tag_erases == 1 && usr_before_tag == 0 && tagged());
    fill();
    memset(nor + 0xEA000, 0xFF, 0x1000);
    tag_erases = 0;
    st_owner_check();
    check("no marker: nothing more erased than before", store_erased() && tag_erases == 0);
    check("nothing written outside ChoralRoot's data sectors, the tag and the stock marker sector", bad_write == 0);
    printf("%s\n", bad ? "SMK OWNER TEST FAILED" : "smk owner test passed");
    return bad != 0;
}
