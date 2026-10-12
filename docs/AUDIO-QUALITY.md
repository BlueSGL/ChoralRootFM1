# Audio quality: the output chain, what was measured, and Options > Output (0.19)

GitHub issue 3 (a user on 0.17 or older, headphones on the 3.5 mm output): "muffled / harsh compared to other
firmwares, especially in the mid to high frequencies (bass sounds good) ... Sloop sounds a lot clearer, fuller ...
all the engines ... maybe it is just level matching / in the output stage". This document holds what was found
(2026-10-12), what 0.19 changes, and how to measure it again. Inference is marked as inference.

**Two rounds.** The first compared with the Sloop checkout at hand, which was **v2.2**. Sloop was at 2.6 by the time
of the report (2026-10-11) and 2.6 changed its master. The second round, the same day, measured **Sloop 2.6** and read
Felucca up to 1.5.1: [Sloop 2.6 and Felucca 1.5.1](#sloop-26-and-felucca-151-the-second-round), which also says what
of the first round no longer holds. Where this document says "Sloop" without a version it is 2.2. The second round
also gave Clean's limiter its look-ahead: [The look-ahead](#the-look-ahead).

Levels are the Q15 sample before the DAC word's `<< 7` unless said otherwise, in dB re 32768; MASTER at its top
(`master_q12` 4088). "dist" is the harness's measure, explained under [How to measure](#how-to-measure).

## Short answers

- **The signal path is the same Felucca chain in ChoralRoot and Sloop.** At equal part level the same patch gives
  the same output to 0.1 dB and the same spectrum (PHASE BRASS, a dry note: 0.17 -17.9 dB rms, 38.9 dB under the total
  above 2 kHz; Sloop -17.8 and 39.0).
- **What can explain the report on 0.17**: (1) level: at the same MASTER position 0.17's single notes are a median
  9.4 dB (10 LU K-weighted) under Sloop's, its chords 5.6 dB, its power-on sound 13.8 dB; (2) mono: 0.17 sends the
  same word to both ears on 164 of 168 sounds, Sloop's chorus and reverb return in stereo; (3) the mono chorus's comb
  (computed: +2.7 / -3.9 dB, the same in both ears, on every sound with a chorus send, not on the bass). That these
  are heard as "muffled" is inference; that they are there is measured.
- **What cannot**: the FX amounts (Sloop is as wet), the reverb's tone, the DAC set-up (identical, pin for pin), a
  hidden filter. The first round put the limiter and the clip here too (Sloop 2.2 drives its own far deeper and
  distorts more). **Against Sloop 2.6 that no longer holds**: its master now looks ahead, and on a loud chord it
  leaves 8 to 11 dB less of its own above 2 kHz than 0.17's does on the same mix
  ([the second round](#sloop-26-and-felucca-151-the-second-round)). On loud chords at a high MASTER the output chain
  is a candidate for "harsh" after all; it is what Clean replaces.
- **What was wrong all the same, and is what 0.19 changes**: the output chain distorted more than it should (a chord
  with the bass in gain reduction all of the time, about 4 % of distortion; a tanh over the whole range; MASTER in
  front of a 16-bit path, so a low MASTER left a quiet note with a quarter of its bits), and a setting of Felucca's
  that ChoralRoot cannot show could be left running by another firmware ([Left behind](#left-behind)).
- **Not fixed by 0.19's output chain**: the level against Sloop (a decision about the part level and the sounds'
  trims, [Open questions](#open-questions-and-proposals)), the chorus's mono return.

## Part 1: ChoralRoot main, 0.17 and Sloop, stage by stage

By reading, and by rendering all three on the host (ChoralRoot through its key matrix with `tests/master_measure.c`;
Sloop with a harness that feeds notes to its `voice.c` at its keys' velocity; every render's captured input through a
copy of the master equal to the firmware's output bit for bit). The scripts and the raw tables (1600 lines) are the
measurement's scratch (`build/scratch/part1/RESULTS.txt` of the 0.19 worktree), not in the tree. Paths under
`firmware/src/`.

| # | Stage | ChoralRoot main / 0.17 | Sloop | Effect of the difference |
| --- | --- | --- | --- | --- |
| 1 | per-voice level | `VOICE_FS` 24000 (`dsp.c`) | the same | none |
| 2 | velocity | 100, linear | 100, linear | none |
| 3 | part LEVEL | **92** (-10 dB; `cr_ui.c`: the default minus 12) | **104** (-4 dB) | **-6 dB on every sound** |
| 4 | per-sound trim | seven sounds, all down 3 .. 8 dB (`cr_bank.c`) | every sound, -13 .. +11.5 dB, 26 of 68 at +7 dB or more (`preset_trim.h`) | with 3: single notes a median 9.4 dB under Sloop's |
| 5 | DIST | 0 | 0 | none |
| 6 | mix line | exact (`(x * gl) >> 12`) | `((x >> 4) * gl) >> 8`: each part to multiples of 16 | Sloop is the noisier (same patch: 10 dB more energy above 10 kHz, all of it arithmetic) |
| 7 | pan, dry stereo | centre; VA SPREAD only | centre, mono | none |
| 8 | FX switch, sends | FX on. 0.17: per sound (TINE EP chorus 45, delay 25, reverb 35; default 24 / 28 / 36). main: 35 / 45 / 25 for every sound | no switch; per sound, the same 24 / 28 / 36 default; 61 of 68 sounds have chorus or reverb | wet re dry: 0.17 -7.7 .. -14.2 dB, main -6.2 .. -6.8, Sloop -9.7 .. -13.8: **Sloop is not dry** |
| 9 | chorus | one tap, returned equal to L and R | two taps half a turn apart, L and R | **0.17 and main: a mono comb**, +2.7 / -3.9 dB (computed); Sloop: a different comb per ear |
| 10 | delay | 0.17: mono, full rate. main: stereo | as 0.17 | |
| 11 | reverb | 0.17: ROOM, mono, full rate. main: stereo FDN at 22.05 kHz | stereo FDN, full rate | 0.17's is as bright as Sloop's (share above 2 kHz -0.9 against -1.0 dB) but **mono**; main's lacks the wet above 10 kHz |
| 12 | master sum, MASTER | `((mix + wet) >> 2) * master >> 10`; `(k10 * k10) >> 8` | the same | none |
| 13 | DC blocker | 2 Hz | the same | none |
| 14 | LOWCUT / BASS+ | 0 on a new unit; **no control** | 0; its menu has it | see [Left behind](#left-behind) |
| 15 | limiter | `LIM_T` 18000, the gain eased over 64 samples (`fx_smooth`) | 18000, 4-sample attack, the gain at once | at equal input within 2 dB, ChoralRoot's the cleaner |
| 16 | clip | a tanh over the whole range (`softclip`) | `knee()`: linear to 16384, tanh above | ChoralRoot adds -45 .. -52 dB to a single note where Sloop adds nothing |
| 17 | DAC word | `<< 7`: the word's top 6 dB unused, no dither | the same | none |
| 18 | DAC set-up (`hal/fm1_audio.h`) | | identical in Felucca, Sloop, Melodee, x0x and ChoralRoot, write for write | none |

Measured, the same MASTER position:

| RMS / K-weighted, dB | 0.17 | main | Sloop |
| --- | --- | --- | --- |
| all factory sounds, a single note, median | -26.8 / -24.5 | -25.4 / -23.5 | -17.4 / -14.5 |
| all factory sounds, a 6-note chord, median | -19.7 / -16.8 | -18.7 / -16.0 | -14.1 / -11.0 |
| the power-on sound, a single note, dry | -26.9 / -32.2 | -26.9 / -32.2 | -13.1 / -10.7 |
| the same patch (PHASE BRASS), a single note, dry | -17.9 / -15.6 | (retired) | -8.2 / -5.9 |
| the same patch at nearly the same level (WHEEL GOSPEL) | -21.1 / -19.0 | -21.1 / -19.0 | -19.9 / -17.9 |

| Share of the energy above 2 kHz / 5 kHz, dB re total | 0.17 | main | Sloop |
| --- | --- | --- | --- |
| all sounds, a single note | -22.7 / -38.2 | -24.4 / -37.0 | -25.1 / -45.8 |
| all sounds, a chord, above 2 kHz | -17.4 | -19.3 | -18.3 |
| the power-on sound, a single note | -52.6 / -53.8 | the same | -11.7 / -26.8 |

ChoralRoot's sound set is not darker than Sloop's. Its power-on sound is: TINE EP at D4 is close to a sine, Sloop's
RHODES is not. A first impression, not "all the engines".

The master: one note is already over the limiter's threshold on 33 of Sloop's 68 sounds (2 of 168 on 0.17); a chord
on the shared BRASS with the bass is 19.3 dB over it on Sloop (15.8 dB of gain reduction, dist -22.9 dB) and 7.5 dB on
0.17 (-4.7 dB, dist -28.0 dB). Sloop's output chain is the more distorted of the two as shipped. It is also louder.

### What can explain "muffled / harsh, fine bass, all engines, headphones, 0.17 against Sloop"

1. **Level** (measured). 6 dB from LEVEL 92, the rest from Sloop's trims. Every engine. The reporter's own guess. A
   quieter source is commonly judged duller (inference: the ear's response at lower levels).
2. **Mono** (measured). The width of Sloop's chorus and reverb on headphones; 0.18 made ChoralRoot's delay and
   reverb stereo, the chorus's return is still mono on main.
3. **The mono chorus's comb** (level measured, comb computed, audibility inferred): on every sound with a chorus
   send, not on the bass part (send 0): "mid to high, bass fine".
4. **The power-on sound** (measured): a first impression only.

### What cannot

- **The FX amounts, the tails, the reverb's tone**: Sloop is as wet; FX on against off never lowers the energy above
  2 kHz by more than 0.3 dB on any of the three. Switching the FX off on 0.17 makes it drier and takes the comb away,
  not brighter.
- **The limiter and the clip**: see above. What ChoralRoot's tanh adds to a single note is real but 0.3 .. 0.6 %,
  low order (inference: not "harsh").
- **The DAC**: below.
- **The identical stages**: 1, 2, 5, 12, 13, 17, 18.

### The DAC, and "a built-in filter"

The four lines `fm1_audio_init` sets (PC0, PC1, PC2, PC6; the HAL's comment calls them "codec control lines") are
the I2S bus itself: JieLi's AC7911B data sheet names PC0 ALNK0 MCLK, PC1 SCLK, PC2 LRCK, PC6 DAT3, and the HAL uses
channel 3. None of them is a strap (a de-emphasis, a filter select, a mute); the writes only make them outputs before
ALNK0 takes them. The DAC chip is not identified anywhere in the material at hand (no schematic, no part number in any
firmware's source; the stock firmware's package is opaque); no firmware drives a codec register, an amplifier enable
or a volume. Whatever filter the DAC has is fixed on the board and the same for every firmware: it cannot be the
difference to Sloop. The HAL is untouched.

Two facts from this: the samples are right-aligned in the 32-bit word (not "left-justified" as the HAL's comment
says), and **the word has 6 dB above what ChoralRoot, Felucca and Sloop send** (`OUT_SHIFT` 7: the Q15 sample's full
scale is half the 24-bit word's). fm1-x0x has an option that uses it ("LOUD: 6 dB more at the headphones and line
out", `fm1-x0x/firmware/src/app/engine.c`). See [Proposed](#proposed-not-built-in).

## What runs between an engine and the DAC on a freshly reset unit

The owner's question: could something be switched on silently? Everything, with its default, on 0.17 and main. The
effect is on a single dry note.

| Stage | Default | Seen / switchable | Effect |
| --- | --- | --- | --- |
| the sound's own patch (filter, envelopes, its LFOs; FM6's 20 Hz DC filter) | the sound's | yes (EDIT) | the sound |
| the part's LFO, Felucca's matrix (`mod.c`), motion | the LFO runs, every depth 0, no slot | no | none |
| velocity | 100 | yes (Options) | Felucca engines: -2.1 dB against 127 |
| voice mode, glide, detune | POLY (bass MONO), 0, unison only | yes | none |
| part LEVEL | 92, both parts | the sound's pages; the bass: a knob | -6 dB against Sloop's 104; no spectral effect |
| per-sound trim | 0; seven sounds down | no | -3 .. -8 dB on those |
| FX switch | **on** | yes (FX) | 0.17: off closes the chord part's sends only, the bass keeps reverb 10; main: both |
| chorus at 45 | on (0.17: TINE EP's; main: every sound) | yes | wet -8.9 dB; +0.3 .. +0.8 dB; the mono comb |
| reverb at 35 | on | yes | wet -14 .. -19 dB (0.17), -10 .. -14 dB (main) |
| delay at 25 | on | yes | wet -17 .. -18 dB (0.17), -10 .. -16 dB (main) |
| Felucca's FX hold layer, SLICER | not built on main; inert on 0.17 | | none |
| **LOWCUT / BASS+ (`settings.lowcut`)** | 0 on a new unit | **no: neither shown nor switchable** | [Left behind](#left-behind) |
| DC blocker, limiter, tanh clip | always | no (0.19: Options > Output) | Part 2 |
| USB Level, Dual Core, USB Record, the click | Master, Off, On, silent | yes | none (Dual Core: the same mix code) |
| a speaker EQ, a headphone detection | none exists | | |

None of these defaults, switched off, raises the share of the energy above 2 kHz on 0.17. The measurable gains there
are level (LEVEL, the trims) and stereo returns.

## Left behind

**Felucca's SPEAKER setting survived a flash to ChoralRoot, and ran with no way to see it or switch it off.**

Felucca's settings (`panel.c` `settings {magic, palette, lowcut, zoom}`) are the head of the flash object ChoralRoot
keeps its own block in (`settings_persist.c` `persist_t`, object 0 at 0xFC000 / 0xFD000; no installer erases the data
sectors). `lowcut` is Felucca's MENU > SPEAKER: 1 LOWCUT (two high-passes near 110 Hz), 2 BASS+ (near 220 Hz, plus
harmonics made from the bass, `fx.c spk_bass`), in the master, on everything, the USB recording included. ChoralRoot
read it (`settings_import`), applied it (`settings_init`: `fx_lowcut = settings.lowcut % 3`), wrote it back with every
save (`settings_export`), carried it in a backup and a restore (`cr_backup.c`), and did not clear it with Options >
Reset Settings. Its own UI never had the entry.

- **Who can have it**: a unit that ran **Felucca 1.0 or later** (a PER4 object, the same layout) with SPEAKER at
  LOWCUT or BASS+ and was then flashed with ChoralRoot. Current Sloop and current Melodee write the object at another
  size, which ChoralRoot rejects (defaults); their older builds wrote sizes it accepts. fm1-x0x uses another storage
  magic.
- **What it did** (0.18's emulator on a seeded flash against a clean one, a CZ-1 chord with the bass, the same
  script; `tools/emu/seed_lowcut.py`):

  | band | LOWCUT | BASS+ |
  | --- | --- | --- |
  | under 100 Hz | -17.5 dB | -18.8 dB |
  | 100 .. 300 Hz | -1.5 dB | -4.0 dB |
  | 300 Hz .. 1 kHz | -0.3 dB | -1.4 dB |
  | above 1 kHz | 0 | -0.2 dB |
  | total | -1.3 dB | -2.4 dB |

- **Does it explain the report?** No, as it stands: it thins the bass, and the report says the bass is good. It is a
  defect of its own, found while looking.
- **0.19**: ChoralRoot holds the stage off. `cr_settings_boot` sets `settings.lowcut` and `fx_lowcut` to 0 before
  anything applies them (the flash object, a rejected one, and the copy in RAM that survives a reset all go through
  it); the next write of the object stores 0, so the flash is clean for whatever firmware comes next; a restore
  (`cr_backup.c crb_commit`) stores 0; Reset Settings clears it. Felucca's own builds and tests are unchanged
  (`panel.c`, `settings_persist.c` untouched). In both Output modes. `tools/emu/test_persist.sh` boots on flash images
  seeded with BASS+ in a ChoralRoot object and LOWCUT in a Felucca one.
- **On 0.17 / 0.18 today**: Reset Settings does not clear it and neither does switching off. What does: restoring a
  backup whose settings object has it off (a backup made on the affected unit carries it), or flashing Felucca,
  setting MENU > SPEAKER to OFF there, and flashing ChoralRoot again. Updating to 0.19 clears it.

Everything else ChoralRoot can inherit from another firmware's flash, read from the code:

| Value | Object | Written compatibly by | ChoralRoot | A foreign value's effect on the sound | Can be left behind |
| --- | --- | --- | --- | --- | --- |
| `lowcut` | settings (0) | Felucca 1.0+, older Sloop / Melodee | used, not shown, not checked (until 0.19) | the table above | **yes: fixed in 0.19** |
| Felucca's palette, `zoom` (LEDs) | settings | Felucca, older Melodee | read, overridden or unused | none | yes, harmless |
| the panel calibration | settings | Felucca, older Melodee / Sloop | used, shown (Calibrate), validated | none (a wrong table swaps controls) | yes, intended |
| `bold` (Hold Time) | settings | Felucca 1.0 | used, shown, validated; Reset clears | none | yes, visible |
| favourites | settings | Felucca, Melodee | not used, passed through | none | yes, inert |
| ChoralRoot's block, the session | settings, 18 | ChoralRoot only | own magic and checksum | (cannot be made by another firmware) | no |
| user sounds (32 slots) | 5, 6 | Felucca, Sloop, older Melodee | appear as user presets after the factory ones; validated, clamped | only when picked | yes, visible |
| FM6 bank voices | 7 | Felucca 1.0, Melodee | bank voices; validated | only for a sound that points at one | yes, not silent |
| Felucca / Sloop projects (track levels, sends, filters, patterns, tempo, tune) | 1 .. 4 | Felucca, Sloop, Melodee | **never read as projects**: `project.c` is not in ChoralRoot's unit; the same sectors are ChoralRoot's sound stores, whose own magic rejects them | none: the parts' parameters come from the tables' defaults | bytes stay, never interpreted |
| samples, slices, motion, song chain, MIDI and tune globals | in projects and sample slots | Felucca | not built | none | no |

`lowcut` is the only value another firmware can leave that changes ChoralRoot's sound without being seen.

## Sloop 2.6 and Felucca 1.5.1 (the second round)

Sloop's sources read from its checkout (now v2.6) and from the first round's copy of v2.2, file against file; both
built on the host with the first round's harness (the 2.6 master's shadow: its `lim_block` and `knee`, checked
against `mix_block`'s own output on all 52 renders: exact) and the same renders. Felucca v1.0.1 .. v1.5.1 from
GitHub's compare and the 19 commits' diffs (the local checkout could not be read with git from this worktree). The
scripts and raw tables are scratch (`build/scratch/part1/` of the round's worktree).

### What Sloop changed between 2.2 and 2.6 on the way to the DAC word

| Stage | 2.2 | 2.6 | Effect |
| --- | --- | --- | --- |
| limiter (`fx.c lim_block`, 2.6) | per sample: the envelope 4 samples behind the peak, the gain at once, `(x >> 4) * g >> 11` | **looks one block ahead**: the output is the mix 32 samples (0.73 ms) late; per block the peak of the 32 samples in the delay and the 32 new ones (a scan of both: 128 compares a block), the envelope at that peak at once; the gain a straight line across the block to `LIM_T / envelope` (one divide a block); a 64-bit product. Threshold still 18000. A **hold** of 48 blocks (35 ms) restarted by any peak within 0.5 dB of the envelope, then a release of 46 ms (`LIM_REL` 11; it was 93 ms). The delay is two int32 lines of 32 in RAM (`.bss`); a second copy of all of it for USB AUDIO = FULL | no sample leaves the limiter over 18000 (-5.2 dBFS: every loud render peaks exactly there; 2.2's went to -3.3). Their notes: "loud kicks and basses no longer crackle with MASTER turned up", "a whistle on loud basses" (the 12-bit product, and the gain creeping back at every cycle before the hold) |
| clip (`knee`) | linear to 16384, tanh above | the same | none; but nothing over 18000 reaches it now, so it bends only the top 0.8 dB of a limited peak |
| MASTER | a step a UI frame | ramped a sample across the block (`master_cur`) | no zipper on the knob |
| part level, trims (`preset_trim.h`, `tools/level_presets.py`) | LEVEL 104; a trim for each of 68 sounds | LEVEL 104; **153 sounds**, the 68 old trims unchanged, the 85 new ones made the same way: BS.1770 integrated loudness of a phrase that fits the sound to **-15 LUFS** at the default LEVEL (held chords -16), in half dB, clamped to LEVEL 127: 35 of 153 sit at the clamp (+11.5 dB) and are still under the target | the same 68 sounds: a single note the same to 0.1 dB (median; -0.7 at most), a chord 0.9 dB **quieter** (the limiter no longer overshoots). The 85 new sounds are a median 3.4 dB quieter than the old 68 on a single note |
| mix line | `((x >> 4) * gl) >> 8` | the same | none |
| new stages, all off or at 0 until asked | | per-track INSERT and FILTER, TEXTURE, the metronome, punch-in FREEZE, send throws | none on a plain note (bit for bit when off, by their comments) |
| kick voice (`drum_synth.c`) | level steps at a block's edge | the level glides exactly to its target (`a0 + r (i + 1) / CTL`) | "a whistle on the kicks": tones at 1.4 / 2.8 kHz gone. (ChoralRoot has no drum voice.) |
| chorus, reverb, delay | two taps L / R; stereo FDN; mono delay | the same (a FREEZE path added) | none |
| `OUT_SHIFT`, dither, the DAC set-up | 7, none | the same | none |
| USB audio | 44.1 kHz | 48 kHz too (a polyphase FIR), a full-level tap with its own limiter | not this document's |

### The same renders, Sloop 2.6

Every factory sound (2.6: 153; FX as loaded, no bass, velocity 100, MASTER full), medians; ChoralRoot's from the
same sweep of this tree (Classic reproduces the first round's main to 0.1 dB):

| RMS / K-weighted, dB | 0.17 | main Classic | main Clean | Sloop 2.2 (68) | Sloop 2.6, the same 68 | Sloop 2.6, all 153 |
| --- | --- | --- | --- | --- | --- | --- |
| a single note, median | -26.8 / -24.5 | -25.4 / -23.5 | -27.9 / -26.0 | -17.4 / -14.5 | -17.4 / -14.4 | -19.1 / -16.8 |
| a 6-note chord, median | -19.7 / -16.8 | -18.7 / -16.0 | -20.6 / -18.4 | -14.1 / -11.0 | -15.1 / -12.0 | -15.7 / -12.2 |
| the power-on sound, a single note, dry | -26.9 / -32.2 | -26.9 / -32.2 | -29.3 / -34.7 | -13.1 / -10.7 | -13.1 / -10.7 | |
| the loudest chords (six notes and the bass, FX) | | -14.0 .. -14.8 / -11.8 .. -12.6 | -12.3 .. -13.5 / -10.5 .. -11.2 | -13.0 .. -13.6 / -10.6 .. -11.8 | -13.9 .. -15.1 / -12.0 .. -12.7 | |

| Share of the energy above 2 / 5 / 10 kHz, dB re total (medians) | main Classic | main Clean | Sloop 2.2 (68) | Sloop 2.6 (153) |
| --- | --- | --- | --- | --- |
| a single note | -24.3 / -37.0 / -48.8 | -24.7 / -37.5 / -49.0 | -25.1 / -45.8 / -54.2 | -24.1 / -40.0 / -49.3 |
| a 6-note chord | -19.3 / -34.9 / -46.6 | -19.4 / -35.0 / -47.5 | -18.3 / -36.2 / -51.6 | -15.7 / -33.3 / -46.1 |

**The level gap to Sloop 2.6**, the same MASTER position (all 153 of its sounds; in brackets against the 68 it shares
with 2.2):

| | 0.17 | main Classic | main Clean |
| --- | --- | --- | --- |
| a single note (median) | 7.7 dB, 7.7 LU (9.4 dB) | 6.3 dB, 6.7 LU (8.0 dB) | 8.8 dB, 9.2 LU (10.5 dB) |
| a 6-note chord (median) | 4.0 dB, 4.6 LU (4.6 dB) | 3.0 dB, 3.8 LU (3.6 dB) | 4.9 dB, 6.2 LU (5.5 dB) |
| the power-on sound, a single note | 13.8 dB | 13.8 dB | 16.2 dB |
| the loudest chords (loudness) | | about level | **1 to 2 LU over Sloop 2.6** |

The first round's 9.4 / 5.6 / 13.8 dB (0.17 against 2.2) become 7.7 / 4.0 / 13.8 dB against 2.6: the single-note gap
is smaller only because 2.6's 85 new sounds are quieter ones; on the sounds both versions have it is the same 9.4 dB,
and the chords' gap is 1 dB smaller because Sloop's limiter no longer overshoots. Sloop is still as bright as
ChoralRoot's set and no brighter on a single note; its chords have 3.6 dB more above 2 kHz than Classic's (median).

**The master's own work.** One note is over Sloop 2.6's threshold on 76 of 153 sounds (0.17: 2 of 168; Clean: 0 of
135), a chord on 142. Its own loud chords (CZ BRASS, SYN BRASS, RHODES, each with the bass) run 9.7 to 18.7 dB of gain
reduction all of the time. What the chain leaves ("dist", and the same above 2 kHz relative to the output's own
content there, "top"):

| dist / top, dB | Sloop 2.2 | Sloop 2.6 | 0.17 / Classic | Clean (with the look-ahead) |
| --- | --- | --- | --- | --- |
| Sloop's CZ BRASS + bass, FX, as it plays (19 dB over) | -23.3 / -17.1 | -26.5 / -24.6 | | |
| Sloop's RHODES + 808 (the power-on pair, 17 dB over) | -25.9 / -25.6 | -29.3 / -29.2 | | |
| **the same input for all four**: ChoralRoot's VA chord6 + bass, FX | -30.2 / -27.2 | -33.2 / -32.8 | -29.4 / -25.0 | -52.6 / -53.5 |
| the same: WHEEL | -29.3 / -27.9 | -31.4 / -30.9 | -28.4 / -26.2 | -40.2 / -39.1 |
| the same: CZ-1 | -27.1 / -14.7 | -31.2 / -28.5 | -28.2 / -17.1 | -42.1 / -40.8 |
| the same: CZ-1, velocity 127 | -27.0 / -16.8 | -30.7 / -28.5 | -28.1 / -18.6 | -40.0 / -39.4 |
| all sounds, a 6-note chord, median dist | -29.5 | -35.1 | -33.4 / -32.5 | -69.4 |
| the transient test, the kick 6 / 12 dB over ([below](#the-look-ahead)) | -19.7 / -13.0 | -19.8 / -13.4 | -20.6 / -13.8 | -22.9 / -14.0 |

(The same input: ChoralRoot's captured mix put through each master at its own input line; Sloop's and Classic's
thresholds are the same 18000, Clean has 5.7 dB more room. "dist" counts a limiter's fast gain movement as well as
what it bends: on the kicks it is nearly all gain movement, in every chain.)

On the same loud chord Sloop 2.6's master is 2 to 4 dB cleaner overall than 2.2's and than Classic's, and above 2 kHz
5 to 11 dB cleaner than Classic's (CZ-1: -28.5 against -17.1). Clean is 9 to 20 dB under Sloop 2.6 on the same input.

### What the first round got wrong

- "The limiter and the clip cannot explain the report: Sloop drives its own far deeper and distorts more." True of
  2.2. **Against 2.6 it is the other way round above 2 kHz**: at the same input Sloop 2.6 leaves 5 to 11 dB less
  there than 0.17's master, and as each firmware ships its loud chords Sloop 2.6's -25 .. -29 dB in the top stands
  against 0.17's -17 .. -26 dB.
- "At equal input within 2 dB, ChoralRoot's the cleaner" (the stage table's row 15): within 1 dB of 2.2 either way;
  2 to 4 dB behind 2.6.
- "Sloop: limiter 18000, 4-sample attack, the gain at once", "one int16 ... the same Felucca chain": the chain of
  2.2. 2.6's limiter is described above.
- The level figures were against 68 sounds; the table above has both.

Everything else of Part 1 was read again in 2.6 and stands: the part LEVEL (104 against 92), the trims, the mix line,
the chorus's two taps, the stereo reverb, the knee at 16384, `OUT_SHIFT`, the DAC set-up.

### Felucca v1.0.1 to v1.5.1: every change on the way to the DAC word

19 commits. **None touches the limiter, the soft clip, the threshold, `OUT_SHIFT`, the DC blocker, the mix line, a
voice's level or velocity, or adds dither, a loudness option or a look-ahead**: `master_out` is v1.0.1's line for
line, `dsp.c` and `hal/fm1_audio.h` are identical. Upstream still has the gain that re-attacks on every peak, which
ChoralRoot smoothed (`fx_smooth`) before 0.17.

| Commit (version) | What | Why (theirs) | ChoralRoot |
| --- | --- | --- | --- |
| db70550 (1.0.2) | USB LEVEL FIXED: MASTER after the master for the DAC, the full level to USB (`fx_usb_fixed`, `usb_fixed_dac`) | #42: record with the speaker turned down | **has it** (`fx.c`; its comment says "Felucca 1.0.5": it is 1.0.2's) |
| db70550 (1.0.2) | BASS+: the low-pass 4 poles instead of 2 (`spk_bass`) | #42: it cancelled up to 6 dB of 300 .. 600 Hz | **has it** (held off since 0.19) |
| 7414269 (1.0.5.2) | `rev_clear` cleared one int16 too few of ROOM's all-pass | PR #121: a faint click after a REVERB TYPE change | does not have it; **not on the unit** (Felucca's reverb is not built: `CR_FX` 1). Two lines if a `CR_FX=0` build is ever shipped |
| 276f72a (1.1.5) | MASTER read before the audio starts (`master.c master_boot`), polled under the splash | #137: MASTER was 2048 (70 % of the knob) until the first read after the splash, then a glide: keys sounded loud with the knob down | **does not have it: worth taking** (`main.c`: `master_q12 = 2048`, `audio_init`, then 430 ms before the first read). Whether anything sounds in that window on ChoralRoot was not traced. Outside the master chain: not done here |
| 276f72a (1.1.5) | the metronome (`click.c`): after the master, after the USB tap, a hard clamp | "never through the limiter, never into a recording" | has its own (`cr_out.c cr_click_mix`), at the same place |
| 276f72a (1.1.5) | USB audio at 48 kHz (`uac_fir.h`) | | the USB agent's |
| 88f1030 (1.4) | BASS+ harmonics x 1.5 instead of x 3 | #180: the limiter pulled the mix down up to 16 dB on each kick | does not have it; dormant (BASS+ is held off). **Take the one line if BASS+ ever gets a control** |
| 88f1030 (1.4) | HALL: a stereo 8-line FDN at 22.05 kHz, the default reverb | ROOM's combs ring | has something else (`cr_fx.c`) |
| 88f1030 (1.4) | SPREAD: POLY voices left and right of PAN | #148 | the VA has its own stereo; the other engines none. A feature |
| 88f1030 (1.4), 129a4cf (1.5), a4d7ca2 (1.5.1) | FX-layer flanger / phaser, LFO 2 SYNC, ENV SYNC, per-track INSERT, dotted delay times, send throws, ANALOG's SYNC / SUB / filter types | features | not built or not applicable (FX layer, sequencer and ANALOG are not ChoralRoot's); INSERT would cost POOL |
| db70550, 2da8a71, 276f72a, f21d6d6 | drum kits, SAMPLE / GRAIN / SLICE fixes ("no click" on an empty slot), FM6 bank storage | | not built (no drum, sample or slice engine); storage only |

For the owner: **the power-on MASTER (#137)** is the one fix on the live path ChoralRoot lacks; nothing upstream helps
the limiter, the clip or the level.

### Verdict

- **Distortion.** Classic (and 0.17, the same master): on loud chords at a high MASTER it now stands **behind**
  Sloop 2.6, by 2 to 4 dB overall and 5 to 11 dB above 2 kHz at the same input. Clean: **ahead** of Sloop 2.6 by 9 to
  20 dB on the same input, and by more as each ships (it has 5.7 dB more room and its sounds are not driven into
  it): the median chord leaves -69 dB where Sloop 2.6's leaves -35. On a single note Classic's tanh adds 0.3 .. 0.6 %
  where Sloop's and Clean's chains add nothing.
- **Level.** At the same MASTER ChoralRoot is under Sloop 2.6 by a median 6.3 dB (Classic) or 8.8 dB (Clean) on a
  single note, 3.0 / 4.9 dB on a chord, 13.8 / 16.2 dB on the power-on sound; on its loudest chords it is level with
  Sloop 2.6 (Classic) or 1 to 2 dB over it (Clean). The gap is a matter of the quiet end: the part LEVEL (6 dB) and
  the trims, which 0.19's output chain does not touch.
- **The issue, the most likely reading now** (inference from the measurements; nobody has compared the two on a
  unit): the reporter was on 0.17 or older against Sloop 2.5 or 2.6. (1) Level, as before: single notes and the
  power-on sound 8 to 14 dB quieter, heard as dull. (2) New with 2.6: on full chords with MASTER up, 0.17's limiter
  and tanh put their residue 17 to 26 dB under the 2 .. 8 kHz content where Sloop 2.6 puts 24 to 33 dB: "harsh in
  the mids and highs, bass fine" fits that, on every engine. (3) Mono, and the mono chorus's comb, as before.
  0.19's Clean answers (2); (1) and the chorus are the open questions below.

## Part 2: the output chain of 0.19, and Options > Output

**Options > Output: Classic / Clean** (the entry after Dual Core; an Option: saved by itself; default Clean in the
0.19 dev builds). Classic is 0.18's master, bit for bit. Clean is a second chain on the same mix. Switching while
sound plays is click-free: the mix is faded to nothing over 11.6 ms, the chain changes at that zero with its limiter
at rest, and it fades in again (`fx.c out_fade`).

### Classic (0.18)

`((mix + wet) >> 2) * MASTER >> 10` -> DC blocker -> limiter (`LIM_T` 18000, 93 ms release, the gain as
`(x >> 4) * g >> 11`) -> `softclip` (tanh over the whole range) -> `<< 7`.

What was measured on it: a 6-note chord with the bass at full MASTER is in gain reduction 100 % of the time on VA,
WHEEL and CZ-1 (2.6 .. 3.8 dB on average, to 8 dB) and comes out with dist -28 .. -29 dB (4 %); in the 2 .. 8 kHz band
that is only 17 .. 26 dB under the band's own content. A single note, the limiter idle: -43 .. -50 dB, the tanh alone.
MASTER is a gain in front of a 16-bit path: at -24 dB a quiet note's error is -32 dB of the note (TINE EP) to -48 dB,
at -40 dB it is -16 .. -32 dB.

### Clean (0.19)

```
mix + wet  --x 0.75 (CLN_TRIM), kept in Q17-->  DC blocker  -->  a delay of 32 samples (0.73 ms)
           -->  limiter (CLN_T 26000, its gain fitted to the peaks still in the delay: hold 40 ms, release 186 ms)
           -->  knee (linear to 26000, tanh above: a safety)  -->  x MASTER  -->  the DAC's 24-bit word
```

| Decision | What | Why |
| --- | --- | --- |
| the clip | linear up to `CLN_K` 26000 (-2.0 dBFS, the limiter's threshold), then `K + R tanh((a - K) / R)`, `R` = 32767 - `K`, to full scale. (Before the look-ahead: linear to 24576, and a limited peak ran 0.5 dB into the curve) | Sloop's `knee()` / Felucca's `soft_knee`, placed at the threshold: with the look-ahead no sample leaves the limiter over it (a test), so the curve is a safety net that nothing in normal use meets; everything comes out as it went in but for the limiter's gain |
| where the headroom comes from | 3.2 dB from the limiter's threshold (26000 instead of 18000: Classic limits at 18000 because its tanh is already 0.8 dB down there; a knee that is straight up to the threshold does not need that), 2.5 dB from a fixed trim in front of it (`CLN_TRIM` 3072 / 4096) | 5.7 dB in all: the chord with the bass that was 5 .. 8 dB over Classic's threshold is 0 .. 2.5 dB over Clean's, and under it most of the time |
| where the loudness goes | chords with the bass come out 0.4 .. 1.4 LU **louder** than Classic (the limiter takes less); a single note and FM6's chords 1.2 .. 2.5 LU **quieter** (the trim). Nothing is given back after the limiter in 0.19's default build: there is no room left under the Q15 sample's full scale. The DAC word has 6 dB above that: [Proposed](#proposed-not-built-in) | the owner's condition (the speaker not noticeably quieter) is met for chords and not for single notes: his decision, with the listening files |
| the limiter | a safety. The peak is held 40 ms (`CLN_HOLD`: a period of 25 Hz), held again by every peak within 0.3 dB of it, then released over 186 ms; the gain glides in a straight line over the 32 samples before the peak comes out of the delay ([The look-ahead](#the-look-ahead); before it: eased down over 1.5 ms after the peak); applied as an exact product (`clean_gain`) | Classic's gain moved with every period of a bass note (0.46 dB at 40 Hz in the test; Clean's: 0.000 dB). `(x >> 4) * g >> 11` dropped four bits and shifted the sample by up to 16 while limiting |
| MASTER | last, after the knee, into the 24-bit word | the knob turns the level down and nothing else: the limiter's work no longer depends on it, and a low setting keeps the resolution |
| bits | the chain's samples are Q17 (the two bits Classic's `>> 2` shifts out are kept: the default trim is `x 3`, nothing lost); the DAC word gets the 7 bits under the Q15 sample (`out_frac`, added in `audio.c`) | the buffers are int32 and the DAC takes 24 bits: no dither is needed, the bits are simply not thrown away |
| LOWCUT / BASS+ | not in the Clean chain | ChoralRoot holds them off (above); if they get a control, they go in |
| USB audio | the master pair is the Q15 sample as before, taken after the limiter: the same delayed, limited signal the DAC gets. USB Level = Master: after MASTER (as before). Fixed: the full level, taken after the knee and before MASTER (`clean_split`, then `clean_post` in `usb_fixed_dac`); the DAC word is the same either way | against Classic the recording's master pair is 2.5 dB lower on a single note and up to 1.4 dB higher on a loud chord, and peaks at the limiter's ceiling, -2.0 dBFS (Classic's tanh kept it under -3.5 dB; before the look-ahead an overshoot could reach full scale); the two part pairs (pre-master) are unchanged |

### Before and after

(The first round's tables: Clean **before the look-ahead**. With it: [The look-ahead](#the-look-ahead), "Before and
after the look-ahead".)

Full MASTER, the held 4 s of each render (`tools/master_measure_run.sh`; four sounds: FM6 TINE EP, VA SYNTH BRASS,
WHEEL FULL ORGAN, CZ-1 BRASS 1; "chord6" = six notes, "+ bass" = with DEEP SUB; FX at the power-on sends).

| Render | Classic: GR share, dist, loudness | Clean: GR share, dist, loudness | Clean - Classic |
| --- | --- | --- | --- |
| FM6 single, dry | 0 %, -48.4 dB, -24.7 LKFS | 0 %, -76.2 dB, -27.1 | -2.5 LU |
| FM6 chord6 + bass, FX | 20.8 %, -39.1 dB, -13.7 | 1.1 %, -94.7 dB, -15.0 | -1.3 LU |
| FM6 chord6 + bass, FX, velocity 127 | 24.7 %, -38.5 dB, -13.3 | 3.6 %, -79.4 dB, -14.3 | -1.0 LU |
| VA single, dry | 0 %, -49.6 dB, -18.6 | 0 %, -89.3 dB, -21.0 | -2.4 LU |
| VA chord6, no bass, dry | 84.9 %, -30.7 dB, -11.8 | 0 %, -97.0 dB, -13.0 | -1.2 LU |
| VA chord6 + bass, dry | 99.9 %, -29.5 dB, -11.9 | 0 %, -99.5 dB, -11.5 | +0.4 LU |
| VA chord6 + bass, FX | 99.9 %, -29.4 dB, -12.2 | 9.6 %, -53.6 dB, -10.9 | +1.2 LU |
| WHEEL single, dry | 0 %, -43.4 dB, -18.2 | 0 %, -89.0 dB, -20.7 | -2.4 LU |
| WHEEL chord6 + bass, dry | 100 %, -29.0 dB, -11.7 | 12.8 %, -46.7 dB, -10.9 | +0.8 LU |
| WHEEL chord6 + bass, FX | 100 %, -28.4 dB, -11.8 | 29.5 %, -41.1 dB, -10.5 | +1.3 LU |
| CZ-1 single, dry | 0 %, -44.2 dB, -19.0 | 0 %, -89.8 dB, -21.4 | -2.4 LU |
| CZ-1 chord6 + bass, dry | 100 %, -29.3 dB, -12.2 | 2.1 %, -99.3 dB, -11.8 | +0.4 LU |
| CZ-1 chord6 + bass, FX | 100 %, -28.2 dB, -12.6 | 28.8 %, -43.4 dB, -11.2 | +1.4 LU |
| CZ-1 chord6 + bass, FX, velocity 127 | 100 %, -28.1 dB, -12.6 | 45.8 %, -41.0 dB, -10.9 | +1.7 LU |

The distortion by band (dB under the band's own content; the loudest case of each sound, chord6 + bass with FX):

| | Classic: under 300 Hz / to 2 kHz / to 8 kHz / above | Clean |
| --- | --- | --- |
| FM6 | -40.5 / -29.9 / -10.7 / -7.6 | -101.2 / -89.7 / -34.1 / -29.7 |
| VA | -31.0 / -28.9 / -25.0 / -22.3 | -55.1 / -52.5 / -54.5 / -53.6 |
| WHEEL | -29.8 / -28.1 / -26.2 / -11.1 | -41.9 / -40.6 / -40.5 / -28.8 |
| CZ-1 | -29.3 / -27.5 / -17.1 / -12.0 | -43.4 / -43.4 / -38.6 / -39.1 |

(FM6's TINE EP has almost nothing above 2 kHz: in Classic what is up there is mostly the chain's own error.) What
Clean has left on the loudest chords is the limiter catching a peak (its 1.5 ms attack); the knee's share is under
-62 dB everywhere. (The second round: it is the gain's steps, not the attack's speed, and the look-ahead does not
lower it: [The look-ahead](#the-look-ahead).)

MASTER turned down (dist, dB re the output; a single dry note):

| MASTER | FM6 Classic / Clean | VA Classic / Clean | WHEEL Classic / Clean | CZ-1 Classic / Clean |
| --- | --- | --- | --- | --- |
| 0 dB | -48.4 / -76.2 | -49.6 / -89.3 | -43.4 / -89.0 | -44.2 / -89.8 |
| -12 dB | -46.5 / -75.8 | -60.1 / -88.9 | -59.5 / -88.5 | -60.1 / -89.4 |
| -24 dB | **-32.2** / -72.3 | -48.1 / -85.3 | -46.7 / -85.0 | -48.1 / -85.8 |
| -40 dB | **-15.6** / -58.6 | -31.5 / -71.6 | -31.0 / -71.3 | -32.1 / -72.1 |

Classic is cleanest about 12 .. 18 dB under full MASTER (the tanh falls away, the word length is not yet short) and
worse on both sides; Clean does not depend on the knob (what is left at -40 dB is half a unit of the 24-bit word, an
offset). On a loud chord the limiter's share stays whatever MASTER says: Clean's -41 .. -54 dB there at every setting,
where Classic at -18 dB reaches -59 dB. A trim of 0 dB instead of -2.5 would make single notes as loud as Classic and
chords 0.7 .. 2.4 LU louder, with the loud chords in gain reduction 67 .. 99 % of the time (dist -35 .. -41 dB): the
other end of the same trade.

### The look-ahead

Until this round Clean's limiter met a peak when it was already there: the gain eased down over 1.5 ms and the
samples before it had arrived went on into the knee. Now the chain's samples wait 32 samples in a delay and the gain
is down when the peak comes out.

**How** (`fx.c clean_block`). The audio ISR hands the chain 32 samples at a time (`CTL`). For each block:

1. each sample goes through the DC blocker into the delay and the sample that went in 32 earlier comes out; the
   block's peak is kept on the way (one pass, no second look at the delay);
2. the loudest of this block and the one before it (the one coming out) is the **window maximum**: two numbers, the
   kept peak of each block. The envelope takes it at once if it is higher, holds 40 ms, releases over 186 ms, as
   before, and is never let under the window maximum;
3. the block's gain is `CLN_T / envelope` (one divide, and only in a block where the envelope moved);
4. the samples coming out are multiplied by a gain that glides in a straight line from the last block's gain to this
   one's, a 32nd of the way a sample (kept at 32 times the gain's resolution: it ends exactly on the target, no
   step at a block's edge). Both ends of the line fit the block coming out under the threshold, so every point of it
   does: **no sample leaves the limiter over `CLN_T`**, from the first sample on, for any input
   (`tests/cr_output_test.c`: noise bursts, single full-level samples, gated squares and kicks up to 30 dB over,
   and the envelope against a brute-force maximum of the delay). A block in which the gain is 1 at both ends is
   copied out as it is.

One block of look-ahead and the linear glide between blocks are **Sloop 2.6's design** (`lim_block`; GPL-3.0,
credited in `fx.c` and `LICENSING.md`). No code was taken. ChoralRoot's own: the block's peak kept from block to
block instead of Sloop's scan of the whole delay every block (128 compares a block less), the hold and release of
the first round (40 ms, 186 ms, restarted within 0.3 dB; Sloop: 35 ms, 46 ms, 0.5 dB), the exact 32-bit product in
two halves instead of a 64-bit one, the exact glide without a divide, the mean over several blocks for a longer
look-ahead (`CLN_LA_K`), a copy loop while the limiter is idle, and the knee moved to the threshold.

**The length**, measured (the firmware's own chain built at each length, `-DCLN_LA_B` / `-DCLN_LA_K`; "top": the
residue above 8 kHz relative to the content there; "gain > 1 kHz": how much of the limiter's gain itself moves at
audio rate, dB re a gain of 1; the kick is the transient test below, 6 dB over):

| Look-ahead | VA chord: dist, top, gain > 1 kHz | WHEEL chord | CZ-1 chord | CZ-1, velocity 127 | kick: dist, the knee's share, gain > 1 kHz |
| --- | --- | --- | --- | --- | --- |
| none (before) | -53.6, -53.6, -94.5 | -41.1, -28.8, -81.6 | -43.4, -39.1, -84.2 | -41.0, -35.4, -81.8 | -24.5, **-33.8**, -80.1 |
| 16 (0.36 ms) | -52.7, -53.3, -77.6 | -40.0, -36.5, -64.4 | -42.2, -32.7, -68.5 | -39.9, -30.9, -64.5 | -22.8, -91.1, -63.2 |
| 24 (0.54 ms) | -52.8, -53.4, -82.4 | -40.1, -37.3, -69.2 | -42.2, -34.9, -72.9 | -39.9, -32.6, -69.4 | -22.8, -91.1, -68.2 |
| **32 (0.73 ms)** | -52.6, -53.4, -87.6 | -40.2, -37.9, -74.5 | -42.1, -37.9, -76.8 | -40.0, -35.7, -74.6 | -22.9, -91.1, -65.3 |
| 40 (0.91 ms) | -52.9, -53.6, -89.8 | -40.2, -37.9, -76.7 | -42.2, -37.6, -81.3 | -40.0, -35.5, -77.0 | -22.9, -91.0, -76.0 |
| 48 (1.09 ms) | -52.9, -53.6, -89.2 | -40.2, -38.0, -76.1 | -42.2, -38.6, -80.1 | -40.0, -36.7, -76.2 | -22.9, -91.1, -73.5 |
| 64 (1.45 ms) | -52.8, -53.6, -91.1 | -40.3, -38.7, -77.9 | -42.3, -39.8, -80.3 | -40.2, -37.8, -78.0 | -23.2, -91.0, -68.1 |

Read plainly:

- **Any length ends the overshoot**: the knee's share falls from -34 dB (the kick) and -63 .. -86 dB (the chords) to
  under -86 dB, and the peak sits at the threshold (-2.03 dBFS) instead of up to -0.1 dBFS.
- **The overall "dist" of a loud chord does not improve with any length** (it is 0.7 to 1.3 dB worse than before).
  That figure is not the attack: it is the limiter turning the level down by 0.1 to 2 dB each time the chord's
  beating brings a higher peak, counted because it happens within 50 ms. Whether the gain moves just before the peak
  or just after it is the same step. The first round's "what Clean has left is its 1.5 ms attack" was wrong in that:
  what is left is how often and how far the gain moves, which is the hold and the release (below).
- **The gain itself is less smooth than before**, at every length: a straight line over 0.7 ms has more above 1 kHz
  than the old 1.5 ms ease that was allowed to finish after the peak (7 dB more at 32 samples, 3.5 dB at 64). It is
  still 75 to 88 dB under the signal on the chords.
- Above 8 kHz the chords' residue stops improving at 32 samples (16 to 32: up to 5 dB; 32 to 64: 0.2 to 2 dB).

**32 samples, 0.73 ms**: the shortest at which the top no longer gains, exactly one audio block (the block's peak is
known before its first sample goes out, with no second buffer), and the cheapest. 64 is a build switch
(`-DCLN_LA_K=2u`: the gain 3.5 dB smoother, 1.45 ms); the listening files have it beside 32.

**The latency: 0.73 ms on everything Clean puts out** (Classic: none). Notes, the arp and the looper all pass through
it and stay where they were against each other. Two things do not pass through it and are now 0.73 ms **ahead** of
the sound in Clean: the metronome / count-in click (`cr_out.c cr_click_mix` is mixed after the master, as Felucca's:
never through the limiter) and, in a USB recording, the chord and bass pairs (taken before the master) against the
master pair. A key's own delay to its sound is a block (0.73 ms) to 2.9 ms already; nobody has judged the click on a
unit, and delaying it too would be 64 more samples of RAM and a second line: not done, named here.

**The transient test** (`tools/master_transient.py`: a kick every 500 ms, a sine falling from 160 to 48 Hz with
0.5 ms of attack and 3 ms of noise on it, on a held 55 Hz bass; its peak 6 or 12 dB over Clean's threshold; the same
mix through each chain):

| | Classic | Clean before | **Clean now** | Sloop 2.2's master | Sloop 2.6's master |
| --- | --- | --- | --- | --- | --- |
| kick 6 dB over: peak out, dBFS | -1.4 | -0.1 | **-2.0** | -2.6 | -5.2 |
| the knee's (tanh's) own share, dB re the output | -27.9 | -33.8 | **-91.1** | | |
| dist (with the gain's movement) | -20.6 | -24.5 | -22.9 | -19.7 | -19.8 |
| loudness, LKFS | -11.6 | -10.4 | -10.5 | -11.1 | -12.0 |
| kick 12 dB over: peak out | -0.3 | -0.1 | **-2.0** | -0.8 | -5.2 |
| the knee's share | -20.3 | -19.3 | **-88.8** | | |
| dist | -13.8 | -14.7 | -14.0 | -13.0 | -13.4 |
| the output's share above 2 kHz (the input's: -30.2 dB) | -27.2 | -26.2 | **-32.0** | -27.8 | -34.5 |
| loudness | -11.5 | -11.0 | -11.3 | -11.1 | -12.0 |

Before, a hit 12 dB over went through the knee at 11 % of the output (-19.3 dB) and came out with 4 dB more above
2 kHz than went in: a crunch on the hit. Now the hit is turned down as a whole and nothing is bent. The "dist" row
stays where it was: a kick 12 dB over a limiter is 12 dB of gain movement in any design.

**Before and after the look-ahead** (the first round's rows; full MASTER; Clean's loudness is unchanged to 0.02 LU
on every row but the kicks):

| Render | Clean before: GR share, dist, the knee's share | Clean now |
| --- | --- | --- |
| a single note, any of the four sounds | 0 %, -76 .. -90 dB | the same, bit for bit but 32 samples late |
| FM6 chord6 + bass, FX | 1.1 %, -94.7, -96.0 | 1.2 %, -93.9, -96.0 |
| FM6 chord6 + bass, FX, velocity 127 | 3.6 %, -79.4, -94.6 | 3.6 %, -77.0, -96.7 |
| VA chord6 + bass, dry | 0 %, -99.5 | the same |
| VA chord6 + bass, FX | 9.6 %, -53.6, -85.8 | 9.8 %, -52.6, -95.5 |
| WHEEL chord6 + bass, dry | 12.8 %, -46.7, -70.0 | 13.0 %, -46.0, -94.6 |
| WHEEL chord6 + bass, FX | 29.5 %, -41.1, -62.7 | 29.8 %, -40.2, -92.0 |
| CZ-1 chord6 + bass, dry | 2.1 %, -99.3, -99.3 | 2.1 %, -76.3, -99.9 |
| CZ-1 chord6 + bass, FX | 28.8 %, -43.4, -67.3 | 28.5 %, -42.1, -93.8 |
| CZ-1 chord6 + bass, FX, velocity 127 | 45.8 %, -41.0, -63.5 | 46.9 %, -40.0, -90.8 |

The distortion by band (dB under the band's own content; under 300 Hz / to 2 kHz / **2 .. 8 kHz** / above):

| | Clean before | Clean now |
| --- | --- | --- |
| FM6 chord6 + bass, FX | -101.2 / -89.7 / **-34.1** / -29.7 | -98.4 / -89.2 / **-34.1** / -29.7 |
| VA | -55.1 / -52.5 / **-54.5** / -53.6 | -53.3 / -52.0 / **-53.5** / -53.4 |
| WHEEL | -41.9 / -40.6 / **-40.5** / -28.8 | -41.9 / -39.0 / **-39.1** / -37.9 |
| CZ-1 | -43.4 / -43.4 / **-38.6** / -39.1 | -42.7 / -41.6 / **-40.8** / -37.9 |
| CZ-1, velocity 127 | -41.2 / -40.9 / **-37.4** / -35.4 | -40.6 / -39.5 / **-39.4** / -35.7 |

In 2 .. 8 kHz: CZ-1 2 dB better, VA and WHEEL 1 to 1.4 dB worse, FM6 the same. Above 8 kHz: WHEEL 9 dB better, the
others within 1.2 dB. On held chords the look-ahead is a small change either way; on attacks it is the difference
between bent and not bent.

**What would lower the chords' figure: a longer hold.** The gain moves because the envelope lets go between the
chord's beats. Measured with the look-ahead in (`-DCLN_HOLD`; the release as it is):

| Hold | VA: dist, loudness | WHEEL | CZ-1 | CZ-1, velocity 127 | kick 6 dB over: loudness |
| --- | --- | --- | --- | --- | --- |
| 40 ms (now) | -52.6, -10.93 | -40.2, -10.52 | -42.1, -11.17 | -40.0, -10.90 | -10.49 |
| 100 ms | -52.6, -10.97 | -41.5, -10.64 | -44.5, -11.28 | -44.3, -11.08 | -11.04 |
| 200 ms | -52.6, -11.01 | -42.7, -10.78 | -44.4, -11.42 | -44.7, -11.23 | -11.92 |
| 500 ms | -53.0, -11.08 | -45.5, -11.07 | -44.5, -11.60 | -45.4, -11.48 | -13.31 |

2 to 5 dB for 0.1 to 0.6 LU on chords, and a level that stays down longer after a loud hit (the kick's column). Not
changed: the brief was to keep the hold and the release, and it is a matter of taste with the listening files.

**Memory.** The delay is 32 frames of two int32 (the chain's Q17 samples: packing them into 16 bits would throw away
what Clean keeps): 256 bytes, in RAM (`.bss`, `cln_dl`), not in the pool. Both are the same on-chip RAM as far as the
project knows (the effects' lines, megabytes of traffic a second, are in the pool and `CR_FX_RAM` showed the code
cache was not their cost: docs/FX.md); the pool is the tight one (91.5 %), RAM has the room (82.1 -> 82.4 %).

**Switching Output.** The fade takes the chain's input to nothing over 16 blocks; then one more block of silence goes
through the old chain, so that Clean's delay plays out the last 32 samples of its fade (without it the end of the
fade, at 6 % of the level, would be cut off: a step); then both limiters are put at rest and the delay is emptied,
and the new chain fades in. 0.73 ms longer than before (24 ms in all). `cr_output_test.c`: no step above the steady
tone's own either way, the delay empty at the change, Clean's first block back silent.

**The make-up idea** (`CR_OUT_MAKEUP`, the DAC word's unused top 6 dB) is safer with the look-ahead in: Clean's peak
is now exactly the threshold, never the knee's range up to full scale, so a make-up of `M` puts the word's peak at
exactly `26000 M` and no higher: 5461 (the trim given back) peaks at -5.5 dB of the DAC's own full scale, where
before an overshoot could reach -3.5. Up to x 2.52 (+8 dB) fits the word. USB's master pair would still need its own
clamp above the Q15 sample. Not enabled.

### The cost on the unit

Counted in `build/choralroot.dis` (`python3 tools/fx_loops.py build/choralroot.dis master_block clean_block
clean_post17`, then the listing read for what a pass runs), at 205 instructions a microsecond, 128 frames a half:

| | instructions a frame | a half |
| --- | --- | --- |
| Classic (`master_block`, the smooth loop without LOWCUT), limiter idle | 76 | 47 us |
| Classic, limiting (a chord: all the time) | about 92 | 57 us |
| Clean before the look-ahead: `clean_block` 53 + `clean_post17` 18 + the word's low bits 12, limiter idle | 83 | 52 us |
| Clean before, limiting (holding) | about 99 | 62 us |
| **Clean now**, idle: `clean_block` 44 a frame (the DC blocker, the peak, in and out of the delay) + about 60 a block (2 a frame) + 18 + 12 | **76** | 47 us |
| **Clean now**, limiting: the same + 28 a frame (the glide, two exact products, two knee tests) | **about 104** | 65 us |

Idle is 7 instructions a frame cheaper than before the look-ahead (the envelope, its tests and the knee's are a
block's work now, not a sample's; a divide a block at most instead of one a sample while releasing); limiting is 5
dearer (the second pass over the block). Flash +96 bytes, RAM +256 (the delay), the pool unchanged. The emulator's
`perf.sh` reads 24 us for the master stage where it read 41 (an estimate with another compiler, not the unit's).

### Classic is 0.18's, bit for bit

- `sh tools/emu/classic_exact.sh build/scratch/ref18` (a `git archive 19b6453` built beside): all 161 emulator
  scripts the 0.18 tree has, this tree's emulator with the Output held at Classic (`EMU_OUTPUT=classic`): 161 WAVs
  identical (104 of them with sound) when the chain was built. Since CZ-1's click fix (abb0b81, after it) the three
  scripts that play CZ-1 differ from 0.18 before any output chain: 158 identical, the three named. With the
  look-ahead in: the same 158, and **all 164 scripts identical to f402608's** (the tree before the look-ahead, both
  held at Classic: `REF_CLASSIC=1 sh tools/emu/classic_exact.sh build/scratch/ref_f402`).
- `tools/emu/test_cr.sh`: a script on a flash whose settings say Classic gives the same WAV as `EMU_OUTPUT=classic`,
  and not the default's.
- `tests/cr_output_test.c`: `out_master` in Classic is 0.18's mix line and `master_block`, sample for sample.
- `sh tests/run_tests.sh`: the 197 golden renders unchanged (Felucca's builds never select Clean).
- The harness's "chain: exact" for both chains: its own run of the chain equals the firmware's output.

### Proposed, not built in

- **The DAC word's upper 6 dB.** `CR_OUT_MAKEUP=1 ./build.sh` (`fx.c CLN_MAKEUP` 5461) gives Clean's 2.5 dB trim back
  after the knee, with MASTER, in the 24-bit word: a single note as loud as Classic, loud chords 3 .. 4 LU louder,
  the limiter as idle as in the tables above. The word then peaks 2.5 dB above anything ChoralRoot has sent
  (-5.5 dBFS of the DAC with the look-ahead: the limiter's ceiling times the make-up, exactly; up to -3.5 before it). fm1-x0x ships +6 dB as an option on the same hardware; nobody has heard this unit's
  headphone and speaker stages up there with ChoralRoot. Not the default, and not complete: with it USB's master pair
  (a Q15 sample) clips above full scale in USB Level = Master, and the SCOPE view's sample wraps (both to be handled
  if it is adopted). No register, rail or clock is involved; `OUT_SHIFT` and the HAL are untouched.
- **The HAL's comments** (`hal/fm1_audio.h`: "codec control lines", "24-bit left-justified") are wrong as found
  above; left as they are until the owner says the HAL may be touched for a comment.

## Open questions and proposals

1. **The level against Sloop** is the largest measured difference and 0.19's output chain does not touch it: part
   LEVEL 92 against 104, and trims. Raising LEVEL to 104 on 0.18 would have put the default chord 99 % of the time in
   Classic's limiter (dist -31.9 dB instead of -39.6); with Clean's headroom and the DAC word's 6 dB it becomes
   possible. A decision about the sounds' levels, by ear.
2. **The chorus's return is mono** (one tap to both sides). Two taps, as Sloop's, would end the comb's sameness in
   both ears. Not built.
3. **Output's default for the release** (Classic or Clean), the trim, and the make-up build: after listening.
   And since the second round: **the limiter's hold** (40 ms now; 200 to 500 ms takes 2 to 5 dB off what is left on
   loud chords for 0.1 to 0.6 LU: [The look-ahead](#the-look-ahead)), and 32 or 64 samples of look-ahead.
8. **The click and the USB part pairs are 0.73 ms ahead of Clean's output** (they do not pass the limiter's delay).
9. **MASTER at power-on** (Felucca 1.1.5, #137): 2048 until the first read, 430 ms after the audio starts.
10. **CZ-1 struck again while it sounds** still shows a click to `tools/emu/wavclicks.py` in the mix of
    `BRASS 1` with the bass (2.30 s and 3.06 s of the script this round first used), before the output chain: in
    f402608's Clean as in this one, not in Classic (whose tanh rounds it). CZ-1's own item (docs/CZ1.md).
4. **A control for LOWCUT / BASS+** (they are useful on the speaker): none added; the stage is held off.
5. **The reverb at 22.05 kHz** has no wet above 10 kHz (-12.9 dB against -4.3 dB): 0.18's cost decision
   (docs/FX.md), named here because it is a "top" difference to 0.17 and Sloop.
6. **The mix's own word length**: `mix_part_loop` shifts each part's sample by 2 before its level (`(b >> 2) * lvl
   >> 10`): the same class of loss as the master had, upstream of it, not measured here.
7. **The DAC chip** is unidentified. Its marking on the board would settle what filter it has.

## How to measure

```
sh tools/emu/build.sh                             # the generated headers
sh tools/master_measure_run.sh OUTDIR             # builds tests/master_measure.c, 26 renders (about 2 min)
sh tools/master_measure_tables.sh OUTDIR          # OUTDIR/table-all.txt, table-loudness.txt
python3 tools/master_listen.py OUTDIR WAVDIR      # the listening files and their README
sh tools/master_measure_run.sh OUTDIR -DCLN_TRIM=4096     # a variant: any of CLN_TRIM, CLN_T, CLN_HOLD, CLN_REL, CLN_MAKEUP,
                                                          # CLN_LA_K (blocks of look-ahead), CLN_LA_B (a block: 8, 16, 32)
python3 tools/master_transient.py OUTDIR                  # the transient test's mixes (the run script makes and uses them)
build/host/master_measure chain MIX.raw.i32 PREFIX        # any mix (int32 L R) through both chains
python3 tools/master_listen_la.py BEFORE NOW WAVDIR       # the look-ahead's listening files (two runs' OUTDIRs)
```

`tests/master_measure.c` is the firmware as the emulator builds it, driven through its key matrix, with the mix
captured from `fx.c`'s buffers after every block; the captured mix is then run through the firmware's own chain
functions for each MASTER setting (Classic a frame at a time, Clean a block at a time as the ISR calls it, its gain
of each sample from `fx.c`'s `CLN_GAIN_TAP`; Clean's record holds the input 32 samples late, as its output is), which
gives the limiter's gain for every sample. Run at the
render's own MASTER this must equal what the firmware put out, to the bit ("chain: exact"; the script fails if not).
`tools/master_analyze.py` reads the held part:

- **GR%**, **GRavg**: the share of the samples with the limiter's gain under 1, and its mean.
- **dist**: the residual of `out - k * in * slow(g)` (the limiter's gain smoothed over 50 ms taken out: what a
  compressor may do inaudibly), dB re the output: the gain's fast movement, the clip's curve and the arithmetic
  (truncations, the word length at a low MASTER). The input is the mix times the gain with nothing lost (floating
  point). **It counts any level change faster than 50 ms as distortion**: a limiter stepping down 1 dB for a new
  peak shows here however cleanly it does it. **clip** (the table's column) is the part that bends the wave: the
  residual of `out - k * in * g` with the gain as it was; **g>1k** is the gain's own content above 1 kHz.
- **LK**: K-weighted loudness (BS.1770's two filters in the frequency domain, no gating), LKFS re the Q15 sample's
  full scale. An estimate.

The data of 2026-10-12: `../audio-0.19/` beside the repository (`table-all.txt`, `table-loudness.txt`, the WAVs and
their `README.txt`); the first measurements (0.18's master alone): `../muffled-review/`; the second round's listening
files (Classic, Clean before and with the look-ahead, 64 samples, the same mixes through Sloop 2.6's master, Sloop
2.6's own chords): `../audio-0.19/lookahead/` and its `README.txt`.

Tests: `tests/cr_output_test.c` (the knee, the gain, the staging, the look-ahead: the block maximum against brute
force, no sample over the threshold, the gain never above 1 and never a step, no DC shift, the delay exactly 32
samples, calls that are not whole blocks; MASTER last, the switch and nothing stale after it, Classic, USB;
`sh tests/run_cr_tests.sh`), `tools/emu/scripts/cr_output.txt`, `cr_output_classic.txt` and `cr_output_loud.txt`
(`tools/emu/test_cr.sh`: Clean is Classic's passage 32 samples late, `tools/emu/wavlag.py`; a loud chord's peak sits
on the limiter's ceiling), the seeded flash images of `tools/emu/test_persist.sh`, `tools/emu/classic_exact.sh`.
