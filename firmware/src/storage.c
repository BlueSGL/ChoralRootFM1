/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,
 * user sample slots 0xA0000..0xDBFFF (eng_sample.c; ChoralRoot, FELUCCA_SAMPLE 0: slots 1-2, 0xA0000..0xC7FFF, stay
 * reserved and unused, slot 3's 0xC8000..0xDBFFF holds the loops, cr_ui.c CRL_FL_BASE), user preset banks 0xDC000..0xDFFFF (upreset.c), the FM6
 * patch bank (fm6_bank.c): copy A 0x9F000, copy B 0xFE000 (the two free sectors).
 * ChoralRoot's project pairs: 0x97000 the VA patch store (va_store.c), 0x99000 / 0x9B000 the FM6 patch store
 * (fm6_ustore.c), 0x9D000 the CZ-1 tone store's slots 1..16 (cz_ustore.c, OBJ_PROJECT0 + 3). FELUCCA_CZ: on the user
 * sample slot 1's flash (unused since the all-synth change, FELUCCA_SAMPLE 0): 0xA0000 / 0xA1000 the CZ-1 tone store's
 * slots 17..32 (OBJ_CZSTORE1), 0xA2000..0xB1FFF the eight CZ-1 banks A..H (cz_bank.c, OBJ_CZBANK0 + k: an A/B pair
 * each). FELUCCA_QUAD: 0xB2000 / 0xB3000 the QUAD patch store (quad_store.c, OBJ_QUADSTORE: the user sample slot 2's
 * flash); 0xB4000 / 0xB5000 the session (cr_settings.c, OBJ_SESSION: where the unit was left, 0.18);
 * 0xB6000..0xC7FFF stays free */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2,
#if FELUCCA_CZ && FELUCCA_QUAD
       OBJ_CZSTORE1, OBJ_CZBANK0, OBJ_QUADSTORE = OBJ_CZBANK0 + 8, OBJ_SESSION, OBJ_COUNT
#elif FELUCCA_CZ
       OBJ_CZSTORE1, OBJ_CZBANK0, OBJ_COUNT = OBJ_CZBANK0 + 8
#else
       OBJ_COUNT
#endif
};

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
#ifndef ST_BLOCKED
#define ST_BLOCKED() 0                         /* (core.h: SAFE MODE, the objects neither loaded nor saved) */
#endif
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

#if BOARD_SMK37
/* SMK-37: the FM-1's places are taken by the SMK's own areas (its app area ends at 0x9CFFF, its settings area is the
 * stock resources): every object moves, A/B neighbours as on the FM-1. In FL_DATA (0x9D000..0xBAFFF, the stock VM):
 * the VA store, the FM6 stores and the CZ-1 store's first half (OBJ_PROJECT0..+3) 0x9D000..0xA4FFF, the user sound
 * banks 0xA5000..0xA8FFF, the FM6 bank 0xA9000 / 0xAA000, the settings 0xAB000 / 0xAC000, the session 0xAD000 /
 * 0xAE000; the store owner tag 0xAF000 (st_owner_check: the same place in every ported firmware); the CZ-1 store's
 * second half 0xB0000 / 0xB1000, the QUAD store 0xB2000 / 0xB3000; 0xB4000..0xBAFFF spare. In USRFLASH (FL_SMP,
 * 0xC2000..0xE9FFF, the stock's user area: ChoralRoot has no samples) the eight CZ-1 banks 0xC2000..0xD1FFF and the
 * loop slots from 0xD2000 (cr_ui.c CRL_FL_BASE). Never near BTIF / USRTRIM (0xC0000..0xC1FFF), the stock resources
 * (0xF4000..) or key_mac (0xFF000). */
#define ST_SMK_BASE 0x9D000u
#define ST_SMK_PAIRS 9u                        /* the pairs below the owner tag */
#define ST_SMK_SECTORS (2u * ST_SMK_PAIRS)
#define ST_SMK_HIGH 0xB0000u                   /* the pairs above it */
#define ST_SMK_HIGH_SECTORS 4u
#define ST_SMK_CZB 0xC2000u                    /* the CZ-1 banks, in USRFLASH */
#define ST_SMK_CZB_SECTORS 16u
#define ST_SMK_LOOPS 0xD2000u                  /* the loop slots (cr_ui.c CRL_FL_BASE), in USRFLASH */
#define ST_SMK_LOOP_SECTORS 20u
/* every place of ChoralRoot's data on the SMK (base, sectors): the owner check and Options > Flash Data clear these */
static const uint32_t ST_SMK_REGIONS[][2] = {{ST_SMK_BASE, ST_SMK_SECTORS}, {ST_SMK_HIGH, ST_SMK_HIGH_SECTORS},
                                             {ST_SMK_CZB, ST_SMK_CZB_SECTORS}, {ST_SMK_LOOPS, ST_SMK_LOOP_SECTORS}};
#if FELUCCA_CZ && FELUCCA_QUAD
_Static_assert(OBJ_COUNT == 19u && OBJ_SESSION + 1u == OBJ_COUNT, "SMK store: a new object needs a place");
#endif
_Static_assert(ST_SMK_BASE + ST_SMK_SECTORS * ST_SECTOR == 0xAF000u, "SMK store: the objects end at the owner tag");
_Static_assert(ST_SMK_HIGH >= 0xAF000u + ST_SECTOR && ST_SMK_HIGH + ST_SMK_HIGH_SECTORS * ST_SECTOR <= 0xBB000u,
               "SMK store: past FL_DATA");
