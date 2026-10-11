# ChoralRoot FM-1 — the looper

`firmware/src/cr_loop.[ch]` (portable C99, no libc, no allocation; host tests `tests/cr_loop_test.c`), driven by
`cr_out.c` in the audio ISR and by `cr_ui.c` (gestures, screens, LEDs, flash slots). Grid reference:
`d_cr_loop.lua`, `d_cr_slots.lua`, `design.md` §14.

## What is recorded

A **semantic event** per chord gesture, written when the gesture ends (or when the capture ends with it held):

| field | bits | meaning |
| --- | --- | --- |
| `t` | u16 | on time, ticks from the loop start |
| `dur` | u16 | length in ticks (>= 1) |
| `root` | u8 | absolute resolved root (transpose and Key Mode applied: the recorded harmony) |
| `vel` | u8 | 1..127 |
| `qx` | u8 | quality (CR_Q_*) << 4 \| extension mask (CR_EXT_*) — the *final* chord (Advanced / Free). A single note (quality 0) has no extensions: its low nibble is its **part**, `CRL_X_BASS` (1) = a bass-side note (below) |
| `layer` | u8 | RAM only (in flash layers are counts): the layer, or `CRL_HID` \| the Replace layer that hid it |

Capture comes from the engine's `cr_out_t.gesture` hook (keyed voices and pads; a same-root retrigger ends one
gesture and starts another). Playback calls `cr_loop_event(c, lv, root, q, ext, vel, on)`: a resolved chord on one
of `CR_MAX_LOOPV` (8) loop voices, bypassing the chord-key state machine; voicing, performance, bass apply live.

**Bass-side notes (Single = Split Bass; since 0.18).** A key played on the bass side is recorded as a bass-side note
and plays back on the bass part: the BASS stream alone (the bass sound, the bass channel on MIDI out), never the
chord part. Until 0.17 it was recorded as an ordinary single note and came back with the chord sound.

- **Decided when it is recorded, and stored.** The engine knows the side when the key goes down (`cr_key_ex`: the
  split in force then, the pitch class in the Chords layout, the key on the Full keyboard) and says so in the gesture
  (`ext | CR_GX_BASS`, with quality and mask 0; `root` = the played pitch + transpose, without the bass register).
  `crl_write` keeps it as `CRL_X_BASS` in the event's `qx`; `crl_play_ev` hands it back to `cr_loop_event` as
  `CR_GX_BASS`, which makes the loop voice a `CR_Z_BASS` voice, as the key was. Nothing at playback is consulted:
  Single (Full, Split Oct, Split Bass), the split point, the layout (Chords / Full), Bass on / off and its Behaviour
  may all have changed, the note stays on the bass part. The other way too: a note recorded on the chord side (or
  with Split Bass off) is never moved to the bass part by turning Split Bass on later. The loop is a recording of
  what was played.
- **Where the bit lives.** In `qx`, not in `layer`: a single note's extension nibble meant nothing (`cr_chord_base`
  ignores it), so the event and the record keep their size (8 bytes in RAM, 7 in flash), and every layer operation
  carries it without knowing: Overwrite, Overdub, Replace's hide and its cut copy (`crl_cut` copies the event),
  undo's restore, quantize, Step's commit, `cr_loop_pack` / `cr_loop_unpack`, the slot's A/B copies, the pending
  save, the backup's loop objects (opaque records). `crl_write` now stores 0 in a chord-side single note's nibble
  (it used to store the extension keys held, which never sounded).
- **What still applies at playback**, as to a live bass-side key: the **bass register** (BASS layer KNOB 2; KNOB 2 on
  the view) shifts it by octaves, at once if it sounds; the bass part's sound, level and voice mode; the loop's
  Level; the BASS stream on / off (off: silent). **What does not**: the chord voicing (KNOB 1) and Transpose (the
  recorded root is absolute, as for every loop event; the octave the key was played in is in it), the performance
  mode (a bass-side note is never arpeggiated, live or looped).
- **Live over a loop.** A looped bass-side note and a live one meet on the bass part. The engine counts owners per
  note (`crx_note_start`): two different pitches are two notes on the BASS stream and the part's voice mode decides
  what is heard (the default bass is mono: the last note wins, as with two live notes); the same pitch is re-struck
  and ends when its last owner ends. Neither end leaves a note stuck (`s_bass`, `cr_loop_bass.txt`).
- **The bass that follows the chords** (Bass on with a Behaviour, Split Bass off at playback) keeps following the
  loop's chords, and the recorded bass-side notes play too: both are notes of the one bass part, the last one wins
  on a mono bass. With Split Bass on at playback nothing follows (as for live chords), so only the recorded bass
  notes and the ones played by hand use the bass part.
