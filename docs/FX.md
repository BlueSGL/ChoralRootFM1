# The effects: a stereo delay and a large reverb (0.18)

ChoralRoot 0.18 replaces Felucca's mono tempo delay and its ROOM / SPRING reverb with a stereo delay in the spirit of
the Moog Matriarch's and a large, slow, modulated reverb. The chorus and the drive are Felucca's, unchanged. This is the
design as built: what the code is, why, what it costs, and how to tune it. The player's side is
[MANUAL.md section 8](MANUAL.md#8-fx).

- the code: `firmware/src/cr_fx.c`, included by `fx.c` when `CR_FX` is 1 (`choralroot.c`, the emulator). With `CR_FX`
  0, the default, `fx.c` is Felucca's as it was: its own units, `tests/reverb_test.c` and the 197 golden renders of
  `tests/run_tests.sh` are untouched, and no golden was updated;
- the tests: `tests/cr_fx_test.c` (`sh tests/run_cr_tests.sh`), `tools/emu/test_cr.sh` (the layer, "the effects are
  global"), `tools/emu/test_persist.sh` (the session), `tools/emu/perf.sh` scenario (l);
- the tools: `tools/fx_halfband.py` (the half-band filter's coefficients), `tools/emu/fx_review.py` (listening renders,
  old against new), `tools/fx_plots.py` (pictures of any WAV: level, spectrogram, Schroeder integral).

## The rule: the effects are global, a sound carries its sends

The owner's decision of 2026-10-11: "A preset should only store a FX send level or wet amount. The FX settings
themselves should be global and not change when changing presets."

| What | Where it lives | Changed by a sound load |
| --- | --- | --- |
| a part's **sends** into the reverb, the chorus and the delay, and its **drive** | the sound: `P_REV`, `P_CHOR`, `P_DLY`, `P_DIST` of the part's parameters, in a factory preset and in a user record (the record's layout is unchanged) | yes: they are the sound's |
| every effect's **own parameters** (16 of the delay and the reverb: `cr_fx.c` `xfp[]`; the chorus's rate and depth: `song.g`) | the session's FX block, by tag (`cr_settings.h` `CSS_FXP_*`, [SETTINGS.md](SETTINGS.md)) | never |
| FX on / off | the settings block (`fx_on`) | no |

- **"Send" on KNOB 4 is the chord part's send**, not a master wet level: it is the value the sound carries, shown and
  turned in the FX layer (`cs.fx_amt[]`, 0..127, shown 0..99), and a sound load replaces it with the new sound's. The
  screen says `Send`. There is no global wet level: the returns are fixed (`XD_WET`, `XR_WET`), matched to the old
  effects (below).
- **The bass part has its own sends**: its sound's, turned in its editor's FX group (BASS held + EDIT, then the FX
  button). Both parts feed the same two buses, so a bass with a reverb send is in the same room as the chords.
- **FX off closes every part's sends** (`fx.c` `fx_off`): the chord part's and the bass part's. Until 0.17 it zeroed
  the chord part's only, and a bass sound with a send kept its reverb while the FX light was off. The drive is an
  insert in the sound: FX off sets the chord part's to 0 as before; the bass part's stays.
- MIDI: CC 91 / 93 / 94 are a part's reverb / chorus / delay **send**, as before.

## The half rate

Both effects run at 22.05 kHz. The two send buses (mono, as `fx.c` has them) each go down through a half-band
filter, the two effects add their stereo wet at the low rate, and one pair of half-band filters brings the sum back up
into the mix. The dry signal is untouched.

```
 send_d (mono, 44.1k) --> [down 2] --> DELAY  --+
                                                +--> wet L, R (22.05k) --> [up 2] x 2 --> + mix_l, mix_r (44.1k)
 send_r (mono, 44.1k) --> [down 2] --> REVERB --+
```

The filter (`xf_ap`, `xf_down`, `xf_up`) is a two-path polyphase all-pass half-band: `H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2`,
each path two first-order all-passes `(a + z^-2) / (1 + a z^-2)` run at the low rate: **four multiplies for two input
samples**. The coefficients are a minimax search's (`python3 tools/fx_halfband.py`), Q11: `199, 1264 | 695, 1785`.

| Measured (`tests/cr_fx_test.c`, through the delay with Tone open) | |
| --- | --- |
| pass band | 200 Hz .. 9 kHz within +0 / -1.1 dB (the -1.1 dB is Tone's pole at 9 kHz), -6.5 dB at 11 kHz |
| below 200 Hz | -0.6 dB at 100 Hz, -2.2 dB at 50 Hz: the delay loop's 40 Hz low cut |
| stop band (the design) | -54 dB above 12.55 kHz |
| a 15 kHz send's alias at 7.05 kHz | -81 dB re a 1 kHz send (two filters and the loop) |
| noise of the delay path | -88 dB re the line's full scale (the int16 line; the filters run on the sends at full resolution) |
| distortion of a 1 kHz send's repeat | -46 dB at -12 dB of the line (the loop's tanh: wanted), -53 dB at -36 dB (the rounding of `dsp.c`'s tanh table) |

**Why half the rate** (the options the brief asked to weigh):

| | Delay a side | Delay memory | Wet band | Cost of both loops |
| --- | --- | --- | --- | --- |
| (a) 44.1 kHz, two lines in the old 131072 B | 0.743 s | 131 072 B | 20 kHz | ~2 x |
| (b) **22.05 kHz, int16 (built)** | **1.000 s** | **88 456 B** | 9.5 kHz | 1 x: **233 us a block** |
| (b') 22.05 kHz in the old 131072 B | 1.486 s | 131 072 B | 9.5 kHz | 1 x |
| (c) 12-bit companded at 44.1 kHz | 0.99 s | 131 072 B | 20 kHz | ~2 x, and the companding's noise pumps with the level |

(b) is the only one that gives the second a side that was asked for and gives memory back: POOL went from 296 224 B
to **283 440 B** used (60 624 B free, was 47 840) with a reverb four times the old one's memory. A bucket-brigade
echo has less than 9.5 kHz of band and a large dark hall does too, so the wet loses nothing it should have; the
length is a constant (`XD_MAX`), and (b') is one number away if a longer delay is ever wanted more than 43 KB.

## The delay

```
                       +-------------------- feedback (0 .. 0.97, eased) -------------------+
                       v                                                                    |
 in --(+)--> tanh --> [ line L, 22114 x int16 ] --> cubic read at tL + wow --> low-pass (Tone) --> low cut 40 Hz --+--> wet L
 in --(+)--> tanh --> [ line R, 22114 x int16 ] --> cubic read at tR + wow --> low-pass (Tone) --> low cut 40 Hz --+--> wet R
                       ^                                                                    |
                       +--------------------------------------------------------------------+
   Stereo: each side feeds itself, both take the input.
   Ping-pong: L takes the input and R's return, R takes L's return only (the crossfade between the two: 12 ms).
```

- **Two lines of `XD_N` = 22114 samples** (1.000 s at 22.05 kHz, + 64 for the wow's swing and the interpolator),
  `int16`, in `.pool`: 88 456 B.
- **The read** is a four-point cubic (Catmull-Rom) at a Q8 position. The position **glides** to its target: a lag of
  ~46 ms, at most half a sample a sample. A new time (the knob, the tempo, a MIDI clock, Sync or Spread) therefore
  bends the pitch of what is in the line by at most a fifth up or an octave down for a moment, as a tape or a BBD
  clock does, and never clicks. At rest the position is a whole sample and the read is that sample itself.
- **The loop**: read -> one-pole low-pass (Tone, ~400 Hz .. 9 kHz) -> one-pole low cut at 40 Hz -> feedback gain ->
  + the input -> `tanh` (`dsp.c softclip`) -> the line. Every repeat is darker and a little more compressed than the
  last; the line cannot clip (tanh saturates at the int16's edge); the low cut keeps the bass and any offset from
  building up. The wet is taken after the filters, so the first repeat is already coloured.
- **Feedback** tops out at 0.97: a second of delay then takes a few minutes to die, and it does die. It does not
  self-oscillate (a decision: see the end).
- **Time**: Sync on: a division of the tempo, `1/32 1/16T 1/32. 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/2T 1/4. 1/2`
  (`XF_DIV`, 48ths of a quarter note; the tempo is `xf_bpm`, the engine's 20 .. 300 BPM, MIDI clock included). A
  division longer than the line plays at half (a 1/2 at 100 BPM is 1.2 s: it plays as a 1/4). Sync off: 21 .. 1000 ms,
  exponential over the knob. Both values are kept; Sync chooses.
- **Spread** is the right side's time as a part of the left's. Sync on: `1:1`, `3:4`, `2:3`, `1:2` (with the left on
  a 1/4: a dotted 1/8, a 1/4 triplet, a 1/8 on the right). Sync off: `x1.00` .. `x0.50`, continuous. At 0 the two
  sides are the same time: in Stereo that is one echo in the middle, in ping-pong a strict left-right bounce. The
  right side is always the shorter one, so Time is the longest time and always fits the line.
- **Wow**: the time's wobble, a 0.33 Hz sine and an eighth as much of a 3.1 Hz one, 0 .. 2 ms. The SAME on both
  sides: with Spread 0 in Stereo the two sides stay the same sample (the FM-1's speaker is mono).
- **Level**: `XD_WET` (Q12) is the Stereo return, `XD_WPP` the 3 dB more that ping-pong gets (its repeat is on one
  side only). Matched to Felucca's delay: see "The level".

`tests/cr_fx_test.c`: the times in samples for 10 tempi x 13 divisions x 4 spreads and an impulse's first repeat on
them; every parameter swept under a held tone with no step; Spread 0 in Stereo the same sample left and right with the
wow at its deepest; ping-pong's L + R holding every repeat.

## The reverb

```
 in --> low cut (2 poles, 20 .. 500 Hz) --> pre-delay (0 .. 138 ms, glides) --> 4 all-pass diffusers --> x input gain --+
                                                                                                                        |
     +------------------------------------------------------------------------------------------------------------------+
     v
   (+)--> [ 8 lines, 57 .. 125 ms at Size 127, one ring of 16384 x int16 ]
     ^         | read at a gliding length (Size); lines 1 3 5 7 also swing on two slow sines (Mod)
     |         v
     |      one-pole low-pass (Damp) --> x g_i (Decay: the same RT60 for every line)
     |         |
     +--- Hadamard 8 x 8 x 181/512 <---+          wet L = l0 - l1 + l4 - l5, wet R = l2 - l3 + l6 - l7, then Width
```

- **The topology** is a feedback delay network: eight delay lines whose outputs are mixed by an orthogonal matrix and
  fed back (Stautner and Puckette, "Designing multi-channel reverberators", Computer Music Journal 1982; Jot and
  Chaigne, "Digital delay networks for designing artificial reverberators", AES 90th Convention 1991), behind
  Schroeder's all-pass diffusers (Schroeder, "Natural sounding artificial reverberation", JAES 1962). The matrix is
  the 8 x 8 Hadamard matrix: three stages of add / subtract, no multiply, then one scaling by 181 / 512. The code is
  written here from those descriptions; nothing is taken from another implementation.
- **The lines** at Size 127, in half-rate samples: `1259 1433 1607 1801 2003 2221 2477 2767` (primes; 57 .. 125 ms; a
  ratio of ~1.12 from one to the next, so their echoes never line up), 0.706 s in all. They live in ONE ring of 16384
  `int16` (line i writes at `w + XR_BASE[i]` and reads behind it), 32 768 B. Size 0 .. 127 scales all of them from a
  quarter to the whole, exponentially; the lengths **glide** (a 64th of a line's length in 256 samples: a size change
  bends the tail's pitch by up to 17 % for a quarter of a second, then it is still) and settle on whole samples.
- **Modulation**: lines 1 and 5 swing on a 0.31 Hz sine and its quarter turn, 3 and 7 on a 0.47 Hz one, 0 .. 0.9 ms
  (Mod). Each side's output has two still and two swinging lines. At the default the pitch deviation of a pass is
  about 2 cents: the tail is chorused and its modes never stand still, and a held chord does not warble. A swinging
  line is read between two samples (linear), which costs it treble at every pass: with Mod up, the top of a long tail
  fades first (at "Inf": 4 kHz is gone in ~15 s, 1 kHz in a few minutes, the bottom stays); **with Mod at 0 nothing
  fades and "Inf" holds everything**.
- **Decay** is the RT60: every line's gain is `10^(-3 L / RT60)` for its own length L, so the lines decay together
  and Size does not change the decay time (`xf_coefs`: an integer `e^-x`, no table). 0.3 .. 72 s, exponential; the
  top position is **"Inf"**: a gain of 1 - 2^-13 a pass, an RT60 of about an hour, i.e. frozen.
- **Damp** is a one-pole low-pass in every line (open .. ~600 Hz): the highs' own, shorter decay.
- **Low cut** (two poles, 20 .. 500 Hz) is in front of everything, so the bass never enters the wash.
- **Pre-delay** 0 .. 138 ms (it glides too), and the **diffusers**: four all-passes of 113, 173, 263 and 397 samples
  (5 .. 18 ms), g = 5/8, in a second ring of 4096 `int16` (8 192 B) with the pre-delay.
- **The input gain** follows the decay as `(1 - g^2)^(1/4)`: a reverb's level under a held chord grows as the square
  root of its RT60, and half of that (in dB) is taken out, so that Decay 72 s is ~6 dB louder than 3 s, not 14.
- **Width** scales the side signal: 0 is a mono reverb, 127 the two sides as they are.

### Why it cannot blow up, and why it ends

1. The Hadamard matrix times `1 / sqrt(8)` is orthogonal (it preserves the lines' energy); 181 / 512 = 0.353516 is a
   hair under `1 / sqrt(8)` = 0.353553.
2. Every line's gain is at most 32764 / 32768; a one-pole low-pass and a linear interpolation never exceed 1 at any
   frequency; the knee and the clamp at the line (`xr_sat`) only take.
3. So one pass round the loop multiplies the energy by less than 1, whatever the parameters.

The arithmetic is the rest of it. Every product in the loop is rounded **towards zero** and every one-pole is written
`y = x + c (y - x)`: no rounding holds an offset or feeds a limit cycle. At the line itself the rule is finer
(`XR_RB`): rounded towards zero, every pass took half a unit off every sample and "Inf" from a moderate send was
silent in 90 s; rounded to the nearest, nothing ends and the loop never switches off. The line therefore rounds
towards zero **less 7/16 of a unit** (a sixteenth of a unit lost a pass: "Inf" holds for a quarter of an hour and
more), and plainly towards zero once a whole block's largest sample is under 8 units (the tail's last second: it ends
exactly). Measured: RT60 0.35 / 1.19 / 2.39 / 4.77 / 9.51 s for the knob's 0.3 / 1.2 / 2.4 / 4.8 / 9.6 s, at any Size.

`tests/cr_fx_test.c`: 48 tails (Decay 0.3 .. 19 s x Size x Damp x Mod after the loudest burst a line takes) all end
in exact silence with the reverb switched off, the longest after 2.3 x its RT60; at "Inf" with Mod at 127 and Size
swept, the lines' energy with no input never rises more than 1 dB above its start (the swing of the modulated lines)
and falls; eight corners of every parameter with an impulse, 5 s of full-scale noise and 5 s of a full-scale square
(the sum of four parts' largest sends), 60 s each: bounded, no offset, and exactly 0 at the end for every finite decay.
**The floor is 0**: there is no noise floor to state. One thing is not zero: a send far over full scale saturates the
lines, and saturating a lopsided wave leaves an offset; at "Inf" it stays as long as the wash does (the master's DC
blocker takes it out of the output).

## Nothing sounds, nothing runs

Felucca's buses ran in every block. Here each effect counts the samples since the last non-zero one it wrote; when a
line's whole length has gone by in silence and no send comes in, its loop is skipped and its filters are cleared
(`xf.d_live`, `xf.r_live`, `xf.up_live`). With both off a block costs **2 us** (the scan of the two sends). In
`perf.sh` (g), a sound with no sends, the FX stage fell from 126 to 67 us a block.

A consequence to know: **the settings and the session are saved when the instrument is quiet** (the master under
-60 dBFS for a second: a flash erase stops the audio for 46 ms). A tail of half a minute or "Inf" would keep that
from ever happening, so the tail alone holds a save back for 30 s at most (`cr_settings.c` `CRS_TAIL_MS`): after
that the save goes through it, one 46 ms hole. At the default Decay the tail is under the threshold 2 .. 4 s after
the last note.

## Fixed point

| | |
| --- | --- |
| lines | `int16`, `.pool` (zeroed at power-on) |
| states, sums | `int32`; the eight lines' products carry 5 more bits than the lines until the one rounding at the line |
| coefficients | Q15 (`c = 1 - k` of the one-poles, the gains); the half-band's Q11; the Hadamard scale 181 / 512; the diffusers 5 / 8 |
| positions, lengths | Q8 samples |
| sends in | saturated at +-131068 (4 x full scale), then a quarter of it into the lines (a loud chord's send is ~+-40000) |
| wet out | saturated at +-131068 before the up-sampler |
| per-sample loops | no division, no 64-bit, no call: `tests/target_budget.txt` `xf_block 78, xf_down 66, xf_up 53, xd_run 531, xr_run 973` (pi32v2 instructions in loops, weighted) |
| coefficients | worked out when a parameter or the tempo moves (`xf_coefs`: a hash of the parameters, compared every block); the gains, the feedback and the filters' coefficients are then eased a block at a time, the width and the mode a sample at a time |

Every bound is in a comment where the product is (the worst case of the half-band's sections, the 65534 x 32767 of
the one-poles, the 8 x 32767 x 32 x 181 of the Hadamard sum).

## Memory

| | Bytes |
| --- | --- |
| the delay's two lines, 2 x 22114 x 2 | 88 456 |
| the reverb's eight lines, 16384 x 2 | 32 768 |
| its pre-delay and diffusers, 4096 x 2 | 8 192 |
| state (`xf`: the coefficients, the filters, a block's half-rate buffers) | ~740 |
| **total, all in `.pool`** | **130 156** |
| Felucca's delay line and ROOM / SPRING buffers, gone from the image | -142 940 |

Size line: XIP 441 988 B (was 436 928), RAM 84 916 B (was 84 944), **POOL 283 440 B of 344 064** (was 296 224):
60 624 B free.

## CPU

The device estimate is `tools/emu/emu.c`'s (host instructions / 259 = us; a block is 128 frames, 2902 us).

| | us a block | of the budget |
| --- | --- | --- |
| both effects on (`cr_fx_test`: noise into both sends) | 233 | 8.0 % |
| both off | 2 | 0.1 % |
| the target | 350 | 12 % |

`sh tools/emu/perf.sh`, one core, before (164bcca) and after; "fx" is the whole stage (the parts' mix into the buses,
the chorus, the delay, the reverb):

| Scenario | Block avg / worst before | after | fx stage avg / max before | after |
| --- | --- | --- | --- | --- |
| (a) TINE EP chord | 543 / 1145 us (39 %) | 704 / 1105 (38 %) | 126 / 300 | 278 / 549 |
| (b) FM PAD chord | 673 / 893 (31 %) | 827 / 1071 (37 %) | 123 / 285 | 273 / 432 |
| (c) FM PAD + bass + loop + arp | 850 / 1165 (40 %) | 997 / 1389 (48 %) | 140 / 263 | 284 / 560 |
| (d) (c) on the SCOPE view | 798 / 1121 (39 %) | 929 / 1268 (44 %) | 134 / 301 | 264 / 462 |
| (e) (c) with the editor's knobs turning | 872 / 1059 (36 %) | 1022 / 1310 (45 %) | 140 / 260 | 290 / 534 |
| (f) VA chord + bass | 739 / 1293 (45 %) | 843 / 1364 (47 %) | 133 / 337 | 234 / 473 |
| (g) CZ-1 chord + bass (no sends) | 815 / 1277 (44 %) | 757 / 1186 (41 %) | 126 / 235 | 67 / 256 |
| (h) the FX layer's knobs turning | 429 / 561 (19 %) | 595 / 828 (29 %) | 128 / 296 | 287 / 438 |
| (i) FM TONE + bass + loop + arp | 748 / 1007 (35 %) | 892 / 1320 (45 %) | 139 / 288 | 280 / 519 |
| (j) 8-note VA chord + bass | 682 / 1143 (39 %) | 781 / 1420 (49 %) | 126 / 260 | 219 / 508 |
| **(l) (c) with both sends at 127, Decay 13 s, Feedback up** | | **885 / 1388 (48 %)** | | 254 / 541 |

The averages are the figure to read: a sound with sends costs ~150 us more a block than before (5 % of the budget;
the old delay and reverb were ~80 us of the old stage), a sound without sends ~60 us less. The worst block of a run
moves by a few per cent from run to run. No block over 80 % in any scenario, single or dual core (the worst: (j) with
Dual Core On, 51 %; before: 47 %). The second core
is not used for the effects: they run on core 0 after both parts are mixed, exactly where Felucca's ran, and the
split render's "one core's samples bit for bit" (`tests/cr_cpu1_test.c`, `perf.sh`'s Dual Core runs) holds as before.

## The level

The sounds' sends were tuned against the old effects. They are not touched; the new effects' returns are set once so
that the same send gives the same loudness (`XD_WET`, `XD_WPP`, `XR_WET` in `cr_fx.c`: the one place):

| Measured | Old | New |
| --- | --- | --- |
| the reverb under a held chord (1 .. 3 s, RMS a side), both at their defaults, the same send | 4122 | 3994 (-0.3 dB) |
| the delay's first repeat (both sides' energy), Stereo | 0 dB | -0.0 dB |
| the delay's first repeat, ping-pong (one side, 3 dB up) | 0 dB | +0.0 dB |
| the power-on sound's stab on the emulator (`fx_review.py`): the wet alone, 0.6 .. 3 s after the stab | 0 dB | +0.2 dB |
| .. the reverb alone | 0 dB | +0.5 dB |
| .. the delay alone (its feedback is 0.455, was 0.42; its Tone is more open) | 0 dB | +2.3 dB |

The defaults: Reverb Size 88, Damp 56, Decay 3.0 s, Pre-delay 17 ms, Mod 56, Low cut 127 Hz, Width 127; Delay Sync
on, 1/8, Spread 1:1, Feedback 58, ping-pong, Tone 84, Wow 24 (free time 248 ms, free spread x1.00).

## The FX layer

SELECT steps six pages; the picker shows the effect, the label the page (`fx · on · 1/2`).

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| Reverb 1 | Size (room) | Damp (moon) | Decay (tail) | Send (mix) |
| Reverb 2 | Pre-delay (pre) | Mod (lfo) | Low cut (cut) | Width (width) |
| Chorus | Rate (lfo) | Depth (lfo) | – | Send (mix) |
| Delay 1 | Time (echoes) | Spread (spread) | Feedback (echoes) | Send (mix) |
| Delay 2 | Sync (sync) | Mode (ping) | Tone (moon) | Wow (lfo) |
| Drive | – | – | – | Send (clip) |

The seven new glyphs (`cr_draw.c cr_picto`: `CR_G_TAIL PRE CUT WIDTH SPREAD SYNC PING`) are drawn in the manner of the
old ones and have fixtures (`design/make_fx_mockups.py` states 2, 3, 5b, 12, 13, 14; `tests/cr_draw_test.c`). The
designer app does not draw them yet: `design/choralroot-fm1-fx-screens.png` is stale for those states. `CR_G_SPRING`
went with the spring. Shots: `build/fx-review/fx_layer_fixtures.png`, `build/emu/test/cr_fx_row*.ppm`.

## How to tune

- **Listen without a unit**: `python3 tools/emu/fx_review.py` renders one stab through nine settings into
  `build/fx-review/` (and through the old effects with `--old EMU`, an emulator built from 0.17), prints the levels and
  draws every WAV. `sh tests/run_cr_tests.sh` leaves the reverb's impulse responses and the delay's repeats there too.
- **Returns**: `XD_WET`, `XD_WPP`, `XR_WET`. `cr_fx_test`'s level checks say where they stand against Felucca's.
- **Defaults**: the `XFP[]` table. A default is only what a new unit and Reset Settings have; the session keeps the
  owner's.
- **The delay's character**: `XD_HPC` (the loop's low cut), the Tone curve in `xf_coefs`, the feedback's top
  (`xfp * 250`), the glide (`>> 10` and the clamp of 128), the wow's two rates and their mix in `xd_run`.
- **The reverb's character**: `XR_LEN` (and `XR_BASE`, their running sum + `XR_GUARD` each), `XR_AP`, the diffusers'
  5 / 8, the two modulation rates in `xr_run`, the Damp curve and the input gain's exponent in `xf_coefs`, `XR_RB` /
  `XR_QUIET` (how long "Inf" holds against how surely a tail ends).
- **A parameter's range or meaning changes**: a new `CSS_FXP_*` tag (never renumber, never reuse), a line in
  `cr_settings.c CSS_FXG`.
- **A longer delay**: `XD_MAX` (and POOL: 4 bytes a sample for the two sides).
- After any of it: `sh tests/run_cr_tests.sh`, `sh tools/emu/perf.sh`, `./build.sh` (the size line), and
  `BUDGET_UPDATE=1 python3 tests/target_budget.py build/choralroot.dis tests/target_budget.txt` if a loop grew on
  purpose.

## Decisions the owner did not make

1. **Half rate for both effects** (above): 9.5 kHz of wet band for a second a side, 43 KB and half the work.
2. **"Send" is the chord part's send**, there is no master wet knob; the cell is named Send.
3. **FX off closes the bass part's sends too.**
4. **The delay does not self-oscillate**: feedback tops out at 0.97. The Matriarch's runs away; here a knob left at
   the top would howl until found, and a tail that never ends keeps the settings from saving. One constant if wanted.
5. **Spread makes the right side shorter**, 1 .. 1/2 of the left; synced it has four steps (1:1, 3:4, 2:3, 1:2).
6. **Sync folds a division longer than a second to half** instead of clamping it.
7. **The wow is the same on both sides** (mono-safe); the stereo movement comes from Spread and ping-pong.
8. **Delay 2's fourth knob is Wow, Reverb 2's is Width**; the old reverb's Type is gone and KNOB 3 is Decay.
9. **Ping-pong is the default mode**, at 1/8 synced with Spread 1:1; the default reverb is 3 s (ROOM's was ~1.8 s).
10. **The delay's time glides (a pitch bend) instead of crossfading**, the reverb's size too.
11. **"Inf" holds for minutes, not for ever, when Mod is up** (the modulated lines lose treble), and holds
    everything with Mod at 0.
12. **A save waits for the tail, 30 s at most** (`CRS_TAIL_MS`), then cuts one 46 ms hole in it.
13. **The retired session tags**: a 0.17 / early-0.18 session's reverb size / damp / type and delay time / feedback /
    colour are not carried over (other meanings and ranges): the new effects start at their defaults; the chorus's
    rate and depth, the sends and the picked effect are kept.
14. **Felucca's goldens are untouched**: its effects stay in `fx.c` behind `CR_FX 0`.
15. **`tests/target_budget.txt` was rewritten** (`BUDGET_UPDATE=1`, as HANDOFF 3y10 asked after the FX work): the
    audio ISR's ceiling is now its real 14 607, not 0.13's 46 077.
