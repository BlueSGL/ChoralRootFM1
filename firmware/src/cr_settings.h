/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the persisted settings record (docs/SETTINGS.md). Standalone (stdint only): Felucca's
 * settings_persist.c includes it for the layout of its ChoralRoot block, so the block survives a build that
 * does not know it.
 *
 * One fixed-layout record, versioned and append-only: a newer field always goes into the reserve at the end
 * (rsv shrinks, the size stays CRS_SIZE), CRS_VERSION goes up, and cr_settings_import() gives it its default
 * when an older record (whose version is lower) is read. Every field is checked on import: a value out of range
 * takes its default; a bad magic / checksum gives all defaults. */
#ifndef CR_SETTINGS_H
#define CR_SETTINGS_H
#include <stdint.h>

#define CRS_MAGIC 0x31535243u           /* "CRS1" */
#define CRS_VERSION 14u
#define CRS_SIZE 192u                   /* bytes, header included; never changes (fields come out of rsv) */
#define CRS_NPM 5                       /* perform modes (cr_engine.h CR_PM_COUNT) */
#define CRS_NPAR 11                     /* parameters per mode (CR_P_COUNT) */
#define CRS_SOUND_DEFAULT 0xFFFFu       /* chord_sound / bass_sound: the UI's own default (FM6 TINE EP, VA DEEP SUB) */
#define CRS_NENG 11                     /* the pool_pos slots, one per engine shown in 0.14 (six shown since 0.18: cr_settings.c) */
#define CRS_ENG_ANALOG 0u               /* engine numbers the record's migrations know (core.h): ANALOG (retired in 0.14), */
#define CRS_ENG_QUAD 15u                /* FM TONE (ENGI_QUAD, code name QUAD): pool_pos slot 0 since version 7 */
#define CRS_POOL_DEFAULT 1u             /* pool_pos: an engine not played yet lands on its first preset */
#define CRS_PALETTE_MOD 0xFFu           /* palette: MOD, ChoralRoot's (gfx.c: the last palette) */
/* view (v9): bits 0..2 the main view 0..4 (Chord, Keyboard, Notes, Geek Out, Scope), bit 7 the view lock (HOME held's
 * menu: locked, HOME tap never cycles the view); the other bits 0. No reserve was left: the lock rides in the view's byte
 * (an older firmware reads a locked record's view as out of range: Chord) */
#define CRS_VIEW(s) ((unsigned)(s)->view & 7u)
#define CRS_VIEW_LOCK(s) (((unsigned)(s)->view >> 7) & 1u)
#define CRS_VIEW_BYTE(v, lock) ((uint8_t)(((v) & 7u) | ((lock) ? 0x80u : 0u)))
/* v12: bit 6 of view, Options > Dual Core On (docs/DUALCORE.md: core 1 started at the next power-on). Still no
 * reserve: it rides in the view's byte as the lock does, set = On, so a fresh, erased or zeroed record reads Off, the
 * default (0.16: one core unless the user turns the second on). v11 (0.15) had it the other way round, set = Off and
 * On the default: a v11 record cannot tell a chosen On from that default, so every record older than v12 loads Off
 * whatever its bit says (cr_settings_import). An older firmware reads an On record's view as out of range: Chord */
#define CRS_DC_ON 0x40u
#define CRS_DUALCORE(s) (((unsigned)(s)->view & CRS_DC_ON) ? 1u : 0u)
/* key_on (v14): bit 0 Key Mode on; bit 1 the KEY layer's page 2 "Key plays" (0 Chords, the default; 1 Notes: Key Mode
 * blocks the roots out of the scale and generates no chord); bit 2 its "Keyboard" (0 Chords, the default; 1 Full: all
 * 27 keys are single notes). Still no reserve and loop_rec is full: the two ride in Key Mode's own byte, 0 = the
 * defaults, so every older record reads them off. A firmware older than v14 reads a record with bit 1 or 2 set as
 * key_on out of range and takes its default: Key Mode off, nothing else (its Single reads 2, Split Bass, the same
 * way: Full Octave) */
