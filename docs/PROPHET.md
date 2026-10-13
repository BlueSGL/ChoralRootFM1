# ChoralRoot FM-1 — P5

**P5** is **engine 16** (`ENGI_PROPHET`): **Melodee's** Prophet-5 engine (Kerem Kilic's fork of Felucca,
<https://github.com/keremimo/melodee>, GPL-3.0-only; its code name `p5_`). Native Sequential programs (the 133 raw
bytes of a program dump), two band-limited oscillators with hard sync and audio-rate Poly-Mod into a four-pole ladder
with a tanh knee in two characters (Rev 1/2: SSI; Rev 3: Curtis), two ADSRs, the wheel's modulation source, **four
voices** (Melodee: five; [the unit's reading](#measured-on-the-unit-2026-10-13)). It came here by way of a remix of
ChoralRoot 0.14-dev that Rosen sent ("the Prophet 5 Engine from Melodee that was added sounds really nice and is
perfect for chords").

**The name.** A player sees **P5** everywhere (the engine pickers, the pool's read-out, the editor, "P5: no save
yet", the emulator's traces): the owner's name for it, 2026-10-13 ("we can name in P5"). The code, the switches and
this file keep their names (`FELUCCA_PROPHET`, `ENGI_PROPHET`, `eng_prophet_test.c`, `p5_`); "Prophet" below means the
instrument and Sequential's data.

**Where it stands** (0.22 in development, 2026-10-13; HISTORY 3y38, 3y39): the engine plays on the host and in the
emulators, its sounds browse like any engine's, its editor is complete. The owner has played the first build
(cd43720, five voices) on the FM-1 **once**, and that reading set the cap to four; nothing of the tree as it is now
(four voices, the shipped list, UNISON on a load) has run on a unit. Not in this phase: a store for user programs
(SAVE is refused), backup ids, the web manager, loading the other programs of the 200 from the device manager (the
owner's plan; it needs that store), a release note. [Open decisions](#open-decisions-the-owners) lists what waits.

| file | what | from |
| --- | --- | --- |
| `firmware/src/eng_prophet_test.c` | the engine: voices, envelopes, the sample loop, the wheel's source, the part's DC blocker; ChoralRoot's load hook, blob, deep pages | Melodee 8a3074b (+ 7cd66fb's DC blocker, 1bc4555's unpacking), the remix's three optimisations, ChoralRoot |
| `firmware/src/prophet_patch.c` | the program: the 133 raw bytes and their indices, INIT, SysEx decode / encode (model 0x32 / 0x31, 133 / 128 bytes) | Melodee, as it is (8a3074b = 1.2.1, byte for byte below the header) |
| `tools/gen_tables.py` `p5_tables` | the engine's nine tables and `P5_KEYTRACK_HALF_Q7` | Melodee 8a3074b's expressions |
| `tools/gen_prophet_factory.py`, `assets/prophet5-factory/` | Sequential's 200 factory programs v1.03, of which `shipped.txt` names the ones in the build -> `build/gen/melodee_prophet_factory.h` (`tools/build.py` generate()) | Melodee 1.2.1 (the packed form), + the PRESETS names, the shipped list |
| `tools/p5_goldens.py` | P5's golden renders and CPU entries alone, after the list changed | ChoralRoot |
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
| the remix's stage 36, three optimisations (`P5_STAGE36` bits): (1) an oscillator with no waveform on computes nothing; (2) the envelope's terms of the cutoff, the Poly-Mod frequency and width once a sample instead of once an oversampling pass; (4) hard sync's two phase products as 32-bit halves instead of a 64-bit multiply | fewer instructions | **bit-identical**, each alone and all three: `tests/run_prophet_test.sh` renders INIT, the shipped programs and 683 stress programs with `P5_STAGE36` 0, 1, 2, 4, 7 and compares every sample's digest |
| the part's DC blocker (`p5_post`, Melodee 7cd66fb: integer code written after the float change, about 5 Hz) | without it 59 of the 201 presets (INIT and all 200: cd43720's count) hold a DC of more than a quarter of their RMS in a held chord (the worst 47 878 on a scale where a voice is 24 000): it would ride into the sends and the limiter | changed on purpose (`P5_DC_BLOCK=0` gives 8a3074b's output for a measurement) |
| no `.dsp_text` / `.dsp_tables` sections: the code runs from flash, the tables are `const` | ChoralRoot's map has no such sections, RAM is the tight region, and RAM changed nothing for the effects' loops on the unit (docs/FX.md `CR_FX_RAM`). `P5_RAM=1 ./build.sh` puts the three hot functions into `.ram_text` (3796 B of RAMTEXT, it links: not run on a unit) | the same |
| the pitch bend: Melodee's `p5_update_bend` (voice.c: the program's own range byte) is not here | the part bends as ChoralRoot's other engines do, through `vmod_t` (voice.c: the channel's range, RPN 0, ±2 semitones). The program's `P5_BEND` byte is kept and unused | tested: +2 / −2 semitones measured |
| the matrix's ENV source is 0 (Melodee's voice.c asks `p5_env_source`) | as FM6's and CZ-1's here; voice.c is not touched | none on a factory program |
| four voices a part (`P5_POLY`; Melodee: five), three budget units each as Melodee's (c176dd4: four units) | the unit's reading of five voices, [below](#measured-on-the-unit-2026-10-13); three units so that a bass does not cost the chord a voice: a measurement build, [Voices](#voices-which-keys-of-a-big-chord-sound) | a chord of more than four keys sounds four of them |
| UNISON on a load | as Melodee's (`V_UNISON` whenever the program says so); cd43720 had left it out, the owner asked for it | [UNISON](#unison) |
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
(`build/choralroot.bin` sha256 501a9d09.., aa5875a's: checked again for 3y39; since the favourites, 3y40, which are
every engine's, the image without P5 is 57371526.., and the rule is the same: nothing of P5 outside the switch). `ENGINES[]` stays append-only (16
after FM TONE's 15; Melodee numbers it 19); `ENGINE_ORDER` shows it **after CZ-1 and before WHEEL** (FM6, FM TONE, VA,
CZ-1, P5, WHEEL: six engines). The display name is `ENG_P5_TEST.name`, "P5" ("Prophet" is Sequential's mark,
LICENSING.md).

- **State**: `p5_part_t` (672 B: four voices of 116 B, the wheel's 32 samples and their pitch ratios, the DC blocker) is
  a member of `engines.c`'s `eng_state` union (6884 B, FM6's size): **the union does not grow**. The program of each part
  (`p5_patch[NPART]`, 4 × 138 B) is in the pool: **POOL +552 B**.
- **Voices**: four (`P5_POLY`, the cap), each **three of the 16 budget units** (`p5_units`) **since 2026-10-13, a
  measurement build** (HISTORY 3y40). One core shares the 16 units between the parts: a chord alone is four voices
  (12 units) and **with a VA or FM6 bass sounding still four** (4 × 3 + 2 = 14 of 16). c176dd4 had four units a
  voice, which left the chord THREE voices whenever a bass sounded (3 × 4 + 2 = 14). The price of three units: **a
  P5 bass under a P5 chord is a fifth P5 voice** (4 × 3 + 3 = 15), and five voices are the reading that did not fit
  with a margin. **To be measured on the unit next**: four P5 chord voices + a VA bass + all three effects
  (`--debug`: `pk_part0_us`, `pk_block_us`, `pk_late`, `stale_halves`), then the same with a P5 bass; **it may go
  back to 4 units**. Where this document says "three voices with a bass" further down, that is c176dd4's four units.
  With Options > Dual Core On each part has its own 16: four and the bass.
- **EDIT 1 / 2** are Melodee's eight offsets on the program (CUT RES TUNA TUNB PWA PWB MIXA MIXB), 0 in every preset;
  the editor shows the deep pages instead.
- `FELUCCA_P5_FACTORY` (default 1): Sequential's programs in the build, the ones `assets/prophet5-factory/shipped.txt`
  names ([The shipped list](#the-shipped-list)). **0: INIT alone** (no factory data in the image;
  `tests/run_prophet_test.sh` builds and plays it). Whether a release carries Sequential's data at all is the owner's
  decision: the data are Sequential's, no licence granted (LICENSING.md).

### Voices: which keys of a big chord sound

Four voices, so a chord of five or six keys sounds four of its notes. The rule is voice.c's, the same for every
engine with a cap: a new note that finds no free voice takes **the oldest held voice that is not the lowest held
note** (it fades over a block, the new note starts on it). The chord engine sends a chord's notes from the bottom up,
so (`tests/cr_prophet_test.c`, measured):

| keys | what sounds | what gives way |
| --- | --- | --- |
| 4 | all four | nothing |
| 5 (D3 F#3 A3 C#4 E4) | the bottom note and the top three (D3 A3 C#4 E4) | the second note from the bottom |
| 6 (.. F#4) | the bottom note and the top three (D3 C#4 E4 F#4) | the second and the third from the bottom |
| any, with a VA or FM6 bass sounding on one core | all four (measured on the host: the count; three units a voice. With c176dd4's four units: three) | |

The root stays, the top stays, the inner notes go. (The bottom-up order is the chord engine's plain order; a play
style that sends the notes in another order changes which is "oldest": not measured per style.) With Dual Core On the
bass does not take from the chord's 16 units.

### UNISON

**A load takes the program's UNISON switch**, on the chord part and on the bass part (the owner, 2026-10-13: "loading
a preset should load the Unison"; 40 of Sequential's 200 have it on, 5 of the provisional 48). INIT is poly.

**What a 4-note chord does on a UNISON program** (`tests/cr_prophet_test.c`, measured on the host): **one note
sounds**, on the program's stacked voices.

- *Which note*: the program's key priority (the VOICE page's Priority). **LOW** (LOW, LOW R: most of them): the
  chord's **lowest note**, D4 of D4 F#4 A4 C#5. **LAST** (LAST, LAS R): the **last note the chord engine sent**,
  which is the top one, C#5, in its plain bottom-up order.
- *How many voices*: the program's VOICES count, **four at most** here (Sequential's count goes to five, and its 0
  means all: both play as four). Of the provisional list: LAGOON BASS, UNISAWYER, UNISON GLIDE four, FEATURE BASS
  four (stored 0), BASSMENT one (a mono bass). They are spread by the program's DETUNE (0 on most: the same pitch,
  the voices' own start phases and drift), at about the level of one voice (voice.c halves the velocity of a stack).
- *The cost*: a voice is a voice. Four stacked voices are **the four voices of a four-note chord: all 16 budget
  units, and the same processor time** (about 1630 us of the half by the unit's reading, below). A UNISON program
  costs no more than a full poly chord, and never more than the cap. With a VA bass sounding on one core the one note
  keeps its four voices (measured on the host; three units a voice: 12 + 2. With four units: three).
- On the **bass part** a UNISON program stacks up to four voices on the bass note while the chord part is silent
  (all 16 units). **Under a chord on one core the stack gives way**: measured with LAGOON BASS as the bass and a
  4-note chord, in either order of arrival: **the bass note keeps one voice and the chord has all four of its notes
  (five P5 voices, 15 units: three units a voice; with c176dd4's four units the chord had three)**,
  which is what a mono bass leaves the chord too (a stack's extra voices are the first the budget takes, and are
  only started when there is room: voice.c). So a UNISON bass is thick alone and a plain mono bass under a chord; it
  does not starve the chord (the reason cd43720 gave for leaving UNISON off a load does not hold at four units a
  voice). With Dual Core On the parts do not share: the bass keeps its four.

How the part's voice mode is kept straight (`eng_prophet_test.c p5_track_loaded`, `p5_own`, `p5_voice_mode`,
`p5_leave`, `p5_voice_restore`):

- every sound load sets the part's mode first (POLY on the chord part, MONO on the bass: `cr_ui.c cu_load`), then
  the engine's load hook makes it UNISON when the program says so. So **the next program without the switch is POLY
  again** (MONO on the bass), and **another engine's sound finds the part as it expects it**.
- the editor's **UNISON cell** switches between UNISON and the part's own mode, as before.
- the editor's engine switch (EDIT held: it keeps the part's mix, and so its voice mode) takes a UNISON that a P5
  program gave off again; a UNISON the player set on the MIX page of a program without the switch stays, as between
  any two engines.
- **the session** keeps the mode as it was left (a UNISON program: UNISON; one with UNISON turned off in the editor:
  POLY) and brings it back after a power cycle (`cr_settings.c css_part_put`; `tools/emu/test_persist.sh` powers on
  with a UNISON program). The engine picker's cancel does the same with its snapshot.
- **held keys do not hang**: every load releases the part's notes before the next block (`panic_req`), in both
  directions (tested: a chord held over UNISON -> POLY and POLY -> UNISON; no voice left, no mono stack left).

### Size (`./build.sh`'s size line)

| build | XIP (of 573 440) | RAM (of 98 304) | POOL (of 344 064) |
| --- | --- | --- | --- |
| before (aa5875a) and `FELUCCA_PROPHET=0` | 483 396 (84.3 %) | 81 332 (82.7 %) | 315 500 (91.7 %) |
| the engine, INIT only (`FELUCCA_P5_FACTORY=0`) | 500 868 (87.3 %) | 81 348 (82.8 %) | 316 052 (91.9 %) |
| **the engine and the provisional 48 (the default)** | **505 588 (88.2 %)** | **81 348 (82.8 %)** | **316 052 (91.9 %)** |
| the engine and all 200 (`P5_SHIPPED=all ./build.sh`) | 519 508 (90.6 %) | 81 348 (82.8 %) | 316 052 (91.9 %) |

The engine is 17 472 B of flash (code about 8 KB of which the sample loop 3350; the nine tables 7430, the largest
`P5_TANH_Q15` 4098; the pages). A shipped program costs **93 to 98 B** (its packed bytes, 53 a program with these 48
and 55 with all 200; its `preset_t` row and 12-character name; its number): 48 programs 4720 B, 200 programs
18 640 B. RAM +16 B (the parts' flags; the pool read-out's name field, below). POOL +552 B (the four programs); the
four-voice state is smaller than the five-voice one but lives in a union of FM6's size, so POOL is the same.
`P5_RAM=1`: RAMTEXT 3016 -> 6812 of 24 576, XIP the same (cd43720's figure, not built again).

## Sounds

`PRESETS` on P5 turns its pool as on any engine (docs/PRESETS.md): **00 INIT** (one saw, the filter open), then the
**shipped factory programs** in the list's order (provisionally 48: 01 ITS PROPHET5 .. 48 UNISON GLIDE). The names
are the programs' in upper case, shortened to PRESETS' 12 characters where needed (88 of the 200:
`tools/gen_prophet_factory.py SHORT`; the program keeps its 20-character name in its bytes). The pool read-out cut a
12-character name's last letter (`cu.pop.sub[12]`: "ENSEMBLE ST", "FOREVER KEY"); with P5 in the build the field is
13 (with `FELUCCA_PROPHET=0` it is as it was, for the byte-identical image).

### The shipped list

The owner, 2026-10-13: "I'd like to trim down the presets it ships with, maybe in the device manager you can have
option to load up all 200". Which of Sequential's 200 are compiled in is **data**:
**`assets/prophet5-factory/shipped.txt`**, one program number (1..200, the program's place in Sequential's file) a
line, comments after `#`, in the order of the pool. No file, or the word `all`: all 200.
`tools/gen_prophet_factory.py` reads it at every build; everything else is sized from what it generates
(`P5_FACTORY_N`: the engine's preset count, the pool, the tests, `va_levels p5`, the regression's entries).

**A program's identity is its Sequential number**, not its place in the list. Every factory program carries that
number in its own 138 bytes (a program dump's group and program bytes; `p5_patch_id`), an edit does not touch it, and
the generated `P5_FACTORY_ID[]` maps place to number. What refers to a factory program, and how:

| record | what it holds | when the list changes in a later build |
| --- | --- | --- |
| **the session** (`OBJ_SESSION`, a part's sound at power-off: `css_part_t`) | the whole program, edited or not (the 138-byte blob, with its number in it), its name, the part's parameters; and the place it had in the pool (`preset`, `fk`) | **the same sound, always**: the program is the record's own bytes. Its place is looked up again from the number (`css_part_put`): the program's place in this build's pool, or, when this build does not ship it, no place: it plays from the record, shown at the pool's INIT position and marked edited, and PRESETS steps on from 00. (cd43720 trusted the stored place: a list changed would have shown the wrong position, and a place past the pool's end would have dropped the record.) |
| the settings block's `chord_sound` / `bass_sound` | the engine and a **pool position** (a byte), as for every engine | used only when no session record is there (the session is written with it by the global save; a unit that has only the block is a case not seen here): then the part comes up on whatever stands at that position now. Not changed: the block's format is every engine's, and for the others the position is not stable either (a user preset saved or deleted moves it). |
| the settings block's per-engine pool positions | nothing for P5 (it has no slot, [Open decisions](#open-decisions-the-owners)) | nothing to go wrong |
| user slots, backups | nothing for P5 (no store yet) | phase B: the store must hold the 138 bytes (they carry the number) |

Tested: `tests/run_prophet_test.sh` builds the test with three lists (the tree's, all 200, seven programs in an order
of their own), writes the session's record of every program with one and reads it with the others: the same program
comes back each time, at that build's place for it or from the record alone; and a program renders the same samples
in every build that ships it.

**To change the list** (a one-file change):

1. edit `assets/prophet5-factory/shipped.txt` (`python3 tools/gen_prophet_factory.py --list all` prints the 200 with
   their numbers and a `U` for UNISON; `--list` prints the list as it stands, with the places);
2. `./build.sh` (any build generates the header again; the generator prints the count and any two programs of the
   list within 8 of the 55 panel bytes of each other);
3. `python3 tools/p5_goldens.py`: P5's lines of `tests/golden.txt` and `tests/cpu_baseline.txt` made again, every
   other line left byte for byte (it refuses to write if a render of another engine moved). The entries are named
   by the program's number (`preset/P5/044_CARS_STRINGS`), so a program that stays keeps its line and its hash;
4. `CR_VERSION=0.22 sh tests/run_all.sh`; then this file's table below and the size table
   (`python3 tools/gen_prophet_factory.py --list`).

The emulator's tests read the names of the places they visit from the generator (`test_cr.sh`: places 01, 03, 15;
`test_persist.sh`: the list's first UNISON program), so they follow the list; they need at least 15 programs.
What does not follow by itself: `tools/emu/scripts/perf_p.txt` (the step to FUNK BASS II as the bass: its place + 1)
and the examples in this file.

**The provisional list** (2026-10-13; the owner edits it after playing). **Chosen from the programs' parameters and
names, not by ear.** The rule: a chord instrument's sounds first, sustained before percussive: 14 pads, strings and
voices, 6 brass, 8 keys, 5 organs, 4 plucks and bells, 2 plain polysynths; then 5 basses (three with UNISON) and 4
leads (two sync, two UNISON); no effect, noise or drum program; none that needs the wheel or aftertouch to sound; of
two programs within 8 of the 55 panel bytes of each other only one (the bank has two such pairs: 21 BASS FACE / 139
BASSMENT, 48 SMALL GONG / 120 FUNKY BELLS). The one-line reason for each is in the list file.

| place | Sequential's number | PRESETS name | the program's name | category | UNISON |
| --- | --- | --- | --- | --- | --- |
| 01 | 1 | ITS PROPHET5 | It's a Prophet 5 | pads, strings, voices | no |
| 02 | 15 | REEDY STRPAD | Reedy String Pad | pads, strings, voices | no |
| 03 | 44 | CARS STRINGS | Cars Strings | pads, strings, voices | no |
| 04 | 110 | REVERB STRGS | Reverberant Strings | pads, strings, voices | no |
| 05 | 129 | OLD STR MACH | Old String Machine | pads, strings, voices | no |
| 06 | 105 | HIGH STRINGS | High Strings | pads, strings, voices | no |
| 07 | 162 | LOW STRINGS | LOW STRINGS | pads, strings, voices | no |
| 08 | 66 | 80S HORROR | 80s Horror Strings | pads, strings, voices | no |
| 09 | 61 | INSTABIL PAD | Instability Pad | pads, strings, voices | no |
| 10 | 70 | VOCAL PAD | Vocal Pad | pads, strings, voices | no |
| 11 | 47 | CHORAL VOICE | Choral Voices | pads, strings, voices | no |
| 12 | 13 | TAARICHOIR | TaariChoir | pads, strings, voices | no |
| 13 | 109 | SOFT SWELL | Soft Swell | pads, strings, voices | no |
| 14 | 54 | MIDNIGHT PWM | Midnight PWM | pads, strings, voices | no |
| 15 | 6 | STABBY BRASS | Stabby Brass | brass | no |
| 16 | 102 | SOFT BRASS | Soft Brass | brass | no |
| 17 | 131 | ICONIC BRASS | Iconic Brass | brass | no |
| 18 | 94 | PINK NEON BR | Pink Neon Brass | brass | no |
| 19 | 161 | BRASS | BRASS | brass | no |
| 20 | 71 | MOVING BRASS | Soft Moving Brass | brass | no |
| 21 | 3 | FOREVER KEYS | Forever Keys | keys | no |
| 22 | 62 | VINTAGE WRLY | Vintage Wurly | keys | no |
| 23 | 43 | REED EP | Reed EP | keys | no |
| 24 | 179 | ELEC PIANO I | ELECTRIC PIANO I | keys | no |
| 25 | 91 | FURRY TINES | Furry Tines | keys | no |
| 26 | 163 | MUTED CLAV | MUTED CLAVINET | keys | no |
| 27 | 166 | HARPSICHORD | HARPSICHORD | keys | no |
| 28 | 11 | CLAVICLE | Clavicle | keys | no |
| 29 | 10 | PDG ORGAN | PDG Organ | organs | no |
| 30 | 168 | PERC ORGAN | PERCUSSIVE ORGAN | organs | no |
| 31 | 170 | HARMONIUM | HARMONIUM | organs | no |
| 32 | 143 | ORGANIZER | Organizer | organs | no |
| 33 | 145 | LESLIED | Leslied | organs | no |
| 34 | 81 | PULSE PLUCK | Pulse Pluck | plucks and bells | no |
| 35 | 133 | PRETTY PLUCK | Pretty Pluck | plucks and bells | no |
| 36 | 79 | MARIMBA | Marimba | plucks and bells | no |
| 37 | 32 | BLINKR BELLS | Blinker Bells | plucks and bells | no |
| 38 | 55 | HYPER POLY | Hyper Poly | polysynth | no |
| 39 | 57 | AGGRO SYNTH | Aggro Synth | polysynth | no |
| 40 | 4 | FUNK BASS II | Funk Bass II | basses | no |
| 41 | 46 | FAT POLYBASS | Fat Poly Bass | basses | no |
| 42 | 139 | BASSMENT | Bassment | basses | yes (1 voice, low note) |
| 43 | 111 | LAGOON BASS | Lagoon Bass | basses | yes (4 voices, low note) |
| 44 | 135 | FEATURE BASS | Feature My Bass | basses | yes (4 voices, last note) |
| 45 | 17 | SYNCHROTRILL | Synchrotrill | leads | no |
| 46 | 167 | SYNC I | SYNC I | leads | no |
| 47 | 82 | UNISAWYER | Unisawyer | leads | yes (4 voices, low note) |
| 48 | 169 | UNISON GLIDE | UNISON GLIDE W RES | leads | yes (4 voices, low note) |

**Sends.** The factory programs are dry, as the instrument. Nothing was added for that: since 0.18 the sends are the
PART's, not the sound's (`cr_bank.c param_kept`), so a P5 program plays with the part's reverb, chorus and delay
as they stand (a new unit: 35 / 45 / 25), exactly as every other engine's sound does.

**Level.** `build/host/va_levels p5` (tests/va_levels.c): a 4-note chord (D4 F#4 A4 C#5) alone, every send 0, the
RMS of 0.3..5.9 s after the master, at the parts' default level, every program played POLY (the tool sets the part's
mode: the five UNISON programs are measured as chords here, not as they load):

| | median | quartiles | examples |
| --- | --- | --- | --- |
| P5, INIT + the provisional 48 | −22.9 dB | −26.5 .. −20.2 | ITS PROPHET5 −20.1, FOREVER KEYS −19.2, REEDY STRPAD −20.7, CARS STRINGS −20.7, STABBY BRASS −25.5, SYNCHROTRILL −18.7, BASSMENT −15.8 (the loudest), UNISON GLIDE −33.9 |
| (P5, INIT + all 200, five voices: cd43720) | −25.2 dB | −31.4 .. −21.2 | |
| VA, 21 chord presets | −23.5 dB | −25.0 .. −22.7 | LUSH PAD −23.3, SLOW STRINGS −21.8, SYNTH BRASS −21.4 |
| FM6, 19 presets | −36.9 dB | −57.8 .. −28.7 | ORGAN −25.3, STRINGS −26.6, PAD −29.0, TINE EP −32.0 (most of FM6's decay within the window) |

The 48's median is 0.6 dB over the VA's (the list has fewer percussive programs than the bank); none touches the
limiter with four notes. **No trim was applied** (the rule: only beyond about 4 dB of the median). Not measured: a
UNISON program as it loads (one note on four voices), six keys with a bass against the limiter (the VA's own test).

### The shipped list from the unit's favourites

The owner's way to choose the list by ear (2026-10-13): flash a build with all 200, mark the programs worth keeping
(SAVE, KNOB 4; or EDIT, PRESETS to browse, KNOB 4 on each), then read the marks off the unit.

```
P5_SHIPPED=all ./build.sh                                   # the build with all 200 (flashed by the orchestrator)
# .. on the unit: OPT + PRESETS to P5; SAVE, KNOB 4 right on a program; or EDIT, PRESETS, KNOB 4 while browsing ..
python3 tools/fm1_install.py --favorites > build/scratch/favorites.txt     # read-only; a moment after the last mark
python3 tools/p5_shipped_from_favorites.py build/scratch/favorites.txt             # the new list, printed
python3 tools/p5_shipped_from_favorites.py build/scratch/favorites.txt --write     # .. into assets/prophet5-factory/shipped.txt
./build.sh && python3 tools/p5_goldens.py && CR_VERSION=0.22 sh tests/run_all.sh
```

`--favorites` prints P5's marked programs by Sequential's number, one a line with the name as a comment (`44     #
CARS STRINGS`); the other engines' marks are comment lines, so the file is a valid list as it is.
`p5_shipped_from_favorites.py` orders it: **a marked program that is in the present list keeps its place, its comment
and its group heading** (the list is grouped by hand: pads, brass, keys ..); **the marked programs that were not in
it follow under one heading, by Sequential number**; `--by-number` drops the groups. The unit knows no categories, so
new programs have to be moved into a group by hand. The marks stay on the unit and name the same programs in the
build with the shorter list (`tests/run_prophet_test.sh`: marked with one list, read with another).

## The editor

EDIT on a P5 part shows its 19 deep pages through the editor's title rules (docs/EDITOR.md §4; no screen plan, no
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
| MOD | `VOICE` | Unison (it switches the part: [UNISON](#unison)) · Voices (1..4) · Detune (0..7) · Glide / Priority (LOW, LOW R, LAST, LAS R: R retriggers) · Vintage |

The 57th panel byte, BEND (the program's own bend range), has no cell: the part bends by the channel's range. A switch
byte other than 0 reads "on", VOICES 0 (as most factory programs have it) reads 4, and so does a stored 5: what plays. Value names are at most
five characters (a cell's width: Melodee's "CURTIS" and "LOW RET" would be cut). No matrix destination of its own
("not modulatable"); the platform's routes still act on the pitch, the cutoff, the widths and the level. An edit marks
the sound edited (`REEDY STRPAD*`) and is kept by the session over a power cycle (the 138-byte blob,
`eng_deep_t.blob_get`: `cr_settings.c css`), like any engine's.

## Saving

**Nothing of a P5 part is saved to a slot.** Since 0.22's favourites the SAVE tap opens the dialog with **the
Favorite choice alone** (`cr_ui.c cu_save_favonly`): KNOB 4 marks the program by its Sequential number; SELECT and
KNOB 1 cannot leave it; **OCT+ is refused**, "P5: no save yet", and nothing is written (until c176dd4 the tap itself
was refused). Why not the
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

So the simplest thing that cannot lose data is to write none. The proper store is phase B (and the owner's plan to
load the rest of the 200 from the device manager needs it: a program that is not compiled in has to live somewhere): flash objects in the free
region as `va_store.c` (0xB6000..0xC7FFF; 32 × 138 B), backup ids, the web manager's engine list and patch kind, and
the settings record ([Open decisions](#open-decisions-the-owners)). A record of engine 16 that arrives anyway (a
restored bank) loads as P5's INIT with its parameters.

## Cost on the unit

### Measured on the unit (2026-10-13)

The owner played **cd43720** (five voices of three units, all 200 programs) on the FM-1, a five-voice chord held, and
read the counters:

| counter | reading | |
| --- | --- | --- |
| `pk_part0_us` | **2033** | the chord part's peak; the count below said 1318 for five voices of ITS PROPHET5's kind: **the count is 1.54 × low** for this engine (CZ-1's was 1.7 ×) |
| `pk_block_us` | **2814** of 2902 | the whole half: 97 % |
| `pk_late` | **17** | halves that finished late |
| `stale_halves` | 0 | none repeated: nothing was heard |

Five voices do not fit with a margin. **So the cap is four voices a part**: `P5_POLY` 4, `p5_units` 4 of the 16
(HISTORY 3y39). Four voices are **about 1630 us** by proportion (2033 × 4 / 5 = 1626; by the count's own shape, a
voice 249 us and the part 74: 2033 × 1068 / 1318 = 1648), which would put the half near 2410 us, 83 % (2814 less the
fifth voice: an inference, **the four-voice build has not been measured**). With the bass sounding on one core the
chord part had three voices at four units (about 1260 us by the same proportion); **at three units (3y40) it keeps
four: about 1630 us + the bass, by the same proportion about 2410 + 150 = 2560 us of 2902 (88 %) with a VA bass: an
inference, the thing to measure**; with a P5 bass it is five P5 voices, the 2814 us reading's class.

### Counted

`python3 tools/p5_cost.py` after `./build.sh`. The sample loop (`p5_samples`) is one loop of
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

A voice's block (32 samples): `p5_render` and two `p5_env_tick`, 512 instructions at most. A part's block: `p5_block`
445, the wheel's source 32 × 90, the DC blocker 32 × 12: 3803 (74 us a half). At 205 instructions a microsecond
(docs/FX.md), a half of 128 frames = 2902 us:

| program | a sample: hit | miss | longest | a voice a half | us | 3 voices | 4 | 4 at 1.54 × |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| no waveform switched on, Curtis (the loop's floor) | 294 | 315 | 335 | 42368 | 207 | 694 | 901 | 1390 |
| INIT: saw A, Curtis | 308 | 329 | 367 | 44160 | 215 | 720 | 936 | 1444 |
| saw A + saw B, Curtis | 325 | 346 | 402 | 46336 | 226 | 752 | 978 | 1509 |
| saw A + saw B, SSI | 361 | 382 | 447 | 50944 | 249 | 820 | 1068 | 1648 |
| saw + pulse A, saw + pulse B, Curtis | 349 | 370 | 462 | 49408 | 241 | 797 | 1038 | 1602 |
| saw + pulse A, saw + pulse B, SSI | 385 | 406 | 507 | 54016 | 263 | 865 | 1128 | 1740 |
| saw A + saw B, the wheel on both frequencies, SSI | 378 | 399 | 464 | 53120 | 259 | 852 | 1111 | 1713 |
| saw A + saw B, Poly-Mod to the filter, SSI | 375 | 396 | 461 | 52736 | 257 | 846 | 1103 | 1702 |
| sync: saw A synced to B, Poly-Mod to A's frequency, Curtis | 365 | 386 | 531 | 51456 | 251 | 827 | 1078 | 1663 |
| everything: saw + pulse A, saw + triangle + pulse B, sync, Poly-Mod to frequency, width and filter, the wheel on all five, SSI | 490 | 511 | 797 | 67456 | 329 | 1061 | 1390 | 2145 |

(The voices' columns include the part's 74 us. "4 at 1.54 ×": the count scaled by what the unit read for this
engine.) The cheapest and the dearest program are a factor 1.5 apart; the SSI filter costs 36 instructions a sample
more than the Curtis (seven table look-ups for three); sync and Poly-Mod cost little until an edge falls in the sample
(a divide and two more waveforms, once a cycle of oscillator B). Not in the table: an oscillator's own edges. A saw's
wrap costs two samples a divide and about 15 instructions each (the BLEP), a pulse twice that: at A4 some twenty
divides a voice a half, about 2 % on top if a divide is worth 35 instructions; three octaves up eight times that (an
estimate: the divide's time on this core is not known here; it may be part of the 1.54). As a check of the walk: 382
a sample plus the block's and the part's share is 422 instructions a voice and sample, and the host's count for ITS
PROPHET5 (another compiler, another machine) is 422 too.

**The three engines on one scale.**

| | a voice a half, counted from the listing | a chord | measured on the unit |
| --- | --- | --- | --- |
| CZ-1 (a two-line tone; docs/CZ1.md) | 16 240 instructions, 79 us | 8 voices: 634 us | **1066 us** (1.7 × the count) |
| P5 | 44 160 .. 67 456, 215 .. 329 us (2.7 .. 4.2 CZ-1 voices) | 4 voices: 936 .. 1390 us (5: 1152 .. 1720) | **2033 us for five voices** (1.54 × the count); four not measured |
| VA | no listing count exists (not made here) | | not measured against a count |

For the VA only the host's instruction counts compare (`tests/cpu_baseline.txt`, eight notes asked, the idle block
taken off, per sounding voice and sample; cd43720's figures): CZ-1 BRASS 1 94, VA LUSH PAD 182, P5 INIT 355, ITS
PROPHET5 422. For the two engines with a listing count the unit's count is 1.35 (CZ-1) and 0.94 (P5) of the host's,
so a VA voice is *estimated* at 22 .. 31 thousand instructions a half (107 .. 154 us): **a P5 voice is about two VA
voices and three to four CZ-1 voices; four of them are an eight-voice VA chord.** (An inference from two ratios, not
a count.)

**Melodee's own measurements** (its docs/PROPHET5_PROTOTYPE.md, on an FM-1, 2026-10-08): five voices of a harsh stress
program (sync, audio-rate Poly-Mod, a 500 Hz wheel on every destination) with distortion, chorus, delay, reverb and
USB audio running peaked at **2158 us (SSI) / 2009 us (Curtis)** of the half's 2902, no late block. Those figures are
of an older DSP (before b351a14's exponential Poly-Mod, which its author puts at +8 % on the host, and before 8a3074b),
with the sample loop and four tables in RAM, and with Melodee's effects, not ChoralRoot's. ChoralRoot's five voices
of a plain two-saw program from flash read 2033 us for the part alone: the same order, and late where Melodee's were
not. Whether RAM for the hot code (`P5_RAM`) buys the fifth voice back is not known (it changed nothing for the
effects' loops: docs/FX.md).

**The emulator's estimates** (`sh tools/emu/perf.sh`, scenarios (p) and (q); host instructions / 259: ESTIMATES, low by
1.2 to 2.2 for other stages, docs/FX.md), a 6-key ITS PROPHET5 chord with the default sends, us of 2902, avg / max.
**These are cd43720's, with five voices of three units; not run again for four** (the chord part's column would be
about a fifth lower, and "chord voices" 3 and 4):

| | block | chord part | bass part | fx | master | chord voices |
| --- | --- | --- | --- | --- | --- | --- |
| (p) + the P5 bass (FUNK BASS II), one core | 1070 / 1700 | 616 / 990 | 161 / 321 | 215 / 460 | 23 / 95 | 4 (the budget) |
| (p), Dual Core On | 1222 / 1905 | 759 / 1194 | 159 / 431 | 212 / 427 | 25 / 100 | 5 |
| (q) + the VA bass (PUNCH BASS), one core | 1154 / 1672 | 745 / 1052 | 65 / 135 | 258 / 431 | 24 / 80 | 4 |
| (q), Dual Core On | 1338 / 1777 | 918 / 1196 | 63 / 173 | 258 / 550 | 27 / 62 | 5 |

### Does it fit?

Five voices did not, by the unit: 2814 of 2902 us with 17 late halves. With four (all figures from the one reading,
by proportion: inferences until the four-voice build is read):

| on one core | the chord part | the half, about |
| --- | --- | --- |
| 5 chord voices, no bass (cd43720, **measured**) | 2033 us | 2814 us, 97 %, late |
| **4 chord voices, no bass** (the cap) | about 1630 us | 2410 us, 83 % |
| **3 chord voices + a VA or FM6 bass** (what the budget gives) | about 1260 us | 2040 us + the bass (a VA bass about 150 by the count): 2200 us, 76 % |
| 3 chord voices + a P5 bass (one voice, 4 units) | about 1260 us | 2040 + about 500 (a P5 voice and its part, × 1.54): 2540 us, 88 % |
| 3 chord voices + a UNISON P5 bass (it keeps one voice under a chord: [UNISON](#unison)) | about 1260 us | as the row above |

- **One core (the default)**: four voices; three whenever the bass sounds.
- **Dual Core On**: the bass part's render moves to core 1 (docs/DUALCORE.md: `track_render` of `trk[1]` is the one job
  there is), so a P5 bass costs core 0 nothing and the chord part has its own 16 units: four voices. **P5's chord
  voices cannot run on core 1 today.** They could: `p5_samples` writes its own voice state and its output only, and
  reads the program, the wheel's block and its parameters (Melodee f12b310 hands every second voice to its worker
  that way); it needs a second kind of job and a private output buffer, and is not built. That, or the loop built
  per program, is the way back to five.
- **The bass part on P5** is not restricted in code. It is the dearest bass there is and takes four budget units a
  voice (sixteen for a four-voice UNISON program).
- If the unit wants less still: the loop built per program (as CZ-1's 43 loops) saves 20 to 28 % (the tool's "built
  alone" column: 276 for 361 on the two-saw SSI row: the switches' tests and what the compiler cannot drop while a
  switch may be on); the SSI filter's seven look-ups; the coefficient set-up once a block instead of once a sample
  when only the envelope moves it.

**To read next on the unit** (this tree): `pk_part0_us`, `pk_block_us`, `pk_late` with a four-key ITS PROPHET5 chord
held (expected about 1630 / 2410 / 0); the same with the bass on; a UNISON program (LAGOON BASS) on the chord part,
and on the bass part under a chord.

## Tests

- `tests/cr_prophet_test.c` (in `sh tests/run_cr_tests.sh`), on the firmware as the emulator builds it: the engine's
  place and its name (P5); the bank (Sequential's 200 from the `.syx`: 40 with UNISON, 80 on the SSI filter, 25 with
  sync) and **the shipped list** (each shipped program unpacks to the `.syx`'s frame of its own number, encodes back
  to its bytes, and is found by that number; decode / encode round trip; bad frames refused); the names; the blob;
  **every shipped program through voice.c** (a four-note chord: none silent, no sample beyond the voices' sum, no DC,
  every voice free after its release, exact silence then); 410 stress programs; note-off; a note struck again (no
  step beyond the wave's own); **the four-voice cap, the units, which keys of a five- and a six-key chord sound,
  four voices kept with the bass sounding (three units a voice)**; the SAVE dialog (Favorite alone, KNOB 4 marks by
  the Sequential number, OCT+ refused, no record) and the browser's KNOB 4; the favourites across three lists
  (`--fav-out` / `--fav-in`); the editor's UNISON cell and GLIDE; **UNISON on a load** (every shipped
  program on both parts; a 4-note chord on a LOW and on a LAST program: four voices on one note, which note, 16
  units; three with a bass; held keys over a load both ways; the editor's cell after a load; another engine by a
  pool load and by the editor's engine switch; the session's record); the bend through the part's MIDI; the 19 pages
  (every cell sets its byte and no other, clamps, reads back; each of the 56 bytes on exactly one cell); the pool;
  SAVE refused.
- `tests/run_prophet_test.sh`: the stage-36 identity (above), `FELUCCA_P5_FACTORY=0`, and **the list**: the test
  built with all 200 and with a list of seven passes; a program's render is the same samples in each build; a
  session record written with one list is read with another ([The shipped list](#the-shipped-list)).
- `tests/regress.c`: P5's golden renders and CPU entries are the generated list's, named by the program's number
  (`preset/P5/044_CARS_STRINGS`, `cpu/P5/..`): **246 goldens** (197 of the other engines, untouched lines, + INIT and
  the 48). All 49 P5 hashes differ from cd43720's for the same programs: the regression's phrase ends on a five-note
  chord, which is four voices now, and the five UNISON programs load in UNISON. `tests/target_budget.txt`:
  `p5_samples`, `p5_render`, `p5_mod_samples`, `p5_post`, `p5_block`.
- `tools/emu/scripts/cr_prophet.txt` in `tools/emu/test_cr.sh`: OPT + PRESETS to P5, three places of the list (the
  names read from the generator), a chord each, the ten editor screens, an edit that is heard, SAVE refused ("P5: no
  save yet"), the two engine pickers with six engines. The screens with the shorter name were looked at (the two
  pickers, the pool's read-out, the save message, the editor's titles: centred, nothing cut).
- `tools/emu/test_persist.sh`: the list's first UNISON program, edited, over a power cycle (SAVE refused, the global
  save, the same flash file again: the part powers on with the program as edited and in UNISON).
- `tests/run_cr_tests.sh` builds `tests/sound_templates.c`'s check of the web tools' tables without the engine: the
  Sound Manager and `fm1_install.py` do not know engine 16 (phase B).
- Not tested: the tree as it is on a unit (cd43720 was played once); a UNISON program by ear; the bass part on a
  UNISON program under a chord; the browser emulator beyond its own suite; MIDI program changes into the pool; the
  wheel (CC 1) and aftertouch by ear; `tools/prophet_listen.sh` and `tools/emu/perf.sh` after their changes for the
  list (the listening tool's A/B program was compiled and run for one program, not the whole script).

## Listening

`sh tools/prophet_listen.sh` -> `../audio-0.22/prophet/` (README.txt there): D, Bm, G, A, D on eight programs (ITS
PROPHET5, FOREVER KEYS, STABBY BRASS, REEDY STRPAD, CARS STRINGS, VINTAGE WRLY, SYNCHROTRILL, FUNK BASS II: all in
the provisional list; the tool finds their places and skips one the list does not ship), each dry and with the
default sends, through the emulator; and three programs rendered by the remix's engine file and by this tree's
(`tools/prophet_ab.c`, the engine alone, this tree's side built with all 200): 01 and 15 differ by the DC blocker and
the interpolated knee only, 33 PICKLE PINCH is the program 8a3074b repaired (its Lo Freq B). **The files there are
cd43720's (five voices); not rendered again.**

## Open decisions (the owner's)

Decided on 2026-10-13 (HISTORY 3y39): the name (**P5**), **UNISON on a load**, **a trimmed list** (the mechanism;
the 48 are provisional), and, by the unit's reading, **four voices**.

1. **The list itself**: the 48 were chosen from the parameters, not by ear. Edit `shipped.txt` after playing
   ([the steps](#the-shipped-list)).
2. **Sequential's data in a release at all** (`FELUCCA_P5_FACTORY`): the data are Sequential's ("no licence granted",
   as Melodee words it and as ChoralRoot words Casio's CZ-1 tones). 48 programs are 4.7 KB of flash, 200 are 18.6.
3. **The unit again**: the four-voice build's counters ([To read next](#does-it-fit)), and UNISON by ear, on the
   chord part and on the bass.
4. **A UNISON bass on one core** is four voices thick alone and one voice under a chord ([UNISON](#unison)): the
   bass's weight changes when the chord comes in. To hear on the unit; the alternatives are a capped stack on the
   bass part or no UNISON on its loads.
5. **Phase B, the store** and **the device manager's "load all 200"**: where (the free region 0xB6000..0xC7FFF;
   200 programs of 138 B are 27.6 KB unpacked), the backup ids, the web manager's engine
   list, and the **settings record**: P5 has no place among its eleven `pool_pos` slots (`cr_settings.c crs_slot`: −1,
   handled), and its `chord_sound` / `bass_sound` hold a pool position, not a program's number
   ([The shipped list](#the-shipped-list)). The sound a part plays at power-off comes back, edited or not, whatever
   the list (the session); what is not remembered over a power cycle is the place a part had in P5's pool when it
   is on another engine (OPT + PRESETS back to P5 lands on 01).
6. **Flash**: XIP is at 88.2 % with the 48 (87.3 % with INIT alone, 90.6 % with all 200).
7. **A fifth voice**: chord voices on core 1, the loop built per program, or RAM for the hot code and tables
   (`P5_RAM`, `ram=True` in `tools/gen_tables.py`: RAM has 16.9 KB free, RAMTEXT 21.5 KB; the hot code would take 3.8
   of it, the four per-sample tables 5.4 KB of RAM). None is built or measured.
