# Audio quality: the output chain, what was measured, and Options > Output (0.19)

GitHub issue 3 (a user on 0.17 or older, headphones on the 3.5 mm output): "muffled / harsh compared to other
firmwares, especially in the mid to high frequencies (bass sounds good) ... Sloop sounds a lot clearer, fuller ...
all the engines ... maybe it is just level matching / in the output stage". This document holds what was found
(2026-10-12), what 0.19 changes, and how to measure it again. Inference is marked as inference.

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
- **What cannot**: the FX amounts (Sloop is as wet), the reverb's tone, the limiter and the clip (Sloop drives its
  own far deeper and distorts more), the DAC set-up (identical, pin for pin), a hidden filter.
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
mix + wet  --x 0.75 (CLN_TRIM), kept in Q17-->  DC blocker  -->  limiter (CLN_T 26000: hold 40 ms, release 186 ms)
           -->  knee (linear to 24576, tanh above)  -->  x MASTER  -->  the DAC's 24-bit word
```

| Decision | What | Why |
| --- | --- | --- |
| the clip | linear up to `CLN_K` 24576 (-2.5 dBFS), then `K + tanh(4 (a - K)) / 4`, to full scale | Sloop's `knee()` / Felucca's `soft_knee`, placed higher: a note under the knee comes out as it went in, bit for bit (a test); only overshoots meet the curve |
| where the headroom comes from | 3.2 dB from the limiter's threshold (26000 instead of 18000: Classic limits at 18000 because its tanh is already 0.8 dB down there; a knee that is straight to 24576 does not need that), 2.5 dB from a fixed trim in front of it (`CLN_TRIM` 3072 / 4096) | 5.7 dB in all: the chord with the bass that was 5 .. 8 dB over Classic's threshold is 0 .. 2.5 dB over Clean's, and under it most of the time |
| where the loudness goes | chords with the bass come out 0.4 .. 1.4 LU **louder** than Classic (the limiter takes less); a single note and FM6's chords 1.2 .. 2.5 LU **quieter** (the trim). Nothing is given back after the limiter in 0.19's default build: there is no room left under the Q15 sample's full scale. The DAC word has 6 dB above that: [Proposed](#proposed-not-built-in) | the owner's condition (the speaker not noticeably quieter) is met for chords and not for single notes: his decision, with the listening files |
| the limiter | a safety. The peak is held 40 ms (`CLN_HOLD`: a period of 25 Hz), held again by every peak within 0.3 dB of it, then released over 186 ms; the gain still eased down over 1.5 ms (`fx_smooth`'s, kept: no audio-rate gain); applied as an exact product (`clean_gain`) | Classic's gain moved with every period of a bass note (0.46 dB at 40 Hz in the test; Clean's: 0.000 dB). `(x >> 4) * g >> 11` dropped four bits and shifted the sample by up to 16 while limiting |
| MASTER | last, after the knee, into the 24-bit word | the knob turns the level down and nothing else: the limiter's work no longer depends on it, and a low setting keeps the resolution |
| bits | the chain's samples are Q17 (the two bits Classic's `>> 2` shifts out are kept: the default trim is `x 3`, nothing lost); the DAC word gets the 7 bits under the Q15 sample (`out_frac`, added in `audio.c`) | the buffers are int32 and the DAC takes 24 bits: no dither is needed, the bits are simply not thrown away |
| LOWCUT / BASS+ | not in the Clean chain | ChoralRoot holds them off (above); if they get a control, they go in |
| USB audio | the master pair is the Q15 sample as before. USB Level = Master: after MASTER (as before). Fixed: the full level, taken after the knee and before MASTER (`clean_split`, then `clean_post` in `usb_fixed_dac`); the DAC word is the same either way | against Classic the recording's master pair is 2.5 dB lower on a single note and up to 1.4 dB higher on a loud chord, and can now reach full scale (Classic's tanh kept it under -3.5 dB); the two part pairs (pre-master) are unchanged |

### Before and after

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
-62 dB everywhere.

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

### The cost on the unit

Counted in `build/choralroot.dis` (`python3 tools/fx_loops.py build/choralroot.dis master_block clean_block
clean_post17`, then the listing read for what a pass runs), at 205 instructions a microsecond, 128 frames a half:

| | instructions a frame | a half |
| --- | --- | --- |
| Classic (`master_block`, the smooth loop without LOWCUT), limiter idle | 76 | 47 us |
| Classic, limiting (a chord: all the time) | about 92 | 57 us |
| Clean: `clean_block` 53 + `clean_post17` 18 + the word's low bits 12, limiter idle | 83 | 52 us |
| Clean, limiting (holding) | about 99 | 62 us |

About 5 us of the 2902 more (0.2 %), at most. Flash: +1852 bytes; RAM: +92. The emulator's `perf.sh` reads 41 us
for the master stage before and after (an estimate, not the unit's).

### Classic is 0.18's, bit for bit

- `sh tools/emu/classic_exact.sh build/scratch/ref18` (a `git archive 19b6453` built beside): all 161 emulator
  scripts the 0.18 tree has, this tree's emulator with the Output held at Classic (`EMU_OUTPUT=classic`): 161 WAVs
  identical (104 of them with sound).
- `tools/emu/test_cr.sh`: a script on a flash whose settings say Classic gives the same WAV as `EMU_OUTPUT=classic`,
  and not the default's.
- `tests/cr_output_test.c`: `out_master` in Classic is 0.18's mix line and `master_block`, sample for sample.
- `sh tests/run_tests.sh`: the 197 golden renders unchanged (Felucca's builds never select Clean).
- The harness's "chain: exact" for both chains: its own run of the chain equals the firmware's output.

### Proposed, not built in

- **The DAC word's upper 6 dB.** `CR_OUT_MAKEUP=1 ./build.sh` (`fx.c CLN_MAKEUP` 5461) gives Clean's 2.5 dB trim back
  after the knee, with MASTER, in the 24-bit word: a single note as loud as Classic, loud chords 3 .. 4 LU louder,
  the limiter as idle as in the tables above. The word then peaks 2.5 dB above anything ChoralRoot has sent
  (-3.5 dBFS of the DAC). fm1-x0x ships +6 dB as an option on the same hardware; nobody has heard this unit's
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
sh tools/master_measure_run.sh OUTDIR -DCLN_TRIM=4096     # a variant: any of CLN_TRIM, CLN_T, CLN_HOLD, CLN_REL, CLN_MAKEUP
```

