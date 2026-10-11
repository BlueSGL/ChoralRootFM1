# The effects: a stereo delay and a large reverb (0.18)

ChoralRoot 0.18 replaces Felucca's mono tempo delay and its ROOM / SPRING reverb with a stereo delay in the spirit of
the Moog Matriarch's and a large, slow, modulated reverb. The chorus and the drive are Felucca's, unchanged. This is the
design as built: what the code is, why, what it costs, and how to tune it. The player's side is
[MANUAL.md section 8](MANUAL.md#8-fx).

- the code: `firmware/src/cr_fx.c`, included by `fx.c` when `CR_FX` is 1 (`choralroot.c`, the emulator). With `CR_FX`
  0, the default, `fx.c` is Felucca's as it was: its own units, `tests/reverb_test.c` and the 197 golden renders of
  `tests/run_tests.sh` are untouched, and no golden was updated;
- the tests: `tests/cr_fx_test.c` (`sh tests/run_cr_tests.sh`), `tests/run_cr_fx_rates.sh` (half against full rate),
  `tools/emu/test_cr.sh` (the layer, "the effects and the sends are the instrument's"), `tools/emu/test_persist.sh`
  (the session), `tools/emu/perf.sh` scenario (l) and its "fx parts" line;
- the tools: `tools/fx_halfband.py` (the half-band filter's coefficients), `tools/emu/fx_review.py` (listening renders,
  old against new), `tools/fx_plots.py` (pictures of any WAV: level, spectrogram, Schroeder integral);
- on the unit: `fm1_install.py --debug` (SysEx 78 PEAK: "What the unit says").

**This is the second build.** The first (both effects at half the rate, every product rounded towards zero) was heard on
the unit and failed: "a lot of various noise or pops", "the delay just abruptly drops out even with the feedback turned
up". What was wrong and what changed is under "What the first build got wrong".

## The rule: the effects and the sends are the instrument's

The owner's decisions of 2026-10-11 and -12: "The FX settings themselves should be global and not change when changing
presets", then, after playing it: "I actually don't think we should save the sends with the presets ... thus I can switch
through the presets and experience the effect on each instrument."

| What | Where it lives | Changed by a sound load |
| --- | --- | --- |
| each **part's sends** into the reverb, the chorus and the delay, and its **drive** | the part: `P_REV`, `P_CHOR`, `P_DLY`, `P_DIST` of `trk[]`, which no load writes (`cr_bank.c param_kept`, as Level and Pan); the chord part's also in `cs.fx_amt[]` | **never** |
| every effect's **own parameters** (16 of the delay and the reverb: `cr_fx.c` `xfp[]`; the chorus's rate and depth: `song.g`) | global | never |
| all of it over a power-off | the session's FX block, by tag (`cr_settings.h`: `fx.amt[]` the chord part's sends, `CSS_FXP_BS_*` the bass part's, `CSS_FXP_*` the parameters; [SETTINGS.md](SETTINGS.md)) | |
| FX on / off | the settings block (`fx_on`) | no |

- **"Send" on KNOB 4 is the chord part's send** (0..127, shown 0..99). There is no master wet level: the returns are
  fixed (`XD_WET`, `XR_WET`), matched to the old effects (below).
- **The bass part's sends** are set in the bass sound's editor, FX group (BASS held + EDIT, then the FX button), and by
  CC 91 / 93 / 94 on the bass channel. Nothing on the main pages sets them, and nothing was added.
