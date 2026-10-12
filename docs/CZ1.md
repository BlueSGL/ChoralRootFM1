# ChoralRoot FM-1 — CZ-1

CZ-1 is ChoralRoot's Casio CZ-1 engine (**engine 14**, `ENGI_CZ`). It is **Melodee's** CZ-1 (Kerem Kilic's fork of
Felucca, <https://github.com/keremimo/melodee>, GPL-3.0): native Casio tones (the 144-byte CZ-1 tone kept verbatim),
two lines with eight-step pitch / timbre (DCW) / volume (DCA) envelopes, the uPD933 phase functions and logarithmic
amplitude, Casio's 64 preset tones, eight 16-tone banks and Casio's tone SysEx over USB-MIDI. This is phase 2 of 3 of
the Melodee platform work (phase 1: FM6, docs/FM6.md; phase 3: USB audio, docs/USB-AUDIO.md).

| file | what | from |
| --- | --- | --- |
| `firmware/src/eng_cz.c` | the engine: the factory presets, BANK / PTCH, the done test, the USB SysEx collector; the phase family's eight-point envelope and note reset (Melodee's `eng_phase.c`: ChoralRoot's PHASE keeps Felucca's own envelopes, so they live here); ChoralRoot's deep pages, blob and load hook | Melodee + ChoralRoot's additions |
| `firmware/src/cz_native.c` | the tone renderer: DCO / DCW / DCA envelopes at the chip's rate law, the 11-bit phase functions, the windows, RING / NOISE, the vibrato, key follow and velocity | Melodee (Devin Acker's uPD933 model, BSD-3-Clause), as it is |
| `firmware/src/cz_patch.h` | the 144-byte tone (128 synthesis bytes + the 16-character LCD name), its check, the init tone | Melodee (`ENGI_CZ` is core.h's) |
| `firmware/src/cz_edit.h`, `cz_legacy.h` | the tone's panel values (decode, encode only the bytes an edit changes) | Melodee (its page-system glue and CZ TOOLS left out) |
| `firmware/src/cz_bank.c` | the eight banks A..H (16 tones each; A..D Casio's until saved, E..H empty) | Melodee (flash objects ChoralRoot's) |
| `firmware/src/cz_store.c` | Casio SysEx: tone send / receive, the handshake | Melodee, adapted (below) |
| `firmware/src/cz_ustore.c` | the tone store: one tone per user slot | ChoralRoot (as `fm6_ustore.c`) |
| `tools/gen_cz1_factory.py`, `tools/make_cz1_factory.py`, `assets/cz1-factory/` | Casio's 64 tones -> `build/gen/melodee_cz1.h` (`tools/build.py` generate()) | Melodee |

The copyright headers are kept ("Copyright (c) Devin Acker / Modifications Copyright (c) 2026 Kerem Kilic (Ellic
Studio)" on `cz_native.c`, Kerem Kilic's on `cz_store.c` and `cz_bank.c`, Leo Kuroshita's on the envelope from
`eng_phase.c`); LICENSING.md lists Melodee, the uPD933 model (`LICENSES/BSD-3-Clause-uPD933.txt`) and Casio's tone data.
Melodee's web-editor side (`editor_cz.c`, commands 75..77) is not ported: ChoralRoot has no web editor.

## The engine

