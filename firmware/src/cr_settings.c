/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the settings record (cr_settings.h, docs/SETTINGS.md).
 *
 * Part 1 (always): defaults, import (version / size / checksum / per-field ranges), seal, apply to the engine and
 * the routing, capture back. Portable C99, no libc: the host test (tests/cr_settings_test.c) builds it with
 * cr_engine.c alone.
 *
 * Part 2 (in the firmware unit, after cr_ui.c and storage.c: CR_VERSION is defined): the flash side on Felucca's
 * settings object (settings_persist.c's persist_t, PER5, whose last member is this record) and the glue to the
 * UI's mirror (cr_ui.c's cs):
 *   cr_settings_boot()   persist_boot: the flash object -> Felucca's fields (panel table, HOLD) + our record
 *   cr_settings_load()   the end of cr_ui_init: the record -> engine, routing, the UI mirror, sounds, palette
 *   cr_settings_poll()   every UI frame: a change (captured from the UI; and of Felucca's HOLD, which Options >
 *                        Hold Time sets outside our record: crs_hold_seen) is saved once nothing changed for
 *                        CRS_QUIET_MS and nothing has sounded for CRS_IDLE_MS (crs_sounding: no voice of the parts,
 *                        no chord held or latched, no scheduled note, the master output below -60 dBFS), never
 *                        while a loop plays (CR_SETTINGS_BUSY()), retried after a flash error. A flash erase holds
 *                        every IRQ for ~45 ms with the audio buffer zeroed (storage_hw.c st_erase): saved under a
 *                        sounding chord it was a 46 ms hole with a click at each edge (docs/INTEGRATION.md,
 *                        Performance)
 *   cr_settings_save()   save now (still deferred while a loop plays)
 * The session (0.18, cr_settings.h cr_session_t: where the unit was left; part 1 has its defaults / import / seal,
 * part 2 its glue, css_*): a flash object of its own (storage.c OBJ_SESSION), read by cr_settings_boot, applied once
 * by cr_settings_load, captured and saved from cr_settings_poll under the same rules.
 * Nothing here writes flash from the audio ISR. */
#ifndef CR_ENGINE_H
#include "cr_engine.h"
#endif
#include "cr_settings.h"

#define CRS_HDR 12u
static const int16_t CRS_PAR_DEF[CRS_NPM][CRS_NPAR] = {     /* = cr_engine.c CR_PAR_DEFAULT (the test checks) */
    {40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 0, 0},
    {40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 30, 0},
    {40, CR_DIV_1_8, 0, 1, 70, 50, 1, 1, 0, 0, 0},
    {40, CR_DIV_1_8, 0, 1, 70, 50, 1, 1, 0, 0, 0},
    {8, CR_DIV_1_8, 0, 3, 100, 50, 1, 1, 0, 0, 0}};
static const int16_t CRS_PAR_MIN[CRS_NPAR] = {1, 0, 0, 1, 1, 50, 0, 1, -12, 0, 0};
static const int16_t CRS_PAR_MAX[CRS_NPAR] = {1000, CR_DIV_COUNT - 1, CR_DIR_RANDOM, 4, 200, 90, 1, CR_NPATTERN, 12, 100, 1};

typedef char crs_size_ok[sizeof(cr_settings_t) == CRS_SIZE ? 1 : -1];
typedef char crs_npm_ok[CRS_NPM == CR_PM_COUNT && CRS_NPAR == CR_P_COUNT ? 1 : -1];