- **The editor's FX group** (`cr_pages.c`: Drive, Chorus, Delay, Reverb) shows and edits the part's sends: the chord
  sound's editor the chord part's (the same four values as the FX layer's Send cells), the bass sound's the bass
  part's. A change there no longer marks the sound as edited.
- **A preset's or a saved sound's send fields** are ignored on load. A save writes the part's current sends into them,
  as it always wrote the part's values: the record's format is unchanged and an older firmware reads it as before.
- **Defaults** (a new unit, Options > Reset Settings): the chord part reverb 35, chorus 45, delay 25, drive 0 (what
  the power-on sound, FM6 TINE EP, was made with); the bass part reverb 10 (VA DEEP SUB's), the rest 0.
- **FX off closes every part's sends** (`fx.c` `fx_off`), the bass part's too.
- MIDI: CC 91 / 93 / 94 set a part's reverb / chorus / delay send; a program change does not touch them.
- A send is reached as a ramp over a block (`fx.c fx_sg`): turning Send, FX on / off and a CC write no step into
  what the delay and the reverb are fed.

## What the first build got wrong

Proved on the host before anything was changed; each has a test now.

1. **The delay dropped out** (`cr_fx_test` "no drop-out"). Every product in its loop was rounded towards zero, and the
   loop's saturator was `dsp.c`'s tanh table, whose first step is 255 / 256. Each pass round the loop therefore took
   one to three units off every sample whatever the feedback: a tail fell in a straight line instead of dying away. At
   a short time that is fast: at 21 ms, 47 passes a second, feedback at its top, a repeat was at -44 dBFS after two
   seconds and **gone half a second later** (measured on the first build: -44.5, -55.2, -111.5 dBFS in three
   consecutive 250 ms windows). Now: every product inside a loop **keeps its remainder** for the next sample
   (`xf_rs`: first-order error feedback, as `fx.c`'s `dc_block` does), and the saturators are exactly 1 below their
   knee. The same burst now falls 2.6 dB every quarter second, the same number in every window, down to -80 dBFS.
2. **Saves cut holes into tails.** The session is saved after every change, when the output is quiet; so that a long
   reverb would not block that for ever I had let the save go ahead after 30 s. A flash erase stops the audio for
   46 ms: every knob turned in the FX layer under a ringing tail ended in a hole half a minute later. That rule is
   gone: no settings or session write while the output is above -60 dBFS, with no time limit (`cr_settings.c
   crs_sounding`). `flash_holes` in `--debug` counts erases into a sounding output.
3. **Quiet tails were grainy and the frozen reverb leaked.** The same rounding, in the reverb: "Inf" from a moderate
   send was a straight line to silence. Now the lines keep their remainders too.
4. **The delay was dull.** At half the rate its repeats stopped at 9.5 kHz beside a dry sound that does not. The delay
   now runs at the full rate (below).
5. **The cost on the unit was far above the estimate** (below: "What the unit says").

## The rates: the delay at 44.1 kHz, the reverb at 22.05 kHz

`CR_FX_DELAY_DS` and `CR_FX_REVERB_DS` (1 full, 2 half; `./build.sh` takes them from the environment) choose each
effect's rate. `sh tests/run_cr_fx_rates.sh` builds the unit test both ways, runs every check on each, and writes the
same material through both: `build/fx-review/cmp_delay_full.wav` / `_half.wav`, `cmp_reverb_full.wav` / `_half.wav`
and the impulse responses, with their spectrograms.

| | Half rate | Full rate |
| --- | --- | --- |
| the delay's band (Tone open) | flat to 9.5 kHz, -63 dB at 12 kHz | -0.2 dB at 9 kHz, -0.5 dB at 15 kHz, -0.6 dB at 18 kHz |
| the delay's memory for 0.74 s a side | 65 KB (or 1.49 s in 131 KB) | **131 072 B** |
| the delay's cost (emulator estimate, a half of 128 frames) | 92 us | **117 us** |
| the reverb's band | to 9.5 kHz | to 20 kHz |
| the reverb's memory | **40 960 B** | 81 920 B |
| the reverb's cost | **153 us** | 302 us |
| both | 222 us (7.6 %) | 417 us (14.4 %) |
| **as built: delay full, reverb half** | | **267 us (9.2 %)** |

**As built: the delay at the full rate, the reverb at half.** The delay's repeats are heard dry and bright beside the
note that made them, and the full rate costs it only 25 us on the estimate (it loses its down- and up-sampler); the
reverb is a dark wash whose top is its own damping, and the full rate would double it (+149 us) and leave 0.37 s of
delay in the memory that is there.

**Memory at the full rate.** A second a side is 176 KB; the pool has 131 KB for the delay with 17.7 KB left free, so
the line is 32768 samples: **0.743 s a side** (a 1/4 at 81 BPM and up; a division longer than the line plays at half).
The owner accepted "or lower if need be". At half rate the same memory is 1.49 s.

### What the unit says, and the formula

The emulator counts host instructions and calls 259 of them a microsecond. On the owner's unit, with the first build
and Dual Core On, SysEx 73 DEBUG read `max_us 2580` of 2902 (89 %) and `late 12`: the emulator's worst half for that
build was 51 %. The estimate was at least 1.7 times too low on the worst half, and nothing said which stage.

`fm1_install.py --debug` now reads **SysEx 78 PEAK** as well (a command of its own: 73's count is one 7-bit byte and
its 127 values fill it). Its values are the worst cases **since the last read** (reading clears them: play, read, and
the numbers are that stretch's):

| Name | |
| --- | --- |
| `pk_block_us` | the largest whole half (the budget is 2902) |
| `pk_tick_us` `pk_part0_us` `pk_part1_us` `pk_fx_us` `pk_master_us` `pk_wait_us` | each stage's largest half |
| `pk_late`, `pk_late_stage`, `pk_late_us` | late halves; the latest one's largest stage (1 tick .. 6 wait) and its time |
| `pk_halves`, `fx_busy_halves` | halves counted; halves in which the effects ran |
| `fx_down_avg_us` / `_max_us`, `fx_delay_*`, `fx_reverb_*`, `fx_up_*` | inside the fx stage, over `fx_busy_halves`: the sends' down-sampler, the delay, the reverb, the wet's up-sampler |
| `flash_erases`, `flash_holes`, `saves_settings`, `saves_session` | since power-on: erases (46 ms of silence each), erases into an output above -60 dBFS (the rule: 0), saves |

The instrumentation is five TIMER4 reads a block. `tools/emu/perf.sh` prints the same four parts ("fx parts"), so the
two compare line by line. The emulator's, for (l), the heaviest sound with both sends at 127:

```
fx parts (device us per half, avg / max): down 12 / 123, delay 121 / 209, reverb 116 / 237, up 22 / 67
```

**The formula.** With `k = (the unit's fx_delay_avg_us + fx_reverb_avg_us + fx_down_avg_us + fx_up_avg_us) / 271`
(271: the emulator's four averages above):

- the reverb at the full rate would cost the unit about `k x 149 us` more a half;
- the delay at half rate would save about `k x 25 us`;
- the limit to hold: `pk_block_us` under about 2030 (70 %) with the heaviest sound, single core.

**Why the estimate is low, as far as reasoning goes** (the counters will say which):

1. *The code cache.* RAM and POOL are one on-chip SRAM (`app.ld`: 0x01C08000 and 0x01C20000), so the lines are not a
   cache problem; the code is. It executes in place from the SPI flash through a cache, and every block runs the
   engines' code and then the effects': code that has fallen out of the cache is read from the flash again. The
   emulator sees none of that. **`CR_FX_RAM=1 ./build.sh`** places the four per-sample loops (`xf_down`, `xf_up`,
   `xd_run`, `xr_run`: 2.9 KB) in `.ram_text`, where the flash driver already lives, and calls them through a
   pointer: no flash read in them. It builds and passes `build.py`'s checks (`.ram_text: 1905 insns, no calls`,
   RAMTEXT 5904 of 24576); it is **off by default because it has not run on a unit**. It is the first thing to try if
   `fx_*` on the unit are far above the emulator's.
2. *The compiler.* The unit is built at `-Os`, the emulator at `-O2`.
3. *Dual Core.* With core 1 rendering the bass, the two cores share the caches and the bus.
4. *What nests in the half.* TIMER5 and the USB recording's service run inside it (`ua_overruns 37` on that read: the
   computer was recording).
5. *Erases.* A flash erase holds every interrupt for 46 ms: the halves after it are late whatever they cost. With the
   first build's save rule that was every knob turn under a tail.

**If neither rate fits the real budget**, in this order: `CR_FX_RAM=1`; the delay at half rate (`CR_FX_DELAY_DS=2`);
a lighter reverb (four lines and two more diffusers: about 60 % of `xr_run`); the effects on the second core when
Dual Core is on (a design only: the two send buses are complete when the parts are mixed, so core 1 could run
`xf_block` on a copy of them while core 0 runs the master of the previous block, at the price of one block, 0.7 ms,
of extra latency on the wet and a second pair of mix buffers; nothing of it is built).

## The delay

```
                       +-------------------- feedback (0 .. 0.988, eased) ------------------+
                       v                                                                    |
 in --(+)--> sat --> [ line L, 32768 x int16 ] --> cubic read at tL + wow --> low-pass (Tone) --> low cut 40 Hz --+--> wet L
 in --(+)--> sat --> [ line R, 32768 x int16 ] --> cubic read at tR + wow --> low-pass (Tone) --> low cut 40 Hz --+--> wet R
                       ^                                                                    |
                       +--------------------------------------------------------------------+
   Stereo: each side feeds itself, both take the input.
   Ping-pong: L takes the input and R's return, R takes L's return only (the crossfade between the two: 6 ms).
```

- **Two lines of 32768 samples** at 44.1 kHz (0.743 s), `int16`, in `.pool`: 131 072 B. They hold half the send
  (`XF_IN`): a loud chord's send is about +-40000, so 20000 in the line, as Felucca's delay took it.
- **The read** is a four-point cubic (Catmull-Rom) at a Q8 position that **glides** to its target: a lag of ~46 ms, at
  most half a sample a sample. A new time (the knob, the tempo, a MIDI clock, Sync or Spread) bends the pitch of what
  is in the line by at most a fifth up or an octave down for a moment, as a tape or a BBD clock does, and never
  clicks. At rest the position is a whole sample and the read is that sample itself.
- **The loop**: read -> one-pole low-pass (Tone, 400 Hz .. 22 kHz, an octave every 22 steps; the default, 70, is
  3.8 kHz) -> one-pole low cut at 40 Hz -> feedback gain -> + the input -> the saturator -> the line. Every repeat is
  darker than the last. The wet is taken after the filters, so the first repeat is already coloured.
- **The saturator** (`xd_sat`) is a straight line up to half of the line's range and a parabola above it whose slope
  falls to 0 at the line's limit: no edge. A repeat under the knee has its harmonics below -90 dB (measured -92 dB at
  -12 dB of the line); at full scale it is -27 dB, a soft clip.
- **Feedback** tops out at 0.988: at the longest time that is seven minutes to -60 dB, at 16 ms eight seconds. It
  does not self-oscillate (a decision: see the end).
- **Time**: Sync on: a division of the tempo, `1/32 1/16T 1/32. 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/2T 1/4. 1/2`
  (`XF_DIV`, 48ths of a quarter note; the tempo is `xf_bpm`, the engine's 20 .. 300 BPM, MIDI clock included). A
  division longer than the line plays at half. Sync off: 16 .. 740 ms, exponential over the knob. Both values are
  kept; Sync chooses.
- **Spread** is the right side's time as a part of the left's. Sync on: `1:1`, `3:4`, `2:3`, `1:2`. Sync off: `x1.00`
  .. `x0.50`. The right side is always the shorter one, so Time is the longest time and always fits the line.
- **Wow**: the time's wobble, a 0.33 Hz sine and an eighth as much of a 3.1 Hz one, 0 .. 2 ms. The SAME on both
  sides: with Spread 0 in Stereo the two sides stay the same sample (the FM-1's speaker is mono).
- **Level**: `XD_WET` (Q12) is the Stereo return, `XD_WPP` the 3 dB more that ping-pong gets (its repeat is on one
  side only). Matched to Felucca's delay: see "The level".

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
- **The rate**: 22.05 kHz, between a two-path polyphase all-pass half-band filter down and a pair of them up
  (`xf_down`, `xf_up`; `tools/fx_halfband.py`: 2 + 2 first-order sections, Q11 `199, 1264 | 695, 1785`, -54 dB above
  12.55 kHz, flat to 9.5 kHz; they run on the send and the wet at full resolution and keep their remainders).
- **The lines** at Size 127, in half-rate samples: `1259 1433 1607 1801 2003 2221 2477 2767` (primes; 57 .. 125 ms),
  0.706 s in all, in ONE ring of 16384 `int16`, 32 768 B. Size 0 .. 127 scales all of them from a quarter to the
  whole; the lengths **glide** (a size change bends the tail's pitch by up to 17 % for a quarter of a second) and
  settle on whole samples.
- **Modulation**: lines 1 and 5 swing on a 0.31 Hz sine and its quarter turn, 3 and 7 on a 0.47 Hz one, 0 .. 0.9 ms
  (Mod). Each side's output has two still and two swinging lines. A swinging line is read between two samples
  (linear), which costs it treble at every pass: with Mod up the top of a long tail fades first.
- **Decay** is the RT60: every line's gain is `10^(-3 L / RT60)` for its own length L (`xf_coefs`: an integer
  `e^-x`, no table). 0.3 .. 72 s; the top position is **"Inf"**: a gain of 1 - 2^-13 a pass, **and the damping out
  of the loop** (a one-pole takes a little of everything at every pass: at Damp's default, 3.4 kHz, 1 kHz is gone in
  15 s whatever the gain). With Mod at 0, "Inf" loses 1 dB in 30 s and 21 dB in ten minutes.
- **Damp** is a one-pole low-pass in every line (open at 0, 12 kHz .. 660 Hz). **Low cut** (two poles, 20 .. 500 Hz) is
  in front of everything. **Pre-delay** 0 .. 138 ms (it glides too), and the **diffusers**: four all-passes of 113,
  173, 263 and 397 samples, g = 5/8, in a second ring of 4096 `int16` (8 192 B) with the pre-delay.
- **The input gain** follows the decay as `(1 - g^2)^(1/4)`, so that Decay 72 s is ~6 dB louder than 3 s, not 14.
- **Width** scales the side signal: 0 is a mono reverb, 127 the two sides as they are.

### Why it cannot blow up

The Hadamard matrix times `1 / sqrt(8)` is orthogonal; 181 / 512 = 0.353516 is a hair under it. Every line's gain is
at most 32764 / 32768; a one-pole low-pass and a linear interpolation never exceed 1 at any frequency; the knee and the
clamp at the line only take. One pass round the loop multiplies the energy by less than 1, whatever the parameters;
the rounding adds at most a unit's worth of noise, which the loop's own loss takes away again.

## The arithmetic, and where a tail ends

- **In a loop, a product keeps its remainder** (`xf_rs`): the delay's two filters and its feedback, the reverb's
  low-pass, gain and line write (three a line), the low cut, the half-band's sections. Nothing is lost over time and
  nothing is biased; with a silent input the result is exactly 0.
- **Feed-forward products round towards zero** (the diffusers, the interpolations, the input gain).
- **A tail ends at one unit of the line.** When a whole block has written nothing larger than +-1 (-90 dBFS of the
  16-bit line; at the output under -77 dBFS at full MASTER even with every line of the reverb at once, in practice
  -84), the loop's products are rounded towards zero for the next block: the last single units are not sent round
  again and the lines reach exactly 0 one pass later. A block that writes anything larger is back to the remainders
  at once. **Nothing above one unit is ever cut**, and there is no other threshold: an effect's loop is skipped only
  when every sample of its lines and every state of its filters is 0 (`xf.d_live`, `xf.r_live`). With both off a
  block costs 2 us.

Measured (`cr_fx_test`):

| | |
| --- | --- |
| a 50 ms burst into the delay, feedback 127, at 16 ms | 2.6 dB a quarter second in every window to -80 dBFS; 8 s of tail (a quiet burst, 30 dB lower: the same slope, 5.8 s) |
| .. at 60 ms, a quiet burst | 0.7 dB a quarter second to -84 dBFS, 16.5 s |
| .. at the longest time, feedback 120 | 1.8 dB every two seconds to -82 dBFS, 54 s |
| the delay's tail from -20 to -80 dBFS: its noise (the wet of a send and of the same send upside down, halved sum) | -91 dBFS at -20, -90 at -50, -89 at -80: never less than 9 dB under the tail |
| the reverb's tail (Decay 7 s) from -20 to -80 dBFS: its noise | -85 / -86 / -86 dBFS: never less than 6 dB under the tail |
| the reverb at "Inf", Mod 0, Damp 0, ten minutes | the tail -24 -> -45 dBFS; the rounding's noise, which goes round with it, 40 to 28 dB under it (-68 dBFS at its loudest) |
| RT60 of a click for the knob's 0.3 / 1.2 / 2.4 / 4.8 / 9.6 s | 0.34 / 1.19 / 2.37 / 4.64 / 9.06 s, at any Size |
| 48 tails (Decay 0.3 .. 19 s x Size x Damp x Mod) | all end in exact silence with the reverb off, the longest after 2.3 x its RT60 |
| every corner, 5 s of noise and 5 s of a square at +36 dB (four parts' sends at their clamps), 60 s | bounded; exactly 0 at the end for every finite decay |
| a 220 Hz send at +12 dB into both | the wet peaks at 3.5 x full scale (the master's limiter and tanh follow), its largest sample step 5.7 % of the peak: no wrap, no edge |
| 16 parameters swept under a held tone and its tail at 30, 6 and 1 ms a detent; the tempo stepped | no step: the wet's second difference stays under a quarter of its largest slope |

Where each stage saturates: a send is clamped at 4 x full scale (it comes out of the parts' own clamps); the delay's
line at `xd_sat` (soft); the reverb's input at the line's range (a clamp, 6 dB above the loudest chord), its lines
at `xr_sat` (a knee at 3/4, then the limit); the half-rate wet at 4 x full scale before the up-sampler (a clamp that
the lines' own limits keep it from reaching); then the master's limiter and tanh, as for the dry sound.

The lines are 16 bits. 32 bits for the reverb's lines would put its noise 48 dB lower and cost 33 KB, which is the
delay's last 0.15 s; with the numbers above it was not taken.

## Memory

| | Bytes |
| --- | --- |
| the delay's two lines, 2 x 32768 x 2 | 131 072 |
| the reverb's eight lines, 16384 x 2 | 32 768 |
| its pre-delay and diffusers, 4096 x 2 | 8 192 |
| state (`xf`) | ~1 000 |
| **total, all in `.pool`** | **173 032** |
| Felucca's delay line and ROOM / SPRING buffers, gone from the image | -142 940 |

Size line: XIP 444 148 B (0.17's: 436 928), RAM 85 156 B (84 944), **POOL 326 316 B of 344 064** (296 224):
17 748 B free.

## CPU (the emulator's estimate; see "What the unit says")

A half is 128 frames, 2902 us.

| | us a half | of the budget |
| --- | --- | --- |
| the delay alone (`cr_fx_test`: noise into its send) | 114 | 3.9 % |
| the reverb alone, with its down- and up-sampler | 155 | 5.3 % |
| both | 267 | 9.2 % |
| both off | 2 | 0.1 % |
| the target | 350 | 12 % |

`sh tools/emu/perf.sh`, one core, before (0.17's effects, 164bcca) and now; "fx" is the whole stage (the parts' mix into
the buses, the chorus, the delay, the reverb). Since the sends are the parts', every sound plays into the effects
(the chord part's defaults), (g) too, which had none:

| Scenario | Half avg / worst before | now | fx stage avg / max before | now |
| --- | --- | --- | --- | --- |
| (a) TINE EP chord | 543 / 1145 us (39 %) | 744 / 1167 (40 %) | 126 / 300 | 320 / 557 |
| (b) FM PAD chord | 673 / 893 (31 %) | 866 / 1163 (40 %) | 123 / 285 | 313 / 640 |
| (c) FM PAD + bass + loop + arp | 850 / 1165 (40 %) | 1037 / 1319 (45 %) | 140 / 263 | 324 / 574 |
| (f) VA chord + bass | 739 / 1293 (45 %) | 842 / 1380 (48 %) | 133 / 337 | 265 / 558 |
| (g) CZ-1 chord + bass | 815 / 1277 (44 %) | 971 / 1683 (58 %) | 126 / 235 | 266 / 579 |
| (j) 8-note VA chord + bass | 682 / 1143 (39 %) | 788 / 1432 (49 %) | 126 / 260 | 251 / 642 |
| **(l) (c) with both sends at 127, Decay 13 s, Feedback up** | | **919 / 1278 (44 %)** | | 284 / 701 |

A sound costs about 190 us more a half than with Felucca's effects, on the estimate (6.5 % of the budget). No half
over 80 % in any scenario, single or dual core (the worst: (g), 58 %; with Dual Core On 57 %). The second core is not used
for the effects: they run on core 0 after both parts are mixed, exactly where Felucca's ran, and the split render's
"one core's samples bit for bit" (`tests/cr_cpu1_test.c`, `perf.sh`'s Dual Core runs) holds as before.

`tests/target_budget.txt` (pi32v2 instructions in loops, weighted): `xf_block 128, xf_down 65, xf_up 57, xd_run 522,
xr_run 1075`; no divide and no call in a loop. A pass of `xd_run` is one sample at 44.1 kHz (246 instructions in
its loops), a pass of `xr_run` one at 22.05 kHz (415).

## The level

The new effects' returns are set once so that the same send gives the same loudness as Felucca's (`XD_WET`, `XD_WPP`,
`XR_WET` in `cr_fx.c`: the one place):

| Measured | Old | New |
| --- | --- | --- |
| the reverb under a held chord (1 .. 3 s, RMS a side), both at their defaults, the same send | 4122 | 4303 (+0.4 dB) |
| the delay's first repeat (both sides' energy), Stereo | 0 dB | +0.0 dB |
| the delay's first repeat, ping-pong (one side, 3 dB up) | 0 dB | +0.1 dB |

The defaults: Reverb Size 88, Damp 56, Decay 3.0 s, Pre-delay 17 ms, Mod 56, Low cut 127 Hz, Width 127; Delay Sync
on, 1/8, Spread 1:1, Feedback 58, ping-pong, Tone 70 (3.8 kHz), Wow 24 (free time 250 ms, free spread x1.00).

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
went with the spring.

## How to tune

- **Listen without a unit**: `python3 tools/emu/fx_review.py` renders one stab through nine settings into
  `build/fx-review/` (and through the old effects with `--old EMU`, an emulator built from 0.17), prints the levels and
  draws every WAV; `sh tests/run_cr_fx_rates.sh` the half / full pairs.
- **Returns**: `XD_WET`, `XD_WPP`, `XR_WET`. `cr_fx_test`'s level checks say where they stand against Felucca's.
- **Defaults**: the `XFP[]` table; the sends' in `cr_ui.c cu_sends_default`.
- **The delay's character**: the 40 Hz low cut and the Tone curve in `xf_coefs`, the feedback's top (`xfp * 255`),
  the glide (`6 + XD_SH` and the clamp of 128), the wow's two rates in `xf_block`, the knee in `xd_sat`.
- **The reverb's character**: `XR_LEN` (and `XR_BASE`, their running sum + `XR_GUARD` each), `XR_AP`, the diffusers'
  5 / 8, the two modulation rates in `xf_block`, the Damp curve and the input gain's exponent in `xf_coefs`.
- **A parameter's range or meaning changes**: a new `CSS_FXP_*` tag (never renumber, never reuse), a line in
  `cr_settings.c CSS_FXG`.
- After any of it: `sh tests/run_cr_tests.sh`, `sh tools/emu/perf.sh`, `./build.sh` (the size line), and
  `BUDGET_UPDATE=1 python3 tests/target_budget.py build/choralroot.dis tests/target_budget.txt` if a loop grew on
  purpose.

## Decisions the owner did not make

1. **The delay at the full rate, the reverb at half** (above), from the emulator's numbers: the unit's counters may
   say otherwise, and both are one build flag.
2. **0.74 s a side** (a 32768-sample line: what the pool has).
3. **16-bit lines** for the reverb (32 bits: 48 dB less noise for the delay's last 0.15 s).
4. **`CR_FX_RAM` is off**: the loops in RAM are built and checked but not run on a unit.
5. **A tail ends at one unit of the line** (-90 dBFS of it), nowhere above.
6. **"Inf" takes the damping out of the loop.**
7. **Nothing is saved while a tail rings**, with no time limit: with the reverb frozen, nothing is stored until it is
   turned down or FX is off; a power cut loses the changes since the last quiet moment. (The loops' own saves, at a
   loop's stop, are as they were and can still fall into a tail: `flash_holes` will show it. The save policy is
   being reworked separately.)
8. **The sends' defaults**: chord 35 / 45 / 25 / 0 (reverb, chorus, delay, drive), bass reverb 10.
9. **The editor's FX group stays**, now the part's sends; a change there is not an edit of the sound.
10. **A sound saved from now on stores the part's sends** in its unused fields (FX off: zeros).
11. **The delay does not self-oscillate**: feedback tops out at 0.988.
12. **Spread makes the right side shorter**, 1 .. 1/2 of the left; synced it has four steps.
13. **Sync folds a division longer than the line to half** instead of clamping it.
14. **The wow is the same on both sides** (mono-safe).
15. **Ping-pong is the default mode**, at 1/8 synced with Spread 1:1; the default reverb is 3 s.
16. **The delay's time and the reverb's size glide (a pitch bend)** instead of crossfading.
17. **The delay's free Time has a new session tag** (25: 16 .. 740 ms; 9's 21 .. 1000 ms is retired): a unit that
    ran the first build starts at the default free time. Its other effect settings and its sends are kept.
18. **Felucca's goldens are untouched**: its effects stay in `fx.c` behind `CR_FX 0`.
19. **The engine picker's KNOB 4 does nothing now** (no parameter there was missing one); the record's `pick_roots`
    byte is reserved and written 0.