`FELUCCA_CZ` (core.h, default 0: Felucca's unit; 1 in `choralroot.c`, the emulator and `tests/regress.c`). The engine
number is `13 + FELUCCA_SLICE + FELUCCA_VA` = **14** on ChoralRoot (after the VA's 13; Melodee numbers it 15):
`ENGINES[]` stays append-only, `ENGINE_ORDER` shows it **after the VA** since 0.14 (after PHASE before; the PRESETS
browse, the engine picker's roots: FM6, FM TONE, VA, CZ-1, WHEEL). Its per-voice state (`cz_part_t`: six envelopes, the vibrato, the noise, 8
voices) is a member of `engines.c`'s `eng_state` union (smaller than FM6's: the union does not grow); the tone of each
part (`cz_patch[NTRK]`, 4 x 144 bytes) is in the pool, and since 0.18 what the renderer builds from it once per tone
(`cz_defs_`, 2 x 412 bytes: "The render's cost on the unit" below).

EDIT 1 / 2 are Melodee's: **BANK** (A..H) and **PTCH** (0 = INIT, 1..16), TONE fixed at "native" (`P_E7` 2); HOME's
knobs are BANK, PTCH. Turning BANK / PTCH loads that bank slot's tone (`cz_bank_poll`, the UI frame); an empty slot
keeps the tone. **PRESETS** = INIT TONE (the init voice) and Casio's 64, A-1 BRASS 1 .. H-8 TYPHOON SOUND, named as
Melodee lists them (12 characters: FUNKY CLAV 1, SYN STRINGS, TYPHOON, ..); a factory preset loads Casio's tone
whatever the banks hold, with BANK / PTCH at its place in banks A..D (A-1 .. B-8 = BANK A PTCH 1..16, ..). Dry, as the
CZ-1 (no sends). The engine plays its own envelopes (`ownenv`: the platform's ADSR is not applied), POLY up to the 8
shared voices (2 budget units each), no `poly` cap (CPU below).

Velocity amounts (V.PIT, V.WAV, V.AMP) follow the note velocity. Casio's tones with no DCA sustain point (BELLS,
SITAR, JET ROAR) play their whole envelope regardless of the key, as on the CZ: SITAR rings ~60 s after the note-off,
BELLS ~21 s, JET ROAR ~28 s (`tests/regress.c` gives these three 90 s to end). None of them is a bank row.

## The blob and the tone store

`eng_deep_t.blob_size` 144: the tone as it is (128 synthesis bytes + the LCD name). core.h's blob cap is now
`ENG_BLOB_MAX` 160 (was 128): `cr_ui.c`'s picker snapshot and `cu_snd_crc` size their buffers by it; the VA's 110-byte
and FM6's 128-byte blobs and their store records are unchanged. A bad tone (`cz_patch_valid`) loads the init tone.

The tone store (`cz_ustore.c`) keeps a tone per user slot, 1:1, as `fm6_ustore.c` does: SAVE of a CZ-1 sound stores the
tone of the part it came from, another engine's sound over the slot or an erase clears it, loading the slot gives the
part its tone back (`cz_user_pending`, `cz_track_loaded` through `fm6_track_loaded` on every load path). A slot of no
tone loads its BANK / PTCH from the banks. 32 x 144 bytes do not fit one storage object (3840 bytes), so two objects of
16 slots, 2320 bytes each (16-byte header: "CZ1U", version 1, 16 slots, the first slot, the tone size, the used mask),
mirrored in the pool.

## Flash

| object | storage.c | A / B | backup id |
| --- | --- | --- | --- |
| the tone store, slots 1..16 | `OBJ_CZSTORE0` = `OBJ_PROJECT0 + 3` | 0x9D000 / 0x9E000 (the last project pair; 0x97000 VA store, 0x99000 / 0x9B000 FM6 store) | 12 |
| the tone store, slots 17..32 | `OBJ_CZSTORE1` | 0xA0000 / 0xA1000 | 13 |
| the CZ-1 bank A..H | `OBJ_CZBANK0 + k` | 0xA2000 + k x 0x2000 / + 0x1000 (to 0xB1FFF) | 14 + k |
| (FM TONE's patch store: engine 15, `eng_quad.c`, code name QUAD, docs/QUAD.md) | `OBJ_QUADSTORE` = `OBJ_CZBANK0 + 8` | 0xB2000 / 0xB3000 | 22 |

0xA0000..0xB1FFF is the user sample slot 1's flash, unused since the all-synth change (`FELUCCA_SAMPLE` 0; storage.c's
map); 0xB2000 / 0xB3000 hold FM TONE's patch store (0.14), 0xB4000 / 0xB5000 the session (0.18, docs/SETTINGS.md), 0xB6000..0xC7FFF stays free, 0xC8000.. holds the loops. SAFE
MODE's Flash Data erase covers 0xA0000..0xB3FFF too (`cr_ui.c` `cu_flash_erase`: 56 sectors with FM TONE's pair).

## The banks (cz_bank.c)