static uint32_t crs_fnv(const cr_settings_t *s, uint32_t size)
{
    const uint8_t *b = (const uint8_t *)s;
    uint32_t h = 2166136261u, i;
    for (i = CRS_HDR; i < size; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}
static void crs_copy(void *d, const void *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ((uint8_t *)d)[i] = ((const uint8_t *)s)[i];
}

void cr_settings_defaults(cr_settings_t *s)
{
    uint32_t i;
    int m, p;
    for (i = 0; i < sizeof *s; i++)
        ((uint8_t *)s)[i] = 0;
    s->magic = CRS_MAGIC;
    s->version = CRS_VERSION;
    s->size = CRS_SIZE;
    s->playstyle = CR_PS_SIMPLE;
    s->extadd = CR_EXTADD_ADD_NOTE;
    s->secret = CR_SECRET_OFF;
    s->scale = CR_SCALE_MAJOR;
    s->single = CR_SINGLE_FULL;
    s->split_pc = 5;
    s->vel = 100;
    s->bass_on = 0;                                /* (always 0 in the block: Bass on is the session's, cr_settings.h) */
    s->bass_mode = CR_BASS_CHORDS_ONLY;
    s->perform_mode = CR_PM_STRUM;
    s->bpm = 120;
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            s->par[m][p] = CRS_PAR_DEF[m][p];
    s->loop_level = 100;
    s->loop_count_in = 1;
    s->metro_vol = 70;
    s->pick_roots = 1;                             /* the engine picker: the roots choose engines */
    s->usb_in = 1;                                 /* USB Record (ChoralRoot In) on */
    s->usb_level = CRS_USB_MASTER;
    for (i = 0; i < 3u; i++) {
        s->midi_en[i] = i != CR_STREAM_RAW;        /* RAW stream off (Orchid) */
        s->midi_ch[i] = (uint8_t)i;                /* channels 1 / 2 / 3 */
    }
    s->clock_mode = CRS_CLOCK_OUT;                 /* Orchid sends clock */
    s->palette = CRS_PALETTE_MOD;
    s->fx_on = 1;
    s->chord_sound = CRS_SOUND_DEFAULT;
    s->bass_sound = CRS_SOUND_DEFAULT;
    for (i = 0; i < (uint32_t)CRS_NENG; i++) {
        crs_pool_set(s, 0, i, CRS_POOL_DEFAULT);
        crs_pool_set(s, 1, i, CRS_POOL_DEFAULT);
    }
    cr_settings_seal(s);
}

void cr_settings_factory(cr_settings_t *s)
{
    uint8_t slot = s->loop_slot;
    cr_settings_defaults(s);
    if (slot <= 9u)
        s->loop_slot = slot;
    cr_settings_seal(s);
}

void cr_settings_seal(cr_settings_t *s)
{
    s->magic = CRS_MAGIC;
    s->version = CRS_VERSION;
    s->size = CRS_SIZE;
    s->check = crs_fnv(s, CRS_SIZE);
}

int cr_settings_equal(const cr_settings_t *a, const cr_settings_t *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    uint32_t i;
    for (i = CRS_HDR; i < CRS_SIZE; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

/* a field out of range takes its default */
#define CRS_FIX(f, lo, hi) do { if ((int)s->f < (lo) || (int)s->f > (hi)) s->f = d.f; } while (0)
static void crs_sanitize(cr_settings_t *s)
{
    cr_settings_t d;
    int m, p, i;
    cr_settings_defaults(&d);
    CRS_FIX(playstyle, 0, CR_PS_FREE);
    CRS_FIX(extadd, 0, 1);
    CRS_FIX(secret, 0, CR_SECRET_ALL);
    CRS_FIX(key_on, 0, 7);                         /* (v14: bit 1 Key plays Notes, bit 2 the Full keyboard) */
    CRS_FIX(tonic, 0, 11);
    CRS_FIX(scale, 0, 1);
    CRS_FIX(transpose, -24, 24);
    CRS_FIX(single, 0, CR_SINGLE_SPLIT_BASS);      /* (v14: 2 Split Bass) */
    CRS_FIX(split_pc, 0, 11);
    CRS_FIX(vel, 1, 127);
    CRS_FIX(bass_on, 0, 1);
    CRS_FIX(bass_mode, 0, CR_BASS_SOLO);
    CRS_FIX(bass_voicing, -2, 4);
    CRS_FIX(perform_on, 0, 1);
    CRS_FIX(perform_mode, 0, CRS_NPM - 1);
    CRS_FIX(perf_sel, 0, 6);
    CRS_FIX(sticky, 0, 1);
    CRS_FIX(bpm, 20, 300);
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            if (s->par[m][p] < CRS_PAR_MIN[p] || s->par[m][p] > CRS_PAR_MAX[p])
                s->par[m][p] = d.par[m][p];
    CRS_FIX(loop_sync, 0, 5);
    CRS_FIX(loop_quant, 0, 6);
    CRS_FIX(loop_count_in, 0, 1);
    CRS_FIX(loop_level, 0, 100);
    for (i = 0; i < 3; i++) {
        if (s->midi_en[i] > 1u) s->midi_en[i] = d.midi_en[i];
        if (s->midi_ch[i] > 15u) s->midi_ch[i] = d.midi_ch[i];
    }
    CRS_FIX(clock_mode, 0, CRS_CLOCK_IN);
    CRS_FIX(raw_sound, 0, 1);
    if ((s->view & 0x38u) || CRS_VIEW(s) > 4u)    /* (v9: bit 7 the lock, v12: bit 6 Dual Core On; an unknown bit
                                                    * or view: the default) */
        s->view = d.view;
    CRS_FIX(motion, 0, 2);
    CRS_FIX(leds, 0, 1);
    CRS_FIX(fx_on, 0, 1);
    CRS_FIX(metro_on, 0, 1);
    CRS_FIX(metro_sig, 0, 2);
    CRS_FIX(metro_vol, 0, 100);
    CRS_FIX(loop_slot, 0, 9);
    CRS_FIX(pick_roots, 0, 1);
    CRS_FIX(usb_in, 0, 1);
    CRS_FIX(usb_level, 0, CRS_USB_FIXED);
    if (CRS_LOOP_MODE(s) >= CRS_NLOOPMODE)         /* (an unknown mode: the defaults; v13: bit 5 the KEY layer's keys,
                                                    * bits 6 / 7 LOCK's styles: every bit of the byte has a job) */
        s->loop_rec = d.loop_rec;
    for (i = 0; i < CRS_NENG; i++)                 /* (a pool: INIT + 64 CZ-1 presets + 32 user ones at most) */
        for (m = 0; m < 2; m++)
            if (crs_pool_get(s, (unsigned)m, (unsigned)i) > 127u)
                crs_pool_set(s, (unsigned)m, (unsigned)i, crs_pool_get(&d, (unsigned)m, (unsigned)i));
    /* palette, chord_sound, bass_sound, pool_pos: checked against the pools by the UI glue (the pools are the UI's) */
}

int cr_settings_import(cr_settings_t *s, const void *blk, uint32_t n)
{
    cr_settings_t in;
    uint32_t size;
    uint32_t hnew = 2166136261u, i;
    int rc;
    for (i = 0; i < sizeof in; i++)
        ((uint8_t *)&in)[i] = 0;
    if (blk && n >= CRS_HDR) {                     /* (blk may be s itself: read it all before the defaults) */
        crs_copy(&in, blk, n < CRS_SIZE ? n : CRS_SIZE);
        if (in.size > CRS_SIZE && in.size <= n)
            for (i = CRS_HDR; i < in.size; i++) {
                hnew ^= ((const uint8_t *)blk)[i];
                hnew *= 16777619u;
            }
    }
    cr_settings_defaults(s);
    if (!blk || n < CRS_HDR)
        return 0;
    size = in.size;
    if (in.magic != CRS_MAGIC || in.version == 0u || size < CRS_HDR || size > n)
        return 0;
    if (size > CRS_SIZE) {                         /* a newer writer with a longer record: its prefix is ours */
        if (hnew != in.check)
            return 0;
        size = CRS_SIZE;
    } else if (crs_fnv(&in, size) != in.check) {
        return 0;
    }
    rc = in.version == CRS_VERSION && in.size == CRS_SIZE ? 1 : 2;
    /* the writer's fields over the defaults; an older version's never-written reserve stays default */
    crs_copy((uint8_t *)s + CRS_HDR, (const uint8_t *)&in + CRS_HDR, size - CRS_HDR);
    if (in.version < CRS_VERSION) {
        /* version-specific migrations go here: if (in.version < 2) s->new_field = d.new_field; ... */
        if (in.version < 2u) {                     /* version 2: the metronome and the loop slot */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->metro_on = d.metro_on;
            s->metro_sig = d.metro_sig;
            s->metro_vol = d.metro_vol;
            s->loop_slot = d.loop_slot;
        }
        if (in.version < 3u) {                     /* version 3: the engine picker's roots (a 0 there: not set) */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->pick_roots = d.pick_roots;
        }
        if (in.version < 4u) {                     /* version 4: USB audio (zeros there would switch both off) */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->usb_in = d.usb_in;
            s->usb_level = d.usb_level;
        }
        if (in.version < 5u)                       /* version 5: the USB playback removed, its byte (v4's usb_out, */
            s->loop_rec = 0;                       /* 1 by default there) cleared */
        if (in.version < 6u) {                     /* version 6: presets per engine (docs/PRESETS.md): the old list
                                                    * positions mean nothing in the pools: the defaults (TINE EP, SUB
                                                    * BASS), every engine's first preset */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->chord_sound = d.chord_sound;
            s->bass_sound = d.bass_sound;
            for (i = 0; i < (uint32_t)CRS_NENG; i++) {
                crs_pool_set(s, 0, i, crs_pool_get(&d, 0, i));
                crs_pool_set(s, 1, i, crs_pool_get(&d, 1, i));
            }
        }
        if (in.version < 7u) {                     /* version 7 (0.14): ANALOG retired; its pool_pos slot 0 is FM
                                                    * TONE's. The chord part's place there: rsv_usb, where a 0.14 dev
                                                    * build kept it (0 in a 0.13 record: the first preset); the bass
                                                    * part's: bass_sound's when the bass played FM TONE. A part's sound
                                                    * on ANALOG: the default (TINE EP, DEEP SUB) */
            uint8_t q = s->loop_rec;               /* (rsv_usb then) */
            crs_pool_set(s, 0, 0, q && q <= 127u ? q : CRS_POOL_DEFAULT);
            crs_pool_set(s, 1, 0, s->bass_sound != CRS_SOUND_DEFAULT && (s->bass_sound >> 8) == CRS_ENG_QUAD ?
                                      (uint8_t)(s->bass_sound & 0xFFu) : CRS_POOL_DEFAULT);
            s->loop_rec = 0;
            if (s->chord_sound != CRS_SOUND_DEFAULT && (s->chord_sound >> 8) == CRS_ENG_ANALOG)
                s->chord_sound = CRS_SOUND_DEFAULT;
            if (s->bass_sound != CRS_SOUND_DEFAULT && (s->bass_sound >> 8) == CRS_ENG_ANALOG)
                s->bass_sound = CRS_SOUND_DEFAULT;
        }
        if (in.version < 8u) {                     /* version 8: the looper's record mode and keys (docs/LOOPER-MODES.md)
                                                    * in the byte that was rsv_usb (0 since v7): the defaults */
            cr_settings_t d;
            cr_settings_defaults(&d);
            s->loop_rec = d.loop_rec;
        }
        if (in.version < 10u) {                    /* version 10: the arp's Hold defaults off (it latched with no
                                                    * control in the ARP layer); LOCK is the latch */
            int m;
            for (m = 0; m < CRS_NPM; m++) s->par[m][CR_P_HOLD] = 0;
        }
        if (in.version < 12u)                      /* version 12 (0.16): Dual Core is Off by default and view bit 6
                                                    * means On (v11: set = Off, On the default, so its bit cannot
                                                    * tell a chosen On from that default): every older record Off */
            s->view &= (uint8_t)~CRS_DC_ON;
        if (in.version < 13u)                      /* version 13: the KEY layer's keys in bit 5 of loop_rec (no older
                                                    * writer set it: an unknown bit there): pick, the default */
            s->loop_rec &= (uint8_t)~(CRS_KEY_PLAY | CRS_LOCK_KEEP | CRS_LOCK_PUSH);   /* (and LOCK's two styles) */
        if (in.version < 14u)                      /* version 14: Key plays and Keyboard in bits 1 / 2 of key_on (no
                                                    * older writer set them; one out of range there read as off): Chords,
                                                    * Chords; Single's 2 (Split Bass) did not exist: Full Octave */
        {
            s->key_on = (uint8_t)(s->key_on == 1u ? 1u : 0u);
            if (s->single > CR_SINGLE_SPLIT) s->single = CR_SINGLE_FULL;
        }
        /* version 15 (0.18): no field of the block changed (the session record beside it has what is new); bass_on
         * was 0 in every older record and stays 0 */
    }
    crs_sanitize(s);
    cr_settings_seal(s);
    return rc;
}

/* ------------------------------------------------------ the session record --- */
typedef char css_size_ok[sizeof(cr_session_t) == CSS_SIZE && sizeof(css_fx_t) == 108 && sizeof(css_part_t) == 384 ? 1 : -1];
static uint32_t css_fnv(const cr_session_t *x, uint32_t from, uint32_t size)
{
    const uint8_t *b = (const uint8_t *)x;
    uint32_t h = 2166136261u, i;
    for (i = from; i < size; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}
static void css_fx_defaults(css_fx_t *f)
{
    uint32_t i;
    for (i = 0; i < sizeof *f; i++)
        ((uint8_t *)f)[i] = 0;
    f->version = CSS_FX_VERSION;
    f->sel = CSS_FXE_REVERB;
    for (i = 0; i < (uint32_t)CSS_NFXE; i++)
        f->amt[i] = CSS_FX_NONE;
}
void cr_session_defaults(cr_session_t *x)
{
    uint32_t i;
    for (i = 0; i < sizeof *x; i++)
        ((uint8_t *)x)[i] = 0;
    css_fx_defaults(&x->fx);
    cr_session_seal(x);
}
void cr_session_seal(cr_session_t *x)
{
    x->magic = CSS_MAGIC;
    x->version = CSS_VERSION;
    x->size = CSS_SIZE;
    x->check = css_fnv(x, CSS_HDR, CSS_SIZE);
}
uint32_t cr_session_hash(const cr_session_t *x) { return css_fnv(x, CSS_HDR + 4u, CSS_SIZE); }
int cr_session_fx_get(const cr_session_t *x, unsigned tag, int *v)
{
    uint32_t i;
    for (i = 0; i < x->fx.n && i < (uint32_t)CSS_NFXP; i++)
        if (x->fx.par[i].tag == tag) {
            *v = x->fx.par[i].v;
            return 1;
        }
    return 0;
}
int cr_session_fx_set(cr_session_t *x, unsigned tag, int v)
{
    uint32_t i;
    for (i = 0; i < x->fx.n && i < (uint32_t)CSS_NFXP; i++)
        if (x->fx.par[i].tag == tag)
            break;
    if (i >= (uint32_t)CSS_NFXP || !tag || tag > 255u)
        return 0;
    x->fx.par[i].tag = (uint8_t)tag;
    x->fx.par[i].rsv = 0;
    x->fx.par[i].v = (int16_t)v;
    if (i == x->fx.n)
        x->fx.n++;
    return 1;
}
static void css_sanitize(cr_session_t *x)
{
    uint32_t k, i;
    if (x->voicing < -12 || x->voicing > 12) x->voicing = 0;   /* (cr_engine.c CR_VOICE_MIN / MAX: cr_voicing_step clamps) */
    if (x->octave < -2 || x->octave > 2) x->octave = 0;
    if (x->bass_on > 1u) x->bass_on = 0;
    if (x->lock > 1u) x->lock = 0;
    if (!x->lock) x->latched = 0;                  /* (the latch belongs to LOCK) */
    if (x->fx.version != CSS_FX_VERSION || x->fx.n > (uint32_t)CSS_NFXP)
        css_fx_defaults(&x->fx);                   /* another layout of the FX block: the sounds' own sends, the
                                                    * effects' defaults */
    for (i = 0; i < (uint32_t)CSS_NFXE; i++)
        if (x->fx.amt[i] > 127u) x->fx.amt[i] = CSS_FX_NONE;
    for (k = 0; k < 2u; k++) {
        css_part_t *r = &x->part[k];
        if (r->valid != 1u || r->np > (uint32_t)CSS_NP || r->nblob > (uint32_t)CSS_BLOB) {
            for (i = 0; i < sizeof *r; i++)
                ((uint8_t *)r)[i] = 0;             /* no sound stored: the pool's (the block's chord_sound / bass_sound) */
            continue;
        }
        r->edited = r->edited != 0u;
        r->name[CSS_NAME - 1] = 0;
    }
}
int cr_session_import(cr_session_t *x, const void *blk, uint32_t n)
{
    struct { uint32_t magic; uint16_t version, size; uint32_t check; } h;
    const uint8_t *src = (const uint8_t *)blk;
    uint32_t size, use, hc = 2166136261u, i;
    if (!blk || n < CSS_HDR + 4u) {
        cr_session_defaults(x);
        return 0;
    }
    crs_copy(&h, blk, CSS_HDR);
    size = h.size;
    if (h.magic == CSS_MAGIC && h.version != 0u && size >= CSS_HDR + 4u && size <= n)
        for (i = CSS_HDR; i < size; i++) {
            hc ^= src[i];
            hc *= 16777619u;
        }
    else
        hc = ~h.check;
    if (hc != h.check) {                           /* a bad magic / version / size / checksum: nothing stored */
        cr_session_defaults(x);
        return 0;
    }
    use = size > CSS_SIZE ? CSS_SIZE : size;       /* a newer writer with a longer record: its prefix is ours */
    if ((const void *)x != blk)                    /* (blk may be x itself, not overlap it otherwise) */
        crs_copy(x, blk, use);
    for (i = use; i < CSS_SIZE; i++)               /* an older, shorter record: what it lacks is 0, the default of every
                                                    * field added after it (another default: a migration line below) */
        ((uint8_t *)x)[i] = 0;
    if (use < (uint32_t)((const uint8_t *)&x->fx - (const uint8_t *)x) + (uint32_t)sizeof x->fx)
        css_fx_defaults(&x->fx);
    /* version-specific migrations go here, as cr_settings_import's: if (h.version < 2u) x->new_field = ...; */
    css_sanitize(x);
    cr_session_seal(x);
    return h.version == CSS_VERSION && h.size == CSS_SIZE ? 1 : 2;
}

/* ------------------------------------------------------ engine <-> record --- */
void cr_settings_apply(const cr_settings_t *s, cr_t *c, cr_settings_out_t *o)
{
    int m, i;
    unsigned p;                                    /* (a cr_param_t: that name is the screen's in the unit) */
    cr_set_playstyle(c, (cr_playstyle_t)s->playstyle);
    cr_set_ext_addition(c, (cr_extadd_t)s->extadd);
    cr_set_secret(c, (cr_secret_t)s->secret);
    cr_set_key(c, (int)CRS_KEY_ON(s), s->tonic, (cr_scale_t)s->scale);
    cr_set_key_plays(c, (int)CRS_KEY_PLAYS(s));
    cr_set_keyboard(c, (int)CRS_KEYBOARD(s), c->split_note);   /* (the split key: the UI's, with its octave) */
    cr_set_transpose(c, s->transpose);
    cr_set_single_notes(c, (cr_single_t)s->single, s->split_pc);
    cr_set_bass_mode(c, (cr_bassmode_t)s->bass_mode);
    cr_bass_voicing_step(c, s->bass_voicing - c->bass_voicing);
    cr_set_bass(c, s->bass_on);
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            if (cr_get_param(c, (cr_pmode_t)m, p) != s->par[m][p])
                cr_set_param(c, (cr_pmode_t)m, p, s->par[m][p]);
    cr_set_perform_mode(c, (cr_pmode_t)s->perform_mode);
    cr_set_perform(c, s->perform_on);
    cr_set_sticky(c, s->sticky);
    cr_set_tempo(c, s->bpm);
    cr_set_stream(c, CR_STREAM_RAW, s->midi_en[CR_STREAM_RAW] || s->raw_sound);
    if (o) {
        for (i = 0; i < 3; i++) {
            o->midi_en[i] = s->midi_en[i];
            o->ch[i] = s->midi_ch[i];
        }
        o->part[CR_STREAM_MAIN] = 0;
        o->part[CR_STREAM_BASS] = 1;
        o->part[CR_STREAM_RAW] = s->raw_sound ? 0 : CRS_NONE;
        o->clock_out = s->clock_mode == CRS_CLOCK_OUT;
        o->clock_in = s->clock_mode == CRS_CLOCK_IN;
    }
}

void cr_settings_capture(cr_settings_t *s, const cr_t *c, const cr_settings_out_t *o)
{
    int m, i;
    unsigned p;
    s->playstyle = c->playstyle;
    s->extadd = c->extadd;
    s->secret = c->secret;
    s->key_on = CRS_KEY_BYTE(c->key_on, c->key_notes, c->full);
    s->tonic = c->tonic;
    s->scale = c->scale;
    s->transpose = c->transpose;
    s->single = c->single;
    s->split_pc = c->split_pc;
    s->bass_on = c->bass_on;
    s->bass_mode = c->bass_mode;
    s->bass_voicing = c->bass_voicing;
    s->perform_on = c->perform_on;
    s->perform_mode = c->perform_mode;
    s->sticky = c->sticky;
    s->bpm = c->bpm;
    for (m = 0; m < CRS_NPM; m++)
        for (p = 0; p < CRS_NPAR; p++)
            s->par[m][p] = (int16_t)cr_get_param(c, (cr_pmode_t)m, p);
    if (o) {
        for (i = 0; i < 3; i++) {
            s->midi_en[i] = o->midi_en[i] ? 1u : 0u;
            s->midi_ch[i] = (uint8_t)(o->ch[i] & 15u);
        }
        s->raw_sound = o->part[CR_STREAM_RAW] != CRS_NONE;
        s->clock_mode = o->clock_in ? CRS_CLOCK_IN : o->clock_out ? CRS_CLOCK_OUT : CRS_CLOCK_OFF;
    }
    cr_settings_seal(s);
}

/* =========================================== the firmware unit: flash + UI glue --- */
#ifdef CR_VERSION
#ifndef CR_SETTINGS_BUSY
#define CR_SETTINGS_BUSY() 0                       /* (cr_ui.c defines it: a loop is playing, no flash erase) */
#endif
#define CRS_QUIET_MS 1500u                         /* a change is saved once nothing changed for this long */
#define CRS_IDLE_MS 1000u                          /* .. and nothing sounded for this long (crs_sounding) */
#define CRS_QUIET_PEAK 32                          /* the master output (audio.c scope_buf, Q15): -60 dBFS */

#include "settings_persist.c"                      /* Felucca's record, PER5: its last member is cr_settings_t */

#define CSS_SCAN_MS 200u                           /* the session is looked at this often (css_poll) */
#define CSS_GAP_MS 250u                            /* .. and two flash writes are at least this far apart */

static persist_t crs_rec;                          /* the last record read or written (Felucca's fields + ours) */
static cr_settings_t crs_seen;                     /* the last captured state (change detection) */
static uint8_t crs_hold_seen;                      /* .. and Felucca's HOLD then (panel.c settings_hold, the record's
                                                    * `bold`): Options > Hold Time sets it, outside our block. The one
                                                    * field of Felucca's the UI changes with no save call: the panel
                                                    * table is saved by the calibration (cr_settings_save); the
                                                    * palette, LOWCUT and Felucca's LEDS have no control here (the
                                                    * block has ChoralRoot's palette and LEDs) */
static uint8_t crs_booted, crs_loaded, crs_pending;
static uint32_t crs_change_ms, crs_retry_ms;
static uint32_t crs_loud_ms;                       /* the last time something sounded (crs_sounding, the poll) */
static int crs_last_rc;                            /* the last import: 1 current, 2 migrated, 0 defaults */
static uint32_t crs_saves;                         /* flash writes done (diagnostics, the emulator) */
/* the session (cr_settings.h cr_session_t; docs/SETTINGS.md "The session"): ONE copy in RAM, css_buf, in the pool: what
 * flash held at power-on until cr_settings_load applied it, then the scratch the state is captured into; the state
 * last seen and the one flash holds are kept as hashes (cr_session_hash), so that change detection costs no second
 * and third kilobyte */
typedef char css_fit_ok[P_COUNT <= CSS_NP && ENG_BLOB_MAX <= CSS_BLOB && sizeof psnd[0].name == CSS_NAME &&
                        CR_NMOD <= 8 ? 1 : -1];
static cr_session_t css_buf __attribute__((section(".pool")));
static uint32_t css_seen, css_flash;               /* cr_session_hash of the state last captured / in flash (0: none) */
static uint32_t crs_obj_crc;                       /* the settings object's payload CRC in flash (0: none): the session's
                                                    * `bind` */
static uint8_t css_have;                           /* power-on: css_buf holds a session to apply (cr_settings_load) */
static uint8_t css_pending;
static int8_t css_last_rc;                         /* the session at power-on: 1 current, 2 migrated, 0 none, -1 one
                                                    * that does not belong to these settings (not applied) */
static uint32_t css_change_ms, css_scan_ms, css_retry_ms, crs_wr_ms;
static uint32_t css_saves;                         /* flash writes done (diagnostics, the emulator) */

/* the USB settings to usb.c (which devices the host is given; ua_off at boot, usb_start builds the configuration)
 * and fx.c (USB Level) */
static void crs_usb_apply(const cr_settings_t *s)
{
    fx_usb_fixed = s->usb_level == CRS_USB_FIXED;
#if FELUCCA_UAC
    ua_off = ua_off_want = s->usb_in ? 0u : UA_OFF_IN;
#endif
}

static void cr_settings_boot(void)                 /* persist_boot (flash_ok known), before settings_init / panel_init */
{
    int n = -1;
    crs_booted = 1;
    memset(&crs_rec, 0, sizeof crs_rec);
#if FELUCCA_FLASH
    if (flash_ok)
        n = st_load(OBJ_SETTINGS, &crs_rec, sizeof crs_rec);
    crs_obj_crc = n > 0 ? st_crc32(&crs_rec, (uint32_t)n) : 0u;   /* (as stored: before the import normalises it) */
#endif
    if (n <= 0 || !settings_import(&crs_rec, n)) {  /* absent / unknown: defaults, Felucca's fields as they are */
        memset(&crs_rec, 0, sizeof crs_rec);
        settings_export(&crs_rec);
    }
    crs_last_rc = cr_settings_import(&crs_rec.cr, &crs_rec.cr, sizeof crs_rec.cr);
    crs_usb_apply(&crs_rec.cr);                    /* (before usb_start: the configuration the host first reads) */
    cr_c1_off = (uint8_t)!CRS_DUALCORE(&crs_rec.cr);   /* Options > Dual Core Off (the default): core 1 not started */
    css_have = 0;                                  /* the session: read here, applied by cr_settings_load */
    css_flash = 0;
    css_last_rc = 0;
#if FELUCCA_FLASH
    if (flash_ok && (n = st_load(OBJ_SESSION, &css_buf, sizeof css_buf)) > 0 &&
        (n = cr_session_import(&css_buf, &css_buf, (uint32_t)n)) != 0) {
        if (crs_last_rc == 1 || css_buf.bind == crs_obj_crc) {   /* it belongs to these settings (cr_settings.h) */
            css_have = 1;
            css_last_rc = (int8_t)n;
            css_flash = cr_session_hash(&css_buf);
        } else {
            css_last_rc = -1;
        }
    }
#endif
}

/* the record's pool_pos slot of the engine of ENGINE_ORDER rank r (cr_settings.h): CRS_NENG = 11 slots, fixed per engine
 * in 0.13's display order, so a 0.13 record means what it meant whatever the order shown today; ANALOG's slot 0 is FM
 * TONE's since version 7 (ANALOG retired, FELUCCA_ANALOG 0). -1: an engine with no slot (none on ChoralRoot) */
#if FELUCCA_ANALOG || !FELUCCA_VA || !FELUCCA_CZ || !FELUCCA_QUAD
#error "cr_settings.c: the record's engine slots are ChoralRoot's (FELUCCA_ANALOG 0, FELUCCA_VA / CZ / QUAD 1)"
#endif
typedef char crs_neng_ok[CRS_NENG == NENG_SHOWN && CRS_ENG_QUAD == ENGI_QUAD ? 1 : -1];   /* (one per engine shown) */
static const uint8_t CRS_SLOT_ENG[CRS_NENG] = {ENGI_QUAD, ENGI_FM6, ENGI_VA, 2, ENGI_CZ, 3, 5, 6, 7, ENGI_PHYS, 11};
static int32_t crs_slot(uint32_t r)
{
    uint32_t e = eng_vis(r), k;
    for (k = 0; k < (uint32_t)CRS_NENG; k++)
        if (CRS_SLOT_ENG[k] == e)
            return (int32_t)k;
    return -1;
}

/* a stored sound (engine << 8 | pool position) into the part, if it is one of a pool today; 1 loaded */
static int crs_sound(uint32_t part, uint16_t v)
{
    uint32_t e = v >> 8, pos = v & 0xFFu;
    if (v == CRS_SOUND_DEFAULT || !cu_engine_melodic(e) || pos >= pool_count(e))
        return 0;
    if (e != trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req || pos != cu_part_pos(part))
        cu_pool_load(part, e, pos);
    return 1;
}

/* the UI's mirror -> the record (with the engine's fields read under the IRQ lock) */
static void crs_capture(cr_settings_t *s)
{
    cr_settings_out_t o;
    uint32_t i;
    *s = crs_rec.cr;
    for (i = 0; i < 3u; i++) {
        o.midi_en[i] = cr_route.midi_en[i];
        o.ch[i] = cr_route.ch[i];
        o.part[i] = cr_route.part[i] == CR_NOPART ? CRS_NONE : cr_route.part[i];
    }
    o.clock_out = cr_route.clock_out;
    o.clock_in = cr_route.clock_in;
    fm1_irq_off();
    cr_settings_capture(s, &cr, &o);
    fm1_irq_on();
    /* the UI's own (and what the UI holds ahead of the ISR: the queue may not be drained yet) */
    s->playstyle = cs.playstyle;
    s->extadd = cs.extadd;
    s->secret = cs.secret;
    s->key_on = CRS_KEY_BYTE(cs.key_on, cs.key_notes, cs.kbd_full);
    s->tonic = cs.tonic;
    s->scale = cs.scale;
    s->transpose = cs.transpose;
    s->single = cs.single;
    s->vel = cs.vel;
    s->bass_mode = cs.bass_mode;
    s->bass_on = 0;                                /* (always 0 here: Bass on is the session's, css_capture; an older
                                                    * firmware would switch the engine's bass on from this byte) */
    s->perform_on = cs.perform_on;
    s->perf_sel = cs.perf_sel;
    s->sticky = cs.sticky;
    s->bpm = cs.bpm;
    for (i = 0; i < (uint32_t)CRS_NPM * CRS_NPAR; i++)
        s->par[i / CRS_NPAR][i % CRS_NPAR] = cs.par[i / CRS_NPAR][i % CRS_NPAR];
    for (i = 0; i < 3u; i++) {
        s->midi_en[i] = cs.ch[i] != 0u;
        if (cs.ch[i])
            s->midi_ch[i] = (uint8_t)(cs.ch[i] - 1u);
    }
    s->raw_sound = cs.raw_sound;
    s->loop_sync = cs.loop_len;
    s->loop_quant = cs.loop_quant;
    s->loop_count_in = cs.loop_count_in;
    s->loop_level = cs.loop_level;
    s->metro_on = cs.metro;
    s->metro_sig = cs.metro_sig;
    s->metro_vol = cs.metro_vol;
    s->loop_slot = cs.loop_slot;
    s->loop_rec = (uint8_t)(CRS_LOOP_REC(cs.loop_mode, cs.loop_keys) | (cs.key_play ? CRS_KEY_PLAY : 0u) |
                            (cs.lock_keep ? CRS_LOCK_KEEP : 0u) | (cs.lock_push ? CRS_LOCK_PUSH : 0u));
    s->pick_roots = cs.pick_roots;
    s->usb_in = cs.usb_in;
    s->usb_level = cs.usb_fixed;
    s->split_pc = cs.split;
    s->view = (uint8_t)(CRS_VIEW_BYTE(cs.view, cs.view_lock) | (cs.dualcore ? CRS_DC_ON : 0u));
    s->motion = cr_motion;
    s->leds = cs.leds;
    s->fx_on = cs.fx_on;
    s->chord_sound = (uint16_t)((trk[CR_PART_CHORD].eng_req % NENGINES) << 8 | (cs.sound & 0xFFu));
    s->bass_sound = (uint16_t)((trk[CR_PART_BASS].eng_req % NENGINES) << 8 |
                               cs.pool_pos[1][eng_rank(trk[CR_PART_BASS].eng_req % NENGINES)]);
    for (i = 0; i < (uint32_t)NENG_SHOWN; i++) {
        int32_t k = crs_slot(i);
        if (k < 0)
            continue;
        crs_pool_set(s, 0, (unsigned)k, cs.pool_pos[0][i]);
        crs_pool_set(s, 1, (unsigned)k, cs.pool_pos[1][i]);
    }
    cr_settings_seal(s);
}

/* ------------------------------------------------------------ the session's glue --- */
/* the FX layer by tag (cr_settings.h css_fx_t): cr_ui.c CU_FX's effects in its order, and the bus parameters KNOB
 * 1..3 turn. A reworked effect: its new parameters get new CSS_FXP_* tags and a line here; a parameter gone loses its
 * line (its tag stays retired) */
static const uint8_t CSS_FXE[CU_NFX] = {CSS_FXE_REVERB, CSS_FXE_CHORUS, CSS_FXE_DELAY, CSS_FXE_DRIVE};
static const struct { uint8_t tag, g; } CSS_FXG[] = {
    {CSS_FXP_RSIZE, G_RSIZE}, {CSS_FXP_RDAMP, G_RDAMP}, {CSS_FXP_RTYPE, G_RTYPE}, {CSS_FXP_CRATE, G_CRATE},
    {CSS_FXP_CDEPTH, G_CDEPTH}, {CSS_FXP_DTIME, G_DTIME}, {CSS_FXP_DFDBK, G_DFDBK}, {CSS_FXP_DCOLOR, G_DCOLOR}};

/* a part's sound as it is -> its record (as the engine picker's snapshot, cr_ui.c cu_pick_begin) */
static void css_part_get(uint32_t part, css_part_t *r)
{
    const track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    const eng_deep_t *d = cp_deep(t);
    uint32_t i;
    r->valid = 1;
    r->eng = t->eng_req;
    r->preset = t->preset;
    r->user = t->user;
    r->trim = t->trim;
    r->edited = psnd[part].edited != 0u;
    r->kind = psnd[part].kind;
    r->fk = psnd[part].fk;
    r->slot = psnd[part].slot;
    r->np = P_COUNT;
    for (i = 0; i < CSS_NAME - 1u && psnd[part].name[i]; i++)
        r->name[i] = psnd[part].name[i];
    for (i = 0; i < P_COUNT; i++)
        r->p[i] = t->p[i];
    if (d && d->blob_get && d->blob_set && d->blob_size && d->blob_size <= CSS_BLOB) {
        d->blob_get(t, r->blob);
        r->nblob = (uint8_t)d->blob_size;
    }
}
/* .. and back (cu_pick_end's restore); 0: not this firmware's (another parameter count, an engine gone or without
 * this blob, a pool entry that cannot be): the part keeps the sound the block's chord_sound / bass_sound loaded */
static int css_part_put(uint32_t part, const css_part_t *r)
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    const eng_deep_t *d;
    uint32_t i, e = r->eng;
    if (!r->valid || r->np != P_COUNT || e >= NENGINES || !cu_engine_melodic(e))
        return 0;
    d = ENGINES[e]->deep;
    if (r->nblob != (d && d->pages && d->npages && d->get && d->set && d->blob_get && d->blob_set &&
                     d->blob_size <= CSS_BLOB ? d->blob_size : 0u))
        return 0;
    if ((r->kind == PK_USER && r->slot >= UP_SLOTS) || r->user > UP_SLOTS ||
        (r->kind == PK_FACTORY && r->fk >= ENGINES[e]->npresets))
        return 0;
    fm1_irq_off();
    t->eng_req = (uint8_t)e;
    t->preset = r->preset;
    t->user = r->user;
    t->trim = r->trim;
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = r->p[i];
    panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
    fm1_irq_on();
    fm6_track_loaded(t);                           /* (the engine's own state for the sound) */
    if (r->nblob)
        d->blob_set(t, r->blob);                   /* the deep patch as it was left */
    str_cpy(psnd[part].name, r->name, sizeof psnd[0].name);
    psnd[part].edited = r->edited;
    psnd[part].kind = r->kind;
    psnd[part].fk = r->fk;
    psnd[part].slot = r->slot;
    t->engine = t->eng_req;                        /* (power-on: no fade) */
    return 1;
}

/* the unit's state now -> *x (sealed; `bind` is the writer's: css_write) */
static void css_capture(cr_session_t *x)
{
    uint32_t i;
    cr_session_defaults(x);
    fm1_irq_off();
    x->voicing = cr.voicing;
    fm1_irq_on();
    x->octave = cu.octave;
    x->bass_on = cs.bass_on != 0u;
    x->lock = cu.clock != 0u;
    x->latched = x->lock ? cu.mlatch : 0u;
    x->fx.sel = CSS_FXE[cs.fx_sel % CU_NFX];
    for (i = 0; i < CU_NFX; i++)
        x->fx.amt[CSS_FXE[i]] = (uint8_t)(cs.fx_amt[i] & 127u);
    for (i = 0; i < NELEM(CSS_FXG); i++)
        cr_session_fx_set(x, CSS_FXG[i].tag, song.g[CSS_FXG[i].g]);
    css_part_get(0, &x->part[0]);
    css_part_get(1, &x->part[1]);
    cr_session_seal(x);
}

/* power-on (cr_settings_load, once): the session over what the block gave. Nothing sounds yet: the engine's fields
 * directly (IRQ off), LOCK's latch through the queue as the keys post it */
static void css_apply(const cr_session_t *x)
{
    uint32_t i, m, got[2];
    int v;
    got[0] = (uint32_t)css_part_put(0, &x->part[0]);
    got[1] = (uint32_t)css_part_put(1, &x->part[1]);
    for (i = 0; i < NELEM(CSS_FXG); i++)
        if (cr_session_fx_get(x, CSS_FXG[i].tag, &v) && v >= GP[CSS_FXG[i].g].min && v <= GP[CSS_FXG[i].g].max)
            song.g[CSS_FXG[i].g] = (int16_t)v;
    for (i = 0; i < CU_NFX; i++)
        if (CSS_FXE[i] == x->fx.sel)
            cs.fx_sel = (uint8_t)i;
    if (got[0] && cs.fx_on)
        cu_sends_to_fx();                          /* (the restored sound's own sends; FX off: they are 0 there) */
    for (i = 0; i < CU_NFX; i++)
        if (x->fx.amt[CSS_FXE[i]] != CSS_FX_NONE)
            cs.fx_amt[i] = x->fx.amt[CSS_FXE[i]];
    cu_fx_apply();
    fm1_irq_off();
    cr_voicing_step(&cr, x->voicing - cr.voicing);
    if (x->bass_on)
        cr_set_bass(&cr, 1);
    fm1_irq_on();
    cu.octave = x->octave;                         /* (cu_post_kbd follows: the split key's note moves with it) */
    if (x->bass_on) {                              /* v15: the bass comes back on, on its sound (ALGORITHM's place:
                                                    * cu_pos_sync) */
        cs.bass_on = 1;
        cs.bass_sound = 1;
    }
    if (x->lock) {                                 /* LOCK and the chord keys it held down (cu_lock_tap, cu_mod_release) */
        cu.clock = 1;
        cu.mlatch = x->latched;
        cr_post(CRE_MODLOCK, 0, 0, 1);
        for (m = 0; m < CR_NMOD; m++)
            if ((x->latched >> m) & 1u)
                cr_post(CRE_MOD, m, 0, 1);
    }
    cu_trace("session: applied (parts %u %u, voicing %d, octave %d, bass %u, lock %u latched %02X, fx %u)\n",
             (unsigned)got[0], (unsigned)got[1], (int)x->voicing, (int)x->octave, (unsigned)x->bass_on,
             (unsigned)x->lock, (unsigned)x->latched, (unsigned)cs.fx_sel);
}

static int css_write(void)                         /* the state now -> flash; 0: written, or nothing to write */
{
#if FELUCCA_FLASH
    uint32_t h;
    if (!flash_ok)
        return 0;
#ifdef ST_BLOCKED
    if (ST_BLOCKED())
        return 0;                                  /* SAFE MODE: the flash untouched */
#endif
    css_capture(&css_buf);
    css_seen = h = cr_session_hash(&css_buf);
    if (h == css_flash)
        return 0;                                  /* unchanged: no erase cycle */
    css_buf.bind = crs_obj_crc;
    cr_session_seal(&css_buf);
    if (st_save(OBJ_SESSION, &css_buf, CSS_SIZE) != 0)
        return -1;
    css_flash = h;
    css_saves++;
    crs_wr_ms = fm1_ms;
#endif
    return 0;
}

/* every UI frame, from cr_settings_poll: the state is captured every CSS_SCAN_MS (not while the engine picker
 * previews a sound: cpk) and a change written under the block's rules: nothing changed for CRS_QUIET_MS, nothing
 * sounded for CRS_IDLE_MS, no loop playing, and CSS_GAP_MS after the other object's write (one erase at a time) */
static void css_poll(void)
{
    if ((uint32_t)(fm1_ms - css_scan_ms) >= CSS_SCAN_MS && !cpk.on) {
        uint32_t h;
        css_scan_ms = fm1_ms;
        css_capture(&css_buf);
        h = cr_session_hash(&css_buf);
        if (h != css_seen) {
            css_seen = h;
            css_change_ms = fm1_ms;
            css_pending = 1;
        }
    }
    if (!css_pending || CR_SETTINGS_BUSY() || (uint32_t)(fm1_ms - css_change_ms) < CRS_QUIET_MS ||
        (uint32_t)(fm1_ms - crs_loud_ms) < CRS_IDLE_MS || (uint32_t)(fm1_ms - crs_wr_ms) < CSS_GAP_MS)
        return;
    if (css_pending == 2u && (uint32_t)(fm1_ms - css_retry_ms) < 1000u)
        return;
    if (css_write() == 0) {
        css_pending = 0;
    } else {
        css_pending = 2;
        css_retry_ms = fm1_ms;
    }
}

/* the end of cr_ui_init: the record -> the engine (directly, IRQ off: nothing sounds yet), the routing, the mirror */
static void cr_settings_load(void)
{
    cr_settings_t *s = &crs_rec.cr;
    cr_settings_out_t o;
    uint32_t i;
    if (!crs_booted)
        cr_settings_boot();
    panel_init();                                  /* (a calibration table from flash: validated) */
    fm1_irq_off();
    cr_settings_apply(s, &cr, &o);
    fm1_irq_on();
    cs.playstyle = s->playstyle;
    cs.extadd = s->extadd;
    cs.secret = s->secret;
    cs.key_on = (uint8_t)CRS_KEY_ON(s);
    cs.key_notes = (uint8_t)CRS_KEY_PLAYS(s);
    cs.kbd_full = (uint8_t)CRS_KEYBOARD(s);
    cs.tonic = s->tonic;
    cs.scale = s->scale;
    cs.transpose = s->transpose;
    cs.single = s->single;
    cs.vel = s->vel;
    cs.bass_mode = s->bass_mode;
    cs.perform_on = s->perform_on;
    cs.perf_sel = s->perf_sel;
    cs.sticky = s->sticky;
    for (i = 0; i < (uint32_t)CRS_NPM * CRS_NPAR; i++)
        cs.par[i / CRS_NPAR][i % CRS_NPAR] = s->par[i / CRS_NPAR][i % CRS_NPAR];
    for (i = 0; i < 3u; i++)
        cs.ch[i] = (uint8_t)(s->midi_en[i] ? s->midi_ch[i] + 1u : 0u);
    cs.raw_sound = s->raw_sound;
    cr_route.clock_out = o.clock_out;
    cr_route.clock_in = o.clock_in;
    cs.loop_len = s->loop_sync;
    cs.loop_quant = s->loop_quant;
    cs.loop_count_in = s->loop_count_in;
    cs.loop_level = s->loop_level;
    cs.metro = s->metro_on;
    cs.metro_sig = s->metro_sig;
    cs.metro_vol = s->metro_vol;
    cs.loop_slot = s->loop_slot;
    cs.loop_mode = (uint8_t)CRS_LOOP_MODE(s);
    cs.loop_keys = (uint8_t)CRS_LOOP_KEYS(s);
    cs.key_play = (uint8_t)CRS_KEY_KEYS(s);
    cs.lock_keep = (uint8_t)((s->loop_rec & CRS_LOCK_KEEP) != 0);
    cs.lock_push = (uint8_t)((s->loop_rec & CRS_LOCK_PUSH) != 0);
    cs.pick_roots = s->pick_roots;
    cs.usb_in = s->usb_in;
    cs.usb_fixed = s->usb_level;
    fx_usb_fixed = cs.usb_fixed;                   /* (USB Record: crs_usb_apply at boot, before usb_start) */
    cs.split = s->split_pc;
    cs.view = (uint8_t)CRS_VIEW(s);
    cs.view_lock = (uint8_t)CRS_VIEW_LOCK(s);
    cs.dualcore = (uint8_t)CRS_DUALCORE(s);        /* (what this power-on did: cr_c1_off, cr_settings_boot) */
    cr_motion = s->motion;
    cs.leds = s->leds;
    palette_set(s->palette < NPALETTES ? s->palette : NPALETTES - 1u);
    for (i = 0; i < (uint32_t)NENG_SHOWN; i++) {    /* each part's place per engine (the pools: checked on use) */
        int32_t k = crs_slot(i);
        if (k < 0) {                                /* (an engine with no slot: its first preset) */
            cs.pool_pos[0][i] = cs.pool_pos[1][i] = CRS_POOL_DEFAULT;
            continue;
        }
        cs.pool_pos[0][i] = crs_pool_get(s, 0, (unsigned)k);
        cs.pool_pos[1][i] = crs_pool_get(s, 1, (unsigned)k);
    }
    if (crs_sound(0, s->chord_sound))              /* the chord part's sound (its own sends become the FX amounts) */
        cu_sends_to_fx();
    else
        s->chord_sound = CRS_SOUND_DEFAULT;
    cs.fx_on = s->fx_on;
    cu_fx_apply();
    if (crs_sound(1, s->bass_sound))               /* what BASS tap brings (Bass on: the session's, css_apply) */
        trk[CR_PART_BASS].engine = trk[CR_PART_BASS].eng_req;
    else
        s->bass_sound = CRS_SOUND_DEFAULT;
    if (css_have) {                                /* power-on: where the unit was left (the session) */
        css_have = 0;
        css_apply(&css_buf);
    }
    cu_pos_sync();
    cu_route();
    cu_post_kbd();                                 /* (the split key's note: the UI's, with its octave) */
    cu_set_tempo(s->bpm);
    crs_loaded = 1;
    crs_capture(&crs_seen);                        /* the state now: nothing to save until it changes */
    css_capture(&css_buf);
    css_seen = cr_session_hash(&css_buf);
    css_scan_ms = css_change_ms = fm1_ms;
    css_pending = 0;
    crs_hold_seen = settings_hold;
    crs_change_ms = fm1_ms;
    crs_pending = 0;
}

static int crs_write(void)                         /* the captured state -> flash; 0: written (or no flash) */
{
#if FELUCCA_FLASH
    persist_t p = crs_rec;
    if (!flash_ok)
        return 0;
#ifdef ST_BLOCKED
    if (ST_BLOCKED())
        return 0;                                  /* SAFE MODE (core.h): kept in RAM only, the flash untouched */
#endif
    settings_export(&p);                           /* Felucca's fields (panel table, HOLD, LEDs, palette) */
    p.cr = crs_seen;
    cr_settings_seal(&p.cr);
    if (!memcmp(&p, &crs_rec, sizeof p))
        return 0;                                  /* unchanged: no erase cycle */
    if (st_save(OBJ_SETTINGS, &p, sizeof p) != 0)
        return -1;
    crs_rec = p;
    crs_obj_crc = st_crc32(&p, sizeof p);
    crs_wr_ms = fm1_ms;
    crs_saves++;
#else
    crs_rec.cr = crs_seen;
#endif
    return 0;
}

/* something sounds or is about to: a voice of the parts (release tails too), a chord the engine holds or latches,
 * a scheduled note (arp, strum), or the master output (the FX tails) above CRS_QUIET_PEAK over the scope's last
 * 512 samples (~23 ms). No flash erase then: it would cut the audio (see the top) */
static int crs_sounding(void)
{
    uint32_t i;
    if (cr_parts_busy() || cr_snap.voices || cr_snap.pending)
        return 1;
    for (i = 0; i < SCOPE_N; i++)
        if (scope_buf[i] > CRS_QUIET_PEAK || scope_buf[i] < -CRS_QUIET_PEAK)
            return 1;
    return 0;
}


/* every UI frame (main.c settings_poll) */
static void cr_settings_poll(void)
{
    cr_settings_t now;
    if (!crs_loaded)
        return;
    if (crs_sounding())
        crs_loud_ms = fm1_ms;
    css_poll();                                    /* the session: its own object, its own pending flag */
    crs_capture(&now);
    if (!cr_settings_equal(&now, &crs_seen) || settings_hold != crs_hold_seen) {   /* (HOLD: crs_write's
                                                    * settings_export puts it in the record) */
        crs_seen = now;
        crs_hold_seen = settings_hold;
        crs_change_ms = fm1_ms;
        crs_pending = 1;
        return;
    }
    if (!crs_pending || CR_SETTINGS_BUSY() || (uint32_t)(fm1_ms - crs_change_ms) < CRS_QUIET_MS ||
        (uint32_t)(fm1_ms - crs_loud_ms) < CRS_IDLE_MS || (uint32_t)(fm1_ms - crs_wr_ms) < CSS_GAP_MS)
        return;
    if (crs_pending == 2u && (uint32_t)(fm1_ms - crs_retry_ms) < 1000u)
        return;
    if (crs_write() == 0) {
        crs_pending = 0;
    } else {
        crs_pending = 2;                           /* flash error: retry in a second */
        crs_retry_ms = fm1_ms;
    }
}

/* save now (Felucca's settings_save: the calibration, the Options): still not while a loop plays */
static void cr_settings_save(void)
{
    if (!crs_loaded) {                             /* (power-on calibration: before the UI) */
        if (!crs_booted)
            cr_settings_boot();
        crs_seen = crs_rec.cr;
    } else {
        crs_capture(&crs_seen);
    }
    crs_hold_seen = settings_hold;
    crs_pending = 1;
    crs_change_ms = fm1_ms - CRS_QUIET_MS;
    if (!CR_SETTINGS_BUSY() && crs_write() == 0)
        crs_pending = 0;
    if (crs_loaded && !CR_SETTINGS_BUSY() && css_write() == 0)   /* the session with it (unchanged: no write) */
        css_pending = 0;
}

/* Options > Reset Settings (the owner, 2026-10-12): every option back to what a new unit has, now and in flash. The
 * record: cr_settings_factory (cr_settings_defaults: the same defaults as a unit with no record and as SAFE MODE);
 * Felucca's one field the Options set outside it: HOLD. Then the unit as a power-on leaves it with those settings:
 * the default sounds on both parts (TINE EP, DEEP SUB; an edit not saved is gone: the one place where it is, since
 * 0.18 keeps it over a power-on), their levels and pans, the effects' parameters, the bass off, and cr_settings_load again: the record -> the engine, the routing, the UI's mirror. Saved at once
 * (cr_settings_save: after the loop stops, if one plays: cu_reset stopped it).
 * NOT touched: the panel calibration (persist_t.panel: settings_export writes the table in RAM, as every save), the
 * saved sounds and patches (upreset / va / fm6 / cz / quad stores), the loops and the slot selected, Felucca's
 * favourites. Dual Core: Off is saved; a core that this power-on started runs until the next one (cr_c1_off) */
static void cr_settings_reset(void)
{
    uint32_t i, k;
    if (!crs_loaded)
        return;
    cr_settings_factory(&crs_rec.cr);
    settings_hold = HOLD_DEF;
    for (k = 0; k < 2u; k++)                       /* the session too (0.18): the parts' own parameters (Level, Pan: what
                                                    * a sound's load keeps), the effects' parameters and the effect
                                                    * picked, as cr_ui_init leaves them; cu_reset: LOCK, the voicing,
                                                    * the octave. The session then captured is a new unit's */
        for (i = 0; i < P_E0; i++)
            trk[k ? CR_PART_BASS : CR_PART_CHORD].p[i] = TP[i].def;
    for (i = 0; i < NELEM(CSS_FXG); i++)
        song.g[CSS_FXG[i].g] = GP[CSS_FXG[i].g].def;
    cs.fx_sel = 0;
    trk[CR_PART_CHORD].p[P_LEVEL] = (int16_t)(TP[P_LEVEL].def - 12);   /* (cr_ui_init's) */
    trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)(TP[P_LEVEL].def - 12);
    cu_pool_load(0, ENGI_FM6, cu_pool_find(ENGI_FM6, "TINE EP"));
    cu_sends_to_fx();
    cu_pool_load(1, ENGI_VA, cu_pool_find(ENGI_VA, "DEEP SUB"));
    cs.bass_sound = 0;                             /* ALGORITHM at OFF: the bass off */
    cs.bass_on = 0;
    cr_settings_load();
    cu_loop_conf();                                /* the looper's options and the metronome -> the ISR */
    cu_usb_apply();                                /* USB Record on again, if it was off (usb.c) */
    cr_settings_save();
}
#endif /* CR_VERSION */