- **Step entry**: a bass-side key entered as a step is a bass-side step.
- **Lights and screens**: a looped bass-side note glows dim on the keys of its note name like any loop note (by
  name, so the bass register does not move it); the PLAY menu's marks, the step grid, the cursor's note name and the
  corner dial do not show the part (a bass-side step is named by its note, as a single note is).

## Timing

- Ticks: 96 per quarter note at the engine's BPM, accumulated from the ms clock with the remainder kept
  (no drift). A BPM change changes the loop's speed, never its pitch. Bar = 384 (4/4) or 288 (3/4, 6/8) ticks.
- Two clocks since 0.18 (the Rate, below): `crl.rt`, the tempo's ticks (the count-in, the click), and `crl.t`, the
  LOOP's ticks, which every time of the loop's content is in. At Rate x1 they tick together.
- **Free**: REC arms; the first chord starts the take; REC or LOOP ends it (length = elapsed, 24..65535 ticks).
- **1/2/4/8/16 bars** (KNOB 1 in the PLAY menu; SELECT until 0.17): REC starts a one-bar count-in (clicked; KNOB 4 turns it off), then
  records the sync length and commits by itself. REC/LOOP earlier: the length rounds up to whole bars.
- The take is the first cycle: playback continues seamlessly from the commit.
- What REC does over a loop is the **record mode** (below, "Record modes"); **Overdub**: REC while playing (or
  stopped with a loop: it starts) arms; the next chord opens a layer whose times are cycle-relative; REC ends it,
  LOOP ends it and stops. Empty layers are discarded.
- **Quantize** (KNOB 3: none, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32) moves note-ons to the nearest grid point at commit;
  note-offs stay (>= 1 tick); a note-on rounded past the end goes to 0.
- **Undo** (REC held; F#4 in the PLAY menu with Keys = Loops): the layer in progress, else the last layer (the first
  stays: CLEAR); a Replace layer's undo brings back what it erased.
- **Clear**: D#4 held 1 s in the PLAY menu with its keys on loops (GLO). **Panic**: stops, keeps the
  loop; a fresh take is dropped (a take only armed or counting in: the loop kept), a partial overdub kept. **Level**
  (page 2, KNOB 1) scales the loop's velocities (0 = mute).
- **Metronome**: METRO tap = click on/off (on the take's / the loop's / its own grid), held = 4/4 3/4 6/8 picker,
  KNOB 1 there or OPT + SELECT = click level. The click is a 25 ms two-pole sine burst mixed after fx.c (cr_out.c
  `cr_click_mix`), 2.5 kHz on the bar's first beat.

## The Rate (0.18)

"We should also have the ability to change the rate of the loop so I could make it go faster or slower." The PLAY
menu's KNOB 2: **x1/4, x1/2, x3/4, x1 (default), x1.5, x2, x4** (`cs.loop_rate`, the session's `loop_rate`: manual,
kept by the global save; posted as `LC_RATE` -> `cr_loop_set_rate`).

- **What it scales**: how fast the loop's own time runs against the tempo. `crl_tick1` runs once per tempo tick
  (`crl.rt`) and advances the loop's clock `crl.t` by the Rate in quarters of a tick (`CRL_RATE_Q2` 1 2 3 4 6 8 16,
  the remainder kept in `rfrac`): 0, 1, 2 or 4 loop ticks a tempo tick, each with the take's end, the voices' ends
  and the events due. The loop is derived from the clock, so it cannot drift from it; an event fires on a tempo
  tick, as it always did (5.2 ms at 120 BPM). At x1 the code path is the one there was: one loop tick a tempo tick
  (`tests/cr_loop_test.c`: every scenario older than the Rate passes unchanged).
- **Locked to the clock**: a loop of whole bars starts on a beat again at x1/4, x1/2, x2 and x4 (4 bars: every 16, 8,
  2, 1 bars); at x3/4 and x1.5 its length in beats is not whole for most lengths (4 bars: 21.33 and 10.67 beats) and
  its start falls between the beats until the rounds add up (every third one).