Melodee's: eight banks of 16 tones (`cz_bank_t`, 2332 bytes: magic "CZCB", version 1, a 16-character name, the used
mask, 16 tones), one RAM cache (the pool) of the bank in use. A bank never saved is its default: BANK A..D Casio's 64
(A-1 .. B-8, C-1 .. D-8, E-1 .. F-8, G-1 .. H-8, named "CZ-1 A1-B8" ..), E..H empty. As in Melodee a bank comes in and
goes out whole through the **backup** (Melodee's backup ids 9..16, ChoralRoot's 14..21): `cr_backup.c` lists a saved
bank, restores one (Melodee's check), and the parts on that bank reload its tones. A backup carries only saved banks:
restoring one without a bank brings the default back. There is no Casio bank dump (the CZ-1 has none); a tone goes in
and out by Casio SysEx (below).

## Casio SysEx (cz_store.c)

Melodee's, on any channel n; Casio's low nibble first, no checksum. The bytes reach `cz_sx_byte` from `usb.c`
`sysex_byte` (beside FM6's `fm6_sx_byte`), the UI frame serves them (`cz_service`, `cr_ui.c` beside `fm6_service`):

| message | effect |
| --- | --- |
| `F0 44 00 00 7n 30 <288 nibbles> F7` (a 144-byte CZ-1 tone dump) | the CZ-1 part's tone, verbatim; "CZ TONE LOADED", the sound marked edited |
| `F0 44 00 00 7n 30 <256 nibbles> F7` (a 128-byte CZ-101 / 1000 tone) | the same, with the CZ-1 line levels, velocity and DCW key-follow table filled in (the part's name kept) |
| `F0 44 00 00 7n 20 60 ..` / `21 60 ..` (receive request + the tone) | the handshake (`7n 30` back), then the tone in; the acknowledgement |
| `F0 44 00 00 7n 10 60 F7` / `11 60` (send request) | the handshake, then the part's tone as a 128- / 144-byte dump |

The CZ-1 part: the part the sound editor shows if it plays CZ-1, else the chord part, else the bass part (the channel
does not choose, as FM6). An invalid tone is refused ("CZ INVALID TONE"); no CZ-1 part: "CZ: NO CZ-1 PART". The frames
out go through `CZ_SEND` (the device: `ota_wire_send`).

## The deep pages (the sound editor)

`CZ_DEEP` (37 pages, `section = {0, 3, 5, 35, 36}`); every column is a panel value of `cz_edit.h` (`cz_dref`); a set
writes back only the tone bytes whose encoding changed (an imported tone keeps its exact rates and levels until that
value is turned), the name stays. SUS sounds only before END (turning it past END gives "-"), a new END drops a SUS at
or after it, END's own level is 0: the CZ's rules (`cz_ed_put`). `mod_dst` is -1 everywhere: no matrix.

| section (editor group) | pages | KNOB 1..4 |
| --- | --- | --- |
| OSC (FX) | `LINE 1`, `LINE 2` (a stack of two) | WAVE (SAW SQUARE PULSE NULL DBL SINE SAW-PULS MULTISIN PULSE2) · WAVE2 (OFF + the eight) · WINDOW (OFF SAW DN TRIANGLE TRAPEZ HALF SAW 2 SAW UP 2 SAW 6 2 SAW 7) · LEVEL (1..15); then the mixer (the two LEVELs) |
| | `DETUNE` | SIGN (+ / -) · OCT (0..3) · NOTE (0..11) · FINE (0..60) (line 2) |
| FILTER (SEL) | `DCW` + `DCW+` | W.KEY1 W.KEY2 (DCW key follow 0..9) · V.WAV1 V.WAV2 (DCW velocity 0..15) / A.KEY1 A.KEY2 (DCA key follow) · V.AMP1 V.AMP2 (DCA velocity) |
| ENV (ENV) | `PITCH n`, `DCW n`, `DCA n` (n = line 1, 2; in that order) | five pages each: `X n` R1..R4, `X n+` L1..L4, `X n B` R5..R8, `X n B+` L5..L8, `X n S` SUS (1..8, -) · END (1..8) (PITCH: · V.PIT 0..15); three screens an envelope under the **cz** band, top right `DCW 1 · 1-4`, `· 5-8`, `· END` |
| LFO (LFO) | `VIB` | WAVE (TRI SAW UP SAW DN SQR) · DELAY · RATE · DEPTH (0..99) |
| MOD (SEQ) | `TONE` | LINE (LINE1 LINE2 1+2 1+1) · MOD (OFF RING NOISE) · OCT (-1 0 +1) |

The ENV group's screens follow the pages' runs: `cr_edit.c` now groups the FILT / ENV pages by instance **and name**
(`PITCH 1` and `DCW 1` are two runs; the VA's and FM6's pages group as before). The **cz** band (`CR_W_CZ`, cr_draw.c;
`ce_is_cz`: a run whose pages hold R1.. and SUS / END): the eight steps from 0 to L1 at R1 .. L k at R k, the END step
to 0, a dashed hold after the SUS step's point, steps after END not drawn; a step's width grows with its distance and
the slowness of its rate; the step of the cell just turned (R k / L k; SUS / END: their step) thick in its knob's
colour, digits 1..8 (and S) under the steps. `cr_screen_t.wv` grew to 20 bytes (R1..R8, L1..L8, SUS, END, the lit step).

Melodee's **CZ TOOLS** page (NAME, LINE 1 > 2, 2 > 1, COMPARE) is **not ported**: ChoralRoot's editor pages hold
values, not actions, and the editor's own SAVE names the sound; COMPARE's pool copy is left out with it.

## The bank rows (cr_bank.c)

Five chord rows at the END of `CB_CHORD` (PRESETS 44..48; the other rows keep their places) and one bass at the end of
`CB_BASS` (ALGORITHM 13). Felucca's PHASE imitations (CZ STRINGS / CZ ORGAN / CZ BRASS) stay as they are. Measured with
`tests/va_levels.c cz` (a 6-note chord at LEVEL 92, velocity 100, alone / with SUB BASS; the share of the hold under
the limiter, the post-master peak and RMS; trims in 0.5 dB steps):

| row | Casio | trim 0: alone / with SUB BASS | trimmed: alone / with SUB BASS | trim |
| --- | --- | --- | --- | --- |
| CZ BRASS 1 | A-1 BRASS 1 | 56.1 % -3.9 / -14.8 dB, 79.4 % -4.1 / -14.5 | 0 % -7.6 / -19.2, 0 % -6.2 / -17.2 | -10 (-5 dB) |
| CZ PIANO | D-1 PIANO 1 | 0 % -22.7 / -53.0, 0 % -16.3 / -21.4 | (as trim 0) | 0 |
| CZ STRINGS 2 | A-5 STRINGS 2 | 1.9 % -5.6 / -15.8, 27.5 % -4.7 / -15.0 | 0 % -8.2 / -18.7, 0 % -6.5 / -16.9 | -6 (-3 dB) |
| CZ PIPE | E-3 PIPE ORGAN 1 | 79.0 % -3.7 / -15.0, 95.2 % -3.7 / -14.7 | 0 % -7.7 / -20.1, 0 % -6.8 / -17.7 | -12 (-6 dB) |
| CZ VIBES | F-2 VIBRAPHONE | 0 % -18.7 / -46.8, 0 % -15.3 / -21.4 | (as trim 0) | 0 |
| CZ BASS (with TINE EP's chord) | B-5 SYNTH.BASS | 0 % -8.6 / -30.2 | (as trim 0) | 0 |

The fifth chord row is VIBRAPHONE rather than BELLS: BELLS has no DCA sustain and rings ~21 s after every chord change
(the voices pile up), VIBRAPHONE decays as a mallet should. PIANO 1 decays too (its RMS over the 5.6 s hold is low).

## CPU and memory

`tools/emu/perf.sh` (g): a 6-note CZ STRINGS 2 chord + CZ BASS, held 3 s, a chord change, 3 s, released (device
estimate, avg / worst block of the 2.9 ms budget): **33 % / 43 %** (VA's (f): 30 % / 39 %; FM PAD (b): ~25 % / 30 %);
no block over 80 %. Under 60 %: CZ-1 keeps the shared 8 voices (no `poly` cap). `tests/va_levels.c cz`: ~34 % with
SUB BASS. The host cost (`tests/regress.c`, one part, 8 notes): 1757..2487 instructions a sample (JET ROAR the
heaviest; the VA's MORPH PAD 2697, FM6's DRAWBARS 1920). Since 0.18's render (below): 799..1264 (INIT TONE, JET ROAR), 44..57 %
less for every tone of eight sounding notes; `tests/cpu_baseline.txt` is rewritten (every other entry 32..44 lower: the
master and the mix; FM6's were stale by ~210 before, not by this).

`./build.sh` (against the same tree with `FELUCCA_CZ=0`): XIP 320348 -> 345472 B (55.1 -> 59.4 %: the 64 tones 9216 B
and the code); RAM 79512 -> 79640 B (80.9 -> 81.0 %); POOL 303160 -> 311300 B (88.1 -> 90.5 %, +8140: the tone store
2 x 2320, the bank cache 2332, the parts' tones 576, the SysEx buffers 296 + 295; `eng_state` does not grow). The blob
cap's +32 bytes in `cr_ui.c`'s picker snapshot are in both.

### The render's cost on the unit (0.18)

Measured on the unit (SysEx 78 PEAK `pk_part0_us`), an eight-note CZ-1 chord took 1699..1985 us of the 2902 us half:
with the 0.18 effects, late halves. Counted in `build/choralroot.dis` (instructions on the usual path over 205 a
microsecond, the unit's measured rate; jump-table bytes are not instructions), a voice of a two-line tone cost about
50 800 instructions a half (248 us): 265 a sample and 4 200 a block. It is now about 16 100 (79 us): 91 a sample and
1 100 a block. Eight voices: about 1 980 us -> 630 us. Every sample is the one it was (below). What changed, in
`cz_native.c`:

1. **The envelopes' definitions are built once per tone, not per voice and block.** `cz_native_defs` (six envelopes
   of eight points: a `memset`, a 64-bit multiply and a divide by 44100 a point, a divide by 96 for the DCA: about
   3 250 instructions a voice a block) wrote them onto the render's stack every time. The rates and targets are the
   tone's: they live in `engines.c cz_defs_[ENG_STATE_NPART]` (`cz_defs_t`, 412 B a part, 824 B of pool; not in
   `eng_state`, whose bytes another engine may have left when a part comes to CZ-1 without a clear) and are rebuilt
   when `eng_cz.c cz_pgen[part]` moved. Every writer of `cz_patch` bumps it after its write, as `fm6_pgen`:
   `cz_init`, `cz_tone_put` (a preset, a user slot, an engine change, INIT, the picker, the blob: the session's
   restore and the backup go through `blob_set`), `cz_dset` (the editor), `cz_bank.c cz_bank_poll` (BANK / PTCH),
   `cz_store.c cz_sx_import` (Casio SysEx). The DCA's key follow depends on the note: a voice's eight key-followed
   rates a line are in its own state (`cz_voice_t.krate`, in `eng_state`: free), built by `cz_native_krate` when the
   tone's generation or the voice's note is not the one they were built for (`kgen`, `knote`; `cz_note_on`'s `memset`
   leaves none). `cz_env_tick` takes the rates as an argument. 1+1' reads line 1's definitions for both lines
   instead of copying them.
2. **A line's samples run in a loop built for its wave and window.** `cz_native_wave` was a call a sample and line
   with a switch on the wave, a switch on the window and an interpolated cosine. Now each line renders its block on
   its own into a buffer on the stack (a line reads no other; ring modulation and the sum take both afterwards), a
   cycle at a time: `cz_native_line` picks, by the wave the cycle plays (a line has two, alternating) and the window,
   one of 43 loops (`cz_native_run`, built with the two as constants: 7 waves x 6 windows, and the silent wave), which
   runs until the phase wraps or the block ends. The carrier comes from `CZ_CARRIER[2048]` (`tools/gen_tables.py`:
   `(pd_cos((p << 5) + (p >> 6)) + 32767) >> 1` for the 2048 phases there are, in the same integers;
   `tests/cr_cz_test.c` compares every entry with the expression). Line 2 with NOISE, whose generator moves the
   increment every sample, keeps a sample's switches (`cz_native_noise`). A line's loop is 27..42 instructions a
   sample (34 for the plain cosine-phase waves) where the call and its loop were 110; the mix of the two lines 23.
   Flash: 10.3 KB (the table 4096 B, in flash as `SINE` is; `ram=True` in `gen_tables.py` would put it in RAM if
   the unit shows the flash cache in the way).
3. **The set-up does what the tone needs.** A line the tone does not play (LINE 1 or LINE 2 alone) still ticks its
   three envelopes: their state is what a LINE change under a held note finds, so skipping them would change that
   note; its two DCA curves, its velocity and key-follow scaling, its pitch and its phase function are no longer
   worked out. For a line that plays: the DCA curve (`cz_native_amp`) once when the envelope did not move in the
   block, the pivot's two divides only when the DCW depth changed (`cz_voice_t.pk`), no multiply by a velocity factor
   of 1, no divide by a key follow of 0, the ramp's divide by the block as the shift it is.

Not done: skipping a line whose DCA has ended. Ring modulation multiplies the lines and the sum halves them, so an
ended line still shapes the output through its DC filter's tail (`dc += raw - (dc >> 10)` runs on with the wave at
full scale: the DCA only scales after it); it is not silent sample for sample until that tail is, and the phase and
the cycle's toggle would have to be advanced in step for a later note on the same voice (`voice_was`).

A patch write from the main loop that the audio ISR interrupts was always audible for a block as a half-written tone;
it still is, with the definitions of the tone before it until the generation moves at the end of the write.

**Bit for bit.** `tests/regress.c`'s goldens of INIT TONE and the 64 (unchanged), the emulator's 52 WAVs
(`test_cr.sh`, `test_persist.sh`, `perf.sh` with and without Dual Core: the same SHA-256 before and after), and a
comparison of the two source trees built side by side: every tone at three velocities and eight notes with the
pitch, fine tune, cutoff, shape and amplitude moving every block, held and released; 600 random tones (LINE, octave,
both waves of both lines, the window, ring and noise, key follow, velocity amounts, detune, the vibrato's wave); the
whole mix with CZ-1 on both parts while every column of the 37 pages is turned under held notes, LINE and MOD are
changed under them, other tones and blobs are loaded, with a bend and TUNE: the same hash of every output sample,
of `cz_native_done` and of the voices' final state.

## Tests

- `tests/cr_cz_test.c` (in `tests/run_cr_tests.sh`): the engine's number and place; Casio's 64 (valid, names, BANK /
  PTCH); every tone through the blob and back, a bad blob; every tone rendered (3 notes, velocity 127 / 1) within range
  and not silent; SysEx: a 144-byte dump in, the send request's dump out identical, a 128-byte dump, an invalid tone,
  no CZ-1 part; the banks (A..D Casio's, E..H empty, BANK / PTCH, a bank restored as backup object 18 and reloaded);
  the 37 pages (titles, sections, every column's min / max / default, the panel value it is, one byte for one R, the
  SUS / END rules, Casio's encodings); the tone store (save, load, reboot, the second object, clear, erase, a slot of no
  tone); the backup's objects 12..21.
- `tests/cr_backup_test.c`: 27 objects (12, 13, 14..21 listed; a saved bank and the store round trip).
- `tests/regress.c`: goldens and CPU baselines for INIT TONE and the 64 (additions only; nothing else changed).
- `tests/target_budget.txt`: `cz_native_render`, `cz_native_wave`, `cz_env_tick` added; the audio ISR's static count
  is 43986 (was 37094: 1880 -> 1882 instructions, the register allocation moved a loop head; its code is the same).
  0.18: `cz_native_wave` is inlined; `cz_native_line` (its 43 loops; the static measure weighs a loop in a loop four
  times: 45287) and `cz_native_noise` are watched instead.
- `tools/emu/scripts/cr_cz.txt` (`test_cr.sh`): CZ BRASS 1, the editor's ENV screen 4 (DCW 1) under the cz band (shot),
  KNOB 1 on DCW 1's L1 (the `deep:` trace, step 1 lit), the chord darker after it (its HF share), the other groups;
  `cz_persist_*.txt` (`test_persist.sh`): an edited CZ-1 sound in U01 across a relaunch; `perf_g.txt` (`perf.sh`).
- `web/test_backup.mjs`: ids 12..21, their names.