_Static_assert(ST_SMK_CZB >= 0xC2000u && ST_SMK_CZB + ST_SMK_CZB_SECTORS * ST_SECTOR <= ST_SMK_LOOPS &&
               ST_SMK_LOOPS + ST_SMK_LOOP_SECTORS * ST_SECTOR <= 0xEA000u, "SMK store: past FL_SMP");
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    copy &= 1u;
#if FELUCCA_CZ && FELUCCA_QUAD
    if (obj == OBJ_SESSION)
        return ST_SMK_BASE + (8u * 2u + copy) * ST_SECTOR;
    if (obj == OBJ_QUADSTORE)
        return ST_SMK_HIGH + (2u + copy) * ST_SECTOR;
#endif
#if FELUCCA_CZ
    if (obj == OBJ_CZSTORE1)
        return ST_SMK_HIGH + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0)
        return ST_SMK_CZB + ((obj - OBJ_CZBANK0) * 2u + copy) * ST_SECTOR;
#endif
    if (obj == OBJ_SETTINGS)
        return ST_SMK_BASE + (7u * 2u + copy) * ST_SECTOR;
    if (obj == OBJ_FM6BANK)
        return ST_SMK_BASE + (6u * 2u + copy) * ST_SECTOR;
    if (obj >= OBJ_UPRESET0)
        return ST_SMK_BASE + ((4u + obj - OBJ_UPRESET0) * 2u + copy) * ST_SECTOR;
    return ST_SMK_BASE + ((obj - OBJ_PROJECT0) * 2u + copy) * ST_SECTOR;
}

/* The owner of the store. Several FM-1 firmwares are ported to the SMK-37 with the same board layer and this same
 * store area, each with its own object formats: a firmware must never parse another's data. The sector after the
 * objects (0xAF000, the same place in every port) holds "SMK37DATA" + the firmware family (FELUCCA_DATA_FAMILY,
 * build.py; ChoralRoot: CHORAL). Ours: load as ever. None (erased): data written before the tag existed, kept only
 * with FELUCCA_DATA_UNTAGGED_OK (0 here: the SMK build never had untagged data, so an erased tag with objects
 * present is another firmware's). Another family's: clear every place ChoralRoot keeps data in (the objects, the
 * CZ-1 banks and the loop slots: st_smk_clear) and take the store. Calibration, BT pairing and key_mac are outside
 * them: never touched. Called first in persist_boot (cr_shim.c), before any store read; never in SAFE MODE. */
#ifndef FELUCCA_DATA_FAMILY
#define FELUCCA_DATA_FAMILY "CHORAL"
#endif
#ifndef FELUCCA_DATA_UNTAGGED_OK
#define FELUCCA_DATA_UNTAGGED_OK 0
#endif
#define ST_OWNER_OFF 0xAF000u
_Static_assert(ST_OWNER_OFF == ST_SMK_BASE + ST_SMK_SECTORS * ST_SECTOR, "SMK store owner: the sector after the objects");
static uint8_t st_owner_cleared;                /* (the console says so) */
#ifndef ST_FEED
#define ST_FEED() ((void)0)                     /* (the device: the watchdog, between erases) */
#endif
static void st_smk_clear(void)
{
    uint32_t r, k;
    for (r = 0; r < sizeof ST_SMK_REGIONS / sizeof ST_SMK_REGIONS[0]; r++)
        for (k = 0; k < ST_SMK_REGIONS[r][1]; k++) {
            ST_FEED();
            st_erase(ST_SMK_REGIONS[r][0] + k * ST_SECTOR);
        }
}
static void st_owner_check(void)
{
    static const char FAM[] = FELUCCA_DATA_FAMILY;
    uint8_t tag[16], want[16];
    uint32_t i, erased = 1, ours = 1;
    for (i = 0; i < 16u; i++)
        want[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
    if (st_read(ST_OWNER_OFF, tag, 16))
        return;
    for (i = 0; i < 16u; i++) {
        erased &= tag[i] == 0xFFu;
        ours &= tag[i] == want[i];
    }
    if (ours)
        return;
    if (!erased || !FELUCCA_DATA_UNTAGGED_OK) {     /* another firmware's store (or untagged data): clear it */
        st_smk_clear();
        st_owner_cleared = 1;
    }
    st_erase(ST_OWNER_OFF);
    st_prog(ST_OWNER_OFF, want, 16);
}
#else
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK)
        return copy ? 0xFE000u : 0x9F000u;
#if FELUCCA_CZ && FELUCCA_QUAD
    if (obj == OBJ_QUADSTORE)
        return 0xB2000u + copy * ST_SECTOR;
    if (obj == OBJ_SESSION)
        return 0xB4000u + copy * ST_SECTOR;
#endif
#if FELUCCA_CZ
    if (obj == OBJ_CZSTORE1)
        return 0xA0000u + copy * ST_SECTOR;
    if (obj >= OBJ_CZBANK0)
        return 0xA2000u + (obj - OBJ_CZBANK0) * 2u * ST_SECTOR + copy * ST_SECTOR;
#endif
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}
#endif /* BOARD_SMK37 */

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > ST_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || st_current(obj, &h) < 0 || h.len > max)
        return -1;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (ST_BLOCKED() || obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}
