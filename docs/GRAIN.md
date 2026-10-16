# GRAIN: a granular engine over material it makes itself (design, 2026-10-15)

**Status: a design and listenable sketches, no firmware code.** Branch `claude/grain-engine-f724f9` from main da1f8e2
(v0.22-beta3). The owner decides after hearing the sketches (`build/grain-sketch/`, below). The owner's words: "Could the
grain synth have a way to generate its own samples from a couple knobs to quickly get a variety of interesting samples to
work with?"

Felucca's GRAIN (`firmware/src/eng_grain.c`, engine 8) granulated the SAMPLE engine's ADPCM sets. ChoralRoot dropped
SAMPLE, GRAIN and DRUM on 2026-10-07 (flash, POOL) and kept slot 8 reserved. This GRAIN keeps Felucca's player, the
part it was good at (a grain pool shared by the voices, the Hann window, SIZE DENS POS SPRD RAND, the 1/sqrt(overlap)
level), and replaces its source: no samples, no decoder, no seek index. **Four knobs render the material into a short
RAM buffer: TYPE (a family of sound), TONE (its brightness or colour), MOTION (how it moves over its length) and SEED
(a variant).** The sound stores the four values, never audio; the same values render the same buffer, bit for bit, on
the host and the unit.

## In three sentences

A part on GRAIN holds a 743 ms buffer at 22.05 kHz (16 384 samples, 32 KB of POOL) that a small deterministic
macro-synth fills in about 4 .. 40 ms of core 0's main loop whenever a sound loads or a source knob turns, in slices,
while the grains keep reading the buffer they had. Up to 16 grains a part, shared by up to 8 voices, read it at the
note's rate against the buffer's root (C4), windowed, spread round POS, reversed and detuned by RAND, through a filter
and the platform's envelope: Felucca's player over RAM. The engine costs about 10 .. 11 KB of flash (the core is 7.4 KB
measured), 64 KB of POOL for two parts, nothing of RAM to speak of, and at its worst about 540 .. 800 us of a 2902 us
half for a full part, a quarter of a VA pad's.

## 1. The source generator

### The buffer

| | rate | samples | ms | bytes a part | two parts | POOL after (now 238 936 B, 69.4 %) |
| --- | --- | --- | --- | --- | --- | --- |
| **proposed** | 22 050 | 16 384 | 743 | 32 768 | 65 536 | 304 472 B, **88.5 %**, 39.6 KB left |
| a second | 22 050 | 22 050 | 1000 | 44 100 | 88 200 | 327 136 B, 95.1 %, 16.9 KB left |
| shorter, three buffers | 22 050 | 12 288 | 557 | 24 576 | 73 728 (3) | 312 664 B, 90.9 %, 30.7 KB left |
| full rate, half a second | 44 100 | 22 050 | 500 | 44 100 | 88 200 | 95.1 % |

**22.05 kHz**: the delay and the reverb already run there (docs/FX.md "The rates"); a grain played two octaves up
reads the buffer at two source samples an output sample (Felucca's cap, kept), so the material's 11 kHz top is the
output's 22 kHz there; granular textures do not miss the top octave, and the render costs half. **743 ms** is a
power of two of samples (the position arithmetic stays shifts), longer than the longest grain (393 ms) by nearly two,
and leaves POOL at 88.5 % with two buffers. **Two buffers, one a part**: the normal unit has one part on GRAIN (the
chord), and then the other part's buffer is free and serves as the render's scratch: the new material lands there,
the pointers swap at a block boundary, grains in flight finish on the old buffer, and the next render waits until no
grain holds it (at most 393 ms). With both parts on GRAIN there is no scratch: that part's render goes in place with
a fade (the part's grains fade over one block, the buffer renders, the grains come back: 20 .. 60 ms of quiet on a
source knob turn, only in that case). The alternative, always seamless, is three buffers of 557 ms (90.9 %); or two
buffers and both parts sharing the chord part's material (open question 2).

### The four knobs

| knob | range | what |
| --- | --- | --- |
| **TYPE** | 8 families (a picker, F_ENUM) | PLUCK DRONE BELL BREATH CHIRP SWEEP VOWEL METAL |
| **TONE** | 0 .. 127 | the brightness or colour within the family |
| **MOTION** | 0 .. 127 | how the material moves over its 743 ms: decay, sweep, vibrato, re-plucks, strums; material that is interesting to granulate has movement inside it, and a still buffer sounds like a looped sample |
| **SEED** | 0 .. 127 | a variant: ratios, detunes, phases, noise, the partials, the vowel order; the same SEED is the same material |

The eight families, written from the firmware's own blocks (dsp.c: `osc_saw` with its polyBLEP, `osc_pulse`, `osc_tri`,
`sine_i`, the trapezoidal SVF (`tsvf_lp` and a band-pass tap of the same filter), `noise32`, `softclip`, `soft_knee`;
felucca_tables.h `pitch_inc`, `SVF_G`), all int32, every family one loop over the samples with a control tick every 32:

| TYPE | what it is | TONE | MOTION | SEED | instructions a sample (device listing) | render, ms at 205 / us (x 1.5: the unit's factor so far) |
| --- | --- | --- | --- | --- | --- | --- |
| PLUCK | a Karplus-Strong string, the buffer its own delay line (period 84 samples: C4 + 6 ct) | the burst's and the loop's brightness | the decay; above 64 a strum of re-plucks (two .. five) | the burst | 49 | 3.9 / 5.9 |
| DRONE | three detuned saws (the third a pulse on odd seeds) and a sub sine through the SVF, its cutoff swept by a triangle LFO | the cutoff's centre | the detune (0.1 .. 2.4 %) and the sweep's rate (0.25 .. 4 Hz) | phases, the pulse's width, the resonance | 327 | 26 / 39 |
| BELL | two-operator FM, the modulator at an inharmonic ratio, the index decaying | the index (0.4 .. 8 rad) | the index's decay (600 .. 40 ms); above 64 a second pair beats against the first | the ratio (twelve) | 100 | 8.0 / 12 |
| BREATH | white noise through a resonant band-pass whose centre wanders under a tremolo | the centre (300 Hz .. 6.5 kHz) | the wander's depth, the tremolo's rate and depth | the walk, the resonance | 129 | 10 / 16 |
| CHIRP | a sine sweeping a span of semitones, repeated; a second one a fifth up on some seeds | the span (6 .. 57 semitones) | chirps a buffer (1 .. 16) | up / down / alternating, the second voice | 103 | 8.2 / 12 |
| SWEEP | a saw and a pulse (an octave down on some seeds), driven, through the SVF sweeping | the resonance and the sweep's top | sweeps a buffer (1 .. 8) | the pulse's width, the sweep's shape (down, up, both) | 240 | 19 / 29 |
| VOWEL | a narrow pulse with vibrato through three band-passes at a vowel's formants, travelling round A E I O U | where on the cycle it starts | how far it travels (still .. two rounds) and the vibrato's depth | the vowel order, the vibrato's rate | 296 | 24 / 36 |
| METAL | six sine partials at inharmonic ratios, each decaying at its own rate, a noise strike | the partials' spread (1.5 .. 8) and the strike | the decay (900 .. 80 ms) | the ratios, the levels | ~187 | 15 / 22 |

Every render ends with one pass (`gs_finish`, 23 instructions a sample, 1.8 ms): a one-pole high-pass at 20 Hz takes
the DC out (PLUCK's loop and SWEEP's pulse carry some) and the buffer is normalized to a peak of 28 000 (a gain of at
most 8). So every buffer has the same peak; the RMS differs by family (CHIRP 0.59, PLUCK 0.12: table in
`build/grain-sketch/INDEX.md`), which the part LEVEL and the presets' trims take care of as for every engine.

The code is `tools/grain_sketch.c` (`gs_*`), written as the firmware would have it: `gs_begin(knobs)`, `gs_step(n)`
renders the next n samples from the state it keeps, `gs_finish()`. The firmware takes the same functions; the
sketches' WAVs are the goldens of step 1.

### When and where the render runs

- **When**: a sound loads with GRAIN, the part's engine switches to GRAIN, or one of TYPE TONE MOTION SEED changes
  (the editor's set, the macros' P_E, the matrix never: the source knobs are not modulation destinations; a render
  is not a per-tick thing). The engine's `block()` hashes the four values; a change posts a request. **Latest wins**:
  a request during a render restarts it on the new values at its next slice; a knob turned through ten detents
  renders once or twice, not ten times.
- **Where: core 0's main loop, in slices.** `main.c`'s loop draws a frame every 15 ms and waits the rest of the
  frame in an idle loop (`while (fm1_ms - m < 15u) { ui_input(); ed_service(); }`). A `cr_grain_poll()` there
  renders one slice of 1024 samples a turn (DRONE, the heaviest: 1.6 ms, 2.4 with the unit's factor; PLUCK 0.25 ms)
  and returns, so the keys are still read every slice; the idle loop runs several slices a frame. The audio ISR
  preempts the main loop as it preempts a frame's drawing: **the audio never waits for the render**. A whole buffer
  is 16 slices: DRONE lands in 26 .. 40 ms of main-loop time, inside two or three frames; PLUCK in one.
- **The swap**: the slices write the scratch (the other part's buffer, or in place with the fade: above). When the
  render is done, `block()` (the audio ISR) sees the ready flag at a block boundary and takes the new pointer; the
  grains in flight hold their own `src` pointer and finish on the old buffer; the next render waits for the pool to
  have no grain on the old buffer (`gp_part_t` counts them). Nothing is copied.
- **Core 1** is the alternative, not the first build: it is nearly idle (the bass's render 300 .. 500 us and the
  reverb's job 200 of each 2902 us half), so a third job "render the next 2048 samples" in each block would land
  DRONE in eight blocks (23 ms) with no main-loop cost at all; but core 1 is optional (Dual Core Off, the single-core
  package, a unit that gave it up), so the main-loop path has to exist anyway. Core 1 can take the slices later
  with the same `gs_step`.
- **Latency on a knob turn**: a detent's render reaches the ear 30 .. 60 ms later (the slices, then the swap at a
  block boundary, then the grains that start next). While turning, the old material plays: no gap, no click.

## 2. The grain player

From `eng_grain.c`, what stays, what goes, what changes:

| | Felucca's GRAIN | this GRAIN |
| --- | --- | --- |
| source | ADPCM zones in flash, a zone per key range, decoded on the fly from a seek index built in the block hook | one int16 buffer in POOL, written by the generator; no decoder, no index, no zones |
| the pool | GR_NG 12 grains a part shared by GR_POLY 3 voices (a voice's share GR_NG / voices, at least 2) | **GP_NG 16 grains, GP_POLY 8** (ChoralRoot's chords are eight notes); the share rule kept: 16 / voices, at least 2 |
| a grain | position, length (SIZE 10 .. 393 ms), rate Q16, direction, Hann window (257-point table, linear interpolation), level 1 / sqrt(overlap); the window times the level ramps linearly over the block | **kept as it is** (`gp_run` is `gr_run` with `src[pos++]` for the decode); a reverse grain reads `src[--pos]`: Felucca's 128-sample reverse windows go |
| the rate | the note against the zone's root, PITCH, a random detune (RAND, quadratic, up to +-1 octave), capped at 2 source samples an output sample | the note against the buffer's root (GS_ROOT16 = C4, PLUCK's 961), PITCH -24 .. 24, the detune as before, times 22 050 / 44 100; the cap kept: **the range is C1 .. C6 from one buffer** (two octaves up from the root at the cap; downwards unlimited) |
| the start | DENS a second (1 .. 98), SPRD jitters the interval; POS (+ the track's SHP modulation) the centre, SPRD the random spread; at most one start a voice a block | the same; POS also moves by **SCAN** (a slow triangle over the buffer, 0 off .. a round in 0.5 s: the frozen-scan and the wandering-pad movements of the sketches) and still by the matrix (SHP -> POS) |
| RAND | the detune and the share of reversed grains in one knob | split: **RAND** the detune, **REV** the share of reversed grains (0 .. 100 %) |
| TONE | a one-pole low-pass a voice (+ the track's FLT modulation) | **CUT, RES**: the SVF low-pass a voice (`tsvf`), the FILT group's band drawn; the track's FLT modulation adds to CUT |
| the cost bound | the part's decode load capped at 8 source samples an output sample | none needed: a grain costs the same at any rate; the pool's size is the bound |
| state | `gr_part_t` in its own pool section (27 KB with the index and the reverse windows) | `gp_part_t`: 16 x 40 B + 16 B = 656 B, a member of `eng_state`'s union (FM6's 6.9 KB: no growth); a voice's `gp_voice_t` (next-grain countdown, the filter) in `voice_t.s[]` as Felucca kept it |
| GR_POLY | 3 | 8 (the budget below) |
| voices.c's view | `sampled 1` (positions, no phases), `keep` the filter | the same |

**Why one root and not two or three.** A sample set needs zones because a recording repitched three octaves is a
different instrument. Here the material is synthetic and the player's repitch is the granular sound (formants and
decays shift with the note: a chord of grains is meant to sound like that), the generator's tuning is exact at the
root, and C1 .. C6 covers the chord part and the bass. A second buffer at another root would double POOL for a
difference the owner may not want; **if a note above C6 is wanted, the generator can render the buffer at the
chord's root instead** (a render on every chord change: 4 .. 40 ms of main loop, acceptable for the PLUCK .. CHIRP
families, slow for DRONE) as a later mode, not now.

## 3. The pages, presets, slot, records

### The knobs: twelve values, three rows of four, plus the filter

The eight `P_E0 .. P_E7` are **TYPE TONE MOTION SEED | SIZE DENS POS SPRD** (EDIT 1: the source; EDIT 2: the
grains; `engine_t.knob` = TYPE TONE MOTION SEED). The whole sound is a small patch the engine owns, `eng_deep_t` as
the VA's, the eight P_E macros into it (`block()` picks a macro change up, `set` writes the macro back, as the VA):

| page (the editor's title rules, docs/EDITOR.md section 4) | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | group / screen |
| --- | --- | --- | --- | --- | --- |
| `SOURCE` | TYPE (picker: PLUCK .. METAL) | TONE | MOTION | SEED | OSC screen 1, one lane, **the wave band** (below) |
| `GRAIN` | SIZE (10 .. 393 ms) | DENS (1 .. 98 /s) | POS (0 .. 100 %) | SPRD (0 .. 100 %) | OSC screen 2, lane A, the wave band |
| `GRAIN+` | RAND (detune) | REV (reversed %) | PITCH (-24 .. +24 st) | SCAN (off .. fast) | OSC screen 2, lane B |
| `FILTER` | CUT | RES | – | – | FILT screen 1, the filter's curve (the title rules draw it for a CUT column) |
| ENV, LFO, MOD, FX, MIX | the platform's | | | | as WHEEL's: the ADSR (`P_ATK ..`), the track LFO, the four routes and EXPR |

Three rows of four and a filter row: the owner's grammar (up to four cells a row, the knob colours, the active row
highlighted, SELECT walking the lanes). No matrix of its own ("not modulatable"); the platform matrix's SHP reaches POS
(Felucca's binding) and FLT reaches CUT.

**The wave band** (`ENG_B_WAVE`, new in `cr_draw.c`; the SOURCE and GRAIN screens): the buffer drawn as its envelope
across the 240 columns (68 samples a column: a min / max scan of the live buffer at frame time, 16 384 reads, ~0.25
ms, no RAM), POS as a bright mark and SPRD as a dimmer span in KNOB 3's and KNOB 4's colours on the GRAIN screen,
and **the live grains as ticks** moving across the band (each grain's position read from the pool at frame time, a
tick's height its window). On the SOURCE screen the band redraws when a render lands: a knob turn visibly reshapes
the material. One big thing per screen, animated.

### Presets

A pool for the engine (docs/PRESETS.md: INIT at 00, then the factory presets, then the user's), the engine after
WHEEL in `ENGINE_ORDER` and the picker's keys. INIT: DRONE 64 / 64 / 0, SIZE 92 DENS 88 POS 50 SPRD 40, RAND 14 REV 0
PITCH 0 SCAN 30, CUT 100 RES 20 (the sketches' "pad" grains). Eight factory presets to start, by ear with the owner:
CLOUD PAD (DRONE), SHIMMER (BELL, PITCH +12), BREATH PAD (BREATH), FROZEN (VOWEL, SPRD 0, SCAN slow), STRUM (PLUCK,
MOTION 100), SWARM (CHIRP, RAND 60), ACID DUST (SWEEP, small grains), GLITCH (METAL, SIZE 24 DENS 112 SPRD 100 REV 50).
Each with its ADSR and sends (`preset_t`) and its patch blob (`blob_preset`).

### The slot and the records

- **Engine 8** again: `ENGINES[8] = &ENG_GRAIN`, `FELUCCA_GRAIN 1` in choralroot.c and the emulator, the bit out of
  `ENG_RETIRED`, `NENG_SHOWN` follows. `engines.c`'s `#error "GRAIN and SLICE play the SAMPLE engine's sets"` goes:
  this GRAIN needs no SAMPLE. The source file: `eng_grain.c` replaced (Felucca's player lives on inside it, its
  decoder goes to git history), or the new engine beside it as `eng_grain_syn.c` with Felucca's kept for Felucca's
  renders (`tests/golden.txt` has `preset/GRAIN/00_CLOUD_PAD ..` from the old engine): the orchestrator's call, step 4.
- **The blob**: `'G'`, version 1, then TYPE TONE MOTION SEED SIZE DENS POS SPRD RAND REV PITCH SCAN CUT RES (14
  bytes, PITCH signed), zeros to 16; `eng_deep_t.blob_size` 16 (`upool.c` UPP_MAX 188: fine). **The sound stores
  the knobs, never audio.** A user record, a backup (`cr_backup.c`), a SysEx sound export and the web manager
  (docs/SOUNDS.md: patch kind `grain`) carry the 16 bytes as any engine's blob.
- **Old records of engine 8** (a Felucca unit's, or ChoralRoot's "retired: stand-in" cases): their P_E were SRC POS
  SIZE DENS PTCH SPRD RAND TONE; a record with no blob loads INIT's source with the record's SIZE DENS POS SPRD
  PITCH RAND in the new places and TONE as CUT (`cr_ui.c cu_load_user`, where the stand-ins are), so an old GRAIN
  sound still granulates something of the right shape.

## 4. Budgets (measured where the host can measure)

Counted as docs/FX.md "The cost on the unit" counts: the device compiler's listing (`python3 tools/grain_cost.py`:
`tools/grain_sketch.c`'s core compiled alone with the firmware's `-Os` for pi32v2, `build/grain-cost/unit.dis`), 205
instructions a microsecond, and the unit's factor of 1.2 .. 1.7 over the count seen so far (the fx stage 1.2 .. 1.3,
CZ-1 1.7, P5 1.54) given as x 1.5. The baseline is main da1f8e2's size line: XIP 547 020 B of 573 440 (95.4 %), RAM
78 652 of 98 304 (80.0 %), POOL 238 936 of 344 064 (69.4 %).

### Flash

| piece | bytes | from |
| --- | --- | --- |
| the generator: `gs_begin` (the eight families' set-up, inlined) | 1246 | the listing |
| the eight family loops | 4514 (PLUCK 198, DRONE 1124, BELL 368, BREATH 460, CHIRP 386, SWEEP 846, VOWEL 950, METAL 182) | the listing |
| `gs_step`, `gs_finish`, `pow2_q16` | 348 | the listing |
| the player: `gp_spawn`, `gp_run`, `gp_voice_block`, `gp_voice_on`, `gp_init` | 1280 | the listing |
| **the core** | **7420 B of .text** (7130 before the per-family split) | `objdump -h` |
| its own tables: the Hann window 514, the norms 34, the ratios 24, the vowels 30 | ~600 B of .rodata (the listing's 4192 includes `SINE`, `TANH_Q15`, `SVF_G`, `PITCH_OCT`, which the firmware has) | |
| the engine's glue: `engine_t`, the pages and `eng_deep_t`, `block()` with the request / swap, the blob get / set, the presets (9 x ~24 B), the old-record mapping | ~2.5 .. 3 KB | estimate from the VA's and QUAD's glue |
| the wave band in `cr_draw.c` | ~1 KB | estimate (the filter band is 0.8 KB) |
| **the engine** | **~11 .. 12 KB: XIP 547 020 -> ~559 000 B, 97.4 .. 97.6 %, 14 .. 15 KB left** | |

The tightest resource. Not measured: a build with the engine in. Step 4 reads the size line; `FELUCCA_SIZE`'s minsize
pass (tools/size_fns.py) can take the generator's set-up code (`gs_begin`, 1.2 KB, runs once a render).

### POOL and RAM

| piece | bytes | where |
| --- | --- | --- |
| two buffers | 65 536 | POOL: 69.4 % -> **88.5 %** |
| `gp_part_t` x 2 (the pool of 16 grains, the pointers) | 2 x 656 | `eng_state`'s union (no growth: FM6's member is 6.9 KB) |
| the generator's state `gs_t` (one render at a time) | ~220 | .bss |
| the patch x 2 parts, the macros as last seen | 2 x 32 | .bss |
| **RAM** | **~300 B: 80.0 % -> 80.3 %** | |

### CPU

| piece | instructions | per | us a half (x 1 / x 1.5) |
| --- | --- | --- | --- |
| a grain, forward (`gp_run`'s loop body 26, the fetch loop 9 taken 0.5 .. 2 times a sample at rates 0.5 .. 2) | 30 .. 44, 37 typical | an output sample | **23 / 35** |
| a grain, reverse | the same (26 + 9 a fetch) | | 23 / 35 |
| the pool full, 16 grains | | | 370 / 555 |
| a voice's block: its filter and clamp loop 33 a sample, the pool scan and the start ~60 a block | 33 | a sample | 21 / 32 |
| the platform's `voice_amp` on the voice's output | ~10 | a sample | 6 / 9 |
| 8 voices | | | 220 / 330 |
| a grain start (`gp_spawn`: two `pow2_q16`, the fits) | ~150 | a start, at most one a voice a block | 8 voices: 6 / 9 |
| **a part at its worst: 8 voices, the pool full** | | | **~600 / ~900 (21 / 31 %)** |
| CLOUD PAD's chord (SIZE 92: 142 ms grains, DENS 88: 24 a second: 3.4 grains a voice; 6 notes fill the pool) | | | ~550 / ~830 |
| GLITCH's chord (SIZE 24: 20 ms, DENS 112: 61 a second: 1.2 a voice; 8 notes: 10 grains) | | | ~450 / ~680 |

Against the unit's readings (docs/INTEGRATION.md "Measured on the unit": the chord part's worst half fm6 1647, va
2293, cz 1439, quad 1509 us with 8 notes): **GRAIN would be the lightest chord engine, at a third of FM6's and a
quarter of the VA's worst.** The pool's size is the bound whatever the knobs do: DENS and SIZE change how many grains
sound, never past 16.

### The render burst

| TYPE | ms a buffer (x 1 / x 1.5) | slices of 1024 (ms a slice, x 1.5) |
| --- | --- | --- |
| PLUCK | 3.9 / 5.9 | 16 x 0.4 |
| BELL | 8.0 / 12 | 16 x 0.7 |
| CHIRP | 8.2 / 12 | 16 x 0.8 |
| BREATH | 10 / 16 | 16 x 1.0 |
| METAL | 15 / 22 | 16 x 1.4 |
| SWEEP | 19 / 29 | 16 x 1.8 |
| VOWEL | 24 / 36 | 16 x 2.2 |
| DRONE | 26 / 39 | 16 x 2.4 |
| + `gs_finish` | 1.8 / 2.7 | one pass |

In the main loop's idle time: no cost to the audio half; a UI frame slips by one slice at most (2.4 ms of a 15 ms
frame). On the host (`tools/grain_sketch.c`, -O2, Apple silicon) a buffer renders in 0.1 .. 0.3 ms: the host says
nothing about the unit.

## 5. Risks and open questions for the owner

Risks:

1. **Flash**: the engine takes about half the XIP left (26.4 KB -> 14 .. 15 KB). The core is measured; the glue and
   the band are estimates until step 4's build. If XIP is wanted for other things, the generator can lose a family
   (DRONE 1.1 KB, VOWEL 0.95 KB, SWEEP 0.85 KB are the large ones) or the band can wait.
2. **Both parts on GRAIN**: the in-place render with a fade (above), or three buffers, or a shared buffer (question 2).
3. **The render on a knob turn** is 30 .. 60 ms behind the hand (the slices, the swap, the next grain starts) and the
   old material plays until then; turning SEED fast sounds like stepping through variants a few frames late. If the
   owner wants it tighter, core 1's job lands DRONE in 23 ms and the lighter families in a block or two.
4. **Levels**: every buffer peaks the same, the RMS differs by family (0.12 .. 0.59); the presets' LEVEL and `CB_TRIM`
   even them, as for the other engines; `tests/va_levels.c`-style measurement in step 6.
5. **Aliasing**: a grain two octaves up reads the 22 kHz buffer at two samples an output sample with linear
   interpolation and no filter: the material's top folds (Felucca had the same cap at the same rate ratio for a
   22 kHz zone). Audible on BREATH and METAL high up; CUT tames it; the owner judges on the unit.
6. **Determinism**: the generator is integer code with no float and no libc; the same input renders the same bytes on
   the host and on pi32v2 (step 1's golden test proves the host side; the unit's buffer can be read back through the
   console or SysEx and hashed for the proof).
7. **Felucca's goldens**: `tests/golden.txt` and `tests/cpu_baseline.txt` have the old GRAIN's four presets; replacing
   `eng_grain.c` re-records those four lines (GOLDEN_UPDATE / BUDGET_UPDATE, the diff reviewed), or the new engine
   lives beside the old one under its own flag.

Open questions:

1. **The buffer**: 743 ms at 22.05 kHz, two buffers (88.5 % POOL; both parts on GRAIN re-render in place with a
   fade), or three of 557 ms (90.9 %, always seamless), or two of 1 s (95.1 %)? Proposed: the first.
2. **A second part on GRAIN**: its own material (two buffers, the fade when both render) or the chord part's
   material shared (its source knobs follow the chord part's; its grain knobs its own)? Proposed: its own.
3. **The families**: which of the eight to keep, drop or replace after hearing `seeds-*.wav`; the names (PLUCK DRONE
   BELL BREATH CHIRP SWEEP VOWEL METAL are five characters or fewer but BREATH, which the picker can show as BREATH
   in its big type and the cell as BRTH); the knob names TONE MOTION SEED (or COLOR, MOVE, VAR).
4. **SEED's knob**: 128 variants on a knob, or fewer (16, each a detent) with OPT + the knob rolling a new one? The
   sketches use 0 .. 127; eight are in the tours.
5. **A FREEZE / LIVE type later**: the buffer filled from the live output (the chord part's dry mix, or the master)
   instead of the generator, the same player over it: a ninth TYPE and a capture path (`fx.c` mix -> the scratch
   buffer, 743 ms, taken on a button or continuously as a ring the grains read behind the write point). Not in
   this design's steps; the player and the buffer are ready for it.
6. **GP_POLY 8 with 16 grains** (two a voice at eight notes: a thin pad at full chords; 24 grains would cost
   +185 / +280 us) or 6 voices as the sketches play? Proposed: 8 and 16, then listen on the unit.
7. **The render's home**: the main loop (proposed; works on every unit) or core 1 first?

## 6. Steps, each with its test

1. **The generator in the firmware's tree**: `firmware/src/grain_src.c` from `tools/grain_sketch.c`'s `gs_*` (the
   sketch tool then includes it instead of its own copy). Test `tests/cr_grain_test.c` section 1: every family at a
   grid of TONE x MOTION x SEED renders (no sample past the clamp, DC under 0.5 %, peak 28 000), slices of 512 equal
   one call of 16 384 bit for bit, the 24 sketch settings hash to goldens recorded from the sketches' WAVs, and the
   host time a family (information). `./build.sh` with the file compiled but unreferenced: nothing in the size line.
2. **The player as an engine on a static buffer**: `eng_grain.c` (`gp_*`, `engine_t`: `note_on`, `render`, `block`,
   `poly 8`, `sampled`, `keep`), the buffer filled once by the generator at boot. Test section 2 (a `cr_va_test`-style
   driver): no grain reads outside the buffer at any SIZE / POS / SPRD / PITCH / REV, the pool's shares at 1 .. 8
   voices, the window's continuity across blocks (no step at a block boundary), the rate cap, 1 s of a 6-note chord
   on each planned preset: no int32 wrap, peak under 0.9 FS. `tests/regress.c`: the engine's goldens and CPU lines.
3. **The render scheduler**: the request from `block()` (the hash of the four knobs), `cr_grain_poll()` in
   `main.c`'s idle loop (1024-sample slices), the scratch rule (the other part's buffer, or in place with the
   fade), the swap at a block boundary, the wait for the old buffer's last grain, latest wins. Test section 3: knob
   changes mid-play on the host driver with a fake main loop: every block's output continuous (no sample jump over
   a swap beyond a window's slope), the old buffer untouched while a grain holds it, ten requests in a row render
   at most twice; `tests/cr_c1_thread_test.c`-style: the ISR and the poll from two threads, ThreadSanitizer clean.
4. **The platform**: `ENGINES[8]`, `FELUCCA_GRAIN 1` (choralroot.c, the emulator, regress), `ENG_RETIRED`,
   `ENGINE_ORDER` after WHEEL, `eng_state`'s member, the blob (`eng_deep_t`, `upool.c`, `cr_backup.c`, SysEx, the
   installer / manager tools, docs/SOUNDS.md), the pool and the presets (`cr_bank.c`), the old engine-8 record
   mapping, the deep pages. Tests: `cr_engine_test`, `cr_backup_test`, the pool tests, `test_retired.sh` (an engine-8
   record of the old GRAIN loads as INIT + its grain knobs), `descdump` -> `web/test_web.mjs`'s mock, `./build.sh`'s
   size line read and written down.
5. **The editor**: the SOURCE / GRAIN / GRAIN+ / FILTER pages on the unit's grammar, the wave band (`cr_draw.c`
   `CR_W_WAVE`: the envelope, POS, SPRD, the live grains), the picker's key. Tests: `tools/emu/scripts/cr_grain.txt`
   (each knob, the band's redraw after a render, SELECT through the lanes), `tests/run_cr_draw.sh` golden,
   `tests/cr_layout_test.c` if a layout file is added (`design/layouts/grain.json`: the owner may lay the rows out in
   the layout tool as P5's).
6. **The unit**: flashed by the orchestrator with a backup first; `tools/perf_unit.py` scenario `grain` (8 notes +
   bass, CLOUD PAD and GLITCH: `part0` worst and average against this document's 600 / 900), the render's time read
   from a counter (73 DEBUG: the longest slice, the longest render), the owner's listening; the presets by ear;
   `tests/va_levels.c`-style levels for `CB_TRIM`. docs: HISTORY, HANDOFF's state line, PRESETS.md's pool list,
   EDITOR.md's engine table, INTEGRATION.md's engines, MANUAL.md's engine page.
7. **Later, each its own piece**: the render on core 1; a FREEZE / LIVE type; a key-tracked render (the buffer at
   the chord's root) if the owner wants notes above C6.

## The sketches

```
cc -O2 -w -Ibuild/gen -o build/host/grain_sketch tools/grain_sketch.c -lm && build/host/grain_sketch build/grain-sketch
```

(`build/gen/felucca_tables.h` from `./build.sh` or `python3 tools/gen_tables.py build/gen/felucca_tables.h`.) 104
WAVs and `INDEX.md` in `build/grain-sketch/`, 22 MB, rendered in 0.2 s:

- `src-<family>-<setting>.wav`: the buffer as rendered, 743 ms at 22.05 kHz, for the settings `mid` (TONE 64 MOTION
  64 SEED 0), `dark-slow` (28 / 20 / 3) and `bright-fast` (104 / 104 / 11).
- `seeds-<family>.wav`: TONE 64 MOTION 64 with SEED 0 .. 7 in a row, 100 ms apart: what SEED gives a family.
- `gran-<family>-<setting>-<grains>.wav`: the buffer granulated by the player (`gp_*`), a C3 G3 C4 E4 chord held 2.2
  s of 3 s at 44.1 kHz, no effects, a plain 30 / 400 ms envelope, normalized to -2 dBFS; the grain settings `pad`
  (SIZE 92 DENS 88 POS 50 % wandering +-20 % at 0.3 Hz, SPRD 40 RAND 14 CUT 100 RES 20: Felucca's CLOUD PAD),
  `glitch` (SIZE 24 DENS 112 SPRD 100 RAND 90: half reversed, detuned up to an octave, CUT open: Felucca's GLITCH)
  and `frozen` (SIZE 108 DENS 72 SPRD 0 RAND 10 CUT 92, POS scanning 10 -> 90 % over the three seconds).

Listening order: the seed tours (is the family worth having), then `mid` against `dark-slow` and `bright-fast` (do
TONE and MOTION move it), then `gran-*-pad` (the engine as a pad), `-frozen`, `-glitch`. `python3
tools/grain_cost.py` prints the budgets' table from the device listing (Docker, as `./build.sh`).