#define CRS_KEY_NOTES 0x02u
#define CRS_KBD_FULL 0x04u
#define CRS_KEY_ON(s) ((unsigned)(s)->key_on & 1u)
#define CRS_KEY_PLAYS(s) (((unsigned)(s)->key_on & CRS_KEY_NOTES) ? 1u : 0u)
#define CRS_KEYBOARD(s) (((unsigned)(s)->key_on & CRS_KBD_FULL) ? 1u : 0u)
#define CRS_KEY_BYTE(on, notes, full) ((uint8_t)(((on) ? 1u : 0u) | ((notes) ? CRS_KEY_NOTES : 0u) | ((full) ? CRS_KBD_FULL : 0u)))
enum { CRS_CLOCK_OFF, CRS_CLOCK_OUT, CRS_CLOCK_IN };
enum { CRS_NONE = 0xFF };               /* out part: no part */
enum { CRS_USB_MASTER, CRS_USB_FIXED };
/* loop_rec (v8): bits 0..3 the record mode (cr_loop.h CRL_MODE_*: 0 Overwrite .. 4 Step), bit 4 the PLAY menu's keys
 * (0 Play: they play chords, 1 Loops: the slot shortcuts); the other bits 0. 0 = Overwrite, Play (the defaults).
 * v13: bit 5 the KEY layer's keys (0 pick: a root = the tonic, MAJ / MIN = the scale, the default; 1 play: the keys
 * play as outside the layer; OPT tapped in the layer). Still no reserve: it rides in this byte, next to the other
 * "what do the keys do in this menu" bit. A firmware older than v13 reads a record with bit 5 set as an unknown bit
 * and gives the whole byte its defaults (Overwrite, Play): its record mode and PLAY-menu keys are then lost on a
 * downgrade, nothing else.
 * v13 also: bits 6 and 7 the LOCK layer's two styles (B3 held): bit 6 "Type key" (0 Clears: a type key clears the
 * latched extensions, today's; 1 Keeps), bit 7 "Ext keys" (0 Toggle, today's; 1 Pushed). The same downgrade caveat */
#define CRS_NLOOPMODE 5u
#define CRS_KEY_PLAY 0x20u
#define CRS_LOCK_KEEP 0x40u
#define CRS_LOCK_PUSH 0x80u
#define CRS_LOOP_MODE(s) ((unsigned)(s)->loop_rec & 15u)
#define CRS_LOOP_KEYS(s) (((unsigned)(s)->loop_rec >> 4) & 1u)
#define CRS_KEY_KEYS(s) (((unsigned)(s)->loop_rec & CRS_KEY_PLAY) ? 1u : 0u)
#define CRS_LOOP_REC(mode, keys) ((uint8_t)(((mode) & 15u) | ((keys) & 1u) << 4))  /* usb_level: USB audio follows MASTER / takes the full level (docs/USB-AUDIO.md) */

typedef struct {
    /* header (12 bytes) */
    uint32_t magic;                     /* CRS_MAGIC */
    uint16_t version;                   /* CRS_VERSION of the writer */
    uint16_t size;                      /* CRS_SIZE of the writer */
    uint32_t check;                     /* FNV-1a over bytes [12, size) */
    /* version 1 */
    uint8_t playstyle, extadd, secret, key_on;       /* cr_playstyle_t, cr_extadd_t, cr_secret_t, Key Mode (bit 0;
                                                      * v14: bit 1 Key plays Notes, bit 2 the Full keyboard) */
    uint8_t tonic, scale;                            /* 0..11, cr_scale_t */
    int8_t transpose;                                /* -24..24 */
    uint8_t single;                                  /* cr_single_t: Full Octave / Split Oct / (v14) 2 Split Bass */
    uint8_t split_pc, vel, bass_on, bass_mode;       /* 0..11; key velocity 1..127; (0 at power-on); cr_bassmode_t */
    int8_t bass_voicing;                             /* bass register, octaves -2..4 */
    uint8_t perform_on, perform_mode, perf_sel;      /* on, cr_pmode_t, the PERF layer's entry 0..6 (Strum 2 ...) */
    uint8_t sticky, pool_pos10_chord;                /* latch; v6: pool_pos of engine rank 10 for the chord part
                                                      * (crs_pool_get; was rsv0) */
    uint16_t bpm;                                    /* 20..300 */
    int16_t par[CRS_NPM][CRS_NPAR];                  /* per-mode perform parameters (cr_engine.h cr_param_t) */
    uint8_t loop_sync, loop_quant, loop_count_in, loop_level;   /* 0..5 (Free, 1..16 bars), 0..6, 0/1, 0..100 */
    uint8_t midi_en[3], midi_ch[3];                  /* per stream (MAIN BASS RAW): MIDI out on, channel 0..15 */
    uint8_t clock_mode, raw_sound, view, motion;     /* CRS_CLOCK_*, RAW also plays part 0, View 0..4 (bits 0..2; v9:
                                                      * bit 7 the view lock, CRS_VIEW / CRS_VIEW_LOCK; v12: bit 6
                                                      * Dual Core On, CRS_DUALCORE), Motion 0..2 */
    uint8_t palette, leds, fx_on, pool_pos10_bass;   /* palette index (CRS_PALETTE_MOD), LEDs Glow / Stock, FX on; v6: the
                                                      * bass part's pool_pos of engine rank 10 (was rsv1) */
    uint16_t chord_sound, bass_sound;                /* v6: the chord part's sound and the bass part's (what BASS tap
                                                      * brings) as (engine << 8) | pool position (docs/PRESETS.md);
                                                      * CRS_SOUND_DEFAULT the UI's. v1..5: positions in the old bank
                                                      * list, read as CRS_SOUND_DEFAULT */
    /* version 2 */
    uint8_t metro_on, metro_sig, metro_vol, loop_slot;   /* the click, 4/4 3/4 6/8, its level 0..100, slot 0..9 */
    /* version 3 */
    uint8_t pick_roots;                              /* the engine picker's white roots: 1 engines, 0 they play */
    /* version 4. loop_rec (v8): the looper's record mode and PLAY-menu keys, CRS_LOOP_MODE / CRS_LOOP_KEYS (the last
     * free byte: before v8 it was rsv_usb, v4's usb_out (Options > USB Audio Out, the playback removed in v5: 0
     * since; a 0.14 dev build's version-6 record kept FM TONE's chord place there, read once by the version-7
     * migration, then 0) */
    uint8_t loop_rec, usb_in, usb_level;             /* (above; v13: bit 5 the KEY layer's keys, CRS_KEY_KEYS), Options >
                                                      * USB Record (on), USB Level (CRS_USB_*) */
    /* version 6 */
    uint8_t pool_pos[2][10];                         /* per part (chord, bass), per engine slot 0..9 (slot 10's:
                                                      * pool_pos10_*): the pool position last played there, where
                                                      * OPT + PRESETS lands (CRS_POOL_DEFAULT before). The slots are
                                                      * 0.13's display order, fixed: 0 FM TONE (ANALOG's until version
                                                      * 7), 1 FM6, 2 VA, 3 PHASE, 4 CZ-1, 5 LOFI, 6 VOICE, 7 TRIO,
                                                      * 8 WHEEL, 9 PHYS, 10 NOISE (cr_settings.c crs_slot). The reserve
                                                      * is used up: a new field needs a longer record (docs/SETTINGS.md) */
} cr_settings_t;

