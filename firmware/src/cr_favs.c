/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the favourites (0.22; docs/PRESETS.md "Favourites"): a mark on a SOUND, a bit per sound.
 *
 * What a bit names (cr_ui.c cu_fav_key turns a pool entry into it):
 *   a factory sound      (its engine's index, a stable id): the id is the preset's index in the engine's own table
 *                        (engine_t.presets: the index a user record's binding already stores, cr_bank.c, so it has to
 *                        stay what it is from release to release; nothing was reordered since 0.14), and for P5 the
 *                        program's SEQUENTIAL NUMBER 1..200 (eng_prophet_test.c p5_preset_id), never its place in the
 *                        pool: the shipped list (assets/prophet5-factory/shipped.txt) is trimmed and reordered, the
 *                        marks stay on the programs
 *   a user sound         its slot 0..31 (CRF_T_USER). A record that OVERWRITES a factory preset is that factory sound
 *                        (the same place in the pool): its factory key, so the mark survives Overwrite and Reset
 *   INIT                 nothing: it cannot be a favourite
 * Felucca's favorites.c (a 16 x 32 byte bitmap inside its settings record, ui.c's browser filter) is not compiled into
 * ChoralRoot (ui.c is not part of the unit) and is not used here: its bitmap would have to live in the settings
 * record, which never grows, it has no room for engine 16, and it knows P5's programs only by their place.
 *
 * The RAM form: crf_bits, a fixed run of bytes per section (CRF_SEC: 72 bytes). The flash form, a storage.c object of
 * its own (OBJ_FAVS, the A/B pair 0xB6000 / 0xB7000: a save goes to the other copy, the commit record last, so a torn
 * write leaves the marks as they were), written by crf_flush:
 *   byte 0   the version, CRF_VER (1). Another version: read as "no marks" (and replaced by the next change)
 *   byte 1   the number of sections that follow
 *   then per section: tag (the engine's index, 0x80 the user slots), len, len bytes of bits (bit i of byte i / 8 is id
 *            i; the zero bytes at the end are not stored; a section without a mark is not stored)
 * A section this firmware does not know is skipped, bits past a section's size are ignored: a newer firmware's object
 * is read for what is known of it. Empty (no mark anywhere): two bytes, {1, 0}.
 *
 * When it is written: cr_settings.c crf_poll, as the Options are (the change settled, nothing sounding, no loop
 * playing: a flash erase holds the audio 45 ms), and with the global save and a backup's LIST. Not in SAFE MODE.
 * Main loop only. Needs storage.c (FELUCCA_FLASH with ChoralRoot's object list); without it the marks live in RAM. */
#define CR_FAVS 1
#define CRF_VER 1u
#define CRF_T_USER 0x80u                             /* the section of the 32 user slots */
#define CRF_T_P5 16u                                 /* P5 (engines.c: indices are append-only): ids are 1..200 */
#define CRF_PACK_MAX 96u
static const struct { uint8_t tag, bytes; } CRF_SEC[] = {
    {CRF_T_USER, 4},                                 /* slots 0..31 */
    {7, 2},                                          /* WHEEL: 5 presets */
    {12, 8},                                         /* FM6: 25 */
    {13, 8},                                         /* VA: 25 */
    {14, 16},                                        /* CZ-1: 64 and INIT TONE */
    {15, 8},                                         /* FM TONE (QUAD): 16 */
    {CRF_T_P5, 26},                                  /* P5: Sequential's 1..200 (bit 0 unused) */
};
#define CRF_NSEC ((uint32_t)(sizeof CRF_SEC / sizeof CRF_SEC[0]))
#define CRF_BYTES 72u
typedef char crf_pack_ok[2u + 2u * 7u + CRF_BYTES <= CRF_PACK_MAX && sizeof CRF_SEC == 14u ? 1 : -1];
static uint8_t crf_bits[CRF_BYTES];
static uint8_t crf_dirty;                            /* a mark changed since the last write */
static uint8_t crf_saves;                            /* flash writes done (diagnostics, the emulator) */
static int8_t crf_last_rc;                           /* power-on: 1 read, 0 none, -1 an object not understood */
static uint32_t crf_change_ms;                       /* when the last mark changed (the poll's quiet time) */

static uint8_t *crf_sec(uint32_t tag, uint32_t *bytes)   /* the section's bits, 0: no such section */
{
    uint32_t k, off = 0;
    for (k = 0; k < CRF_NSEC; off += CRF_SEC[k].bytes, k++)
        if (CRF_SEC[k].tag == tag) {
            *bytes = CRF_SEC[k].bytes;
            return crf_bits + off;
        }
    return 0;
}
static int crf_has(uint32_t tag, uint32_t id)
{
    uint32_t n = 0;
    const uint8_t *b = crf_sec(tag, &n);
    return b && id < n * 8u && ((b[id / 8u] >> (id % 8u)) & 1u);
}
/* 1: the mark changed (to be written: crf_dirty); 0: it was so already, or there is no such sound to mark */
static int crf_set(uint32_t tag, uint32_t id, int on)
{
    uint32_t n = 0;
    uint8_t *b = crf_sec(tag, &n);
    if (!b || id >= n * 8u || (tag == CRF_T_P5 && (!id || id > 200u)) || crf_has(tag, id) == !!on)
        return 0;
    b[id / 8u] = (uint8_t)(on ? b[id / 8u] | (1u << (id % 8u)) : b[id / 8u] & ~(1u << (id % 8u)));
    crf_dirty = 1;
    crf_change_ms = fm1_ms;
    return 1;
}
static uint32_t crf_count(void)                      /* the marks in all */
{
    uint32_t i, n = 0;
    for (i = 0; i < CRF_BYTES * 8u; i++)
        n += (crf_bits[i / 8u] >> (i % 8u)) & 1u;
    return n;
}
static uint32_t crf_pack(uint8_t *out)               /* the flash form into out (CRF_PACK_MAX); its length */
{
    uint32_t k, off = 0, n = 2, i;
    out[0] = CRF_VER;
    out[1] = 0;
    for (k = 0; k < CRF_NSEC; off += CRF_SEC[k].bytes, k++) {
        uint32_t len = CRF_SEC[k].bytes;
        while (len && !crf_bits[off + len - 1u])
            len--;
        if (!len)
            continue;
        out[n++] = CRF_SEC[k].tag;
        out[n++] = (uint8_t)len;
        for (i = 0; i < len; i++)
            out[n++] = crf_bits[off + i];
        out[1]++;
    }
    return n;
}
/* the flash form -> the marks. 0: read; -1: not understood (another version, cut short): no marks */
static int crf_unpack(const uint8_t *in, uint32_t n)
{
    uint32_t k, p = 2, i;
    for (i = 0; i < CRF_BYTES; i++)
        crf_bits[i] = 0;
    if (n < 2u || in[0] != CRF_VER)
        return -1;
    for (k = 0; k < in[1]; k++) {
        uint32_t tag, len, bytes = 0;
        uint8_t *b;
        if (p + 2u > n || p + 2u + in[p + 1u] > n) {
            for (i = 0; i < CRF_BYTES; i++)
                crf_bits[i] = 0;
            return -1;
        }
        tag = in[p];
        len = in[p + 1u];
        b = crf_sec(tag, &bytes);
        for (i = 0; b && i < len && i < bytes; i++)
            b[i] = in[p + 2u + i];
        p += 2u + len;
    }
    {   /* P5: ids 1..200 only (a foreign object's stray bits) */
        uint32_t bytes = 0;
        uint8_t *b = crf_sec(CRF_T_P5, &bytes);
        b[0] &= 0xFEu;
        b[25] &= 0x01u;
    }
    return 0;
}
#define CRF_FLASH (FELUCCA_FLASH && FELUCCA_CZ && FELUCCA_QUAD)   /* (storage.c: OBJ_FAVS is in ChoralRoot's list) */
static void crf_boot(void)                           /* power-on, a restored object: flash -> the marks */
{
    uint8_t b[CRF_PACK_MAX];
    int n = -1;
    crf_dirty = 0;
    crf_last_rc = 0;
#if CRF_FLASH
    n = st_load(OBJ_FAVS, b, sizeof b);
#endif
    if (n <= 0) {                                    /* none (a new unit, SAFE MODE, an empty object), or one too long to be ours */
        (void)crf_unpack(b, 0);
        return;
    }
    crf_last_rc = crf_unpack(b, (uint32_t)n) == 0 ? 1 : -1;
}
/* the marks -> flash when one changed; 0: written or nothing to write, -1: a flash error (still to be written) */
static int crf_flush(void)
{
#if CRF_FLASH
    uint8_t b[CRF_PACK_MAX];
    uint32_t n;
    if (!crf_dirty)
        return 0;
    if (ST_BLOCKED()) {                              /* SAFE MODE: the flash untouched, the marks in RAM */
        crf_dirty = 0;
        return 0;
    }
    n = crf_pack(b);
    if (st_save(OBJ_FAVS, b, n) != 0)
        return -1;
    crf_saves++;
#endif
    crf_dirty = 0;
    return 0;
}
/* upreset.c up_put: slot k was erased, or a record went into an empty slot: the slot's mark is not that sound's */
static void crf_slot_new(uint32_t k) { (void)crf_set(CRF_T_USER, k, 0); }
