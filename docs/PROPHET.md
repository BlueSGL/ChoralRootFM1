# ChoralRoot FM-1 — PROPHET

PROPHET is **engine 16** (`ENGI_PROPHET`): **Melodee's** Prophet-5 engine (Kerem Kilic's fork of Felucca,
<https://github.com/keremimo/melodee>, GPL-3.0-only; its code name `p5_`). Native Sequential programs (the 133 raw
bytes of a program dump), two band-limited oscillators with hard sync and audio-rate Poly-Mod into a four-pole ladder
with a tanh knee in two characters (Rev 1/2: SSI; Rev 3: Curtis), two ADSRs, the wheel's modulation source, five
voices. It came here by way of a remix of ChoralRoot 0.14-dev that Rosen sent ("the Prophet 5 Engine from Melodee
that was added sounds really nice and is perfect for chords").

**This is phase A** (0.22 in development, 2026-10-13): the engine plays on the host and in the emulators, its sounds
browse like any engine's, its editor is complete, and its cost on the unit is **counted, not measured**. Nothing of
it has run on an FM-1. Not in this phase: a store for user programs (SAVE is refused), backup ids, the web manager,
a release note. [Open decisions](#open-decisions-the-owners) lists what waits for the owner.

| file | what | from |
| --- | --- | --- |
| `firmware/src/eng_prophet_test.c` | the engine: voices, envelopes, the sample loop, the wheel's source, the part's DC blocker; ChoralRoot's load hook, blob, deep pages | Melodee 8a3074b (+ 7cd66fb's DC blocker, 1bc4555's unpacking), the remix's three optimisations, ChoralRoot |
| `firmware/src/prophet_patch.c` | the program: the 133 raw bytes and their indices, INIT, SysEx decode / encode (model 0x32 / 0x31, 133 / 128 bytes) | Melodee, as it is (8a3074b = 1.2.1, byte for byte below the header) |
| `tools/gen_tables.py` `p5_tables` | the engine's nine tables and `P5_KEYTRACK_HALF_Q7` | Melodee 8a3074b's expressions |
| `tools/gen_prophet_factory.py`, `assets/prophet5-factory/` | Sequential's 200 factory programs v1.03 -> `build/gen/melodee_prophet_factory.h` (`tools/build.py` generate()) | Melodee 1.2.1 (the packed form), + the PRESETS names |
| `tests/cr_prophet_test.c`, `tests/run_prophet_test.sh` | the host test | ChoralRoot |
| `tools/p5_cost.py` | the cost on the unit, counted from the device compiler's code | ChoralRoot |
| `tools/prophet_ab.c`, `tools/prophet_listen.sh` | one engine file against another; the listening files | ChoralRoot |

## Where it comes from

Melodee's history of the engine: c7f1bea (prototype), 1fbeccd (playable), b351a14 (calibrated to a Rev-4 demo, the 200
factory programs), **8a3074b** ("Lo Freq B seven octaves, free-running phase, smoother saturation": the last INTEGER
version), f12b310 (experimental dual core: only scaffolding around the same sample code), fa64ef0 (float on the
hardware FPU), 7cd66fb (the part's DC blocker), 1bc4555 (the bank packed), 1.2.1. ChoralRoot's build has no FPU flags
and keeps none, so **the DSP is 8a3074b's**, statement for statement, with these differences:

| difference | why | the samples |
| --- | --- | --- |
| the remix's stage 36, three optimisations (`P5_STAGE36` bits): (1) an oscillator with no waveform on computes nothing; (2) the envelope's terms of the cutoff, the Poly-Mod frequency and width once a sample instead of once an oversampling pass; (4) hard sync's two phase products as 32-bit halves instead of a 64-bit multiply | fewer instructions | **bit-identical**, each alone and all three: `tests/run_prophet_test.sh` renders the 201 presets and 683 stress programs with `P5_STAGE36` 0, 1, 2, 4, 7 and compares every sample's digest |
| the part's DC blocker (`p5_post`, Melodee 7cd66fb: integer code written after the float change, about 5 Hz) | without it 59 of the 201 presets hold a DC of more than a quarter of their RMS in a held chord (the worst 47 878 on a scale where a voice is 24 000): it would ride into the sends and the limiter | changed on purpose (`P5_DC_BLOCK=0` gives 8a3074b's output for a measurement) |
| no `.dsp_text` / `.dsp_tables` sections: the code runs from flash, the tables are `const` | ChoralRoot's map has no such sections, RAM is the tight region, and RAM changed nothing for the effects' loops on the unit (docs/FX.md `CR_FX_RAM`). `P5_RAM=1 ./build.sh` puts the three hot functions into `.ram_text` (3796 B of RAMTEXT, it links: not run on a unit) | the same |
| the pitch bend: Melodee's `p5_update_bend` (voice.c: the program's own range byte) is not here | the part bends as ChoralRoot's other engines do, through `vmod_t` (voice.c: the channel's range, RPN 0, ±2 semitones). The program's `P5_BEND` byte is kept and unused | tested: +2 / −2 semitones measured |
| the matrix's ENV source is 0 (Melodee's voice.c asks `p5_env_source`) | as FM6's and CZ-1's here; voice.c is not touched | none on a factory program |
| a load does not take the program's UNISON switch (Melodee: `V_UNISON` whenever the program says so) | the chord part stays POLY ("a chord on a mono preset", `cr_ui.c cu_load`), the bass part MONO: a chord sounds as a chord, and a unison bass does not take 15 of the 16 budget units. The editor's UNISON switch does switch the part. 40 of the 200 programs have UNISON on: they play without it | an [open decision](#open-decisions-the-owners) |
| only `P5_OVERSAMPLE` 1 | Melodee's 2x never passed its budget; the generator makes the 1x coefficient table only | the same |

The remix's engine file is Melodee **b351a14** with stage 36 (not 8a3074b: its Lo Freq B is ten octaves down and
starts every note at the bottom of the ramp, its SSI output is the uninterpolated knee). What this tree does not take
from the remix: the 192-frame half buffer, the second core on by default, the voice caps and the chord-aware
shedding (`voice.c`, `cr_out.c`), six voices with dual core, the external bank switch, the factory bank and the
tables as committed generated headers (here both are generated at build time; the tables were compared once: the
remix's `choralroot_prophet_tables.h` and `build/gen/felucca_tables.h` hold the same ten tables, value for value:
`tools/prophet_listen.sh` prints it), its 9 pages of 36 untyped 0..127 cells with a screen plan and a special case
in `cr_edit.c` (here 19 pages, 56 named and ranged cells, the editor's title rules, `cr_edit.c` untouched), and its
user-slot scheme ([Saving](#saving)).

## The engine

`FELUCCA_PROPHET` (core.h, default 0: Felucca's unit; 1 in `choralroot.c`, the emulator, `tests/regress.c`,
`tests/va_levels.c`). **`FELUCCA_PROPHET=0 ./build.sh` gives the image of before the engine, byte for byte**
(`build/choralroot.bin` and the `.fwsc` package identical to aa5875a's: checked). `ENGINES[]` stays append-only (16
after FM TONE's 15; Melodee numbers it 19); `ENGINE_ORDER` shows it **after CZ-1 and before WHEEL** (FM6, FM TONE, VA,
CZ-1, PROPHET, WHEEL: six engines). The display name is "PROPHET" (the owner may rename it: `ENG_P5_TEST.name`; "Prophet"
is Sequential's mark, LICENSING.md).

- **State**: `p5_part_t` (788 B: five voices of 116 B, the wheel's 32 samples and their pitch ratios, the DC blocker) is
  a member of `engines.c`'s `eng_state` union (6884 B, FM6's size): **the union does not grow**. The program of each part
  (`p5_patch[NPART]`, 4 × 138 B) is in the pool: **POOL +552 B**.
- **Voices**: five (`P5_POLY`), each **three of the 16 budget units** (Melodee's figures, kept). One core shares the 16
  units between the parts, so a full chord alone is five voices (15 units) and **with the bass sounding four** (12 + the
  bass's 2 or 3); with Options > Dual Core On each part has its own 16.
- **EDIT 1 / 2** are Melodee's eight offsets on the program (CUT RES TUNA TUNB PWA PWB MIXA MIXB), 0 in every preset;
  the editor shows the deep pages instead.
- `FELUCCA_P5_FACTORY` (default 1): Sequential's 200 programs in the build. **0: INIT PROPHET alone** (no factory data in
  the image; `tests/run_prophet_test.sh` builds and plays it). Whether ChoralRoot ships the bank is the owner's
  decision: the data are Sequential's, no licence granted (LICENSING.md).

### Size (`./build.sh`'s size line)

| build | XIP (of 573 440) | RAM (of 98 304) | POOL (of 344 064) |
| --- | --- | --- | --- |
| before (aa5875a) and `FELUCCA_PROPHET=0` | 483 396 (84.3 %) | 81 332 (82.7 %) | 315 500 (91.7 %) |
| the engine, `FELUCCA_P5_FACTORY=0` | 500 660 (87.3 %) | 81 348 (82.8 %) | 316 052 (91.9 %) |
| **the engine and the bank (the default)** | **519 028 (90.5 %)** | **81 348 (82.8 %)** | **316 052 (91.9 %)** |

XIP +35 632 B: the engine 17 264 (code about 8 KB of which the sample loop 3350; the nine tables 7430, the largest
`P5_TANH_Q15` 4098; the pages), the bank 18 368 (the 200 programs packed, 55 B each: 11 266; their `preset_t` rows and
12-character names: about 7.1 KB). RAM +16 B (the parts' flags; the pool read-out's name field, below). POOL +552 B
(the four programs). `P5_RAM=1`: RAMTEXT 3016 -> 6812 of 24 576, XIP the same.

## Sounds

`PRESETS` on PROPHET turns its pool as on any engine (docs/PRESETS.md): **00 INIT** (INIT PROPHET: one saw, the filter
open), then **01..200 Sequential's factory programs** in the bank's order (01 ITS PROPHET5 .. 200 CAT; on a
Prophet-5 they are 111..588). The names are the programs' in upper case, shortened to PRESETS' 12 characters where
needed (88 of them: `tools/gen_prophet_factory.py SHORT`; the program keeps its 20-character name in its bytes).
The pool read-out cut a 12-character name's last letter (`cu.pop.sub[12]`: "ENSEMBLE ST", "FOREVER KEY"); with
PROPHET in the build the field is 13 (with `FELUCCA_PROPHET=0` it is as it was, for the byte-identical image).

**Sends.** The factory programs are dry, as the instrument. Nothing was added for that: since 0.18 the sends are the
PART's, not the sound's (`cr_bank.c param_kept`), so a Prophet program plays with the part's reverb, chorus and delay
as they stand (a new unit: 35 / 45 / 25), exactly as every other engine's sound does.

**Level.** `build/host/va_levels p5` (tests/va_levels.c): a 4-note chord (D4 F#4 A4 C#5) alone, every send 0, the
RMS of 0.3..5.9 s after the master, at the parts' default level:

| | median | quartiles | examples |
| --- | --- | --- | --- |
| PROPHET, 201 sounds | −25.2 dB | −31.4 .. −21.2 | ITS PROPHET5 −20.1, FOREVER KEYS −19.2, REEDY STRPAD −20.7, CARS STRINGS −20.7, STABBY BRASS −25.5, SYNCHROTRILL −18.7 |
| VA, 21 chord presets | −23.5 dB | −25.0 .. −22.7 | LUSH PAD −23.3, SLOW STRINGS −21.8, SYNTH BRASS −21.4 |
| FM6, 19 presets | −36.9 dB | −57.8 .. −28.7 | ORGAN −25.3, STRINGS −26.6, PAD −29.0, TINE EP −32.0 (most of FM6's decay within the window) |

PROPHET's median is 1.6 dB under the VA's; its sustained programs sit 1 to 3 dB over the VA's pads and about 5 dB over
FM6's sustained sounds. One program of the 201 touches the limiter with four notes. **No trim was applied** (the rule:
only beyond about 4 dB of the median). Not measured: six notes with a bass against the limiter (the VA's own test).

## The editor

EDIT on a PROPHET part shows its 19 deep pages through the editor's title rules (docs/EDITOR.md §4; no screen plan, no
change in `cr_edit.c`). A cell is one raw byte of the program, named and ranged as Melodee's panel (`prophet_edit.h
P5_PANEL`); turning it writes that byte only. All 56 controls that change the sound:

| Group | Screen | KNOB 1–4 (lane A / lane B) |
| --- | --- | --- |
| OSC | `OSC A` | Freq · Saw · Pulse · Width / Sync |
| OSC | `OSC B` | Freq · Fine · Saw · Tri / Pulse · Width · Lo freq · Keyboard |
| OSC | `MIXER` | Osc A · Osc B · Noise |
| FILT | `FILTER`, under the filter's curve | Cutoff · Reso · Env amt · Key trk (OFF HALF FULL) / Filter rev (1/2: the SSI model, 3: the Curtis) · Aftertch |
| ENV | `ENV 1 · amp`, under its ADSR | Attack · Decay · Sustain · Release / Velocity · Rel sw (the RELEASE switch: both envelopes') |
| ENV | `ENV 2 · filter`, under its ADSR | Attack · Decay · Sustain · Release / Velocity |
| LFO | `LFO` | Rate · Amount (the wheel's initial amount) · Noise (its mix) · Aftertch / Saw · Tri · Square |
| LFO | `WHEEL` | Freq A · Freq B · PW A · PW B / Filter (the wheel's five destinations) |
| MOD | `POLYMOD` | Env · Osc B (the two amounts) · Freq A · PW A / Filter (the three destinations) |
| MOD | `VOICE` | Unison · Voices (1..5) · Detune (0..7) · Glide / Priority (LOW, LOW R, LAST, LAS R: R retriggers) · Vintage |

The 57th panel byte, BEND (the program's own bend range), has no cell: the part bends by the channel's range. A switch
byte other than 0 reads "on", VOICES 0 (as the factory programs have it) reads 5: what plays. Value names are at most
five characters (a cell's width: Melodee's "CURTIS" and "LOW RET" would be cut). No matrix destination of its own
("not modulatable"); the platform's routes still act on the pitch, the cutoff, the widths and the level. An edit marks
the sound edited (`REEDY STRPAD*`) and is kept by the session over a power cycle (the 138-byte blob,
`eng_deep_t.blob_get`: `cr_settings.c css`), like any engine's.

## Saving

**SAVE on a PROPHET part is refused**: "PROPHET: no save yet", nothing is written (`cr_ui.c cu_save_open`). Why not the
remix's scheme (57 seven-bit values in the 32-slot record's tail, bytes 91..143, marker 'P','5'):

- the bytes are free today (`up_rec_t.packed[144]` holds `P_COUNT` = 91 values; `up_valid` reads no further; `cb_store`
  zeroes the rest), but with no headroom at all: the scheme's first byte is 91, the next common parameter would land
  on it;
- it keeps 57 of the 133 bytes (the name and every byte Sequential's data has beyond the panel are lost; a value over
  127 is refused);
- **a record of engine 16 is not a record to anything older or to the web tools**: `up_valid` wants `engine <
  NENGINES`, so 0.21 and before show the slot empty, and so does today's Sound Manager (`web/fm1sounds.js recordValid`:
  `engine >= ENGINE_NAMES.length`), which then offers the slot for the next sound it places. That is a way to lose a
  saved sound, and the web manager is not part of this phase.

So the simplest thing that cannot lose data is to write none. The proper store is phase B: flash objects in the free
region as `va_store.c` (0xB6000..0xC7FFF; 32 × 138 B), backup ids, the web manager's engine list and patch kind, and
the settings record ([Open decisions](#open-decisions-the-owners)). A record of engine 16 that arrives anyway (a
restored bank) loads as INIT PROPHET with its parameters.

## Cost on the unit

**Counted, not measured.** `python3 tools/p5_cost.py` after `./build.sh`. The sample loop (`p5_samples`) is one loop of
1012 instructions with 86 conditional branches; 17 of them test a switch of the program, the rest are a sample's own
(an oscillator's edge, a clamp, the filter's coefficients kept or set up). The tool finds the 17 from the listing (the
compiler reads the switch bytes before the loop: the code before the loop is evaluated for what the program alone
decides), fixes them for a program and counts the paths through the loop of `build/choralroot.dis`:

- **hit**: no oscillator edge in the sample, the filter's coefficients as the sample before (an envelope at rest, no
  modulation of the cutoff);
- **miss**: the same through the coefficient set-up: the cutoff moved since the sample before, as it does all through
  a filter envelope's attack and decay, and with Poly-Mod or the wheel on the filter. A pad's ordinary case: the table
  below uses it;
- **longest**: every edge and clamp in one sample (a bound, not a sample that happens).

A voice's block (32 samples): `p5_render` and two `p5_env_tick`, 515 instructions at most. A part's block: `p5_block`
445, the wheel's source 32 × 91, the DC blocker 32 × 12: 3835 (75 us a half). At 205 instructions a microsecond
(docs/FX.md), a half of 128 frames = 2902 us:

| program | a sample: hit | miss | longest | a voice a half | us | 3 voices | 4 | 5 | 5 at 1.7 × |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| INIT PROPHET: saw A, Curtis | 308 | 329 | 367 | 44 172 | 215 | 721 | 937 | 1152 | 1959 |
| saw A + saw B, Curtis | 325 | 346 | 402 | 46 348 | 226 | 753 | 979 | 1205 | 2049 |
| **saw A + saw B, SSI** (ITS PROPHET5) | 361 | 382 | 447 | 50 956 | 249 | 821 | 1069 | **1318** | 2240 |
| saw + pulse A, saw + pulse B, SSI | 385 | 406 | 507 | 54 028 | 264 | 865 | 1129 | 1393 | 2367 |
| saw A + saw B, the wheel on both frequencies, SSI | 378 | 399 | 464 | 53 132 | 259 | 852 | 1112 | 1371 | 2330 |
| saw A + saw B, Poly-Mod to the filter, SSI | 375 | 396 | 461 | 52 748 | 257 | 847 | 1104 | 1361 | 2314 |
| sync, Poly-Mod to A's frequency, Curtis | 365 | 386 | 531 | 51 468 | 251 | 828 | 1079 | 1330 | 2261 |
| everything on (both waves and the triangle, sync, Poly-Mod to all three, the wheel on all five), SSI | 490 | 511 | 797 | 67 468 | 329 | 1062 | 1391 | 1720 | 2925 |

(The voices' columns include the part's 75 us. "5 at 1.7 ×": the count has been low against the unit by up to 1.7,
docs/HANDOFF.md item 21: CZ-1's eight voices, 630 us counted, 1066 measured.) The cheapest and the dearest program are
a factor 1.5 apart; the SSI filter costs 36 instructions a sample more than the Curtis (seven table look-ups for
three); sync and Poly-Mod cost little until an edge falls in the sample (a divide and two more waveforms, once a cycle
of oscillator B). Not in the table: an oscillator's own edges. A saw's wrap costs two samples a divide and about 15
instructions each (the BLEP), a pulse twice that: at A4 some twenty divides a voice a half, about 2 % on top if a
divide is worth 35 instructions; three octaves up eight times that (an estimate: the divide's time on this core is
not known here). As a check of the walk: 382 a sample plus the block's and the part's share is 422 instructions a
voice and sample, and the host's count for ITS PROPHET5 (another compiler, another machine) is 422 too.

**The three engines on one scale.**

| | a voice a half, counted from the listing | a chord | measured on the unit |
| --- | --- | --- | --- |
| CZ-1 (a two-line tone; docs/CZ1.md) | 16 240 instructions, 79 us | 8 voices: 634 us | **1066 us** (1.7 × the count) |
| PROPHET | 44 172 .. 67 468, 215 .. 329 us (2.7 .. 4.2 CZ-1 voices) | 5 voices: 1152 .. 1720 us | not measured |
| VA | no listing count exists (not made here) | | not measured against a count |

For the VA only the host's instruction counts compare (`tests/cpu_baseline.txt`, eight notes asked, the idle block
taken off, per sounding voice and sample): CZ-1 BRASS 1 94, VA LUSH PAD 182, PROPHET INIT 355, ITS PROPHET5 422. For
the two engines with a listing count the unit's count is 1.35 (CZ-1) and 0.94 (PROPHET) of the host's, so a VA voice is
*estimated* at 22 .. 31 thousand instructions a half (107 .. 154 us): **a PROPHET voice is about two VA voices and three
to four CZ-1 voices; five of them are a ten-voice VA chord.** (An inference from two ratios, not a count.)

**Melodee's own measurements** (its docs/PROPHET5_PROTOTYPE.md, on an FM-1, 2026-10-08): five voices of a harsh stress
program (sync, audio-rate Poly-Mod, a 500 Hz wheel on every destination) with distortion, chorus, delay, reverb and
USB audio running peaked at **2158 us (SSI) / 2009 us (Curtis)** of the half's 2902, no late block. Those figures are
of an older DSP (before b351a14's exponential Poly-Mod, which its author puts at +8 % on the host, and before 8a3074b),
with the sample loop and four tables in RAM, and with Melodee's effects, not ChoralRoot's. Read with the row
"everything on" above (1720 us for five voices by the count), they say the count is not far off for this code; they do
not say what the same code costs from flash beside ChoralRoot's effects.

**The emulator's estimates** (`sh tools/emu/perf.sh`, scenarios (p) and (q); host instructions / 259: ESTIMATES, low by
1.2 to 2.2 for other stages, docs/FX.md), a 6-key ITS PROPHET5 chord with the default sends, us of 2902, avg / max:

| | block | chord part | bass part | fx | master | chord voices |
| --- | --- | --- | --- | --- | --- | --- |
| (p) + the PROPHET bass (FUNK BASS II), one core | 1070 / 1700 | 616 / 990 | 161 / 321 | 215 / 460 | 23 / 95 | 4 (the budget) |
| (p), Dual Core On | 1222 / 1905 | 759 / 1194 | 159 / 431 | 212 / 427 | 25 / 100 | 5 |
| (q) + the VA bass (PUNCH BASS), one core | 1154 / 1672 | 745 / 1052 | 65 / 135 | 258 / 431 | 24 / 80 | 4 |
| (q), Dual Core On | 1338 / 1777 | 918 / 1196 | 63 / 173 | 258 / 550 | 27 / 62 | 5 |

### Does it fit?

Beside the chord part a half holds the effects (about 290 us for the delay and the reverb, **measured** on the unit,
docs/FX.md), the master (about 65, counted), the mix, the chorus, the tick and TIMER5's nesting (not counted here:
taken as 150) and the bass (a VA bass: about 150; a PROPHET bass: 324 by the count): **about 500 us without the bass**.
For ITS PROPHET5's kind of program (two saws, SSI, the envelope moving):

| on one core | by the count | if the count is 1.7 low |
| --- | --- | --- |
| 5 chord voices, no bass | 1818 us, 63 % | 2740 us, 94 % |
| 5 chord voices + a VA bass (not reachable on one core: the budget gives 4) | 1968 us, 68 % | 2890 us, 99.6 % |
| **4 chord voices + a VA bass** (what the budget gives) | 1719 us, 59 % | 2467 us, 85 % |
| **4 chord voices + a PROPHET bass** | 1893 us, 65 % | 2868 us, 99 % |
| 3 chord voices + a PROPHET bass | 1645 us, 57 % | 2447 us, 84 % |
| 5 voices of "everything on", no bass | 2220 us, 76 % | 3425 us, over |

(The right column scales PROPHET's own count, the chord part's and a PROPHET bass's, and nothing else.)

**Plainly: by the count, five voices with a bass, reverb, delay and chorus fit one core with a third of the half to
spare. If the count is as low as it was for CZ-1, they do not: four voices with a VA or FM6 bass are then at 85 %, and
four with a PROPHET bass or five alone are at the edge.** Which of the two it is, only the unit can say
(`pk_part0_us` with a five-voice ITS PROPHET5 chord held: 1318 by the count; above about 1700 the cap must come down).

What I would do until then:

- **One core (the default)**: keep five voices of three units. The shared budget already makes it four whenever the
  bass sounds, which is the case that matters. If the unit reads more than 1.3 times the count: four units a voice
  (four voices, three with a bass).
- **Dual Core On**: the bass part's render moves to core 1 (docs/DUALCORE.md: `track_render` of `trk[1]` is the one job
  there is), so a PROPHET bass costs core 0 nothing and the chord part has its own 16 units: five voices, 1818 us by
  the count, 2740 if 1.7 low. **PROPHET's chord voices cannot run on core 1 today.** They could: `p5_samples` writes
  its own voice state and its output only, and reads the program, the wheel's block and its parameters (Melodee
  f12b310 hands every second voice to its worker that way); it needs a second kind of job and a private output
  buffer, and is not built.
- **The bass part on PROPHET** is not restricted in code. It is the dearest bass there is (324 us by the count against
  a VA bass's 65 / 135 in the emulator's estimate) and takes three budget units; on one core "four voices and a
  PROPHET bass" is the combination nearest the limit.
- If the unit wants less: the loop built per program (as CZ-1's 43 loops) saves 20 to 28 % (the tool's "built alone"
  column: 276 for 361 on the two-saw SSI row: the switches' tests and what the compiler cannot drop while a switch may
  be on); the SSI filter's seven look-ups; the coefficient set-up once a block instead of once a sample when only the
  envelope moves it.

## Tests

- `tests/cr_prophet_test.c` (in `sh tests/run_cr_tests.sh`), on the firmware as the emulator builds it: the engine's
  place; the bank (the packed form = the `.syx`'s 200 frames decoded; decode / encode round trip; bad frames refused);
  the names; the blob; **every program through voice.c** (a four-note chord: none silent, no sample beyond the voices'
  sum, no DC, every voice free after its release (the longest 89 s), exact silence then); 410 stress programs with five
  voices; note-off; a note struck again (no step beyond the wave's own); the five-voice cap and the units; UNISON and
  GLIDE; the bend through the part's MIDI; the 19 pages (every cell sets its byte and no other, clamps, reads back;
  each of the 56 bytes on exactly one cell); the pool; SAVE refused.
- `tests/run_prophet_test.sh`: the stage-36 identity (above) and `FELUCCA_P5_FACTORY=0`.
- `tests/regress.c`: 201 golden renders and CPU entries added (`preset/PROPHET/..`, `cpu/PROPHET/..`): **398 goldens, 0
  changed** (the 197 before are untouched lines). `tests/target_budget.txt`: `p5_samples`, `p5_render`,
  `p5_mod_samples`, `p5_post`, `p5_block`.
- `tools/emu/scripts/cr_prophet.txt` in `tools/emu/test_cr.sh`: OPT + PRESETS to PROPHET, three programs, a chord
  each, the ten editor screens, an edit that is heard, SAVE refused, the two engine pickers with six engines.
  Four scripts that stepped to WHEEL step one further now (`cr_engine`, `cr_editor_steps`, `cr_editor_ref`,
  `cr_editor_map`); `test_retired.sh`'s pre-cut build is built without PROPHET (0.17 had none).
- `tools/emu/test_persist.sh`: an edited program over a power cycle (SAVE refused, the global save, the same flash
  file again: the part powers on with the program as edited).
- `tests/run_cr_tests.sh` builds `tests/sound_templates.c`'s check of the web tools' tables without PROPHET: the Sound
  Manager and `fm1_install.py` do not know engine 16 (phase B).
- Not tested: anything on a unit; the browser emulator beyond its own suite; MIDI program changes into the pool; the
  wheel (CC 1) and aftertouch by ear.

## Listening

`sh tools/prophet_listen.sh` -> `../audio-0.22/prophet/` (README.txt there): D, Bm, G, A, D on eight programs (ITS
PROPHET5, FOREVER KEYS, STABBY BRASS, REEDY STRPAD, CARS STRINGS, VINTAGE WRLY, SYNCHROTRILL, FUNK BASS II), each dry
and with the default sends, through the emulator; and three programs rendered by the remix's engine file and by this
tree's (`tools/prophet_ab.c`, the engine alone): 01 and 15 differ by the DC blocker and the interpolated knee only, 33
PICKLE PINCH is the program 8a3074b repaired (its Lo Freq B).

## Open decisions (the owner's)

1. **The factory bank**: ship Sequential's 200 programs (`FELUCCA_P5_FACTORY` 1, 18.4 KB of flash) or INIT alone? The
   data are Sequential's ("no licence granted", as Melodee words it and as ChoralRoot words Casio's CZ-1 tones).
2. **The name** "PROPHET" (Sequential's mark).
3. **The unit's measurement** before anything else: `pk_part0_us` with five voices, and with four and a bass; then the
   voice cap (above).
4. **UNISON on a load**: 40 programs ask for it. Today no load switches the part; the alternative is Melodee's (the
   chord part plays one note, five voices thick).
5. **Phase B, the store**: where (the free region 0xB6000..0xC7FFF), the backup ids, the web manager's engine list,
   and the **settings record**: PROPHET has no place among its eleven `pool_pos` slots (`cr_settings.c crs_slot`: −1,
   handled). The sound a part plays at power-off comes back, edited or not (the record's chord / bass sound and the
   session: `tools/emu/test_persist.sh`); what is not remembered over a power cycle is the place a part had in the
   PROPHET pool when it is on another engine (OPT + PRESETS back to PROPHET lands on 01), and a slot's check would
   reset a place over 127 (`cr_settings.c`: "a pool: INIT + 64 + 32 at most").
6. **Flash**: XIP is at 90.5 % with the bank (87.3 % without).
7. **RAM for the hot code and tables** (`P5_RAM`, `ram=True` in `tools/gen_tables.py`): only if the unit shows flash in
   the way; RAM has 16.9 KB free, RAMTEXT 21.5 KB (the hot code would take 3.8 of it, the four per-sample tables
   5.4 KB of RAM).
