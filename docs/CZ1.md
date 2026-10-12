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

## Pops (0.19)

The owner (HANDOFF, "For 0.19" (b)): "CZ-1 can pop and dropout when holding chords without FX ... probably internal clipping." Two
causes were found in the host render. Neither is clipping: nothing in the voice, the part's sum or the mix wraps or
saturates. Both are steps the engine made in the sample: (A) a note struck again on a voice that still sounds, (B) the
DCW depth of a control block applied at the block's edge. A third cause, a half rendered late on the unit, cannot
show in a host render at all and is not ruled out: `tools/cz_pop_check.py` below.

### How they were looked for

`tests/cz_pops.c` (a tool, not a suite; `tools/cz_pops_report.py` sorts its lines): one factory sound on part 0 at
LEVEL 92 with its trim, the sends at 0, ChoralRoot's mix (`fx_smooth`, `voice_fade_steal`, MASTER 2047), with and
without the CZ-1 bass (SYNTH.BASS, MONO) on part 1. INIT TONE and the 64 (banks E..H are empty until the user fills
them: no other tone exists by default) x six scenarios x four chords (4, 5, 6 notes close, 6 notes over two octaves) x
four roots (C2, C4, C6, C7) x velocity 100 / 127 x bass off / on = **24 960 runs** of 3.6 to 5.2 s. The scenarios:
`hold` (3 s, released), `retrig` (the chord let go and struck again in one block, eight times), `retrig-gap` (struck
again 100 ms after its release), `change` (eight chords with common tones, changed in one block), `steal` (12 notes
40 ms apart, then three chords over the ringing tails), `arp` (the chord's notes up and down every 75 ms, each twice).
Looked at, in the dry mix before the master (`mix_l` / `mix_r`: linear) and in the output:

- **wraps**: the tool is built with `-fsanitize=signed-integer-overflow`; a wrap anywhere in the voice, the part or
  the mix stops the run. The peaks of every stage are recorded through a host-only hook at the end of
  `cz_native_render` (`CZ_PROBE`, not compiled into the firmware).
- **gain steps**: a line's gain (its DCA curve x the voice's ramp, Q15) at the start of a voice's block against the
  end of its block before. The engine ramps every gain across a block, so a difference is a step it made. Counted
  above 5 % of full scale.
- **jumps**: a sample-to-sample step of the dry mix above -35 dB that is at least four times the largest step the
  signal makes in the 600 samples around it (1.25 periods of the lowest note if longer): a saw's edge is not a pop.
- **holes**: 32 samples of zeros between sounding windows. (**Dips**, a fall of 12 dB that is back within 60 ms, are
  printed too but say little: six detuned notes beat.)

`tools/emu/wavclicks.py` (the third difference in 32-frame windows against its neighbourhood) was left as it is: it
finds the same places in the emulator's WAVs (below) but also reads a bright attack as a burst, so it cannot say what
a click is. The gain step can.

### What was found (main, 19b6453)

| | hold | retrig | retrig-gap | change | steal | arp |
| --- | --- | --- | --- | --- | --- | --- |
| runs | 4160 | 4160 | 4160 | 4160 | 4160 | 4160 |
| with a wrap | 0 | 0 | 0 | 0 | 0 | 0 |
| with a gain step > 5 % (tones) | 0 | 4000 (65) | 2056 (42) | 3629 (65) | 3882 (65) | 3148 (52) |
| with a jump in the dry mix | 165 | 758 | 434 | 624 | 442 | 891 |
| with a hole | 0 | 108 | 43 | 0 | 0 | 104 |

Peaks over all runs: a line 29 817 (Q15), a voice's two lines 51 950 (ring modulation: `mulq15(l0, l1) * 2`), a voice
into the part 152 188 (6.3 x `VOICE_FS`), the part after LEVEL 114 205 (the mix loop clamps at 524 287), the dry mix
149 467 (4.6 x full scale at MASTER's top: the master's limiter worked in 59 % of the runs, down to a gain of
0.29). By the code the bounds hold for any tone: a line is `((raw - dc) >> 1) * amp` with |raw - dc| <=
65 534 and amp <= 31 079, the ring product 31 079^2 * 2 / 32 768 = 58 954, and `voice_amp`'s 58 954 x 32 767 is 1.93e9,
under 2^31; eight such voices are 1.4 M, which the mix loop clamps to 884 000 before its multiply.

