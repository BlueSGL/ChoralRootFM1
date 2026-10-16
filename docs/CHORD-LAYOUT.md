# Chord Layout: Classic and FM-1 (the design)

Status: **design, decided by the owner on 2026-10-16** (HISTORY 3y155). No firmware implements it yet. The pictures
are in [design/chord-layout-mockups.html](../design/chord-layout-mockups.html) (rendered:
[design/chord-layout-mockups.png](../design/chord-layout-mockups.png)).

Today's chord keys follow the Orchid ([MANUAL.md "3. The panel"](MANUAL.md#the-keybed) and
["4. Chords"](MANUAL.md#4-chords)). The owner wants a second layout that fits the FM-1's keybed and their hand: the
three small black keys of the left end are the chord types, SUS is two of them pressed with one finger, the four
white keys are the extensions, LOCK moves to a button, and two more keys become roots. **Options > Chord Layout**
chooses between the two: **Classic** (today's, the default) and **FM-1**.

Contents: [The decisions](#the-owners-decisions-2026-10-16) · [The two layouts](#the-two-layouts) ·
[The Options](#the-options-and-where-they-are-kept) · [The combo table](#the-combo-table) ·
[The combine window](#the-combine-window) · [LOCK and LATCH](#lock-and-latch-in-fm-1) ·
[Picking keys](#picking-keys-in-layers) · [MIDI in](#midi-in) · [The Full keyboard](#the-full-keyboard-both-layouts) ·
[Unchanged](#unchanged) · [Every place that reads the keys](#every-place-that-reads-the-chord-blocks-keys) ·
[Cost](#cost) · [Tests](#the-tests-the-build-will-need) · [Open questions](#open-questions)

## The owner's decisions (2026-10-16)

Recorded as decided; the sections below work them out.

1. **Options > Chord Layout: Classic / FM-1.** Classic is today's layout and the default. It lives in the settings
   block (below: `sticky`'s spare bits); an older firmware ignores it and starts in Classic; it is hidden on the
   SMK-37 build.
2. **The FM-1 layout.** F#3 G#3 A#3 = DIM MIN MAJ. F3 G3 A3 B3 = 6 m7 M7 9. C4 .. G5 are roots: 20 instead of 18
   (C4 and C#4 become roots). **SUS = G# + A# held together, always, whatever Secret Chords says** (the owner presses
   both small black keys with one finger).
3. **Secret chords in FM-1**, only with Secret Chords on; each keeps the key it has in Classic: F# + G# + A# = the
   power chord (DIM plus the SUS pair, as DIM + SUS); F# + G# = m add4 (keeps MIN, as MIN + SUS); F# + A# = augmented
   (keeps MAJ, as MAJ + SUS). With Secret Chords off only G# + A# combine; the other pairs keep "last pressed wins".
4. **The combine window** ("maybe even 50ms or more"): a chord starts at once with what is held. If G#'s or A#'s
   partner lands within the window, the sounding chord becomes SUS in place, only its third moving to the fourth,
   with no restrike. A G# or A# held from earlier never waits. The window is an Option, 30 .. 80 ms, default 50.
5. **LOCK in FM-1**: LFO tap = LOCK on / off; LFO held = the LOCK layer; **GLO + LFO = LATCH**. A latched SUS shows
   as two lit keys, and pressing that same pair again on its own switches it off (as one type key does today). The
   LFO light shows LOCK steadily; while LATCH is on it pulses. The screen's `Latch` stays. LOCK cannot be switched
   inside the sound editor, where LFO is the editor's LFO button; that is fine.
6. **Picking keys in layers** (loop slots, naming letters, the KEY layer's tonic, the pickers) stay on D4 .. G5 in both
   layouts. The default; the owner may revisit it ([Open questions](#open-questions) 1).
7. **MIDI in that plays the FM-1's keys follows the layout** ([MIDI in](#midi-in): what the code does today).
8. **The Full keyboard (both layouts)**: KNOB 1 is the **voicing**, not the octave (chords can be played there now);
   the octave stays on OCT- / OCT+. A new Option, **Full keyboard shortcuts On / Off**, default Off. When on: **F#5**
   brings the active layout's chord keys back on the low keys; **G5** switches between single notes and a chord on
   every key. Both **toggle on a quick press and switch only while held on a long press** (the platform's hold time).
   F#5 and G5 stay the shortcut keys while the chord keys are shown, so the roots end at F5 there. The Full keyboard
   keeps F3 .. F5 as notes (25). G5's chord is the latched chord if there is one, otherwise Key Mode's chord for that
   key. LOCK works on the Full keyboard in FM-1 (it is on LFO); in Classic it is still B3, a note there, so latching
   happens with F#5's chord keys showing.
9. **Unchanged**: loops (they store chords, not keys), the looper, chord names, the voicing rules, Key Mode's scale
   picks (MAJ and MIN in the KEY layer are still A#3 and G#3).

## The two layouts

The firmware numbers the keys 0 (F3) .. 26 (G5); a root key k plays MIDI note 53 + k + 12 x octave
(`cr_ui.c cu_key_press`), in both layouts.

```
CLASSIC (today, the default)
black      DIM   MIN   MAJ         SUS
           F#3   G#3   A#3         C#4     D#4         F#4   G#4   A#4         C#5   D#5         F#5
white    6     m7    M7    LOCK  9       D4    E4    F4    G4    A4    B4    C5    D5    E5    F5    G5
         F3    G3    A3    B3    C4      |----------------- roots: D4 .. G5 (18) -----------------|
buttons  LFO = LATCH (tap)                GLO + LFO: nothing

FM-1
black      DIM   MIN   MAJ         C#4   D#4         F#4   G#4   A#4         C#5   D#5         F#5
           F#3   G#3   A#3
                 \__ SUS __/   (G# + A# together, one finger)
white    6     m7    M7    9     C4    D4    E4    F4    G4    A4    B4    C5    D5    E5    F5    G5
         F3    G3    A3    B3    |--------------------- roots: C4 .. G5 (20) ---------------------|
buttons  LFO = LOCK (tap on / off; held: the LOCK layer)    GLO + LFO = LATCH
```

| Key (firmware k) | Classic | FM-1 |
| --- | --- | --- |
| F3 (0) | 6 | 6 |
| F#3 (1) | DIM | DIM |
| G3 (2) | m7 | m7 |
| G#3 (3) | MIN | MIN; with A#3: SUS |
| A3 (4) | M7 | M7 |
| A#3 (5) | MAJ | MAJ; with G#3: SUS |
| B3 (6) | LOCK (tap; held: the LOCK layer) | **9** |
| C4 (7) | 9 | **root** (MIDI 60 at octave 0) |
| C#4 (8) | SUS | **root** (61) |
| D4 .. G5 (9 .. 26) | roots | roots |
| LFO button | LATCH | **LOCK** (tap); the LOCK layer (held) |
| GLO + LFO | nothing | **LATCH** |

The extensions keep their order (6 m7 M7 9 on the four white keys from F3) and the types theirs (DIM MIN MAJ from
F#3); what moves is SUS (from its own key to the pair), 9 (C4 to B3) and LOCK (B3 to LFO).

## The Options, and where they are kept

Three new entries. On the SMK-37 build all three are hidden (`BOARD_SMK37`: its 37 keys are all roots, the chord
block is on its pads, `CU_FN_KEYS` 0; the bits are kept as they are and read as the defaults there).

| Entry (screen name) | Values | Default | Place in the list | What it does |
| --- | --- | --- | --- | --- |
| **Chord Layout** | Classic, FM-1 | Classic | after Secret Chords | which keys are the chord keys (above) |
| **Combine Window** | 30 .. 80 ms, 5 ms a detent | 50 ms | after Chord Layout | [the combine window](#the-combine-window). Drawn grey with `FM-1 layout` under it while the layout is Classic (as the KEY layer draws Snap grey where it does not apply); it can still be set |
| **Full Shortcuts** (the manual: "Full keyboard shortcuts") | Off, On | Off | after Split Point | [F#5 and G5 on the Full keyboard](#the-full-keyboard-both-layouts) |

All three are **automatic** settings ([SETTINGS.md "Manual and automatic"](SETTINGS.md#manual-and-automatic-018)),
like every Option: written by `cr_settings_poll` when the unit is quiet, reset by Options > Reset Settings (Classic,
50 ms, Off).

**Changing the layout.** The new map takes over at once. Every chord-block key held at that moment is released under
the old map first (its `CRE_MOD` up, a held root's note off, as the KEY layer's Keyboard switch does); what LOCK has
latched is kept, because the latch is a set of `cr_mod_t` (DIM .. 9), not of keys: a latched SUS + 9 shows on C#4 and
C4 in Classic and on G#3 + A#3 and B3 in FM-1. LOCK itself stays as it was (on or off).

### The settings block: `sticky`

`cr_settings_t` keeps its 192 bytes and its version (15). The three settings take six spare bits of `sticky`, the
LATCH byte (its bit 0, manual, `CRS_FIX(sticky, 0, 1)` today):

| bits of `sticky` | field | values | 0 means |
| --- | --- | --- | --- |
| 0 | LATCH on (as today) | 0 / 1 | off |
| 1 | Chord Layout (`CRS_LAYOUT_FM1`) | 0 Classic, 1 FM-1 | Classic |
| 2 | Full Shortcuts (`CRS_FULL_SHORT`) | 0 Off, 1 On | Off |
| 3 .. 6 | Combine Window (`CRS_COMBINE`) | (ms - 50) / 5, 4-bit two's complement, -4 .. +6 | 50 ms |
| 7 | free | | |

- **Zero is every default**, so there is no migration and no version bump: every older record reads Classic, 50 ms,
  Off. This is the spare-bit case of SETTINGS.md "How to add a field" (steps 3, 5, 6).
- `crs_sanitize`: the byte's range becomes 0 .. 0x7F, and a window code of 7 or -5 .. -8 takes 0 (50 ms) alone, the
  other bits kept.
- `CRS_STICKY_AUTO` (0x7E): bits 1 .. 6 are automatic, bit 0 stays manual (the `CRS_PICK_AUTO` pattern:
  `cr_settings.c` line 172's merge).
- **The engine reads this byte**: `cr_settings_apply` (`cr_set_sticky(c, s->sticky)`, line 525) must pass
  `s->sticky & 1`, and `cr_settings_capture` (line 560) and the glue's `crs_capture` (line 764) must merge their bit 0
  into the byte instead of writing it whole, or the new bits would be lost or switch LATCH on.
- **A downgrade.** Every older firmware checks the byte with `CRS_FIX(sticky, 0, 1)`: with any new bit set it takes
  the byte's default, LATCH off, and touches nothing else. So an older firmware ignores the layout and starts in
  Classic; the one thing it loses is a LATCH left on at the last save (it is off then). With the three settings at
  their defaults the byte is one it knows. That firmware's next save writes the byte without the bits; back on the
  newer firmware the unit is Classic, 50 ms, Off.
- Why this byte: `pick_roots` and `bass_on` are full (Velocity Curve, Key Touch), and an older firmware that met a
  bit in `key_on` would reset Key Mode, in `scale` the scale, in `fx_on` the effects. `sticky` costs only LATCH.
- **The session** (`cr_session_t`) needs nothing new: LOCK's latch is `latched` (bits `cr_mod_t`) as today, and the
  FM-1's lit keys are derived from it (the map is one to one: SUS = G# + A#; MIN + SUS = F# + G#; MAJ + SUS = F# + A#;
  DIM + SUS = all three; any other set as Classic's keys). Backups carry the block as ever (the same 192 bytes).

## The combo table

The type keys of FM-1 are F#3, G#3, A#3. "Secret applies" is the engine's rule today
(`cr_engine.c crx_secret_quality`): Secret Chords **All**, or **Simple** while Play Style is Simple. The extensions
(F3 G3 A3 B3) stack on any row as ever.

| Type keys held (FM-1) | Secret does not apply | Secret applies | Classic's keys for the same chord | On the screen (root D) |
| --- | --- | --- | --- | --- |
| none | a single note (Key Mode, Key plays Chords: the scale's chord) | the same | none | `D` |
| F# | DIM | DIM | F#3 | `Ddim` |
| G# | MIN | MIN | G#3 | `Dm` |
| A# | MAJ | MAJ | A#3 | `DM` |
| **G# + A#** | **SUS** | **SUS** | C#4 | `Dsus` |
| F# + G# | DIM or MIN, the last pressed | **m add4** (MIN + SUS) | G#3 + C#4 | `Dm` `add4` |
| F# + A# | DIM or MAJ, the last pressed | **augmented** (MAJ + SUS) | A#3 + C#4 | `D+` |
| F# + G# + A# | DIM or SUS, the last pressed (the pair counts as pressed when it became a pair) | **power chord** (DIM + SUS) | F#3 + C#4 | `D` `5` |

So with Secret Chords off, F# + G# + A# is either DIM (F# pressed after the pair) or SUS (the pair completed after
F#), and F# + G# or F# + A# is whichever was pressed last, exactly as two Classic type keys are. Classic's own table
is unchanged (MANUAL "Secret chords"); its Dim + Min, Dim + Maj, Min + Maj stay "last pressed" (unmapped), and FM-1
has no way to hold MIN and MAJ apart from SUS, which is the point of the layout.

**How the UI tells the engine.** The engine keeps its model (chord keys = `cr_mod_t`, `cr_mod_ex` with a stack of
held types, `crx_secret_quality` unchanged). A small resolver in `cr_ui.c` turns the FM-1's held type keys into the
`cr_mod_t` set the engine would see from Classic's keys for the chord in the table, and posts the difference (ups
first, quietly, then the downs):

- the pair completes: MIN or MAJ up, SUS down (in place: [the window](#the-combine-window));
- with secret applying, F# with the pair or one of it: DIM / MIN / MAJ and SUS as the table's Classic column, so the
  engine's secret rule names the chord; without, each key posts its own type in press order and the pair posts SUS;
- **a key of the pair let go while the other stays**: SUS up and the remaining type down **without a press** (a new
  flag on `CRE_MOD`: held, not pressed: `cr_mod_ex` adds it to `mods_down` and the stack but does not run
  `crx_modifier_press`). The chord that sounds is left alone, as letting go of a type key leaves it today in every
  style; the next root plays the remaining type. Without the flag, Advanced would re-strike the held root as MIN.

The chord-key lights in FM-1 come from the UI's own key state (held or latched type keys, and the extension mods
through the key table), not from the engine's `mods_active`, which would light A#3 for m add4's SUS.

## The combine window

One finger on G# and A# never lands on both in the same scan. Waiting for the second key before playing would delay
every MIN and MAJ chord by the window. The owner's rule instead: **play at once, then fix in place.**

**The gesture.** A type key pressed while no type key is held opens a *gesture* at that key's press time (the scan
stamp `fm1_in.note_t0[k]`, `CU_KEY_T0`: not the UI frame, so the window does not move with the frame rate). It stays
open for the window (Options > Combine Window, 30 .. 80 ms, default 50). A type key pressed while the gesture is open
joins it; if that completes the pair (or, with secret applying, one of the secret combos above), the chords the
gesture's first key started or re-typed are **amended in place**:

- Only the notes that differ move: MIN or MAJ to SUS moves the third to the fourth and nothing else (the fourth lies
  between the third and the fifth, so at any KNOB 1 voicing, -12 .. +12, it takes the third's place in the voiced
  order: exactly one note off and one note on). With secret applying: MIN to m add4 moves the fifth to the fourth,
  MAJ to augmented the fifth to the sharp fifth, DIM to the power chord its third and flat fifth to the fifth and
  octave, SUS to the power chord its fourth to the octave.
- **No restrike, in every play style and under both Extension Addition settings.** Play Chord re-strikes on an
  *extension* change; this is not one: the pair is one gesture, and a second strike 20 ms after the first is a flam.
  The amend is `crx_transform_voice` with no reattack and Play Chord ignored (a new flag), the path Add Note already
  takes (`crx_sync_owner`: note-offs and note-ons for the difference only).
- Everything downstream sees the amend as Add Note's diff does: the voices, MIDI out (the third's note-off, the
  fourth's note-on), the RAW stream, a running performance mode (the arp's and pattern's running phase substitutes the
  note, `crx_build_clocked_seq`; a strum not yet past the third plays the fourth), the bass (its root does not move),
  the chord name (it changes to `Dsus` without the new-chord motion) and the keys' lights.
- **The looper** records the chord as SUS from its start: the engine reports the gesture again with the same serial
  (`cr_out_t.gesture`), and `cr_loop.c` takes a report with a serial it already holds as an amendment of that event,
  not as a new one. A loop therefore never plays the MIN-then-SUS flicker.
- When the window closes the gesture closes. **A G# or A# held from earlier never waits**: its gesture closed long
  ago, so a root pressed then plays MIN or MAJ at once, and a partner pressed later is an ordinary type press (Simple:
  it changes nothing sounding, it is SUS for the next root; Advanced and Free: a type change to SUS, re-struck as any
  type press is). Nothing ever waits for a partner.

**The root and the pair together** (R = the root, a and b the pair's keys, all within a few ms):

| Order | Simple | Advanced, Free |
| --- | --- | --- |
| a, b, R | SUS (the pair was complete) | SUS |
| a, R, b (b within the window of a) | MIN or MAJ at R, SUS in place at b | the same |
| a, R, b (b after the window) | MIN or MAJ, and it stays (Simple: fixed at R) | MIN or MAJ at R, then SUS re-struck at b (a type change) |
| R, a, b | a single note, fixed at R (Key Mode, Key plays Chords: the scale's chord), as in Classic today when a type key lands after the root | a re-strikes R as MIN or MAJ (a type press), b within the window: SUS in place; one strike in all |

The R, a, b row is today's Classic behaviour in Simple (no type grace after a root); see
[Open questions](#open-questions) 6.

**Advanced and Free type changes.** A type press with a root held re-strikes it as that type (today's rule); the pair
pressed together over a held root gives that one re-strike (by its first key) and then the in-place SUS. Free with no
root held: the first key sounds the last root again as MIN or MAJ, the partner within the window makes it SUS in
place, and the chord stops when the last chord key is let go, as today. Extension keys pressed during the window are
ordinary extension presses (Add Note adds, Play Chord re-strikes), before or after the amend.

**With LOCK on** (FM-1). The type keys latch as gestures:

- The pair pressed and let go latches SUS (G#3 and A#3 lit). Keys held together latch together, as today.
- A type key pressed with nothing held replaces the latch at its press (today's rule, Clears / Keeps as the LOCK
  layer says). The latch as it was before the gesture is kept for the window, so the gesture is judged whole: G#
  pressed over a latched SUS first makes it MIN, and A# landing within the window turns the gesture into "the same
  pair again": the latch before it comes back (SUS, its extensions), and **the pair goes off when let go** (the
  owner's rule: "pressing that same pair again on its own switches it off, as one type key does today"; its
  extensions stay lit and wait for a type, as today). The secret combos latch and go off the same way (F# + G# again
  alone: m add4 off).
- In Simple nothing sounds from these changes (the latch is for the next root). In Advanced and Free, roots held
  follow the latch at once (`crx_lock_sync`): the pair pressed over held roots re-strikes them once and amends in
  place; switching a latched SUS off with roots held can let a MIN sound for the window before the roots fall back to
  single notes at the release ([Open questions](#open-questions) 8).
- A single type key over a latched pair replaces it as any type key does (F# alone over SUS is DIM, not the power
  chord: the power chord is the three held together or latched together).

**Key Mode.** A held type overrides the scale's choice as today, so the pair gives SUS on any root and a lone G#
gives MIN; the window works the same over the scale's chords (a root pressed just before a lone G# in Simple keeps the
scale's chord). Key plays Notes and Snap are untouched. In the KEY layer with its keys on pick, G#3 and A#3 pick the
scale at the press, as today ([Open questions](#open-questions) 7 for the one-finger pair there); with its keys on
play they are the FM-1 chord keys.

**Classic** has no window: none of this applies to it (two Classic type keys still combine only as secret chords,
by the engine, with no in-place amend).

## LOCK and LATCH in FM-1

| Control | Classic (today) | FM-1 |
| --- | --- | --- |
| LOCK on / off | B3 tap | **LFO tap** |
| the LOCK layer | B3 held past Hold Time | **LFO held past Hold Time**, as a layer button: it stays open, **LFO blinks**, OCT- / OCT+ / HOME / LFO held again close it, holding another layer button switches to that layer |
| LOCK + a chord in one motion | B3 down, then a chord key: LOCK's tap at that moment (`cu.b3_pend`) | the same rule on LFO: a key pressed while LFO is down (before the hold time) is LOCK's tap at that moment, no layer; a knob or a button meanwhile opens the layer (a combo, as every layer button) |
| LATCH on / off | LFO tap | **GLO held, then LFO** (as GLO + SEQ is the metronome and GLO + REC the record mode): LFO is swallowed, GLO's release opens no Options |
| the LOCK layer's footer | `B: on/off · OCT-: back · HOME: home` | `LFO: on/off · OCT-: back · HOME: home` |
| the screen | `lock` and `Latch` at the top right, as today | the same words; `lock` also shows on the Full keyboard now (LOCK works there) |
| in the sound editor | B3 works | LFO is the editor's LFO group button and GLO its SHIFT: LOCK and LATCH cannot be switched there (decided: fine). The keys still play the latch |

**The LFO light** (the button's LED; its levels are those of the key LEDs: lit, middle, glow, faint):

| State | LFO light |
| --- | --- |
| LOCK off, LATCH off | the idle button glow, as every button |
| LOCK on | **lit, steady** |
| LATCH on (or a performance mode's Hold: the engine's `latching`, as today) | **pulses**: lit, middle, glow, middle, 300 ms each (1.2 s round), so it never reads as the layer's 250 ms blink |
| LOCK and LATCH on | pulses between lit and middle (never down to the glow), so LOCK still reads as on |
| the LOCK layer open | blinks (250 ms, as every open layer's button) |

**The chord keys' lights in FM-1** (`cr_leds`, Glow): a type or extension key held or latched is lit, an idle one
faint, as today; a latched SUS lights **G#3 and A#3**, m add4 F#3 + G#3, augmented F#3 + A#3, the power chord all
three. B3 (9) is an extension key and never blinks; there is no LOCK key light (LOCK is on the LFO light). C4 and
C#4 are root keys: dark unless they sound, the key's glow with Key Mode on, as every root. Stock (two levels) keeps
its rule: idle lit, active dark. The KEY layer on pick: G#3 and A#3 glow, the scale's lit, the five others dark.

## Picking keys in layers

**Default, both layouts: the keys that pick stay on D4 .. G5** (decision 6). What that means: the root keys have a
second job in some layers, and in FM-1 two more keys (C4, C#4) are roots on the view. Those two take **no** picking
job; every map keeps its keys where it is today, so the muscle memory and the manual's pictures stay true:

| Layer or page | The pick map (both layouts) | C4, C#4 in FM-1 |
| --- | --- | --- |
| the PLAY menu with Keys = Loops | the white roots D4 .. C5 = slots 1 .. 10, D#4 CLEAR, F#4 UNDO (`cu_white_idx` from D4) | dark, do nothing |
| naming a sound | the white roots type, D#4 a space, F#4 deletes | dark, do nothing |
| the KEY layer, keys on pick | any root D4 .. G5 sets the tonic (all twelve names are there) | dark, do nothing |
| the view menu, the pickers that list on white roots (`cu_layer_keys`, `cu_layer_sel`) | the white roots from D4 | dark, do nothing |
| the layers where the keys play (PERF, FX, LOCK, the engine picker's preview, Options, the loop editor, the PLAY menu with Keys = Play, the KEY layer on play) | they play | they play, as roots |

In code: `CU_ROOT0` (9) splits into `cu_root0()` (the first key that plays: 9 Classic, 7 FM-1) and `CU_PICK0` (9 in
both: `cu_white_idx`, `cu_white_key`, the naming keys 10 and 13, the KEY layer's tonic marks). Revisiting it later
would move only `CU_PICK0`'s users: [Open questions](#open-questions) 1.

## MIDI in

**What the code does today.** No MIDI message plays the FM-1's keys. MIDI in (USB and TRS) is drained in the audio
ISR by `cr_out.c cr_midi_in`: `cr_midi.c crm_map` sends a note on the Perform (chord) or Bass channel to
`midi_event` → `trk_note_on` / `trk_note_off` on that part: **the sound plays the note directly, no chord is built**
(MANUAL "MIDI in": "as an ordinary synth"). CC 7 / 91 / 93 / 94 and program changes go to the UI through `cr_min_q`
as knob turns. Nothing reaches `cr_ui.c cu_key_press`, the chord block, `CRE_KEY` or `CRE_MOD`. The emulators'
MIDI inputs (CoreMIDI in `tools/emu/emu.c`, Web MIDI in `tools/emu/web/emu.js midiIn` and the SMK page) feed that
same firmware queue (`emu_fw_midi_in`), so the same holds there. The only things that press firmware keys from
outside are the emulators' computer-keyboard maps and scripts (`KM_KEY`, firmware key index = MIDI note - 53), and
those send key indices, which the firmware reads through the active layout by itself.

**The change.** Nothing to change for the layout today: MIDI notes do not play keys. The rule for whatever does
(decision 7): a path that plays the FM-1's keys from MIDI must enter at `cu_key_press` / `cu_key_release` with the
key index (note - 53 - 12 x octave), never post `CRE_MOD` or `CRE_KEY` itself, so that the layout, the window, LOCK,
the Full keyboard's shortcuts and the picking maps all follow. Whether such a mode is wanted (a channel whose notes
53 .. 79 press the keys, for driving the chord block from another keyboard) is
[Open questions](#open-questions) 13.

## The Full keyboard (both layouts)

Today (KEY layer, page 2, Keyboard: Full): all 27 keys F3 .. G5 are notes, no chord keys, no LOCK (B3 is a note),
KNOB 1 is the octave, and with Key Mode on and Key plays Chords every key plays its scale chord (0.22).

**KNOB 1 is the voicing on the Full keyboard**, in both layouts, with or without the shortcuts: chords are played
there (Key plays Chords; G5 below), and the octave is on OCT- / OCT+ already. The read-out says `voicing`; a single
note ignores the voicing, as on the Chords keyboard. (`cr_ui.c` line 4737's Full-keyboard branch goes.)

**Options > Full Shortcuts** (Off by default). Off: the Full keyboard is as today, but for KNOB 1. On:

| Key | Job |
| --- | --- |
| F3 .. F5 (25 keys) | notes, as the Full keyboard's |
| **F#5** | **chord keys**: the active layout's chord keys on the low keys, or back to notes |
| **G5** | **chords**: every key plays a chord, or single notes |

- **Quick press = toggle, long press = momentary.** The state flips at the press (no wait). Let go before the hold
  time (Options > Hold Time, 300 .. 600 ms, `HOLD_MS`, the layer buttons' and B3's): it stays flipped. Held past it:
  it flips back at the release. Keys played meanwhile play under the flipped state, and each key's release ends what
  its press started (as Snap's rule).
- **Chord keys showing (F#5 lit).** The low keys are the layout's chord block, and the rest are the Chords keyboard's
  roots **up to F5**: Classic F3 .. C#4 the nine chord keys with B3 = LOCK, D4 .. F5 roots (16); FM-1 F3 .. B3 the
  seven chord keys, C4 .. F5 roots (18). F#5 and G5 stay the shortcuts. Everything follows the Chords keyboard while
  they show: the engine is told `CRE_KEYBOARD` 0 (chords from roots, a lone root's Key Mode chord, Single Notes' split
  by the Split Point), the latch and the play styles, the window in FM-1, the chord-key lights. Hiding them again:
  the chord keys held are released, as a layout change does; the latch stays.
- **Chords on every key (G5 lit).** Every note key plays a chord: **the latched chord** (LOCK on with a latch: its
  type and extensions, the lit keys) **if there is one, otherwise Key Mode's chord for that key** (as Key plays Chords
  plays it). G5's state starts, each time the Full keyboard is entered, from today's behaviour: on when Key Mode is
  on with Key plays Chords, off otherwise; G5 then switches it. With neither a latch nor Key Mode, G5 lights but the
  keys stay single notes and the message `no chord: LOCK or Key Mode` shows ([Open questions](#open-questions) 9).
  With the chord keys showing the roots already follow the Chords keyboard's rules (a lone root plays the latch or
  the scale's chord), so G5's state only matters for the note keys. The engine: a Full-keyboard voice (`zone` set)
  takes the latched spec (`mods_down`'s type and extensions) instead of none while G5 is lit (a new flag,
  `cr_set_full_chords`); today the engine ignores the latch on the Full keyboard.
- **LOCK on the Full keyboard.** FM-1: LFO works everywhere, so LOCK is on / off and its layer opens on the Full
  keyboard too, and the latch is set by showing the chord keys (F#5), holding a chord and letting go. Classic: B3 is
  a note while the chord keys are hidden, and LOCK (tap, hold) while they show, so latching happens with F#5's chord
  keys showing. The top right shows `lock` on the Full keyboard now whenever LOCK is on.
- **The lights.** F#5 and G5 are function keys: faint idle, lit while on (Stock: the reverse). The note keys as the
  Full keyboard's today; with the chord keys showing, the chord block and the roots as the Chords keyboard's.
- **The split** (Single: Split Oct, Split Bass) stays at the second F on the note keys (F3 .. E4 left, F4 .. F5 right;
  F#5, G5 are no notes); with the chord keys showing it is the Chords keyboard's Split Point.
- The KEY layer with its keys on pick still owns the keys (a root sets the tonic, MAJ / MIN the scale); F#5 and G5 do
  nothing there.

## Unchanged

- **Loops** store chords (`cr_mod_t` type and extensions with the root), not keys: a loop recorded in one layout
  plays the same in the other. The looper, its modes, its editor, its step entry ("every key up after a chord": the
  FM-1's pair counts as keys up when both are) are unchanged; the one addition is the gesture amendment above.
- **Chord names** (Orchid and Standard), CHORD-NAMES.md; the secret chords keep their names (`D5`, `D+`, `Dm add4`).
- **The voicing rules**, KNOB 1, 6th with 7th, the octave.
- **Key Mode's scale picks**: MAJ and MIN in the KEY layer are A#3 and G#3 in both layouts.
- The play styles, Extension Addition, Secret Chords' values, the LOCK layer's two styles (Type key, Ext keys),
  Key Mode, Snap, Numerals, Single Notes, the bass, the performance modes, MIDI out, the views.
- Classic is today's firmware, key for key, light for light: every existing test and emulator script runs in Classic.

## Every place that reads the chord block's keys

Found in the code (2026-10-16, origin/main 4ecd5be). "Needs" is the build's work.

| Place | What it does today | Needs |
| --- | --- | --- |
| `cr_ui.c:61` `CU_MOD_OF_KEY[9]` | key 0 .. 8 → `cr_mod_t`, 0xFF for B3 | a second table for FM-1 (keys 0 .. 6: 6 DIM m7 MIN M7 MAJ 9), chosen by the layout; the pair handled by the resolver |
| `cr_ui.c:63` `CU_ROOT0` (9), `:67` `cu_white_idx`, `:76` `cu_white_key` | the first root; the white roots' index from D4 | `cu_root0()` (9 / 7) for "plays"; `CU_PICK0` 9 for the pick maps ([Picking keys](#picking-keys-in-layers)) |
| `cr_ui.c:3706` `CU_KEY_LOCK` 6, `CU_KEY_MIN` 3, `CU_KEY_MAJ` 5 | B3 = LOCK; the KEY layer's scale keys | LOCK key only in Classic; MIN / MAJ unchanged |
| `cr_ui.c:4018` `cu_mods_phys` | the chord keys held → `cr_mod_t` bits (LOCK's `CRE_MODTOUCH`) | through the resolver in FM-1 |
| `cr_ui.c:4063 .. 4165` `cu_lock_menu`, `cu_lock_tap`, `cu_mod_press`, `cu_mod_release` | LOCK, its layer, the latch's reset / toggle / push rules | the type-key gesture: the pair as one type, the pre-gesture latch kept for the window, "the same pair again" off; the silent down |
| `cr_ui.c:4194` `CU_KEY_SENSED` | velocity sensing for keys `>= CU_ROOT0` (or Full) | `cu_root0()`: C4, C#4 get their velocity in FM-1 (all 27 keys have the converter, `cr_sense.c CR_SENSE_KEYS` 0x07FFFFFF) |
| `cr_ui.c:4211` `cu_key_press` (B3 at 4217 .. 4226, the chord block at 4231, the layers at 4239, naming at 4245, the note at 4262) | where a key goes | B3 → LOCK only in Classic; the chord block by `cu_root0()`; F#5 / G5 shortcuts on the Full keyboard; the root note formula unchanged |
| `cr_ui.c:4272` `cu_key_release` | the same, released | the same |
| `cr_ui.c:4296` `cu_btn_press`, `cu_tap` | LFO tap = LATCH; GLO + LFO nothing | FM-1: LFO tap = LOCK, LFO a layer button (`L_BTN[L_LOCK]` = `BT_LATCH`, line 50), LFO + key = LOCK's tap now, GLO + LFO = LATCH |
| `cr_ui.c:5412` (FM-1), `:5208` (SMK) `b3_pend` hold | B3 held past Hold Time: the LOCK layer | Classic only; FM-1 through the layer-button path |
| `cr_ui.c:4737` KNOB 1 | the octave on the Full keyboard | the voicing everywhere |
| `cr_ui.c:5004` `cu_scan_step` | step entry's "keys up" | unchanged (kheld and the latch) |
| `cr_ui.c:5536` `cr_leds`: `key0` (5542), the chord block (5622 .. 5642), B3's LED (5623), `BT_LATCH`'s LED (5572), the layer button's blink (5591), the loop-slot map (5644), the pickers (5659), the KEY pick (5666), naming (5681), the playing keys (5704, 5720, 5731) | every key light and its four levels | the chord block by layout from the UI's key state; no LOCK key in FM-1; LFO: steady / pulse / blink; maps from `CU_PICK0`, playing keys from `cu_root0()`; F#5 / G5 |
| `cr_ui.c:5874` the top right | `lock` unless the Full keyboard | `lock` on the Full keyboard too |
| `cr_ui.c:6471`, `:7242` | "a root held" (`kheld >> CU_ROOT0`) | `cu_root0()`, and the Full keyboard's 25 |
| `cr_ui.c:6631` the KEY layer's screen | the tonic marked on D4 .. G5 | unchanged (`CU_PICK0`) |
| `cr_ui.c:6676` the LOCK layer's screen | footer `B: on/off` | `LFO: on/off` in FM-1 |
| `cr_ui.c:1037` Options (`O_*`, `O_NAME`, `O_MAX`, the get / set / text switches at 1087, 1119, 1207) | the entries | three entries; hidden on the SMK |
| `cr_ui.c:111` `CU_PAD_FN` (SMK) | pads → chord block keys | unchanged (Classic's table; the layout hidden there) |
| `cr_engine.c` `cr_mod_ex` (1060), `crx_modifier_press` (865), `crx_transform_voice`, `crx_build_base`, `crx_secret_quality` (253) | types, secrets, re-strikes, the Full keyboard's zones | a held-not-pressed down; the amend (no reattack, Play Chord ignored, voices flagged by the gesture); `cr_set_full_chords`; the secret rule unchanged |
| `cr_out.c:443` `CRE_MOD` (a: the mod, b: quiet, v: down) | the UI → engine queue | the two flags on `CRE_MOD` (silent, amend), `CRE_COMBINE_END`, `CRE_FULLCHORD` |
| `cr_loop.c` the gesture capture | one event a gesture serial | a repeated serial amends its event |
| `cr_settings.h` / `.c` `sticky` (167; 208, 525, 560, 764, 1104) | LATCH | the bits, the mask, the merge (above) |
| **the calibration** (`cr_ui.c:4796`, `panel.c:12 B_NAME`) | teaches the buttons (printed names: FX SCL ENV LFO EDIT GLO ...) and the knobs; the keys are not calibrated and no key is named there | **nothing**: it names LFO by its print, which does not change |
| `tools/emu/keymap.c` (the native emulator) | F1 .. F4 = keys 1 3 5 8, 2 .. 5 = keys 0 2 4 7, Tab = key 6, its comment and `--help` say DIM MIN MAJ SUS / 6 m7 M7 9 / LOCK; the panel draws note names, not chord labels (`emu.c:380`) | the keys stay (they are indices); the comment and `--help` say both layouts (FM-1: F1 F2 F3 = DIM MIN MAJ, F2 + F3 = SUS, 2 3 4 Tab = 6 m7 M7 9, F4 / W and 5 / A = the roots C#4 / C4, F8 (LFO) = LOCK, F10 (GLO) held + F8 = LATCH) |
| `tools/emu/web/keymap.js` `CHORD`, `tools/emu/web/emu.js:27, 54` | the sticker labels on keys 0 .. 8 and their colours | a `CHORD_FM1` map ({0: 6, 1: DIM, 2: m7, 3: MIN, 4: M7, 5: MAJ, 6: 9}) and the label switched by the firmware's layout (a `web_*` export of `cs.layout`, read each frame as the LEDs are); the LFO button's role label LOCK in FM-1 |
| `tools/emu/web/index.html:92` | the legend: F1 .. F4 DIM MIN MAJ SUS, 2 .. 5, Tab LOCK | the FM-1 line beside it (or switched by the layout) |
| `tools/emu/README.md:80`, `tools/emu/emu.h:9` | "key index = MIDI note - 53" | unchanged |
| the layout tool (`tools/layout/emu_sheet.mjs`, `layout_web.c`) | drives the browser emulator's panel; no key map of its own; LAYOUT-TOOL.md "the 27 keys always play" | nothing beyond keymap.js |
| `tools/emu/web/smk/` (the SMK page) | pads | nothing (hidden there) |
| `web/editor.html` | no keybed; its "chord keys" (lines 1211, 1749: `CHRD`, `VOIC`) are Felucca's sequencer parameters, not ChoralRoot's chord block | **nothing** |
| `web/site/index.html:107 .. 131` | the panel picture's alt text, the keybed SVG (CHORD BLOCK / ROOTS D4 … G5), the chord block SVG, the text on chord types, LOCK and secret chords | a second keybed / chord block picture for FM-1 and a sentence on the Option; the alt text |
| `web/site/img/panel-main.png`, `design/choralroot-fm1-panels.png` (`design/make_mockups.py`) | the panel with the Classic sticker | an FM-1 sticker variant ([Open questions](#open-questions) 14) |
| `design/make_mockups.py`, `make_layers_mockups.py`, `make_looper_mockups.py` | the designer sheets' chord block | Classic as they are; the FM-1's are this page's mock-ups |
| `docs/MANUAL.md` | 3 The keybed (table and drawing), The buttons (LFO), The knobs (KNOB 1 on Full), The lights (B3), 4 Types and extensions, LOCK, Secret chords, Key Mode's Full keyboard, LATCH, 14 Options, 18 Quick reference (Buttons, Keys) | an FM-1 column or paragraph in each; the three Options |
| `docs/manual/*.png` | `layer-key.png` (the tonic on D4 .. G5), `options.png` (Play Style), `chord.png` | unchanged; new: the Chord Layout option and the FM-1 LOCK layer's footer |
| Felucca's own UI (`perform.c:7`, `seq.c:390`, `ui_layer.c:7`, `ui_name.c:17`) | Felucca's black-key mutes and naming | not ChoralRoot's keys: nothing |
| `docs/SETTINGS.md` | the block's table, the "every setting" table | `sticky`'s bits, the three automatic settings |

## Cost

Estimates, to be measured by the build (`tools/build.py`'s report, docs/FLASH.md's account). main today: XIP
537 068 B (93.7 %, about 36 KB spare), RAM 80 176 B, POOL 306 196 B (89.0 %).

| | Estimate | What |
| --- | --- | --- |
| **XIP** | +2.5 .. 4 KB | the resolver and the gesture (about 1 KB), LOCK on LFO and GLO + LFO (0.3 KB), the LED changes and the pulse (0.5 KB), the three Options with their strings (0.4 KB), the Full keyboard's shortcuts and `cr_set_full_chords` (0.8 KB), the engine's silent down and amend (0.4 KB), the loop's amendment (0.2 KB), the settings bits (0.1 KB) |
| **RAM** | about +24 B .bss | `cs.layout`, `cs.combine`, `cs.full_short`; the gesture's start and its first key, the latch before it (2 B), the shortcut keys' press times (8 B) and states. The engine: a flag bit in `cr_voice_t`'s padding and the gesture flag in `cr_t`'s (as the streams' flags were: `cr_t` keeps its size); if no padding is left, +8 B |
| **POOL** | 0 | |
| **Settings** | 0 bytes | six spare bits of the block's `sticky`; the session record unchanged |
| **Flash data** | 0 | no new object, no new sector |
| **CPU** | negligible | a compare a scan while a gesture is open; the amend is Add Note's diff, once |

## The tests the build will need

Host (`tests/`):

1. `cr_settings_test`: `sticky`'s bits round trip; every older byte (0, 1) reads Classic, 50 ms, Off; a window code
   out of range reads 50 ms and keeps the other bits; the bits are automatic, bit 0 manual (the `CRS_STICKY_AUTO`
   merge); `cr_settings_apply` gives the engine `sticky & 1`, capture keeps the bits; Reset Settings gives the
   defaults; the older firmwares' rule modelled (`CRS_FIX(sticky, 0, 1)`: an FM-1 record reads LATCH off, nothing
   else touched).
2. `cr_engine_test`: the silent down does not re-strike (Advanced, Free, LOCK on and off); the amend, MIN → SUS and
   MAJ → SUS: one note-off (the third) and one note-on (the fourth) and no other event, in Simple, Advanced, Free,
   Add Note and Play Chord, at every voicing -12 .. +12, with 6th with 7th Octave up, with every extension set; the
   secret amends (MIN → m add4, MAJ → aug, DIM → power, SUS → power) diff only their notes; with a performance mode
   (arp, pattern, strum) the running sequence substitutes; voices not flagged by the gesture are untouched;
   `cr_set_full_chords` (a zone voice takes the latched spec; off: as today).
3. `cr_loop_test`: a gesture reported twice with one serial is one event, with the amended type; playback has no
   flicker.
4. `cr_midi_test`, `midi_control_test`: unchanged (MIDI notes still bypass the keys in either layout).

Emulator scripts (`tools/emu/test_cr.sh`, with the layout set through the Options first; every existing script runs
in Classic untouched, the regression guard):

5. The keys: in FM-1 C4 and C#4 play 60 and 61; B3 adds the 9; F#3, G#3, A#3 alone DIM MIN MAJ; G#3 + A#3 SUS; the
   combo table's every row, Secret Chords Off, Simple (with Play Style Simple and Advanced) and All.
6. The window, by the scripts' ms timing: the pair 20, 49 ms apart over a root pressed between them: SUS in place
   (the third's note-off and the fourth's note-on on MIDI out, no other note); 51 ms apart at the default: MIN stays
   (Simple), SUS re-struck (Advanced); the window at 30 and 80; the R, a, b order; a G# held for a second then a root:
   MIN at once; Free with no root held; a pair released one key at a time (no re-strike, the next root MIN).
7. LOCK: LFO tap on / off (`lock`, the LFO light lit), LFO held: the layer, LFO blinking, its footer; LFO + a chord key:
   LOCK's tap at once; GLO + LFO: LATCH (`Latch`, the light pulsing: `expect_led` at its four phases), LOCK + LATCH
   (pulse lit / middle); the pair latched: G#3 and A#3 lit; the same pair again alone: off at the release, its
   extensions still lit; the secret combos latched and off; Clears / Keeps and Toggle / Pushed with the pair; LOCK
   not switchable in the sound editor; PANIC clears it.
8. Picking keys: the loop slots, naming, the KEY layer's tonic on D4 .. G5 in both layouts, C4 / C#4 dark and silent
   in those maps and playing in the others; the KEY layer's MAJ / MIN.
9. The layout switch: from the Options with keys held (released), with a latch (kept, its lights moved: SUS + 9 on
   C#4 + C4, then G#3 + A#3 + B3).
10. The Full keyboard: KNOB 1 is the voicing (both layouts, shortcuts Off and On); Full Shortcuts On: F#5 tap / hold
    (the chord keys, the roots ending at F5, both layouts), G5 tap / hold (the latch's chord, the Key Mode chord, the
    message with neither), the split, LOCK on the Full keyboard (FM-1 by LFO; Classic B3 with the chord keys
    showing), the lights of F#5 and G5.
11. `test_persist.sh`: FM-1, a window of 65 ms and the shortcuts On come back after a power cycle without SAVE held
    (automatic); LOCK with SUS latched in FM-1 comes back lit on G#3 and A#3 after a global save.
12. The browser emulator: `test_web_emu.mjs` bit-exact with the native one as ever; the sticker labels follow the
    layout; `test_web_emu_smk.mjs` unchanged (the Options hidden). `BOARD=smk37 ./build.sh` builds.
13. Size: `tools/build.py`'s report against the estimate; docs/FLASH.md's account.

On the unit (the owner): the one-finger pair at 50 ms (and whether 40 or 60 suits better), the pulse's look, LFO as
LOCK in the hands.

## Open questions

1. **Picking keys** (decision 6): they stay on D4 .. G5 in both layouts, and C4 / C#4 do nothing in the pick maps.
   What it means: the loop slots, the naming letters, the KEY layer's tonic and the pickers keep the keys they have
   today; only playing uses the two new roots. The alternative is to shift the maps to start at C4 in FM-1 (one more
   slot key, the letters one key left), which changes where everything is between the layouts. Keep D4 .. G5?
2. **GLO + LFO in Classic**: unused today and in this design. Make it LATCH in both layouts (one gesture to learn),
   or leave Classic exactly as it is?
3. **LFO first, then GLO**: in this design only GLO held then LFO is LATCH (as GLO + SEQ, GLO + REC); LFO held then
   GLO is a combo that opens the LOCK layer. Should either order be LATCH (as GLO + SAVE's revert)?
4. **The LATCH pulse**: lit, middle, glow, middle at 300 ms (1.2 s), and with LOCK on as well lit ↔ middle. Right rate
   and depth on the unit?
5. **The window for the secret combos**: designed to apply the same in-place rule to F# with G# or A# (Secret on).
   The decision names the pair only; confirm the secret combos too?
6. **A root just before the type keys in Simple** stays a single note (or the scale's chord), as in Classic today.
   Should the window also catch a type key landing just after the root (in both layouts, or FM-1 only)?
7. **The KEY layer on pick**: G#3 and A#3 pick the scale at the press, so a one-finger pair there picks minor and then
   major (the second key wins). Ignore the second key of a pair within the window there?
8. **LOCK, Advanced / Free, roots held**: the latched pair pressed again re-types the held roots to MIN for up to the
   window before going off at the release. Acceptable, or should a type key over a latched pair wait the window
   before the held roots follow (sound changes only, never a new chord's start)?
9. **G5 with no latch and Key Mode off**: designed as "nothing, a message". Or major triads, or the last chord played?
10. **G5's state**: designed to start from Key plays each time the Full keyboard is entered, not stored. Keep it in
    the session instead?
11. **The names on the screen**: `Chord Layout`, `Combine Window` (or `SUS Window`), `Full Shortcuts`.
12. **The window's step**: 5 ms (11 values) or 10 ms (6); and grey in Classic or hidden there.
13. **MIDI that plays the keys**: none exists today (the Perform channel plays the sound directly). Wanted, as a
    mode or a channel where notes 53 .. 79 press the FM-1's keys through the active layout?
14. **The sticker pictures**: one panel picture per layout for the site and the manual, or one picture with both?
15. **The layout switch with keys held**: designed to release them (a held chord stops). Or keep each key's job until
    it is let go?
