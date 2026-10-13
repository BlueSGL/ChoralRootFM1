/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* ChoralRoot on the M-Vave SMK-37, the flash map (build with -DBOARD_SMK37=1): every 4 KiB sector of the 1 MiB part
 * through the app's allow-list (FL_RANGE_OK / FL_GUARD, fm1_flash.h), the update loader's (loader.c) and ldr_core.c's
 * record window: none of them may ever reach the head 0x0-0x3FFF, BTIF / USRTRIM 0xC0000-0xC1FFF, USR 0xF4000-0xFDFFF
 * or key_mac 0xFF000-; every stored object (storage.c), the CZ-1 banks, the loop slots, the owner tag and the OTA
 * staging (ota.c, the boot stub's view of it) lie inside the allowed windows, apart from each other; ranges that wrap
 * or straddle a boundary are refused. */
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

static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { (void)off; (void)src; (void)n; return -1; }
#include "../firmware/src/storage.c"
#include "../firmware/stub/stub_core.h"
#define LDR_RANGE_OK(off, n) FL_LDR_RANGE_OK(off, n)       /* what loader.c uses on the SMK */
#define LOOP_SLOTS 10u                                      /* cr_loop.h CRL_SLOTS */

static int bad;
static void check(const char *what, int ok) { printf("smk flash: %-80s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static int protected_sector(uint32_t s)
{
    return s < 0x4000u || (s >= 0xC0000u && s < 0xC2000u) || (s >= 0xF4000u && s < 0xFE000u) || s >= 0xFF000u;
}

static uint8_t owner[0x100];                                /* by sector: who holds it (0 none) */

static int in_regions(uint32_t s)                           /* the sector is one ST_SMK_REGIONS clears */
{
    uint32_t r;
    for (r = 0; r < sizeof ST_SMK_REGIONS / sizeof ST_SMK_REGIONS[0]; r++)
        if (s >= ST_SMK_REGIONS[r][0] && s < ST_SMK_REGIONS[r][0] + ST_SMK_REGIONS[r][1] * ST_SECTOR)
            return 1;
    return 0;
}

int main(void)
{
    uint32_t s, o, c, leak_app = 0, leak_ldr = 0, leak_guard = 0, allowed = 0, ok, k;
    for (s = 0; s < 0x100000u; s += 0x1000u) {
        if (!protected_sector(s))
            continue;
        leak_app += FL_RANGE_OK(s, 0x1000u) || FL_STORE_OK(s, 0x1000u) || FL_RANGE_OK(s, 256u) || FL_RANGE_OK(s + 0xF00u, 256u);
        leak_ldr += LDR_RANGE_OK(s, 0x1000u) || LDR_RANGE_OK(s + 0xF00u, 256u);
        leak_guard += !FL_PROTECTED(s, 0x1000u) || !FL_PROTECTED(s + 0xF00u, 256u);
    }
    for (s = 0; s < 0x100000u; s += 0x1000u)
        allowed += FL_GUARD(s, 0x1000u);
    check("no protected sector passes the app's allow-list (erase, page)", leak_app == 0);
    check("no protected sector passes the loader's allow-list", leak_ldr == 0);
    check("the RAM driver's own net (FL_PROTECTED) covers every protected sector", leak_guard == 0);
    check("straddling ranges refused: 0xBF000+0x2000, 0xE9000+0x2000, 0x3000+0x2000",
          !FL_GUARD(0xBF000u, 0x2000u) && !FL_GUARD(0xE9000u, 0x2000u) && !LDR_RANGE_OK(0xBF000u, 0x2000u) &&
          !LDR_RANGE_OK(0x3000u, 0x2000u) && FL_PROTECTED(0xBF000u, 0x2000u) && FL_PROTECTED(0x3F00u, 0x200u));
    check("wrapping ranges refused (off + n past 4 GiB)",
          !FL_GUARD(0xBB000u, 0xFFFFF000u) && !LDR_RANGE_OK(0x9D000u, 0xFFFFFF00u) && FL_PROTECTED(0xBB000u, 0xFFFFF000u));
    check("app windows: FL_DATA 0x9D000-0xBAFFF, FL_OTA 0xBB000-0xBFFFF, USRFLASH 0xC2000-0xE9FFF",
          FL_GUARD(0x9D000u, 0x1000u) && FL_GUARD(0xBA000u, 0x1000u) && FL_GUARD(0xBF000u, 0x1000u) &&
          FL_GUARD(0xC2000u, 0x1000u) && FL_GUARD(0xE9000u, 0x1000u) && !FL_GUARD(0xEA000u, 0x1000u) &&
          !FL_GUARD(0x9C000u, 0x1000u) && allowed == (0xC0000u - 0x9D000u + 0xEA000u - 0xC2000u) / 0x1000u);
    check("loader: app area 0x4000-0x9CFFF, records 0x9D000-0xBFFFF only",
          LDR_RANGE_OK(0x4000u, 0x1000u) && LDR_RANGE_OK(0x9C000u, 0x1000u) && LDR_RANGE_OK(0xBF000u, 0x1000u) &&
          !LDR_RANGE_OK(0xC0000u, 0x1000u) && FL_REC_HI == 0xC0000u);

    /* who holds each sector: the objects (A/B), the loop slots, the owner tag, the staging */
    memset(owner, 0, sizeof owner);
    ok = 1;
    for (o = 0; o < OBJ_COUNT; o++)
        for (c = 0; c < 2u; c++) {
            s = st_sector(o, c);
            ok &= FL_STORE_OK(s, 0x1000u) && !(s & 0xFFFu) && !owner[s >> 12] && in_regions(s);
            owner[s >> 12] = (uint8_t)(1u + o);
        }
    check("storage.c: all 19 objects (A/B) in the store's windows, no two share a sector", ok && OBJ_COUNT == 19u);
    for (k = 0, ok = 1; k < 2u * LOOP_SLOTS; k++) {
        s = ST_SMK_LOOPS + k * ST_SECTOR;
        ok &= FL_IN(s, 0x1000u, FL_SMP_LO, FL_SMP_HI) && !owner[s >> 12] && in_regions(s);
        owner[s >> 12] = 0xF0;
    }
    check("the ten loop slots (A/B) in USRFLASH, apart from every object", ok && ST_SMK_LOOPS == 0xD2000u);
    check("the owner tag 0xAF000: in FL_DATA, nobody else's, never cleared as data",
          ST_OWNER_OFF == 0xAF000u && FL_STORE_OK(ST_OWNER_OFF, 0x1000u) && !owner[0xAF] && !in_regions(ST_OWNER_OFF));
    for (s = FL_OTA_LO, ok = 1; s < FL_OTA_HI; s += 0x1000u)
        ok &= !owner[s >> 12] && !in_regions(s);
    check("the OTA staging 0xBB000-0xBFFFF: no data, never cleared as data", ok);
    for (k = 0, ok = 1; k < sizeof ST_SMK_REGIONS / sizeof ST_SMK_REGIONS[0]; k++)
        for (s = ST_SMK_REGIONS[k][0]; s < ST_SMK_REGIONS[k][0] + ST_SMK_REGIONS[k][1] * ST_SECTOR; s += 0x1000u)
            ok &= FL_STORE_OK(s, 0x1000u) && owner[s >> 12] != 0;
    check("the regions an owner change and Flash Data clear: data sectors only, all of them in the store", ok);
    check("settings 0xAB000 / 0xAC000, the VA store from 0x9D000, the CZ-1 banks from 0xC2000",
          st_sector(OBJ_SETTINGS, 0) == 0xAB000u && st_sector(OBJ_SETTINGS, 1) == 0xAC000u &&
          st_sector(OBJ_PROJECT0, 0) == 0x9D000u && st_sector(OBJ_CZBANK0, 0) == 0xC2000u &&
          st_sector(OBJ_CZBANK0 + 7u, 1) == 0xD1000u);
    check("FL_PLAIN_LO = the VM start 0x9D000, the boot stub's too", FL_PLAIN_LO == 0x9D000u && STUB_PLAIN_LO == FL_PLAIN_LO);
    check("the boot stub: the staged loader at FL_OTA, the marker in the app area",
          STUB_LDR_OFF == FL_OTA_LO && STUB_LDR_OFF + STUB_LDR_LEN == FL_OTA_HI && FL_IN(STUB_MARK_SEC, 0x1000u, FL_APP_LO, FL_APP_HI));
    {
        uint8_t parm[STUB_PARM_LEN];
        stub_record(parm);
        check("the boot stub's record: the SMK-37 loader's identity, the staging address",
              !memcmp(parm + 8, "ota-SMK-37 Pro_016", 19) && stub_rd32(parm + 72) == FL_OTA_LO);
    }
    check("FL_JEDEC_OK: the SMK part 0xEB6014 and 0x856014 only", FL_JEDEC_OK(0xEB6014u) && FL_JEDEC_OK(0x856014u) &&
          !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu) && !FL_JEDEC_OK(0xEB6015u));
    printf("%s\n", bad ? "SMK FLASH TEST FAILED" : "smk flash test passed");
    return bad != 0;
}