**(A) A note struck again cut its own voice (all 65 tones).** `cz_note_on` cleared the voice's state, so the six
envelopes started at level 0. For a free voice that is right. For a voice that still sounds (`voice_was`: the same
note again, MONO, the arp, a chord change's common tone: `voice_alloc` gives the note its own voice back, and
`voice_start` keeps its phases because "resetting them clicks") the DCA's gain fell from where it was to 0 between
two samples and the DCW's depth with it: a step of the voice's whole sample, then silence, then the attack again
(the "pop and drop-out"). The worst: INIT TONE, a gain of 31 078 of 32 767 to 0; in the mix a step of 42 485 on
STRINGS 1 (51 % of full scale at the output). Every chord change on the keys that shares a note with the chord
before it does this, and every re-strike of a sustained chord.

**(B) The DCW depth stepped at a block's edge (`hold`'s 165 runs: 164 at C2, 160 of them the CZ-1 bass under the
chord; every tone when struck again).** The
depth (`pd.dcw`, so the pivot and the two slopes of the phase function) was the envelope's value once per 32-sample
block. A DCW attack at the fastest rate moves it 218 of 1023 a block; at a block's edge the phase function changed
under the phase, and with it the sample. High notes hide it (a cycle is shorter than a block and has such slopes of
its own); a low note sits on one part of its cycle for many samples and steps: SYNTH.BASS at C2 steps 14 000 of the
dry mix 160 samples after every note-on (43 % of full scale at MASTER's top), HONKY-TONK's C2 chord 22 240. This is
the CZ-1 bass's click under a held chord.

Nothing else: no jump further than 1000 samples from a note-on in `hold`, no step at a voice's end (the DCA's last
block ramps to 0 before `cz_native_done`), none when a voice is given up (`voice_kill`: one block's fade, as every
engine).

### The fixes

- **(A)** `eng_cz.c cz_note_on`: a voice that sounds keeps its six envelopes' levels and its lines' last DCW pivot
  (`cz_voice_t.pk`); stage, gate, vibrato, noise and the key-followed rates restart as before. The envelopes go from
  where they are to step 1's level at step 1's rate. A free voice starts as it did (bit for bit).
- **(B)** `cz_native.c`: `cz_native_pd` gives a block the depth its line's last block ended at (`1024 - pk[0]`) and
  the step a sample to this block's (`dq`, `dstep`: Q16, exactly there after 32). A block whose depth did not change
  (a held note at its DCW sustain: the usual one) has `dstep` 0 and runs `cz_native_line`'s 43 loops as before, the
  same code. A block whose depth changed runs `cz_native_line_mv`: the same 43 loops with the depth of each sample,
  its pivot and its two slopes read from `CZ_RECIP[2048]` (`(1024 << 16) / x`, `tools/gen_tables.py`: the two divides
  `cz_native_pd` does once a block). `cz_native_noise` does the same in its one loop. The chip steps its envelopes at
  its sample rate (`cz_hw_rate`'s 40 kHz), so this is nearer to it than a block's step was.

After, the same matrix: **0 gain steps, 0 holes, 0 wraps, 0 jumps in `hold`**; 41 runs of 24 960 keep a jump, all at
C2 on AFRO PERC (19), SYN DRUMS 2 (13), SYNTH.BASS (4), CELLO (3), CONGA (2): two to six samples of a pulse's own
edge about 156 samples after a note-on, while the depth runs through its range at the fastest rate. They are in
main's renders too and are the tone, not a step of a block (inference from their place inside blocks and their
width; not compared with a CZ-1).

