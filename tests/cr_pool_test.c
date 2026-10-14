/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the sound pool (0.22, firmware/src/upool.c over storage.c sp_*) on the firmware as the emulator builds
 * it: every engine's sound (P5 included) saved into slot 128 and loaded back after a power cycle, its patch byte for
 * byte; the table; a full pool (128 slots) and "no free slot"; erase and rename; the favourites' section 0x81 (slots
 * 33..128) and what 377264f's reader keeps of the object; the quiet writes' spare (the pool's erase ahead); a cut
 * during a save on the firmware's own path. The migration from a real 0.21-format image and the downgrade are
 * tools/emu/test_pool.sh's; the floating banks' power cuts tests/pool_test.c's. */
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
#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define OK(c, ...)                                                      \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
        } else {                                                        \
            printf("ok   ");                                            \
        }                                                               \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    } while (0)

static void power_cycle(void)                        /* the table and the stores from flash, as at power-on */
{
    memset(upn, 0, sizeof upn);
    sp_scanned = 0;
    up_boot();
}

int main(void)
{
    static const uint32_t ENG[] = {ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_QUAD, ENGI_PROPHET};
    static const char *const NM[] = {"VA SAVED", "FM6 SAVED", "CZ SAVED", "QUAD SAVED", "P5 SAVED"};
    track_t *t = &trk[CR_PART_CHORD];
    uint8_t b0[UPP_MAX], b1[UPP_MAX];
    uint32_t i, k, n, bad;
    char nm[13];

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    uint32_t fm6_n0 = pool_count(ENGI_FM6);
    OK(UP_POOL && UP_SLOTS == 128u && upool_boot_rc == 0 && !up_count(), "a fresh unit: 128 slots, none used, no migration");

    /* ---- every engine into slot 128, back after a power cycle ---- */
    for (i = 0; i < NELEM(ENG); i++) {
        const eng_deep_t *d = ENGINES[ENG[i]]->deep;
        uint32_t len = up_patch_len(ENG[i]), pos;
        int16_t p[P_COUNT];
        cu_pool_load(0, ENG[i], pool_count(ENG[i]) > 2u ? 2u : 1u);
        if (d->npages && d->get && d->set) {         /* an edit of the deep patch: its first column a step */
            int32_t v = d->get(t, 0, 0);
            d->set(t, 0, 0, v > 0 ? v - 1 : v + 1);
        }
        d->blob_get(t, b0);
        memcpy(p, t->p, sizeof p);
        OK(len == d->blob_size && len <= UPP_MAX && cb_store(t, 0, 127, NM[i], 0) == 0 && up_used(127) &&
           up_rec(127)->engine == ENG[i], "%s: saved into U128 (%u bytes of patch)", ENGINES[ENG[i]]->name, (unsigned)len);
        power_cycle();
        up_name(127, nm);
        OK(up_used(127) && !strcmp(nm, NM[i]) && up_engine(127) == ENG[i] && up_patch_get(127, ENG[i], b1, len) == 0 &&
           !memcmp(b0, b1, len), "%s: after a power cycle U128 is \"%s\", its patch byte for byte", ENGINES[ENG[i]]->name, nm);
        cu_pool_load(0, ENG[i], 1);
        pos = pool_pos_slot(ENG[i], 127);
        cu_pool_load(0, ENG[i], pos);
        d->blob_get(t, b1);
        for (k = 0, bad = 0; k < P_COUNT; k++)
            bad += !param_kept(k) && k != P_VOICE && p[k] != t->p[k];
        OK(psnd[0].kind == PK_USER && psnd[0].slot == 127 && !memcmp(b0, b1, len) && !bad,
           "%s: PRESETS reaches it at %u (the factory presets before it); loaded, the part's patch and values are the saved ones",
           ENGINES[ENG[i]]->name, (unsigned)pos);
        OK(up_put(127, 0) == 0 && !up_used(127) && up_patch_get(127, ENG[i], b1, len) != 0, "%s: U128 erased, its patch with it",
           ENGINES[ENG[i]]->name);
    }

    /* ---- a rename keeps the patch; another engine's sound over a slot drops it ---- */
    cu_pool_load(0, ENGI_CZ, 3);
    ENGINES[ENGI_CZ]->deep->blob_get(t, b0);
    OK(cb_store(t, 0, 99, "CZ HUNDRED", 0) == 0 && up_rename(99, "RENAMED") == 0 && up_patch_get(99, ENGI_CZ, b1, CZ_BYTES) == 0 &&
       !memcmp(b0, b1, CZ_BYTES) && (up_name(99, nm), !strcmp(nm, "RENAMED")), "rename U100: the name changes, the CZ-1 tone stays");
    cu_pool_load(0, ENGI_VA, 2);
    OK(cb_store(t, 0, 99, "VA OVER", 0) == 0 && up_patch_get(99, ENGI_CZ, b1, CZ_BYTES) != 0 &&
       up_patch_get(99, ENGI_VA, b1, VA_BLOB) == 0, "a VA sound over U100: the CZ-1 tone gone, the VA patch there");

    /* ---- the favourites: a mark on slot 100 (section 0x81), one on slot 32 (0x80) ---- */
    {
        static uint8_t pk[CRF_PACK_MAX];
        uint32_t len, p, s;
        int has81 = 0, has80 = 0;
        (void)crf_unpack(0, 0);
        OK(crf_set(CRF_T_USER, 99, 1) == 1 && crf_set(CRF_T_USER, 31, 1) == 1 && crf_set_type(CRF_T_USER, 99, CRF_BASS) == 1 &&
           crf_has(CRF_T_USER, 99) && crf_type(CRF_T_USER, 99) == CRF_BASS && !crf_has(CRF_T_USER, 100),
           "favourites: U100 marked (typed Bass), U32 marked");
        len = crf_pack(pk);
        for (p = 2, s = 0; s < pk[1] && p + 2u <= len; s++, p += 2u + pk[p + 1u]) {
            has81 |= pk[p] == CRF_T_USER2 && pk[p + 1u] == 9u && pk[p + 2u + 8u] == 0x08u;   /* (id 99 - 32 = 67: byte 8 bit 3) */
            has80 |= pk[p] == CRF_T_USER && pk[p + 1u] == 4u && pk[p + 2u + 3u] == 0x80u;
        }
        OK(has80 && has81 && len <= 264u, "favourites: packed, U32 in 0x80 as before, U100 in 0x81; %u bytes (377264f reads 264 at most)",
           (unsigned)len);
        OK(crf_unpack(pk, len) == 0 && crf_has(CRF_T_USER, 99) && crf_has(CRF_T_USER, 31) && crf_type(CRF_T_USER, 99) == CRF_BASS,
           "favourites: unpacked, both marks and U100's type back");
        {   /* 377264f's reader (cr_favs.c of then: a section it does not know is skipped): built from that commit by
             * tools/emu/test_pool.sh; here what the bytes say: 0x81 and its | 0x40 / | 0x20 forms are tags it lacks */
            OK(crf_off(CRF_T_USER2, &s) == 4u && s == 12u, "favourites: 0x81 is 12 bytes after 0x80's 4 (ids 32..127)");
        }
        OK(up_put(99, 0) == 0 && !crf_has(CRF_T_USER, 99) && crf_has(CRF_T_USER, 31), "favourites: U100 erased: its mark goes");
        (void)crf_unpack(0, 0);
    }

    /* ---- a full pool: 128 saves, then "no free slot" ---- */
    cu_pool_load(0, ENGI_FM6, 1);
    for (k = 0, n = 0; k < UP_SLOTS; k++)
        if (!up_used(k)) {
            snprintf(nm, sizeof nm, "FILL %03u", (unsigned)k + 1u);
            n += cb_store(t, 0, k, nm, 0) == 0;
        }
    OK(up_count() == 128u && cb_free_slot() == UP_SLOTS, "a full pool: 128 slots used (%u saved here), no free slot", (unsigned)n);
    power_cycle();
    for (k = 0, bad = 0; k < UP_SLOTS; k++) {
        char want[13];
        snprintf(want, sizeof want, "FILL %03u", (unsigned)k + 1u);
        up_name(k, nm);
        bad += k != 99u && strcmp(nm, want) != 0;
    }
    OK(up_count() == 128u && !bad && pool_count(ENGI_FM6) == fm6_n0 + 128u,
       "a full pool after a power cycle: every slot's name; FM6's pool %u places", (unsigned)pool_count(ENGI_FM6));
    {   /* the floating banks: 13 used, every sector a valid copy or free */
        uint32_t b, used = 0;
        for (b = 0; b < SP_NBANK; b++)
            used += sp_used(b);
        OK(used == SP_NBANK, "the pool's 13 banks written (slots 129, 130 of the last unused)");
    }

    /* ---- the spare (quiet writes): erased ahead, a save programs only ---- */
    {
        uint32_t e0;
        OK(sp_prep() >= 0 && sp_ready(), "spare: erased ahead (sp_prep)");
        e0 = sp_erases;
        OK(up_rename(5, "NO ERASE") == 0 && sp_erases == e0, "spare: a save on it erases nothing");
        e0 = sp_erases;
        OK(up_rename(6, "ONE ERASE") == 0 && sp_erases - e0 <= 1u, "spare: the next save (no prep between): at most one erase");
    }

    /* ---- a cut in the middle of a save: the old slot or the new one, nothing else changed ---- */
    {
        static uint8_t before[0x10000];
        char a[13], c[13];
        memcpy(before, emu_flash + SP_BASE, sizeof before);
        up_name(7, a);
        emu_flash_fail_after(8);                     /* (emu_hal_fw.h: the 9th program fails, nothing after) */
        n = (uint32_t)up_rename(7, "TORN");
        emu_flash_fail_after(-1);
        power_cycle();
        up_name(7, c);
        for (k = 0, bad = 0; k < UP_SLOTS; k++) {
            char want[13];
            snprintf(want, sizeof want, "FILL %03u", (unsigned)k + 1u);
            up_name(k, nm);
            bad += k != 7u && k != 5u && k != 6u && k != 99u && strcmp(nm, want) != 0;
        }
        OK(n == 2u && !strcmp(c, a) && !bad && up_count() == 128u, "a save cut after 8 programs: rc 2, U008 as it was (%s), the rest intact", c);
    }

    printf("%s: %d checks, %d failed\n", fails ? "CR POOL TESTS FAILED" : "cr pool tests passed", checks, fails);
    return fails ? 1 : 0;
}