/* what cr_out.c's routing takes (cr_route_t, written field by field by the unit's glue) */
typedef struct {
    uint8_t part[3];                    /* 0 = CHORD part, CRS_NONE: none (RAW only: Raw Chord Sound) */
    uint8_t midi_en[3], ch[3];
    uint8_t clock_out;
    uint8_t clock_in;                   /* Options > MIDI Clock = In (clock_out is then 0) */
} cr_settings_out_t;

void cr_settings_defaults(cr_settings_t *s);
/* Options > Reset Settings: the record back to cr_settings_defaults (the one place the defaults come from: a unit
 * with no record, SAFE MODE and this), keeping the one field that is the looper's place and not an option: loop_slot
 * (the loop in RAM belongs to it; the loops are not the reset's). Sealed */
void cr_settings_factory(cr_settings_t *s);
/* a stored block (n bytes) -> *s, defaults for what is absent or invalid. 1: current, 2: migrated (older or newer
 * version: the fields it has are kept), 0: invalid (all defaults) */
int  cr_settings_import(cr_settings_t *s, const void *blk, uint32_t n);
void cr_settings_seal(cr_settings_t *s);         /* header + checksum, before it is stored */
int  cr_settings_equal(const cr_settings_t *a, const cr_settings_t *b);   /* the payloads */
/* the pool position of part (0 chord, 1 bass) on the engine of pool_pos slot r (0..CRS_NENG-1) */
static inline uint8_t crs_pool_get(const cr_settings_t *s, unsigned part, unsigned r)
{
    part &= 1u;
    if (r < 10u)
        return s->pool_pos[part][r];
    return part ? s->pool_pos10_bass : s->pool_pos10_chord;
}
static inline void crs_pool_set(cr_settings_t *s, unsigned part, unsigned r, uint8_t v)
{
    part &= 1u;
    if (r < 10u)
        s->pool_pos[part][r] = v;
    else if (part)
        s->pool_pos10_bass = v;
    else
        s->pool_pos10_chord = v;
}

#ifdef CR_ENGINE_H
/* the record -> the engine's setters (call with the engine idle / the audio IRQ off) and the routing */
void cr_settings_apply(const cr_settings_t *s, cr_t *c, cr_settings_out_t *o);
/* the reverse: the engine's and the routing's fields of *s (the UI's own fields are left as they are) */
void cr_settings_capture(cr_settings_t *s, const cr_t *c, const cr_settings_out_t *o);
#endif

#endif