What changed in the sound. (A): a note struck again while it sounds no longer falls silent and attacks from nothing;
its level, its timbre and its pitch envelope move from where they are through the tone's steps. A sustained chord
struck again is 0.2 to 0.5 dB louder over the half second around the strike (the hole is gone), and a tone whose
first DCA step is its sustain level hardly re-attacks. This is a change of behaviour, not only the pop's removal;
a CZ's own envelopes are said to behave so (not checked on one). (B): the first few blocks of an attack, and any
block in which the DCW moves, differ in the samples where the depth used to be a block's and is now a sample's; level
and timbre are the same (the excerpts' RMS within 0.1 dB), the click is not there.

`../audio-0.19/cz-pops/` has the ten worst (before, a gap, after, in one WAV each) and `README.txt` with the sample
index and the size of each.

### Cost on the unit

Counted in `build/choralroot.dis` against a build of 19b6453, at 205 instructions a microsecond; a voice of a two-line
tone was about 16 100 instructions a half (79 us).

| | before | after |
| --- | --- | --- |
| `cz_native_line`, the 43 loops | 16 .. 42 a sample (median 35) | the same code (the listing differs in the carrier table's address only) |
| `cz_native_pd`, a block whose depth did not change | 21 | 33 |
| `cz_native_render`, a line's choice of loop and `n` to `cz_native_pd` | | about + 5 |
| **a voice a half, the depth at rest** | 16 100 (79 us) | + 8 x 17 = 136: **16 240 (79.2 us), + 0.8 %** |
| `cz_native_line_mv`, the 43 loops | | 16 .. 47 a sample (median 39): + 4.9 a sample on average |
| **a voice a half, both lines' depth moving in all four blocks** | 16 100 | + 8 x (157 + 23) = 1440: **17 540 (85.6 us), + 9 %** |
| `cz_note_on` | 59 instructions | 105 (once a note) |
| `cz_native_noise`'s loop (line 2 with NOISE) | 148 in the listing | 175 (one test a sample at rest) |

Eight voices: about 630 us -> 634 us at rest, 685 us with every depth moving. No divide was added to a loop. Flash:
XIP 458 228 -> 474 068 B (79.9 -> 82.7 %: the second set of loops 7.3 KB, the table 8.2 KB); RAM and POOL unchanged
(`cz_hw_pd_t` is on the render's stack). `tests/target_budget.txt`: `cz_native_render` 338 -> 360,
`cz_native_noise` 134 -> 161, `cz_native_line_mv` added (7333); `cz_native_line` 45 287 as before. The host
(`tests/regress.c`): JET ROAR 1264 -> 1288 instructions a sample. `tools/emu/perf.sh` (g): 142 789 / 228 163 host
instructions a block (avg / max) -> 143 787 / 235 538 (+0.7 % / +3.2 %; a second run of the same build: 143 749 /
238 881, the worst block moves that much between runs); its `wavclicks` line 5 clicks -> 4 (the
x440 burst at the chord's start is x59, the one at the chord change is gone; the three left are the bass's attack,
the same in both builds).

### What stayed bit for bit, and what did not

- Every other engine, the mix and the master: all 161 scripts of `tools/emu/scripts/` rendered with `--wav` through
  an emulator built from `git archive 19b6453` and through this tree's: **158 identical WAVs**. The three that differ
  (`cr_cz`, `cr_select`, `perf_g`) are the three that play CZ-1 notes.
- `tests/regress.c`: 132 of 197 goldens unchanged; **all 65 CZ-1 goldens changed** (their renders have a DCW attack,
  and 60 of them strike a sounding note again) and were rewritten. With both fixes switched off in a copy of this
  tree (`dstep` 0, the levels not kept) all 197 are main's: the two loops' set-up, the table and the hook change no
  sample.
- So a CZ-1 render is **not** bit-exact wherever its DCW depth moves, which is the condition that popped. A block
  whose depth is at rest runs the code it ran; its samples can still differ from main's by the DC filter's state
  (`dc`), which carries the attack's history and comes back to within one step of it.

### Level

The same held 6-note chord (D4 F#4 A4 B4 C#5 E5, velocity 100, LEVEL 92, the sends at 0, `CB_TRIM`'s three trims in),
`build/host/cz_pops level`: the part's peak after its level and the dry mix's RMS over 0.3 .. 2.9 s, 0 dB = 32 768
(full scale at MASTER's top). Sustained sounds only (RMS above -30 dB):

| engine | sounds | peak, median (range) | RMS, median (range) | velocity 127: peak / RMS |
| --- | --- | --- | --- | --- |
| FM6 (TINE EP, PAD, ORGAN) | 3 | -4.7 dB (-11.4 .. -1.8) | -20.4 dB (-21.5 .. -19.7) | -4.7 / -20.4 |
| VA (24 of 25) | 24 | -3.0 dB (-6.0 .. +3.3) | -15.6 dB (-29.4 .. -9.4) | -1.7 / -14.7 |
| FM TONE (13 of 16) | 13 | -3.8 dB (-10.8 .. +3.2) | -16.8 dB (-29.9 .. -5.3) | -2.1 / -16.5 |
| **CZ-1 (36 of 65)** | 36 | **+2.4 dB** (-5.4 .. +8.1) | **-12.3 dB** (-27.9 .. -2.4) | +3.0 / -11.8 |

CZ-1 is the outlier: 5.4 dB above the VA at the peak and 3.3 dB in RMS, 6.2 / 4.5 dB above FM TONE, 7.1 / 8.1 dB above
the FM6 rows; 40 of its 65 sounds peak above full scale into the mix (VA 3 of 25, FM TONE 2 of 16), and that is with
BRASS 1, STRINGS 2 and PIPE ORGAN 1 already trimmed by 5, 3 and 6 dB (+0.6, -0.5, -1.1 dB peak with their trims). The
loudest: INIT TONE +8.1 / -2.4, STRINGS 1 +4.7 / -6.2, PIPE ORGAN 2 +5.0 / -7.6, ORCHESTRA +5.7 / -8.1, STRINGS 4
+6.4 / -8.2. The reason is in the last lines of `cz_native_render`: a line is half scale (`(raw - dc) >> 1`) and
`dsp.c` says such an engine adds its voice `<< 1`; CZ-1 adds it `* 4`, and a ring-modulated voice twice that again
at its peaks. **A trim of -6 dB for the engine would level it** (median +2.4 -> -3.6 dB peak, -12.3 -> -18.3 dB RMS:
between the VA and FM TONE): `* 2` for `* 4` in the three sums, or 12 half-dB steps of `track_t.trim` for every CZ-1
sound; the three `CB_TRIM` rows would then go (they would sit 5, 3 and 6 dB under the rest). Not applied: the master
is being redone, and every CZ-1 sound's level changes with it.

### The unit: `tools/cz_pop_check.py`

A half rendered late (docs/FX.md "Late is not slow") is the other possible cause of what the owner heard, and the
unit's counters were never read after a CZ-1 session. `python3 tools/cz_pop_check.py` prints its plan and checks
that `fm1_install.py --info` reports a ChoralRoot (it refuses anything else); `--read` then reads SysEx 78 PEAK
and judges, for a session played by hand; `--play` selects CZ-1 STRINGS 2 and SYNTH.BASS, clears the counters,
plays 45 s over MIDI (eight notes held with the bass; six-note chords with common tones every 400 ms; eight notes in
turn every 60 ms: MIDI Perform and MIDI Bass on channels 1 and 2), reads and judges. `stale_halves` above 0 is late
audio (audible, and timing: its stage in `pk_late_stage`); `pk_late` above 0 alone is a late half the DAC did not
reach; `pk_part0_us` was predicted at about 630 us for eight voices (685 with every depth moving): above 1450 it is
far from the count, above 2000 a late half is near; all clean, and a pop heard in that stretch was computed.

## Tests

- `tests/cr_cz_test.c` (in `tests/run_cr_tests.sh`): the engine's number and place; Casio's 64 (valid, names, BANK /
  PTCH); every tone through the blob and back, a bad blob; every tone rendered (3 notes, velocity 127 / 1) within range
  and not silent; SysEx: a 144-byte dump in, the send request's dump out identical, a 128-byte dump, an invalid tone,
  no CZ-1 part; the banks (A..D Casio's, E..H empty, BANK / PTCH, a bank restored as backup object 18 and reloaded);
  the 37 pages (titles, sections, every column's min / max / default, the panel value it is, one byte for one R, the
  SUS / END rules, Casio's encodings); the tone store (save, load, reboot, the second object, clear, erase, a slot of no
  tone); the backup's objects 12..21.
  0.19: `CZ_RECIP` against its divide; the pops (a sounding note struck again steps no more than the held note and
  keeps its level; INIT TONE and the 64 let go and struck again at once and 60 ms later: no step at the new note's
  first sample; the 65 at C2: no step at a block's edge above twice the largest inside the blocks; `cz_native_pd`'s
  ramp). On main the three checks of the sound fail: 0 of the level kept, 62 of 130 re-strikes step (the worst
  34 552), 11 tones step at a block's edge (the worst 18 024).
- `tests/cz_pops.c`, `tools/cz_pops_report.py`: the matrix of "Pops" (a tool: `cc` line at its top).
- `tests/cr_backup_test.c`: 27 objects (12, 13, 14..21 listed; a saved bank and the store round trip).
- `tests/regress.c`: goldens and CPU baselines for INIT TONE and the 64 (additions only; nothing else changed).
- `tests/target_budget.txt`: `cz_native_render`, `cz_native_wave`, `cz_env_tick` added; the audio ISR's static count
  is 43986 (was 37094: 1880 -> 1882 instructions, the register allocation moved a loop head; its code is the same).
  0.18: `cz_native_wave` is inlined; `cz_native_line` (its 43 loops; the static measure weighs a loop in a loop four
  times: 45287) and `cz_native_noise` are watched instead. 0.19: `cz_native_line_mv` too ("Pops": Cost on the unit).
- `tools/emu/scripts/cr_cz.txt` (`test_cr.sh`): CZ BRASS 1, the editor's ENV screen 4 (DCW 1) under the cz band (shot),
  KNOB 1 on DCW 1's L1 (the `deep:` trace, step 1 lit), the chord darker after it (its HF share), the other groups;
  `cz_persist_*.txt` (`test_persist.sh`): an edited CZ-1 sound in U01 across a relaunch; `perf_g.txt` (`perf.sh`).
  0.19: `cr_cz_pop.txt` (`test_cr.sh`): STRINGS 2 with the CZ-1 bass, the chord struck again three times 20 ms after
  its release and changed twice in one frame; `wavclicks.py` from 3.4 s: 0 jumps, 0 holes, 0 clicks (main: 3 clicks,
  at the first re-strike and the first change).
- `web/test_backup.mjs`: ids 12..21, their names.