- **A change while it plays is taken at the loop's start** (`rate_want` -> `rate` in `crl_wrap`, the end of the
  cycle), never in the middle: the position does not jump, a sounding note keeps its length in loop time, and a loop
  whose cycles ended on a beat starts the new rate on one. (The other way, scaling the position's speed at once, is
  as glitch-free but lets the loop's start land anywhere between the beats from then on.) Stopped, or with no loop:
  at once; during a take: at its first wrap. The UI shows the rate asked for.
- **Recording at a rate other than x1**: every time of a take, an overdub or Replace is taken from the loop's clock,
  so events are written in loop time and playback at that same rate gives back what was played. Length (the next
  take's bars) and Quantize count the LOOP's bars and grid: at x2 a 4-bar take lasts 2 bars of the metronome, at x1/2
  eight. (The other reading, "Length is bars of the tempo", would make the same setting mean loops of different
  sizes; the owner did not choose: this one keeps one rule, "everything of the loop's content is in loop time".)
- **The tempo's, not the loop's**: the count-in (one bar of the tempo), the metronome (its grid restarts at the
  loop's start when that start is on one of its beats, always at x1 as before; else it keeps running), MIDI clock out
  (cr_out.c: from the tempo) and 0xFA / 0xFC.
- **Following the loop's time**: the corner dial (`cr_loop_ring`), the top line's bar.beat, the PLAY menu's
  timeline and playhead (`cr_loop_pos`, `cr_loop_where`); a Free take's elapsed time is shown in real seconds.

## Record modes (docs/LOOPER-MODES.md)

The record mode (`cs.loop_mode`, settings v8; ALGORITHM in the PLAY menu; posted as `crl.mode`) decides what REC does
when nothing is being captured. REC while capturing ends the capture whatever the mode (a take, a layer, step entry);
REC while armed or counting in cancels.

| mode | REC on an empty slot | REC over a loop (stopped or playing) |
| --- | --- | --- |
| **Overwrite** (default) | a fresh take (Free: armed; synced: the count-in) | stops it and arms the same way; the loop is cleared when the take **starts** (the first chord, the count-in's end: `crl.ow`, `crl_take_start`), so REC / LOOP before that cancels and the loop is intact |
| **Advance** | as Overwrite, where it is | the UI first stops the loop and switches to the next slot (`CRL_NEXT_SLOT`: 10 wraps to 1; `cu_loop_load`, the slot left saved by the slot rule, stopped so written at once), the strip's hop arrow ~800 ms and "slot 3 saved · next: slot 4"; then Overwrite there (that slot's loop goes when the take starts) |
| **Overdub** | a fresh take | arms an overdub (above) |
| **Replace** | a fresh take | arms as Overdub (`crl.rep`); while a gesture of the layer is held the older layers' events whose on-time is reached are hidden instead of played (`crl_fire`), and an older event sounding when a gesture starts is cut there (`crl_cut`: it is hidden and a copy ending there goes into the new layer; its voice ends). Hidden events keep their place (`layer` = `CRL_HID` \| the hiding layer) inside the 512-event cap; undo of that layer (`crl_restore`, by `crl.lev0`, each layer's first event) brings them back whole; the flash record stores only live events |
| **Step** | step entry (`CRL_CAP_STEP`) | the same (the loop stops first) |

**Step entry** (a step sequencer since 2026-10-12; the owner: "I would expect this system to work more like an
Elektron sequencer in which you can record up to 64 steps. So at 1x, 16 steps is a bar").

- **The grid.** A step is a **sixteenth note** of the loop's own time (`CRL_STEP`, 24 ticks; `crl.step_g`): 16 steps a
  bar of 4/4 at Rate x1 (12 in 3/4 and 6/8). The **Rate** scales the pattern with the loop, as it does any loop: x2
  makes the steps thirty-seconds of the tempo, x1/2 eighths. **Quantize does not set the step**: it is the grid a take
  played by hand is pulled to. While the record mode is Step its cell reads `1/16`, dim, and KNOB 3 says `Step: always
  1/16` and leaves the takes' Quantize as it is.
- **Length.** A pattern has 1 .. 64 steps (`CRL_STEPS`). **Length Free: the loop is the steps entered, exactly** ("3
  chords, the loop should just be 3 steps long", the owner 2026-10-11), growing to 64; a chord after the 64th is refused
  (`64 steps: full`; `crl.step_no`), and so is a rest there. **A set Length**: N bars are N x 16 steps and the cursor
  wraps at the end; a Length longer than a pattern can be gives its first whole bars within 64 steps (8 and 16 bars: 4
  bars; 3/4 and 6/8: 5 bars, 60 steps) and says so when step entry begins (`Step: 4 bars (64 steps)`;
  `cr_loop_step_len`). Takes played by hand keep their 8 and 16 bars.
- **Entering.** The cursor starts on step 1. A chord gesture's end writes its chord, one step long, on **the step the
  gesture was pressed on**. The cursor moves on a step
  - when every key is up and nothing is latched, if a chord was written there (`cu.kheld`, `cu.mlatch`: the UI posts
    `LP_STEP KEYSUP`), and
  - **when a root is pressed after one was written there and let go**, whatever is still held or latched
    (`cr_loop_gesture`): the chord key kept down while the roots are tapped under it, or a chord latched by LOCK, gives a
    step a root. Roots held **together** share the step (a chord over a Split Bass bass note; a second root pressed
    before the first is let go).
  OCT+ = a rest (the cursor moves on), OCT- = back one step (nothing erased), in or out of the menu.
- The steps are kept after the loop's events until the commit: REC = they become the loop (the loop there before
  goes), stopped; LOOP = the same and it plays; nothing entered = the loop as it was. The chords sound as they are
  entered (the chord engine plays the keys as always).

**The two faults of the first build (0.18 dev of 2026-10-11), as the owner met them.** (1) "With quantize off it is
capped to 4 total steps": the step was the Quantize value, and with Quantize none a **quarter note**, so a bar was
four steps: a 1-bar Length wrapped on the fifth chord, an empty slot opened in the editor had four steps, and the grid
drew four cells a row. (With Length really on Free the engine did go on: six chords made six quarter-note steps.) (2)
"With quantize 16 it shows 16 steps but only records to the first step": the cursor moved on **only when every key was
up**; with the chord key held down between the roots, or a chord latched by LOCK, every chord was written on the same
step (six events on step 1, a loop one step long); in the editor the cursor does not move by itself at all. Neither
depended on Quantize beyond the step's size. `tools/emu/scripts/cr_loop_stepfaults.txt` plays both; its checks fail on
6619937.

## The PLAY menu (`cr_ui.c cu_loopmenu`, `CR_K_LOOPMENU`)

LOOP held past HOLD_MS opens it and locks it (OCT- / OCT+ / HOME / LOOP held again close it; a LOOP tap still plays /
stops; in step entry OCT+ is a rest and OCT- a step back). Mock-ups: `design/choralroot-fm1-looper-screens.png`
(generator `make_looper_mockups.py`; drawn before 0.18: its knob row is the one of 0.17).

| control | what |
| --- | --- |
| PRESETS | the **slot** (the strip: filled = holds a loop, the selected one red; **a 3 px square in the cell's top right corner = its loop has changes not saved yet**, `lm_dirty`): a switch through `cu_loop_load`, at once when stopped, playing once the knob rests 250 ms (`cu_lp.want`; the strip follows at once) and at the end of the cycle |
| ALGORITHM | the **record mode** (Overwrite · Advance · Overdub · Replace · Step, the picker band) |
| SELECT | the **page** (`cu.lpage`, the first whenever the menu opens; the owner 2026-10-11: "turn the select knob to change the page. It currently changes length") |
| page 1: KNOB 1 / 2 / 3 / 4 | **Length** (Free 1 2 4 8 16 bars; also big in the middle while idle; while the timeline shows, the message "next take: 4 bars") / **Rate** (x1/4 .. x4) / **Quantize** (echoes) / **Count-in** (gate): the cell hot 800 ms, no popup |
| page 2: KNOB 1 | **Level** (bar); KNOB 2 .. 4: nothing (empty cells) |
| GLO tap | **the keys**: play (default) = the roots and the chord block play as on the chord view, so the menu stays open while you play into a take; loops = the slot shortcuts (white roots D4..F5 slots 1..10, D#4 held 1 s clear, F#4 undo) and their LED map, GLO lit. No Options from this menu. The Keys cell of 0.17 (KNOB 4) is gone: "get rid of the Keys / play shortcut and map this to the GLO button like in other pages" |
| EDIT tap | **the loop editor** (below) |
| REC tap / held | by the record mode / undo, as outside the menu |

The bottom line (`foot`, as the KEY layer's label and footer): `1/2 · keys: play · GLO: loops` (the page, what the
keys do, what GLO makes them); in step entry and in the editor their own.

The middle: **idle / armed** the length big ("4 bars", "Free"; armed Free: "the first chord starts the take", after
Advance its message); **recording or playing** the timeline (`lm_*`: the bars with their beat ticks, more than 8: the
half the playhead is in; recording Free the bars as they pass, the current one at the right, at most 4, the elapsed
time under it; a lane of marks per layer, at most 4 (older layers fold into lane 0), the take's lane bright, marks of
the gestures still held where they began; Replace: the held span); **step entry** the step grid (sixteenths, a bar a
row, four rows: the whole pattern of 64 steps on one screen; a set Length: all its steps, Free: the bars reached so
far; the filled steps, the cursor blinking with the REC LED's phase, the cursor step's chord under it, "step 7" (Free),
"step 7/32" (a set Length) or "64 steps · full", the foot "OCT-: back · OCT+: rest · REC: done"). The marks are read from the ISR's loop
with the IRQ off only when its events (`crl.gen`) or the window change (`cu_lm_marks`). The top line: "Loop 3" and at
the right "Rec · Overwrite" (armed: the mode; also outside the menu), "Rec 2.4", "Dub 3.2", "Rep 3.2", "Rec · Step",
"3.2" playing, "empty" for an empty slot.

## Gestures and screens (`cr_ui.c`)

| gesture | stopped | playing |
| --- | --- | --- |
| LOOP tap | play (a take: commit; step entry: commit and play) | stop |
| LOOP held | the PLAY menu (above) | the same, the timeline in the middle, the corner dial in the top line |
| EDIT tap in the PLAY menu | the loop editor | the same (the loop plays on) |
| REC tap | by the record mode (above) | the same; ends a take / layer / step entry |
| REC held | undo: the layers left huge in red ("layers · undo"); none: "nothing to undo" | the same |
| OCT+ / OCT- in step entry | a rest / back one step | — |

The count-in takes the whole panel: the beats to go (4 3 2 1 from the time signature) huge in red, each springing
in, "count-in" under it, the top line `Rec`, the red ring drawing itself in over the bar. It is above popups and
layers, below PANIC.

Orchid's red ring runs round the edge only where the loop is the whole screen — the count-in and the undo screen
(calibration uses it too, as its progress). Every other loop state lives in a small **corner dial** in the top line
(`design/choralroot-fm1-loop-screens.png`, states 2-7 and 9a-9c; 2026-10-09: before, REC armed drew the ring round
every screen, layer, picker and popup until the take ended): a 16 px dial at the right end, the top line's right text
("Oct +1", "Arp", "ready", "2.3", "Dub 3.2") moved left of it. Its looks (`cr_draw.c cr_dial`, `cr_screen_t.dial_mode`):

- **playing**: the dotted track grey, the loop's progress red from 12 o'clock, its arc drawn thick for ~100 ms on each
  downbeat (not with Options > Motion Off);
- **REC armed** (no loop yet, Free: the grey track alone) and **overdub armed** (over the playing arc): the **REC dot**
  in the dial's middle, red, lit like the REC LED;
- **recording the first take**: the track red (no loop yet: the whole circle is being made), the arc the take's
  progress (`lring`: with a loop length set the whole take, N bars; Free the bar's position, sweeping once a bar), the
  REC dot blinking with the REC LED (its 250 ms phase);
- **overdubbing**: the playing dial with the REC dot blinking.

It shows on the views, the layers (KEY, PERF, FX, BASS, LOOP, METRO, the engine picker), the SAVE naming dialog
and the knob popups; not in the sound editor (no top line) nor on the Options pages, where the REC LED and the LOOP
button's green LED carry it. `tools/emu/scripts/cr_rec_dial.txt` shoots the REC looks (`cr_rec_armed.ppm`,
`cr_rec_rec.ppm`, `cr_rec_od.ppm`).

While the loop sounds, its notes glow **dim** on the root keys where they sit in the current octave window
(`cr_snap_t.lnote`, a 128-bit note mask from the 8 loop voices, taken with the snapshot); the player's own notes stay
lit, and a chord shown on screen that is the loop's lights nothing. With Options > LEDs = Glow every idle key glows
dim anyway, so the glow is visible with LEDs = Stock (idle keys lit, the loop's dim): `tools/emu/scripts/cr_loop_glow.txt`.

With Options > MIDI Clock = Out (the default) the ISR sends 0xFA (start) when the loop starts playing (LOOP from
stopped, a take committed into playback) and 0xFC (stop) when it stops, USB MIDI CIN 0x0F; the 0xF8 pulses run as
before, restarting their phase at the start.

With MIDI Clock = In the loop follows the other way (cr_out.c `cr_in_transport`): 0xFA starts it from its start
(restarts it if playing), 0xFB plays it if stopped, 0xFC stops it; a take in progress is left alone, an empty loop
ignores them, and nothing is sent out.

## The loop editor (0.18; `cr_loop.c cr_loop_ed_*`, `cr_ui.c cu_ledit_*`)

"In the play menu, lets make it so you can press Edit and edit the loop; once in the loop edit mode, you select a
step and can change: notes by playing new notes which overwrites the notes, step length (default 1, but you can make
it longer or shorter), loop length in number of steps, and we also need a way to clear a step of notes which could
be done by pressing REC (which when in the loop editor doesn't do the normal record)." And on the first build
(2026-10-12): "adjusting a step's length is changing the steps after it, which is weird. I would expect this system to
work more like an Elektron sequencer".

**A step sequencer's view of any loop, with nothing stored for it.** The grid is the sixteenth note of Step entry
(`CRL_STEP`, 24 ticks, whatever Quantize says), for a pattern entered by steps, a take played by hand, or a step loop
of the first build (whose steps were the Quantize value: quarter-note steps show as a chord every fourth step, four
steps long). **An event belongs to the step it starts in.** Nothing is moved when the editor opens: an event that
starts off the grid keeps its exact time until that step's notes are replaced; opened, looked at and closed, the loop
is byte for byte what it was. **A step's length is its NOTES' length** (`crl_ev_t.dur`), never its place in time:
changing it moves no other step and does not change the loop's length. A long note sounds on over the steps after it,
and those steps still play: the voices are the looper's eight (`CR_MAX_LOOPV`; when all are busy the one that would
end first is taken), each chord its own voice, so a held chord and the next step's chord sound together as two chords
played on the keys would. The steps are read from the events each time (`cr_loop_ed_map`: one pass, a page of 64
steps as two 64-bit maps: 24 bytes in the POOL, the editor's only buffer; 520 bytes before), and **the record's format
is unchanged**: `t` and `dur` are what the record always had; no new field, no flag, version 1, and 0.14 .. 0.17 read
an edited loop's record as any other (`tests/cr_loop_test.c s_edit`; `old_unpack_ok`).

| gesture | does |
| --- | --- |
| EDIT tap in the PLAY menu | opens it (`cr_loop_ed_open`), **at any Quantize setting** (the first build refused a loop with Quantize none: `set Quantize to edit`). Refused: a slot switch on its way (`a slot is on its way`); a capture open (`end the take first`). An empty slot: an empty pattern of the set Length (Free: a bar, 16 steps; more than 64 steps: its first whole bars within 64), which is the slot empty again if nothing is entered |
| SELECT, KNOB 1 | the step cursor, wrapping at the loop's end (`cu_ledit_move`) |
| the keys | they play, as on the view (auditioned by the chord engine). A gesture's end writes its chord INTO the selected step, replacing its notes: the first key of a new push (no gesture open) clears, the keys of one push add up (`crl_ed_write`, `ed_w`). **By side** (3y9's rule): a bass-side note (Split Bass) replaces the step's bass-side notes, anything else the chord side. The new notes start on the step's tick and are **as long as the step's notes were** (one step on an empty step) |
| | **The cursor stays on the step** (it does not advance as Step entry does; an Elektron's grid recording stays on the selected trig too): an editor's step is tried, heard and tried again. SELECT moves on |
| REC tap | clears the selected step (`cr_loop_ed_clear`): no recording in the editor |
| KNOB 2 | **Note len**: how long the selected step's notes sound, 1 .. 64 steps (`cr_loop_ed_setnlen`; shown: the longest of them, a part of a step counted whole, `cr_loop_ed_nlen`). Whole steps only: the event format could carry a shorter gate, but no control was asked for one. A note played off the grid keeps its start and ends on the grid. Nothing else moves. An empty step has none (`-`). Longer than the loop: it sounds across the loop's start |
| KNOB 3 | **Steps**: the loop's length, 1 .. 64 steps, a step a detent (`cr_loop_ed_len`): shorter hides the last step (its events stay in RAM beyond the end, unheard), longer brings it back, and past what there was adds an empty step; `64 steps at most`. **The hidden steps are deleted when the editor closes**; a save while it is open writes what sounds (`cr_loop_pack` skips events beyond the end). A loop played to a length off the grid: the first detent up makes its last step whole. A take longer than 64 steps can be made shorter, not longer |
| KNOB 4 | the step's velocity (the event format's `vel`; the highest of its events shown) |
| LOOP tap | play / stop from the loop's start, as ever; the cursor does not follow the playback, the playhead shows it. A length changed under the playhead: the playhead where it is in the new cycle (`crl_ed_renorm`) |
| REC held | the looper's undo, as outside (below) |
| PRESETS, ALGORITHM, GLO | nothing |
| OCT- / OCT+ / HOME / EDIT tapped / EDIT held again | close it, back to the PLAY menu (`cr_loop_ed_close`): nothing to confirm, an edit is applied when it is made. LOOP held again, another layer, a revert: the menu and the editor close |

**The screen** (`CR_K_LOOPMENU` with `lm_mid = CR_LM_STEPS`, the step grid of Step entry: `cr_draw.c cr_lm_steps`): a
bar a row, **four rows of 16: all 64 steps at a glance** (3/4 and 6/8: four rows of 12). A step with a note starting
in it is red; a step a longer note still sounds in (half of it or more) is dark red (`lm_lane[1]`); an empty one grey;
the grid ends where the loop ends (`lm_steps`). The cursor is the red outline. The steps the **selected** step's note
sounds on into carry a white line along their bottom (`lm_lane[2]`), so turning KNOB 2 is seen to stretch the note
over the following steps and to move nothing. While the loop plays, the step being played is white (grey when empty:
`CR_LM_PLAY` in `lm_pos`; only its rows' strips are redrawn). Under the grid the selected step's chord, big, and `step
5/16`; the cells Step / Note len / Steps / Velocity. **A take longer than 64 steps is paged**: the cursor walks all its
steps and the grid shows the 64 the cursor is in (`step 70/128 · p2`); one page was chosen over a scrolling row
because a pattern fits it whole, and a page turns only for loops no step sequencer made.

**Layers and undo.** The editor works on what sounds (the live events). Its **first edit makes the loop one layer**
(`crl_ed_flat`: the overdubs merged, the events a Replace layer had hidden removed for good), so after an edit REC
held finds `nothing to undo`. Writing "into the current layer like an overdub" was tried in the design and dropped: an
undo would then take the notes the editor wrote and the overdub they happened to share a layer with, bring back
nothing the editor deleted from the layers under it, and a step's length cannot be undone by layer at all. There is
**no editor-level undo**; the way back is the revert (GLO + SAVE: the loop as last saved). Opened and closed without
an edit, the loop and its layers are untouched.

**Saving**: edits are in RAM and set `crl.dirty`, like a take: the global save and leaving the slot write them.

**Cost**: 520 bytes of POOL (`cu_le_map`), about 16 bytes in `cr_loop_t`; no copy of the loop. The UI rebuilds its view only
when the events change (`crl.gen`) or the cursor moves, with the IRQ off for the two passes; the operations run with
the IRQ off from the UI thread, as `cu_rec`'s stop does.

## Slots and the record

Ten slots, and a slot keeps its loop: only the selected one lives in RAM. **Loops are saved by hand** (the owner,
2026-10-11: "Only save the loops after we hold save to manual save, thus if I have some loops recorded that I like
and accidentally record over them, I have a way to recover, and also it should make it so you can record / play a
loop with no glitches", and "I think we just keep one loop in memory; if you switch loop slots, then auto save the
loop if it's dirty / not saved"). The loop in RAM is written to its slot

- **(a) by the global save** (SAVE held: `cu_gsave_now` -> `cu_loop_write`, with the settings and the session; while
  it plays or a capture is open the save waits for the stop: "saves at stop"), and
- **(b) when its slot is left** with unsaved changes (`cu_loop_keep`, below),

and nowhere else: **not at a stop, not after a take, not when idle** (`cu_loop_autosave`, 2026-10-09 .. 2026-10-11, is
gone). A take, an overdub, a clear, an undo, an edit change the loop in RAM (`crl.dirty`); a power cycle or a revert
(GLO + SAVE: `cu_revert` reloads the saved slot from flash) brings the slot's saved loop back. The PLAY menu marks the
slot whose loop is not saved. **No flash write happens while recording or playing**, with the one exception that
existed before and is kept as it was: a second slot switch while the loop still plays (below), a one-off hiccup.

The PLAY menu's PRESETS (or, with its keys on loops, a white root D4..F5) selects a slot (load when stopped, at
the end of the cycle when playing).
If the loop in RAM changed since its last save or load (a take, an overdub, an undo, a clear, an edit: `crl.dirty`;
an overdub in progress ends at the switch, its layer kept), it goes to its slot before the other slot is loaded:

- stopped: written at once, the message folds both: "loop 2 · saved 1" (green);
- playing: no flash erase while a loop plays (Felucca's rule), so it is packed into a buffer of its own (`cr_ui.c
  cu_loop_pend`) as a pending save for its slot, the switch happens at the end of the cycle as before ("loop 2: next
  cycle"), and the pending save is written when the loop next stops ("saved to slot 1"), before any other write to
  the loop slots, and before a second switch while it still plays: then at once, a one-off audio hiccup rather than
  a lost take (traced "loop: pending save flushed while playing"); the slot about to be loaded may be that one;
- a cleared loop deletes its slot's record (the slot comes back empty: "loop 2 · cleared 1");
- a flash error on that save refuses the switch ("save error"): the loop stays in RAM, its slot selected.

Before the end of the cycle, a stop, a clear, a panic or an edit of the loop still playing (an overdub armed, an
undo) cancels a queued switch: the slot it was leaving stays selected, its loop still in RAM
(`cr_loop_t.loads` tells a switch taken from one cancelled), and the copy staged for the switch is still written at
the stop. LOOP held + the root of the slot it is on (keys on loops) keeps its
loop as it is. No save in SAFE MODE, after a backup restore (the flash is newer than the RAM) or without the flash:
the loop left is dropped.

The SAVE-held map of 0.17 (Save / Load / Delete on the root-chosen slot) is removed with its layer, LEDs and screen:
a copy into another slot is no longer offered; a slot is emptied by clearing its loop and saving (or leaving it); the
slot in RAM is reverted by GLO + SAVE. The selected slot is a manual setting (record v2) and comes back at power-on (a
load only). Tests: `tools/emu/test_persist.sh` (`loop_persist_stop`: saved, recorded over, not saved, a power cycle:
the old loop; `_save`: SAVE held: the new one; `_switch`: the slot left and come back to: kept; `_revert`: GLO + SAVE
after recording over a saved loop: the saved one; `cr_loop_slots.txt`: the stopped overdub is NOT written at its stop
and is when its slot is left), `test_cr.sh` (`cr_save_hold.txt`: SAVE held saves, no slot map; `cr_loop_sync.txt`:
SAVE held while playing waits for the stop).

Flash: storage.c's commit record and A/B copies (its hooks, header and CRC; `storage.c` unedited), on user
sample slot 3's 80 KiB (0xC8000..0xDBFFF; ChoralRoot has no SAMPLE engine (`FELUCCA_SAMPLE 0`) and its backup no
sample objects, so nothing else writes it; a Felucca build's `eng_sample.c` ignores non-sample data): slot k copy c at `0xC8000 + (2k + c) * 4 KiB`, header type `0x4C30 + k`. Payload (little-endian):

```
0  u32 "CRL1"   4 u8 version 1   5 u8 sig   6 u8 nlayers   7 u8 flags (bit 0 CRL_RF_PART; 0 until 0.17)
8  u32 len (ticks)   12 u16 nev   14 u16 ppqn (96)
16 nlayers x u16: events per layer, then nev x 7 bytes: t u16, dur u16, root, vel, qx
```

Unpack checks everything (magic, version, ppqn, counts, each field's range) and refuses a bad record as empty: a
newer version or garbage never loads half a loop.

**Byte 7, the flags (0.18).** 0.14 to 0.17 wrote 0 there and never read it. Bit 0, `CRL_RF_PART`: a single note's
`qx` low nibble is its part (`CRL_X_BASS`). `cr_loop_pack` sets it only when the loop holds a bass-side note, so a
loop without one is written byte for byte as 0.17 wrote it. The version stays 1 and the record's size is unchanged.

- **An older record in 0.18** (flags 0): loads and plays as before. A single note's nibble there is not a part (it
  held the extension keys that were down, which never sounded): unpack clears it, so no old loop grows a bass note.
- **A 0.18 record in 0.14 to 0.17**: accepted whole (its checks do not cover byte 7 or a single note's nibble;
  `tests/cr_loop_test.c old_unpack_ok` is those checks). The bass-side notes play as single notes of the chord part,
  which is what 0.17 itself recorded. If that firmware then writes the loop again (a stop after a change, a switch),
  it writes flags 0 and the bass side is lost for good; a loop only played or left alone keeps its record.
- **Backup, restore, the device manager, `fm1_install.py`**: a slot is an opaque object (40..49); the firmware
  validates a restored one with `cr_loop_unpack`, the manager's header line (`loopHeader`: bars, time signature,
  layers, events) reads neither byte 7 nor the events. A backup taken on 0.18 restores on 0.17 and the other way.

## Caps

512 events per loop (all layers, a Replace layer's hidden events and its cut copies included; 4 KiB in RAM + a 4 KiB staging copy for slot switches; 3.6 KiB record, and
3664 bytes in the POOL for the loop being left, packed; 24 bytes there for the editor's map: a page of 64 steps),
32 layers, 16 open gestures, 8 loop voices, 65535 ticks; a step-entered pattern 64 steps. Reaching the event cap ends the take ("loop full").