`tests/master_measure.c` is the firmware as the emulator builds it, driven through its key matrix, with the mix
captured from `fx.c`'s buffers after every block; the captured mix is then run through the firmware's own chain
functions a frame at a time for each MASTER setting, which gives the limiter's gain for every sample. Run at the
render's own MASTER this must equal what the firmware put out, to the bit ("chain: exact"; the script fails if not).
`tools/master_analyze.py` reads the held part:

- **GR%**, **GRavg**: the share of the samples with the limiter's gain under 1, and its mean.
- **dist**: the residual of `out - k * in * slow(g)` (the limiter's gain smoothed over 50 ms taken out: what a
  compressor may do inaudibly), dB re the output: the gain's fast movement, the clip's curve and the arithmetic
  (truncations, the word length at a low MASTER). The input is the mix times the gain with nothing lost (floating
  point).
- **LK**: K-weighted loudness (BS.1770's two filters in the frequency domain, no gating), LKFS re the Q15 sample's
  full scale. An estimate.

The data of 2026-10-12: `../audio-0.19/` beside the repository (`table-all.txt`, `table-loudness.txt`, the WAVs and
their `README.txt`); the first measurements (0.18's master alone): `../muffled-review/`.

Tests: `tests/cr_output_test.c` (the knee, the gain, the staging, MASTER last, the switch, Classic, USB;
`sh tests/run_cr_tests.sh`), `tools/emu/scripts/cr_output.txt` and `cr_output_classic.txt` (`tools/emu/test_cr.sh`),
the seeded flash images of `tools/emu/test_persist.sh`, `tools/emu/classic_exact.sh`.
