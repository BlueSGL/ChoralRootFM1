# ChoralRoot FM-1: the manual

For ChoralRoot 0.17 on the M-VAVE FM-1. It is written for someone who owns an FM-1, has installed ChoralRoot with
the web installer and wants to play it. Nothing here needs the design documents; where one goes deeper, the manual
links to it.

How the manual names things:

- A button is named by its ChoralRoot job, with the label printed on the FM-1 in brackets the first time: **KEY**
  (printed SEL). The printed labels belong to the stock firmware and mostly mean something else here. The tables in
  [The panel](#3-the-panel) and the [quick reference](#18-quick-reference) carry both names.
- The knobs keep their printed names (MASTER, SELECT, PRESETS, ALGORITHM, KNOB 1 to KNOB 4), because the screen uses
  them too.
- Keys are named by their note. The leftmost key is F3, the rightmost G5.
- A **tap** is a short press. A **hold** is a press kept down past the hold time (0.4 s; Options > Hold Time).
- Text in `this type` is what the screen shows.

## Contents

1. [What ChoralRoot is, and is not](#1-what-choralroot-is-and-is-not)
2. [The first five minutes](#2-the-first-five-minutes)
3. [The panel](#3-the-panel)
4. [Chords](#4-chords)
5. [Layers](#5-layers)
6. [Bass](#6-bass)
7. [Performance modes](#7-performance-modes)
8. [FX](#8-fx)
9. [Tempo and metronome](#9-tempo-and-metronome)
10. [The looper](#10-the-looper)
11. [Sounds](#11-sounds)
12. [The sound editor](#12-the-sound-editor)
13. [Views](#13-views)
14. [Options](#14-options)
15. [MIDI and USB](#15-midi-and-usb)
16. [Updating and recovery](#16-updating-and-recovery)
17. [Troubleshooting](#17-troubleshooting)
18. [Quick reference](#18-quick-reference)
19. [Where to read more](#19-where-to-read-more)

## 1. What ChoralRoot is, and is not

ChoralRoot turns the FM-1 into a chord instrument in the manner of the Telepathic Instruments Orchid. The left end of
the keybed says what kind of chord you want, the rest of the keybed plays its root, and the instrument builds the
chord, voices it and writes its name on the screen. Around that it has a bass part that follows the chords,
performance modes (strum, arpeggio, patterns), effects, a looper that records chords, eleven synth engines with a deep
sound editor, MIDI out on three channels and USB audio recording.

What it is not:

- **Not the stock firmware with additions.** ChoralRoot replaces the FM-1's firmware while it is installed. The stock
  sequencer, sampler and drum sounds are not in it; every sound is synthesised. The installer puts the stock firmware
  back whenever you want ([Back to the stock firmware](#back-to-the-stock-firmware)).
- **Not what the panel says.** Most printed labels have another job (SEL is KEY, ARP is PERF, ENV is BASS, and so on),
  and in the sound editor the buttons change again. A sticker sheet would carry the table of the
  [quick reference](#18-quick-reference).
- **No velocity.** The FM-1's keys do not sense it. One fixed velocity is set in Options.
- **No Bluetooth MIDI**, and the computer cannot play audio through the FM-1. USB MIDI works both ways, the TRS jack
  is a MIDI input, and the computer can record the FM-1 over USB.
- **Not affiliated** with Telepathic Instruments or M-VAVE.

You can try everything in this manual without the hardware: the firmware runs in the browser at
<https://quixotic7.github.io/ChoralRootFM1/emu/>.

## 2. The first five minutes

**Switch on.** The screen shows `choralroot` over three stripes, with the version under them for a moment. The logo
stays until you touch something.

**Play a note.** Press D4, the sixth white key from the left. Every key from D4 up to G5 is a **root key**. On its own
a root key plays one note; the screen shows `D` and under it `D4`.

**Play a chord.** The lowest five white keys and the four black keys between them are the **chord block**. Hold MAJ
(the black key A#3) and press D4: D major. The screen shows `D` large and the notes `D4 F#4 A4` under it, and the
three root keys where those notes sound light up. Now hold MIN (G#3) and m7 (the white key G3) together and press D4:

<img src="manual/chord.png" width="240" alt="The chord view: Dm with a small orange 7, the notes D4 F4 A4 C5 under it, C5 in orange">

The chord keys have to be down when the root goes down. The name stays on the screen after you let go.

**Free a hand with LOCK.** Tap B3. It lights, and `lock` appears at the top right. Hold MIN and let go: MIN stays lit
and every root you play is now minor. Tap 9 (the white key C4) to add a ninth, tap it again to take it away. Tap MAJ:
major, with the extensions cleared. Tap B3 again to switch LOCK off.

**Change the voicing.** Hold a chord and turn KNOB 1. Each click moves the lowest note up an octave (or, the other
way, the highest note down); the screen reads `voicing +2`.

**Open a layer.** Hold **FX** for about half a second. Its layer opens and stays open after you let go; the FX button
blinks.

<img src="manual/layer-fx.png" width="240" alt="The FX layer: Reverb as the picked effect, four cells Size, Damp, Type, Amount in the knob colours">

SELECT picks the effect, KNOB 1 to KNOB 4 turn the four cells under it, and the root keys still play so you hear the
change. **OCT-** closes the layer (so do OCT+, HOME, and holding FX again). All the layer buttons work this way: a tap is the button's on / off,
a hold opens its layer. [Layers](#5-layers) has the rules.

**Change the sound.** Turn PRESETS: the next sound of the same engine loads at once, and the screen shows its place,
`02 / FM BELL / FM6 · 02/25`. To change the engine, hold **OPT** (printed GLO) and turn PRESETS: the engines pass by
(FM6, FM TONE, VA, CZ-1 and seven more). Let go of OPT and the engine you stopped on is loaded.

**Add the bass.** Tap **BASS** (printed ENV). A bass note now plays under every chord, and `Bass` shows at the top
right. ALGORITHM changes the bass sound. Tap BASS again to switch it off.

**Record a loop.** Tap **REC**. The REC light comes on, the top line reads `Rec · Overwrite` and a message says
`rec: play to start`. Play a few chords: recording starts with the first one. Tap REC again: `loop recorded`, and the
loop plays straight away. **LOOP** (printed PLAY) stops and starts it. Play over it, change the sound, turn KNOB 1:
the loop holds chords, not audio, so it follows. [The looper](#10-the-looper) has the rest.

**If anything hangs.** Press OCT- and OCT+ together and let go: `PANIC`, every note off. (Do not keep the two held:
after two seconds a countdown starts, see [Power-on and held gestures](#power-on-and-held-gestures).)

## 3. The panel

### The keybed

27 keys, F3 to G5. The left end is the chord block; the other 18 keys are the roots.

| Keys | ChoralRoot | Job |
| --- | --- | --- |
| F#3 G#3 A#3 C#4 (black) | **DIM MIN MAJ SUS** | the chord type, while held |
| F3 G3 A3 C4 (white) | **6 m7 M7 9** | extensions, while held; they stack |
| B3 | **LOCK** | tap: the chord block latches (lit while on); held: the LOCK layer (how it latches) |
| D4 to G5 (18 keys) | **roots** | a single note alone, a chord with a chord key held or latched; OCT- / OCT+ shift them by octaves |

```
black      DIM   MIN   MAJ         SUS
           F#3   G#3   A#3         C#4     D#4   ...
white    6     m7    M7    LOCK  9       D4    E4    F4   ...   G5
         F3    G3    A3    B3    C4      |-------- roots --------|
```

In a layer, a menu or a dialog the root keys may take another job (they pick a slot, type a letter, choose an
engine). The chord block keeps its job everywhere but in the KEY layer, where MAJ and MIN pick the scale.

### The buttons

Two rows of six, and the two octave buttons.

| Printed | ChoralRoot | Tap | Hold |
| --- | --- | --- | --- |
| FX | **FX** | the effects on / off | the FX layer |
| SEL | **KEY** | Key Mode on / off | the KEY layer |
| ENV | **BASS** | the bass on / off | the BASS layer |
| LFO | **LATCH** | latch on / off | nothing |
| EDIT | **EDIT** | the sound editor | the engine picker |
| GLO | **OPT** | the Options | with a knob: the knob's second job |
| HOME | **HOME** | back to the view; on the view: the next view | 0.6 s: the view menu |
| SAVE | **SAVE** | save the sound | while held: save / load / delete a loop |
| ARP | **PERF** | performance on / off | the PERF layer |
| SEQ | **METRO** | metronome on / off | the METRO layer |
| PLAY | **LOOP** | play / stop the loop | the PLAY menu |
| REC | **REC** | record | undo the last layer |
| OCT- / OCT+ | octave | the roots an octave down / up; in layers, menus and dialogs: back / OK | both together: PANIC |

In the sound editor seven of the buttons become the editor's groups; see
[The sound editor](#12-the-sound-editor).

### The knobs

None of them can be pressed. A turn (of any knob but MASTER) shows its value large on the screen for about a second.

| Knob | Turn | With OPT held |
| --- | --- | --- |
| MASTER | the volume | |
| SELECT | the tempo, 20 to 300 BPM; in a layer or a menu: moves the picker (the KEY layer: turns its page) | the metronome's level |
| PRESETS | the chord sound: the next or previous one of its engine | the chord sound's engine |
| ALGORITHM | the bass sound: off, then the bass engine's sounds | the bass level |
| KNOB 1 | the chord's voicing (on the Full keyboard: the octave) | the Single Notes split point |
| KNOB 2 | the bass register, in octaves | |
| KNOB 3 | the first parameter of the performance mode | |
| KNOB 4 | the amount of the selected effect | |

In a layer KNOB 1 to KNOB 4 are the layer's; in the editor they edit the four values of the active lane.

### The screen

- **Top left**: `Key: C` in yellow while Key Mode is on. While a loop records or plays: `Loop 1` in red.
- **Top right**: one status word, the first of these that applies: `Bass Solo`, the performance mode (`Strum`, `Slop`,
  `Arp`, `Pattern`, `Harp`), `Bass`, `Oct +1`, `Latch`, `lock`. While a loop records or plays, the looper's state
  instead (`Rec · Overwrite`, `Rec 2.3`, `Dub 3.2`), and a small dial at the right end that fills as the loop goes
  round.
- **The middle**: the chord and its notes ([Views](#13-views) has the other four views), or the open layer, menu,
  dialog or editor.
- **Knob read-outs** take the whole screen for about a second: the value large, a row of stripes, what it is
  (`120 / bpm`, `+1 / octave`, `34 / reverb`).
- **Messages** are a box over the screen for about a second (`loop recorded`, `saved FM6 26`, `no free slot`).
- **The footer** of a layer lists its keys (`SELECT: effect · FX: on/off · OCT-: back`; it uses the printed names).

The battery level shows only on the Options pages, at the top right.

After three minutes without a key, button, knob or incoming MIDI message the logo comes back, and after ten the
screen goes black, so that nothing stays on it long enough to leave a mark. Its backlight stays on, so in a dark
room a black screen may still glow a little. The lights, the looper and MIDI keep running. Any key, button, knob or
MIDI message wakes it and does its normal job at the same time.

### The lights

| Light | Means |
| --- | --- |
| KEY, PERF, FX, BASS, LATCH, METRO lit | that function is on |
| a layer button blinking | its layer is open |
| EDIT blinking | the sound editor (or the engine picker) is open |
| OPT lit | the Options are open (in the PLAY menu: Keys = Loops; in the KEY layer: its keys play; in the editor: SHIFT) |
| SAVE lit | the save dialog is open |
| REC lit / blinking | armed / counting in, recording or overdubbing |
| LOOP lit, its green light | the slot holds a loop, the loop is playing |
| OCT- lit, OCT+ blinking | in a layer, menu or dialog: back, OK (where there is nothing to confirm: close) |
| OCT- or OCT+ lit on the view | the roots are shifted down or up |
| chord keys | lit while held or latched; B3 lit while LOCK is on; otherwise a faint glow, the lowest level. In the KEY layer: MAJ and MIN glow, the selected scale's is lit, the others are dark |
| root keys | the notes of the sounding chord, by name: every root key with the name of a note that sounds is lit (a chord with a G in it lights both G keys), wherever on the keyboard the chord is voiced; the note a performance mode is playing blinks, on all its keys; a playing loop's notes glow dim the same way. With Key Mode on they show the key (below) |

**Key Mode on the keys.** The key lights have four levels: faint (an idle chord key, B3), low (the glow: a note of
the scale, an idle button), middle (the tonic) and fully lit. With Key Mode off the root keys are dark: only the notes
that sound are lit (and a playing loop's notes glow), so the chord keys' faint row is all that shows on an idle
keyboard; on the Full keyboard all 27 keys are dark until played. With Key Mode on the roots show the key, wherever they play (on the views, in the PERF, FX and LOCK
layers, in the KEY layer too): the notes of the scale glow at the low level, the tonic's keys at a middle level, the
roots outside the scale are dark, white or black, and the notes that sound are fully lit over it. So a glance tells
you that Key Mode is on, which key it is, and where the tonic sits. The layers that use the root keys for something
else (the loop slots, the pickers, naming, the Options) show their own map instead.

**The notes that sound.** While a chord sounds, every root key that carries the name of one of its notes is lit: D
major lights the D, F# and A keys in both octaves (there is one A). The octave does not matter, so a chord that
KNOB 1 has voiced far up or down, beyond the two octaves of the root keys, still shows on them, and so does a chord
moved by OCT- / OCT+. The lights show what sounds, not what you press: with Transpose at +5 in the KEY layer, D4
alone lights the two G keys. The chord keys never light for a note; they keep showing the chord type and LOCK. On
the Full keyboard all 27 keys are note keys, so every key of that name lights there. The bass part's own note does
not light the keys.

Options > LEDs chooses between Glow (idle keys glow dim, active ones are bright) and Stock (the other way round).
Stock has two levels only and is as it always was: idle chord keys and, with Key Mode off, every root key are lit;
with Key Mode on, the keys of the scale are lit, the tonic like the rest, and the keys outside it are dark.

## 4. Chords

### Types and extensions

Hold one chord type and any extensions, then press a root.

| Key | Name | Notes above the root (semitones) | On the screen (root D; the extensions on D major) |
| --- | --- | --- | --- |
| F#3 | DIM | 3, 6 | `Ddim` |
| G#3 | MIN | 3, 7 | `Dm` |
| A#3 | MAJ | 4, 7 | `D` |
| C#4 | SUS | 5, 7 | `Dsus` |
| F3 | 6 | adds 9 | `D` with a small `6` |
| G3 | m7 | adds 10 | `D` with a small `7` (on a sus chord: `m7`) |
| A3 | M7 | adds 11 | `D` with a small `M7` |
| C4 | 9 | adds 14 | `D` with a small `9` |

- With two types held, the one pressed last counts (unless it is a [secret chord](#secret-chords)).
- Extensions stack: MAJ + 6 + 9 reads `D` `6 9`. Three extensions read `JAZZ`, all four `WTF`.
- A root with no chord key is a single note (unless Key Mode is on).
- Under the name the screen lists the notes as they are voiced. Extension notes are orange.
- The name of the last chord stays on the screen until the next one.

### LOCK

B3 switches LOCK on and off; B3 is lit and `lock` shows at the top right while it is on. With LOCK on:

- the chord keys you are holding stay on when you let go (they stay lit);
- then, with no chord key held, a **type** key replaces the latched chord with that type alone, and an **extension**
  key switches its extension on or off without touching the type.

Example: hold MIN, let go: minor. Tap 6: m6. Tap 9: m6/9. Tap 9 again: m6. Tap MAJ: major, nothing else.

**Back to single notes.** With one type latched, press that same type key again on its own: the type goes off when
you let go, and the roots play single notes. The extensions you had latched are not cleared: they stay lit, do
nothing while there is no type, and are back the moment you press a type key again, whichever it is. So with MAJ and
M7 latched: MAJ again and D4 is a single D with M7 still lit; MAJ once more and D4 is Dmaj7 again. A second chord key
pressed while the type key is down cancels this (the keys held together latch together, as always); a root played
meanwhile does not. This is always so with LOCK on, whatever the two settings below say. (In Key Mode with Key plays
set to Chords a lone root is still the scale's chord, with the latched extensions.)

Switching LOCK off, or PANIC, clears what is latched. LOCK is off at power-on. On the Full keyboard (Key Mode,
below) there is no LOCK: B3 is a note there.

**The LOCK layer.** Hold B3 past the hold time (a tap is still on / off; a hold does not switch LOCK). The layer
stays open like the others, B3 blinks, and the top left says `lock · on` or `lock · off`. The keys play as usual
in it, so you hear what a setting does. OCT-, OCT+, HOME or holding B3 again closes it. Two settings, both
remembered and both acting only while LOCK is on:

| Knob | Setting | Values |
| --- | --- | --- |
| KNOB 1 | **Type key** | `Clears` (default): a type key replaces the latched chord with that type alone, as described above. `Keeps`: a type key changes the type and leaves the latched extensions on, so with 6 and 9 on you can go from MAJ to MIN and keep them |
| KNOB 2 | **Ext keys** | `Toggle` (default): each extension key switches its extension on or off. `Pushed`: the extension keys work as a set, the way the type keys do. Press one or several together and let go: that is the set. The next extension key you press, with none held, starts a new set of just what you press then |

In Pushed, a second extension key pressed while the first is still down joins the set, and with two or more on,
pressing one of them on its own makes the set that one key. To switch the extensions off, press the only extension
that is on once more, on its own: it goes off when you let go (it keeps sounding until then, and if you add another
key while holding it, both stay on). Example: 6: 6 is on. 7: 7 is on, 6 is off. 7 again: nothing is on. 7, and 9
while holding it: 7 and 9. With `Keeps` and `Pushed` together a type key changes only the type and an extension key
only the extensions. With `Clears`, a type key that replaces another type clears the extensions as always; the
same type pressed again (off, above) and a type pressed when none is latched leave them alone.

### Voicing and octave

- **KNOB 1** revoices the chord: one click up moves the lowest note up an octave, one click down moves the highest
  note down, from -12 to +12. The screen adds `voicing +2` under the notes. The voicing applies to every chord that
  follows, and to a playing loop. (On the Full keyboard there is no chord to voice: KNOB 1 is the octave there.)
- **OCT-** and **OCT+** shift the root keys by octaves, from -2 to +2 (`Oct +1` at the top right). A low voicing can
  put chord notes below D4, where no key can light for them; the notes line on the screen always has them.

### Play styles

Options > Play Style decides what the chord keys do while a root is already down.

| Style | A chord key pressed while a root is held | A chord key pressed with no root held |
| --- | --- | --- |
| **Simple** (default) | nothing: the chord was fixed when the root went down | nothing |
| **Advanced** | a type key replays the held chord as that type; an extension key switches its extension on or off | nothing |
| **Free** | as Advanced | the last root sounds again as that chord, for as long as a chord key is held |

Options > Extension Addition matters in Advanced and Free: **Add Note** (default) adds or removes just the
extension's note while the rest of the chord rings on; **Play Chord** strikes the whole chord again.

**With LOCK on** the chord keys mean the same in all three styles: the lit keys are the chord, and the LOCK layer's
`Type key` and `Ext keys` settings decide what a press does to them. The styles only differ in what follows the lit
keys. Simple: the next root you play. Advanced: also the roots you are holding, at once (a key that goes off leaves
the held chord, a new set replaces the old one, no type lit leaves a single note). Free: the same, and while you
hold a chord key with no root down, the last root sounds again as the lit chord; it stops when you let go of the
chord key, lit or not. A key that will go off when you let go (the one lit type or extension pressed again) still
sounds in that chord until then.

### Secret chords

Options > Secret Chords: Off (default), Simple (only while Play Style is Simple) or All. When it applies, two types
held together make a third chord:

| Held | Chord | On the screen |
| --- | --- | --- |
| DIM + SUS | power chord (root, fifth, octave) | `D` with a small `5` |
| MAJ + SUS | augmented | `D+` |
| MIN + SUS | minor with a fourth instead of a fifth | `Dm` with a small `add4` |

The small `5` and `add4` are red. Extensions stack on top as usual.

### Key Mode

Tap **KEY** (printed SEL): the KEY light comes on and `Key: C` shows at the top left. Now a root key on its own plays
the chord that belongs on that note of the scale. In C major: D gives `Dm`, E `Em`, F `F`, G `G`, A `Am`, B `Bdim`.
A key outside the scale plays a sus chord on a neighbouring scale note (C# in C gives `Csus`). A chord key held
overrides the scale's choice of type. The root keys light the key: the scale's notes glow, the tonic's keys are
brighter, the keys outside the scale are dark ([The lights](#the-lights)).

Hold KEY to open the **KEY layer**:

<img src="manual/layer-key.png" width="240" alt="The KEY layer: a keyboard with the tonic lit yellow, and four cells Tonic, Scale, Transpose, Single">

| Control | In the KEY layer |
| --- | --- |
| any root key | that note becomes the tonic and Key Mode switches on. The scale stays as it is, unless MAJ or MIN is held with the root |
| MAJ, MIN (the chord keys) | the scale: Major or Minor (`Key: E minor`), at the press, with LOCK on or off. In this layer they are not chord keys: the chord you have latched with LOCK is untouched and is back when the layer closes. The other chord keys do nothing here; B3 is still LOCK |
| KNOB 1 | Tonic, step by step (Key Mode switches on) |
| KNOB 2 | Scale: Major or Minor |
| KNOB 3 | Transpose, -24 to +24 semitones. It moves everything you play from then on, in or out of Key Mode |
| KNOB 4 | Single: Full, Split Oct or Split Bass (below) |
| SELECT | turns the layer's page: page 1 is the four settings above, page 2 has Key plays and Keyboard (below). The top line says `1/2` or `2/2`. SELECT does not change the tempo in this layer |
| KNOB 1, page 2 | **Key plays**: Chords or Notes |
| KNOB 2, page 2 | **Keyboard**: Chords or Full |
| OPT (printed GLO) tap | switches the keys between **pick** and **play** (below). It does not open the Options from here |
| OCT-, OCT+, HOME, KEY held again | close the layer |

A quick way to set a key: hold KEY and press a root at once (the layer opens straight away), then OCT- to close it.
For a minor key press MIN as well, before or after the root.

While the layer is open the chord keys' lights show the scale instead of the chord: MAJ and MIN glow, the one of
the selected scale is fully lit, and the six chord keys without a job here are dark. (With Options > LEDs set to
Stock, which has no half level, only the selected scale's key is lit.) Outside the layer the lights mean what they
always do.

**Keys: pick or play.** The layer opens with its keys on **pick**: the root keys set the tonic, MAJ and MIN the
scale, and nothing sounds. Tap **OPT** (printed GLO) in the layer and the keys **play** instead, exactly as outside
the layer: roots, chord keys, LOCK, with their usual lights. Now you can hear the key while KNOB 1 to KNOB 4 change
the tonic, the scale, the transposition and Single. The top of the layer says which it is (`key · keys pick`,
`key · keys play`), the footer shows the way to the other, and OPT is lit while the keys play. Tap OPT again for
pick. The choice is remembered, also after switching off. OPT held with KNOB 1 is still the split point.

**Single: Full, Split Oct, Split Bass.** Single decides what the keys below the split point do.

- `Full`: nothing special.
- `Split Oct`: single notes (not chords) on keys whose note name lies below the split point sound an octave lower.
  The split point is F by default, so C to E are the lowered notes, in every octave; OPT + KNOB 1 or Options > Split
  Point moves it.
- `Split Bass`: two instruments side by side. The keys below the split point play the **bass part's sound** and
  nothing else, as single notes at their own pitch (the bass register, KNOB 2 on the view, shifts them by octaves);
  the other keys play the chord part and nothing else. With the usual keys that means the roots whose note name is
  below the split point (C to E by default) are bass notes and the others play chords as ever. While Split Bass is
  on the bass is played by hand: it does not follow the chords, and Bass on / off and the Bass Behaviour are left as
  they are and ignored until Single is changed back. The top right says `Split Bass`. The bass part is one voice.

**Key plays: Chords or Notes** (page 2, KNOB 1). `Chords` is Key Mode as described above: a lone root plays the
scale's chord. `Notes` keeps Key Mode as a guide and a guard only: the roots and the chord keys behave exactly as
with Key Mode off (a lone root is a single note, a chord key gives its chord), but a root outside the scale does
nothing at all: no sound, no MIDI, nothing in a loop, no light. The key still shows on the root keys.

**Keyboard: Chords or Full** (page 2, KNOB 2). `Chords` is the usual keybed: nine chord keys and eighteen roots.
`Full` turns all 27 keys, F3 to G5, into an ordinary chromatic keyboard playing single notes of the chord part:

- There are no chord keys and no LOCK: B3 is a note, and holding it does not open the LOCK layer. What you had
  latched with LOCK is kept untouched and is back, lit, when you return to `Chords`.
- **KNOB 1** on the view changes the **octave** (the same octave as OCT- and OCT+; the read-out says `octave`),
  since there is no chord to voice. The voicing you had is kept for the Chords keyboard.
- With Key Mode off the keyboard is chromatic. With Key Mode on, the notes outside the scale do nothing and the key
  shows on all 27 keys (Key plays does not matter here: there are no chords to make).
- The bass, the performance modes (an arpeggio of the notes you hold), the looper and MIDI out follow what you play
  as they follow a single note.
- The split is at the **second F**: F3 to E4 is the left side, F4 to G5 the right. `Split Oct` plays the left side
  an octave lower; `Split Bass` gives the left side to the bass part and the right side to the chord part. This
  split is fixed on the Full keyboard; the split point you set with OPT + KNOB 1 belongs to the Chords keyboard and
  is not changed by it.
- In the KEY layer with its keys on pick, the keys still pick: a root sets the tonic, MAJ and MIN the scale (B3 does
  nothing there). With the keys on play they are the Full keyboard.

Key plays, Keyboard and Single are remembered, also after switching off.

The Geek Out view shows the transposition (`Trans +2`).

### LATCH

Tap **LATCH** (printed LFO): what you play keeps sounding after you let go, until the next thing you play replaces
it. `Latch` shows at the top right. With a performance mode on, LATCH is what keeps an arpeggio running with your
hands off the keys. Tap LATCH again and the held notes stop.

LOCK and LATCH are different things: LOCK holds the **chord keys** for you, LATCH holds the **sound**.

### PANIC

OCT- and OCT+ pressed together: every note stops (on MIDI too: all notes off is sent), the octave goes back to 0,
the LOCK latch is cleared, LATCH goes off and a playing loop stops. A take that was being recorded is dropped; the
loop and the settings are kept. The screen goes red for a second and is then blank until you play.

## 5. Layers

A layer is a screen that borrows KNOB 1 to KNOB 4 (and sometimes the root keys) for one job. Six buttons have one:
KEY, PERF, FX, BASS, METRO and LOOP.

- **Tap = use.** A tap is always the button's on / off action, also while its own layer is open.
- **Hold = open, and it stays.** Hold the button past the hold time (0.4 s) and its layer opens and stays open after
  you let go. The button blinks. Touching a key or a knob while the button is down opens the layer at once.
- **OCT- or HOME = close.** So does holding the layer's own button again (the way you opened it). Holding another
  layer button switches straight to that layer.
- **The knob row.** Under the layer's picker, four cells show what KNOB 1 to KNOB 4 do, in the knob colours (blue,
  orange, white, green), each with a small picture of its value. The cell you just turned lights up. A dash means the
  knob has no job here.
- **SELECT** moves the picker at the top of the layer (in the KEY layer it turns the page; in the LOCK layer it stays
  the tempo).
- **OCT- is back, OCT+ is confirm.** Where a layer has something to confirm, OCT+ does it (the engine picker's
  sound, the loop slots' Save / Load / Delete, the save dialog). The layers below have nothing to confirm, so there
  OCT+ closes the layer just as OCT- does. On / off is always a tap of the layer's own button.
- The chord block keeps its job in every layer except KEY's, where MAJ and MIN pick the scale.

| Layer | Hold | The picker (SELECT) | The root keys | A tap of its button | OCT- / OCT+ |
| --- | --- | --- | --- | --- | --- |
| KEY | KEY (SEL) | a keyboard with the tonic lit | set the tonic (OPT tap: they play instead) | Key Mode on / off | close |
| PERF | PERF (ARP) | the performance mode | play as usual | performance on / off | close |
| FX | FX | the effect | play as usual | effects on / off | close |
| BASS | BASS (ENV) | the bass behaviour | the white keys D4 E4 F4 G4 pick the behaviour; nothing sounds | bass on / off | close |
| METRO | METRO (SEQ) | the time signature | D4 E4 F4 pick 4/4, 3/4, 6/8 | metronome on / off | close |
| PLAY menu | LOOP (PLAY) | see [The looper](#10-the-looper) | play, or pick loop slots | play / stop | close (in step entry: back a step / a rest) |

Three more things open the same way and are described in their own chapters: **EDIT** held is the engine picker
([Sounds](#11-sounds)), **HOME** held for 0.6 s is the view menu ([Views](#13-views)), and **SAVE** held shows the
loop slots for as long as you hold it ([The looper](#10-the-looper)).

PRESETS and ALGORITHM keep their jobs in a layer (except in the PLAY menu). The hold time is set in Options > Hold
Time (300, 400, 500 or 600 ms).

## 6. Bass

The bass is a second, monophonic part with its own sound. It is **off every time you switch on**.

- **BASS** tap switches it on and off. The screen shows the bass sound's place (`17 / DEEP SUB / VA · 17/25`), or
  `00 / off`.
- **ALGORITHM** turns through off (`00 off`) and then the sounds of the bass part's engine. Its first sound is
  `00 INIT`, the engine's plain starting sound.
- **KNOB 2** moves the bass register, -2 to +4 octaves.
- **OPT + ALGORITHM** is the bass level.

Hold BASS for the **BASS layer**:

| Control | In the BASS layer |
| --- | --- |
| SELECT, KNOB 1, or the white keys D4 E4 F4 G4 | Behaviour (below) |
| KNOB 2 | Register |
| KNOB 3 | Sound (as ALGORITHM, without wrapping round) |
| KNOB 4 | Level |

The root keys do not sound while this layer is open, whatever its footer says; close it to hear the change.

| Behaviour | What the bass does |
| --- | --- |
| **Chords Only** (default) | plays the chord's root, in the bass register, under chords; a single note has no bass |
| **Unison Bass** | plays under everything, at the pitch of the key you pressed rather than in the bass register (KNOB 2 still moves it by octaves) |
| **Bass Single Notes** | plays the root under everything; a single note sounds on the bass alone |
| **Solo** | only the bass sounds; the chord sound is silent (`Bass Solo` at the top right) |

The same setting is Options > Bass Behaviour. The behaviour and the register are remembered; the level is not (it is
back at its normal value when you switch on).

To edit the bass sound, hold BASS and tap EDIT. To save it, hold BASS and tap SAVE. To change the bass part's
engine, open the bass sound's editor and hold EDIT there ([Sounds](#11-sounds)).

## 7. Performance modes

A performance mode plays the notes of your chord for you. **PERF** (printed ARP) tap switches the chosen mode on and
off; the top right names it. Hold PERF for the **PERF layer**: SELECT picks the mode (picking one switches the
performance on), the root keys play so you hear it, KNOB 1 to KNOB 4 are the mode's parameters, and a PERF tap is
still on / off. OCT-, OCT+, HOME or holding PERF again closes the layer.

While an arpeggio or a pattern is playing, the top half of the PERF layer shows it: the chord's name, its notes on a
line and the sounding note hopping along, with the mode's name small at the top left (`Arpeggiate · on`). The four
cells under it stay as they are, and turning KNOB 1 to KNOB 4 does not disturb it. The mode picker always comes
first: for two seconds after the layer opens, and for two seconds after every turn of SELECT, the top half is the
picker with the mode's name large, so you can read what you switched to even while LATCH keeps the notes going.
When nothing is being stepped (no keys held, or Strum, Slop, Harp) the picker stays.

| Mode in the picker | What it does | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- | --- |
| **Strum**, **Strum 2 Octaves** | the notes one after another | Rate (ms) | Direction | Range (octaves) | Hold |
| **Slop** | a strum with uneven timing | Amount (%) | Rate | Direction | Range |
| **Arpeggiate**, **Arp 2 Octaves** | the notes in turn, in time with the tempo | Division | Direction | Gate (%) | Swing (%) |
| **Pattern** | one of twelve rhythmic figures over the chord's notes | Pattern | Division | Gate | Swing |
| **Harp** | a fast run over several octaves | Rate | Direction | Gate | Range |

- Direction: Up, Down, Up-down, Down-up, Played or Random.
- Division: 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 and the triplets of 1/2 to 1/32.
- Rate is in milliseconds at 120 BPM and follows the tempo.
- The two "2 Octaves" entries are the same modes with the range set to two octaves.
- The twelve patterns: pulse, alberti, waltz, cascade, updown, syncopate, gallop, skip, ballad, stride, drive, ladder.
  The layer shows the number; KNOB 3 outside the layer shows the name too.
- Strum's Hold keeps the strummed chord sounding after you let go, as LATCH does.

Outside the layer **KNOB 3** turns the mode's first parameter (the strum rate, the slop amount, the arp division,
the pattern, the harp rate). An arpeggio stops when you let go of the keys; switch LATCH on to keep it running.

On the Chord view, while an arpeggio or a pattern is playing, the screen shows it: the chord's name, its notes on a
line, and the note sounding lifted onto a block that hops from note to note. When the arpeggio stops the chord view
is back. Strum, Slop and Harp keep the chord view.

## 8. FX

Four effects: **Reverb**, **Chorus**, **Delay** and **Drive**. Each has an amount, and one of the four is the
selected one.

- **FX** tap switches all four on and off for the chord sound.
- **KNOB 4** is the amount of the selected effect (turning it switches the effects on).
- Hold FX for the **FX layer**: SELECT picks the effect, KNOB 4 is its amount, KNOB 1 to KNOB 3 are its parameters,
  a FX tap is still on / off, and the root keys play. OCT-, OCT+, HOME or holding FX again closes the layer.

| Effect | KNOB 1 | KNOB 2 | KNOB 3 |
| --- | --- | --- | --- |
| Reverb | Size | Damp | Type (Room or Spring) |
| Chorus | Rate | Depth | |
| Delay | Time (a division of the tempo) | Feedback | Colour |
| Drive | | | |

Two things to know:

- **The amounts belong to the sound.** Every preset brings its own four amounts, so turning PRESETS replaces yours.
  Save the sound to keep them ([Sounds](#11-sounds)). The bass sound has its own four amounts in its editor's FX
  group.
- **The parameters on KNOB 1 to KNOB 3 are shared** by the whole instrument and are not stored: they are back at
  their starting values the next time you switch on.

## 9. Tempo and metronome

- **SELECT** sets the tempo, 20 to 300 BPM. Arpeggios, patterns, the delay, the loop and the MIDI clock follow it.
- **METRO** (printed SEQ) tap switches the click on and off (`metronome on`).
- Hold METRO for the **METRO layer**: SELECT (or D4, E4, F4) picks the time signature, 4/4, 3/4 or 6/8; KNOB 1 is the
  click's level. OPT + SELECT sets the level without opening the layer.
- The click is not part of the USB recording.

With Options > MIDI Clock set to In, the tempo follows the incoming clock instead ([MIDI and USB](#15-midi-and-usb)).

## 10. The looper

The looper records **what you play on the root keys, as chords**: which root, which chord, when and for how long. It
does not record audio. When the loop plays, the chords go through the instrument again, so the loop follows the sound
you choose now, the voicing on KNOB 1, the performance mode and the bass. The tempo changes its speed, never its
pitch.

### Recording and playing

| Button | What it does |
| --- | --- |
| **REC** tap | record, in the way the record mode says (below). While recording: ends the take. While armed or counting in: cancels |
| **LOOP** (PLAY) tap | play / stop. While recording: ends the take |
| **REC** held | undo the last layer (the screen shows how many layers are left); the first layer stays: `nothing to undo` |
| **LOOP** held | the PLAY menu |

With the settings a new FM-1 has (length Free, mode Overwrite):

1. Tap REC. REC lights, the top line reads `Rec · Overwrite`, the message says `rec: play to start`.
2. Play. The first chord starts the take; REC blinks and the top line counts bars and beats (`Rec 2.3`).
3. Tap REC or LOOP. `loop recorded`: the loop plays at once, at the length you played.
4. LOOP stops and starts it. When it stops it is saved to its slot (`saved to slot 1`).

Tapping REC again starts over: the loop stops and REC is armed, and the old loop is replaced only when you play the
first chord. Tap REC once more before that and it is `cancelled`, the old loop intact.

The lights and the top line tell you where you are: REC lit is armed, REC blinking is recording, LOOP lit means the
slot holds a loop, its green light means it plays. The small dial at the right of the top line fills once per loop;
it carries a red dot while REC is armed (steady) or recording (blinking).

### The PLAY menu

Hold LOOP. The menu stays open like any layer (OCT-, OCT+, HOME or holding LOOP again closes it), and with Keys set to Play you can keep
playing and recording with it open.

<img src="manual/play-menu.png" width="240" alt="The PLAY menu: Loop 1, empty; ten slot boxes; the record mode Overwrite; the length Free; cells Quantize, Count-in, Level, Keys">

| Control | In the PLAY menu | Default |
| --- | --- | --- |
| PRESETS | the **slot**, 1 to 10 (the strip at the top: a filled box holds a loop) | 1 |
| ALGORITHM | the **record mode**: Overwrite, Advance, Overdub, Replace, Step | Overwrite |
| SELECT | the **length** of the next take: Free, 1, 2, 4, 8 or 16 bars | Free |
| KNOB 1 | **Quantize**: none, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | none |
| KNOB 2 | **Count-in**: On or Off | On |
| KNOB 3 | **Level**: how loud the loop plays, 0 to 100 (0 mutes it) | 100 |
| KNOB 4, or OPT tap | **Keys**: Play or Loops (below) | Play |

While a loop records or plays, the middle of the menu is a timeline: the bars, a line of marks for each layer, the
playhead. All of these settings are remembered.

- **Length.** Free: REC arms, the first chord starts the take, and it is as long as you play. With a number of bars,
  REC starts a one-bar count-in (the screen counts the beats down inside a red ring, with a click), then records
  exactly that many bars and plays by itself. Ending such a take early rounds it up to whole bars.
- **Count-in** Off skips the count: with a number of bars the take then starts the moment you tap REC.
- **Quantize** moves the starts of the chords to the nearest grid point when the take ends.
- **Keys = Loops** turns the root keys into shortcuts while the menu is open (OPT is lit): the white keys D4 to F5 pick
  slots 1 to 10, D#4 held for a second clears the loop (`hold to clear`, then `loop cleared`), F#4 is undo. With
  Keys = Play the keys play as usual.

### Record modes

The record mode decides what a REC tap does when nothing is being recorded.

| Mode | REC on an empty slot | REC when the slot holds a loop |
| --- | --- | --- |
| **Overwrite** | a new take | a new take that replaces the loop (it is replaced when the take starts) |
| **Advance** | a new take | moves to the next slot (after 10 comes 1), saves the loop it leaves, and records a new take there |
| **Overdub** | a new take | arms an overdub (it starts the loop if it was stopped): the next chord opens a new layer on top. REC ends the layer; LOOP ends it and stops |
| **Replace** | a new take | as Overdub, but while you hold a chord the older chords under it are taken out. Undo brings them back |
| **Step** | step entry | step entry (the loop there is replaced when you finish) |

While an overdub is armed or running the top line reads `Dub 3.2` (`Rep 3.2` for Replace).

**Step entry** writes chords one step at a time, with no clock running. The steps are the Quantize grid (1/16 when
Quantize is none). The PLAY menu shows the grid and the cursor.

- Play a chord and let go of every key: the chord is written at the cursor and the cursor moves on.
- OCT+ is a rest (the cursor moves on). OCT- goes back one step; nothing is erased.
- REC finishes and leaves the loop stopped (`steps recorded`); LOOP finishes and plays it.
- With LOCK on and a chord latched, letting go of the root does **not** move the cursor, because a chord key still
  counts as down. Use OCT+ to move on after each chord, or enter steps with LOCK off.

### Slots and saving

There are ten slots. One loop is loaded at a time, and a slot keeps what was recorded in it:

- A loop is saved to its slot **when it stops**, and before you move to another slot. You do not need to save by
  hand, and the loop is still there after switching off. A loop that is still playing when you switch off loses what
  changed since it last stopped, so stop it first.
- Choosing another slot (PRESETS in the PLAY menu, or a white key with Keys = Loops) loads it: at once when the loop
  is stopped, at the end of the cycle when it plays (`loop 2: next cycle`).
- Clearing a loop empties its slot.

**SAVE held** (outside the save dialog) shows `Loops` for as long as you hold it, for the rarer jobs: the white keys
D4 to F5 choose a slot, SELECT or KNOB 1 chooses Save, Load or Delete, and OCT+ does it. Save copies the loop into
the chosen slot (while the loop plays it is written at the stop). Load of the slot you are on throws away its unsaved
changes. Delete empties a slot (`stop to delete` while the loop plays); if it is the slot you are on, the loop stays
loaded until you move to another slot.

Limits: 512 chords in a loop over at most 32 layers; reaching the limit ends the take (`loop full`).

With MIDI Clock set to Out the FM-1 sends start and stop with the loop; set to In, the loop follows incoming start,
continue and stop.

More detail: [LOOPER.md](LOOPER.md), [LOOPER-MODES.md](LOOPER-MODES.md).

## 11. Sounds

ChoralRoot plays two sounds at once: the **chord sound** (PRESETS) and the **bass sound** (ALGORITHM). Each belongs
to one of eleven engines.

### The engines

| Engine | What it is | Factory presets |
| --- | --- | --- |
| **FM6** | six-operator FM that plays DX7 voices | 25 |
| **FM TONE** | four-operator FM in the style of the Digitone | 16 |
| **VA** | ChoralRoot's virtual analog: four oscillators, a morphing filter, four envelopes, four LFOs, a modulation matrix, stereo | 25 |
| **CZ-1** | Casio CZ-1 phase distortion, with Casio's own 64 tones | 64 |
| **PHASE** | phase distortion with two lines | 6 |
| **LOFI** | a chip voice: pulse, triangle, saw, noise and small wave tables | 5 |
| **VOICE** | a formant oscillator: sung vowels | 4 |
| **TRIO** | three oscillators with ring modulation and sync into a filter, after early home-computer sound chips | 5 |
| **WHEEL** | a tonewheel organ with nine drawbars | 5 |
| **PHYS** | physical models: bars, plates and strings | 9 |
| **NOISE** | noise sources through a resonant filter | 4 |

This is the order in which the instrument shows them. At power-on the chord sound is the one you last played (on a
new unit FM6 `TINE EP`), and the bass sound is VA `DEEP SUB` until you choose another.

### Presets: one pool per engine

Every engine has one list, its **pool**:

- `00 INIT`, the engine's plain starting sound;
- then the factory presets;
- then the sounds you added.

<img src="manual/sound-popup.png" width="240" alt="The sound read-out: 02, FM BELL, FM6 · 02/25, two of the 25 stripes lit">

**PRESETS** turns through the pool of the chord sound's engine and wraps round. The read-out is the place, the name,
and `FM6 · 02/25`: the engine, the place and the number of presets in the pool (`00 INIT` is not counted). The
stripes under it are the presets: none is lit on INIT, all of them on the last preset. A small square after the name
marks a factory preset you have saved over. **ALGORITHM** does the same for the bass, with `00 off` in front of the
pool.

When you step to another preset the stripes move from where they were: going up, the ones in between light one after
another; going down, they go out. Only the first read-out after switching on, and the first after changing the engine,
fills from the left. A long way (from the last preset round to INIT, say) takes under half a second.

**OPT + PRESETS** changes the chord sound's engine: hold OPT, turn PRESETS until the engine you want is in the
middle (the screen shows how many presets it has), let go. You land on the sound you last used in that engine.

### The engine picker

Hold **EDIT**. The engine picker opens and stays (EDIT blinks). It is a preview: nothing is final until you confirm.

| Control | In the engine picker |
| --- | --- |
| the white root keys D4 to G5 | the eleven engines, in the order of the table above (D4 = FM6 ... G5 = NOISE). The sound moves to that engine and keeps its envelope, effects and mix |
| SELECT | the same, one engine at a time |
| KNOB 1 (or PRESETS) | the engine's pool, as PRESETS |
| KNOB 2 | INIT: the engine's plain starting sound (`init sound`) |
| KNOB 4 | what the root keys do here: `roots: engines` or `roots: play` (to hear the preview). Remembered |
| OCT+, EDIT tap or EDIT held again | keep what you chose |
| OCT- or HOME | cancel: the sound as it was before |

Held from the view, EDIT picks for the chord sound. For the **bass engine**, open the bass sound's editor first (BASS
held + EDIT) and hold EDIT there.

### Saving

Tap **SAVE**. (With BASS held: the bass sound. In the editor: the sound being edited.)

1. Choose **Overwrite** or **Save as new** with KNOB 1 or SELECT. The current name is under it, with `*` if you have
   changed the sound. The dialog opens on Overwrite when the sound is one of yours or has been changed, otherwise on
   Save as new.
   - **Overwrite** saves over the current preset at once and keeps its name. Over a factory preset this is allowed:
     your version takes its place in the pool and the factory one can be brought back. `00 INIT` cannot be
     overwritten; there Overwrite does a Save as new.
   - **Save as new** adds the sound at the end of the engine's pool and asks for a name.
2. **OCT+** or SAVE again takes the choice. **OCT-** or HOME cancels.
3. Naming (Save as new only), up to 12 characters:

   | Key | Types |
   | --- | --- |
   | the white roots D4 to G5 | `ABC` `DEF` `GHI` `JKL` `MNO` `PQRS` `TUV` `WXYZ` `0123` `4567` `89-.`; tap the same key again within 0.8 s for the next character of its group |
   | D#4 | a space |
   | F#4 | deletes the last character |
   | KNOB 2 | steps the last character through the whole set |
   | OCT+ or SAVE | saves (`saved FM6 26`) |
   | OCT- or HOME | cancels |

   The name starts as the current one, in grey; the first key you type replaces it.

To **delete** a sound you added, or to **reset** a factory preset you saved over: open the save dialog on it and hold
SAVE for a second. The screen asks `delete?` or `reset to factory?`; OCT+ does it, OCT- keeps it.

There is room for **32 saved sounds** in all, shared by the engines and the two parts (`no free slot` when they are
used up). A saved sound holds the engine's whole patch, its envelopes and modulation and its four effect amounts.
Changes you have not saved are lost when you load another sound or switch off.

Level and Pan (the editor's MIX group) are the exception: they belong to the part, not to the sound. They stay as
they are when you change sound, and they are back at their normal values when you switch on.

### Sounds from outside

- An FM6 sound can receive a DX7 voice, and a CZ-1 sound a Casio tone, as SysEx over USB MIDI; save it afterwards.
  See [FM6.md](FM6.md) and [CZ1.md](CZ1.md).
- The installer page and the device manager move single sounds to and from files ([MIDI and USB](#15-midi-and-usb),
  [SOUNDS.md](SOUNDS.md)).
- A sound saved on an older version with the retired ANALOG engine loads on the VA
  (`ANALOG retired: on the VA`).

More detail: [PRESETS.md](PRESETS.md).

## 12. The sound editor

The editor is the one dense place in ChoralRoot. It takes the whole screen, with no footer.

### In and out

| Gesture | Result |
| --- | --- |
| **EDIT** tap | the editor on the chord sound (EDIT blinks) |
| **BASS** held + **EDIT** | the editor on the bass sound. BASS has to be down; with the BASS layer merely open, EDIT opens the chord sound |
| **OPT** held + **EDIT**, in the editor | switch between the chord sound and the bass sound |
| **EDIT** tap or **HOME**, in the editor | leave |

While the editor is open the keys play as usual (chord block, roots, OCT- / OCT+, PANIC), MASTER is the volume,
ALGORITHM is the bass sound, and PERF works as outside. KEY, FX, BASS, LATCH, METRO, LOOP and REC are not available:
their buttons are the editor's groups.

### Groups, screens, lanes

<img src="manual/editor-osc.png" width="240" alt="The editor on the VA sound LUSH PAD: the four oscillators as rows, OSC 1 active, columns Wave, Level, Coarse, Fine"> <img src="manual/editor-filter.png" width="240" alt="The editor's FILTER screen: the filter curve over two rows of four cells">

- Seven buttons are the **groups**. The top row follows the signal: OSC, FILT, ENV, LFO. The bottom row is the output
  stage: MOD, FX, MIX. The current group's button is lit.

  | Printed | FX | SEL | ENV | LFO | SEQ | PLAY | REC | GLO |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | In the editor | **OSC** | **FILT** | **ENV** | **LFO** | **MOD** | **FX** | **MIX** | **SHIFT** |

- A group has one or more **screens**. Tapping the group's button again goes to its next screen.
- A screen has one or more **lanes**: the four values that are on KNOB 1 to KNOB 4 right now. The active lane is
  drawn in the knob colours; the rest is grey.
- **SELECT** steps through the lanes, and on past the last lane into the next screen, so SELECT alone reaches
  everything in a group.
- **KNOB 1 to KNOB 4** change the active lane's four values. One click is about 5 % of the range, or one step of a
  list.
- **SHIFT** (printed GLO) makes one click one unit. Tap it to latch it (`fine` in the top line, the button lit); hold
  it for fine steps only while held.
- The top line has the sound's name (with `*` once changed, and `· BASS` in orange on the bass sound) and, at the
  right, where you are (`OSC 2 · A`, `ENV 2 · filter`, `MOD 3`).
- **PRESETS** in the editor opens the engine picker as a preview ([Sounds](#11-sounds)).
- **SAVE** opens the save dialog for the sound being edited and returns to the editor.
- A layer opened over the editor (PERF held, EDIT held, HOME held, SAVE held) closes back to the editor.
- The editor remembers the group you were in and each group's screen and lane, until you switch off.

Two groups are the same for every engine:

| Group | Lanes | KNOB 1 to KNOB 4 |
| --- | --- | --- |
| **FX** | one | Drive · Chorus · Delay · Reverb: the sound's four effect amounts |
| **MIX** | two | Level · Pan · Voice · Glide, and Trans · Detune · Priority · Gl. mode |

The sections below list what the other groups hold for each engine. They name the screens; for what each parameter
does, follow the link.

### VA

| Group | Screens |
| --- | --- |
| OSC | (1) the four oscillators as rows: Wave · Level · Coarse · Fine. (2) the same four: Mode · Shape · Key trk · Sync/Ring. (3) the mixer: the four levels as tall bars |
| FILT | the filter curve over two lanes: Cutoff · Reso · Ftype · Env amt, and Key trk · Spread · Drive |
| ENV | four screens, `ENV 1 · amp`, `ENV 2 · filter`, `ENV 3 · free`, `ENV 4 · free`, each the envelope's shape over Attack · Decay · Sustain · Release and Hold (ENV 1 also Velocity) |
| LFO | (1) the four LFOs as rows: Rate · Wave · Depth · Fade. (2) their Sync. (3) `VOICE`: Uspread, the stereo spread of a unison |
| MOD | the modulation matrix: eight rows of Source · Dest · Amount |

**Quick modulation** (the VA only): hold **ENV** or **LFO** and turn a knob. The envelope or LFO you last looked at
(ENV 2 or LFO 1 if none yet) now modulates the parameter under that knob, and the turn sets the amount; the cell
shows `LFO1 +20`. A modulated parameter carries a small mark in its cell (yellow for an envelope, red for an LFO,
white for several sources). **OCT- held + a knob** removes every modulation of that parameter (`cleared`). The
matrix has eight slots (`matrix full`); a parameter that cannot be modulated says `not modulatable`.

More: [VA.md](VA.md).

### FM6

| Group | Screens |
| --- | --- |
| OSC | the six operators as rows, three screens: (1) Level · Coarse · Fine · Detune. (2) Mode · Vel sens · AM sens · Rate scl. (3) `SCALE`: Break pt · L depth · R depth · Curve |
| FILT | `ALGO`: Algo · Feedback · Transp · Osc sync |
| ENV | seven screens: `ENV 1` to `ENV 6`, one per operator, each the DX7 envelope's shape over Rate 1 to 4 and Level 1 to 4; then `PITCH EG` |
| LFO | `LFO`: Speed · Delay · PM depth · AM depth, and Wave · Sync · PM sens |
| MOD | (1) `FUNC`: Bend · Porta · Engine, and Step · Porta md · Gliss · DX vel. (2) `CTRL`: the wheel, foot, breath and aftertouch ranges and targets |

More: [FM6.md](FM6.md).

### FM TONE

| Group | Screens |
| --- | --- |
| OSC | (1) `SYN 1`, under a drawing of the algorithm: Algo · Ratio C · Ratio A · Ratio B, and Harm · Dtune · Feedback · Mix. (2) `SYN 2`: Offset C · Offset A · Offset B1 · Offset B2 |
| FILT | (1) `FILTER`, under the filter curve: Attack · Decay · Sustain · Release (the filter's envelope), and Freq · Reso · Type · Env depth. (2) `FILTER 2`: Env delay · Key track, and Base · Width |
| ENV | (1) `ENV A/B`, the two operator envelopes: Attack · Decay · End · Level for A and for B. (2) `ENV 2`: Delay · Trig · Reset for A and B, Phase, Velocity. (3) `ENV 3`: A Key · B1 Key · B2 Key. (4) `AMP`: Attack · Decay · Sustain · Release, and Level · Pan · Drive |
| LFO | three screens, `LFO 1` to `LFO 3`: Speed · Mult · Fade · Dest, and Wave · Phase · Trig · Depth |
| MOD | four fixed routes with an Amount each: ENV to FILT, ENV to PITCH, ENV to SHAPE, LFO to AMP |

Ratio B is two values in one cell: a click steps the upper one (B2), a click with SHIFT the lower one (B1); the top
line then reads `fine · B1`. More: [QUAD.md](QUAD.md) (the engine's working name was QUAD).

### CZ-1

| Group | Screens |
| --- | --- |
| OSC | (1) the two lines as rows: Wave · Wave · Window · Level. (2) `DETUNE`: Sign · Oct · Note · Fine. (3) the mixer: the two lines' levels |
| FILT | `DCW`: the key follow and velocity of the DCW (timbre) and of the DCA (volume), for each line |
| ENV | eighteen screens: for each of `PITCH 1`, `DCW 1`, `DCA 1`, `PITCH 2`, `DCW 2`, `DCA 2` the eight-step envelope on three screens: Rate and Level 1 to 4, Rate and Level 5 to 8, then Sustain and End |
| LFO | `VIB`: Wave · Delay · Rate · Depth |
| MOD | `TONE`: Line · Mod · Oct |

More: [CZ1.md](CZ1.md).

### PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE

These seven have one screen in each group:

| Group | Holds |
| --- | --- |
| OSC | the engine's first four parameters (the screen carries the engine's own title, such as `PHS`) |
| FILT | its second four (again under the engine's own title) |
| ENV | Attack · Decay · Sustain · Release, under the envelope's shape |
| LFO | Rate · Wave · Vibrato · Wah |
| MOD | four fixed routes with an Amount each: ENV to FILT, ENV to PITCH, ENV to SHAPE, LFO to AMP |

The whole editor: [EDITOR.md](EDITOR.md).

## 13. Views

The view is what the screen shows when no layer, menu or editor is open. There are five.

| View | Shows |
| --- | --- |
| **Chord** (default) | the chord name large, the voiced notes under it; while an arpeggio or a pattern plays, the notes on a line with the sounding one hopping along ([Performance modes](#7-performance-modes)) |
| **Keyboard** | the chord name and a keyboard with the chord's notes lit |
| **Notes** | the chord name small and the notes as a list |
| **Geek Out** | the chord, its notes and keyboard, and status lines: voicing and play style, bass and tempo, the audio load, the second core (`c1 off`, `c1 ok ...`), the transposition |
| **Scope** | the waveform of the output, with the chord name small above it |

- **HOME** tap goes back to the view from anywhere. On the view itself it steps to the next view.
- **HOME held** for 0.6 s opens the view menu: SELECT or the white keys D4 to A4 choose the view, KNOB 1 (or B4)
  switches **Lock** on and off; OCT+, OCT-, HOME or holding HOME again closes it. With Lock on, a HOME tap never changes the view; it only goes home.
- Options > View sets the same thing.

Options > Motion (Full, Calm, Off) sets how much the screens animate; the hopping note of an arpeggio is not an animation and shows with Off too. The sound editor never animates. The other four views never change with the performance mode.

## 14. Options

Tap **OPT** (printed GLO). One setting fills the screen:

<img src="manual/options.png" width="240" alt="An Options page: Play Style, value Simple, the next entry Extension Addition under it, the battery at the top right">

- **SELECT** moves to the next or previous setting. The white root keys jump to the first eleven.
- **KNOB 1** changes the value. Changes act at once.
- **OPT**, **HOME**, **OCT-** or **OCT+** closes the Options.

| Setting | Values | Default | What it does |
| --- | --- | --- | --- |
| **Play Style** | Simple, Advanced, Free | Simple | what chord keys do while a root is held ([Play styles](#play-styles)) |
| **Extension Addition** | Add Note, Play Chord | Add Note | in Advanced and Free: an extension adds its note, or replays the chord |
| **Secret Chords** | Off, Simple, All | Off | two chord types together make a third chord ([Secret chords](#secret-chords)) |
| **Velocity** | 1 to 127 | 100 | the velocity of every key, for the sound and for MIDI out |
| **Bass Behaviour** | Chords Only, Unison Bass, Bass Single Notes, Solo | Chords Only | what the bass plays ([Bass](#6-bass)) |
| **Single Notes** | Full Octave, Split Oct, Split Bass | Full Octave | Split Oct: single notes below the split point sound an octave lower. Split Bass: the keys below it play the bass part ([Key Mode](#key-mode)) |
| **Split Point** | C to B | F | where that split is on the Chords keyboard; reads `(Split off)` while Single Notes is Full Octave. The Full keyboard splits at its second F |
| **MIDI Perform** | Off, ch 1 to ch 16 | ch 1 | the channel the chord sound's notes go out on, and come in on |
| **MIDI Bass** | Off, ch 1 to ch 16 | ch 2 | the same for the bass |
| **MIDI Raw Chord** | Off, ch 1 to ch 16 | Off | the channel for the plain chord, without the performance mode. From Off the first step is ch 1; choose a channel of its own (the Orchid uses 3) |
| **Raw Chord Sound** | Off, On | Off | On: the plain chord also plays on the chord sound, under an arpeggio or pattern. With no performance mode on, every note then sounds twice |
| **MIDI Clock** | Off, Out, In | Out | Out: sends clock, and start / stop with the loop. In: follows them |
| **View** | Chord, Keyboard, Notes, Geek Out, Scope | Chord | the view ([Views](#13-views)) |
| **Motion** | Full, Calm, Off | Full | screen animation |
| **LEDs** | Glow, Stock | Glow | Glow: idle keys glow dim, active ones are bright. Stock: the reverse |
| **Hold Time** | 300, 400, 500, 600 ms | 400 ms | how long a hold is |
| **USB Record** | Off, On | On | On: the computer can record the FM-1 ([USB audio recording](#usb-audio-recording)) |
| **USB Level** | Master, Fixed | Master | Master: the recording follows the MASTER knob. Fixed: it is recorded at full level and MASTER only sets what you hear |
| **Dual Core** | Off, On | Off | On: the bass plays on the chip's second core (below). Takes effect after a restart |
| **Version** | | | the firmware's version |
| **Calibrate** | | | OCT+ starts the panel calibration ([Calibration](#calibration)) |
| **Reset Settings** | | | OCT+ twice puts every setting back to its default (below) |

In SAFE MODE two more entries appear at the end, Safe Mode and Flash Data ([SAFE MODE](#safe-mode)).

**Reset Settings** is the last entry: turn SELECT to the end of the list. It puts the FM-1 back to the settings a
new one has, in one step, which is the quick way out when an option you do not remember setting makes it behave
strangely, and the way to make sure two people are looking at the same instrument. Press **OCT+**: the entry reads
`OCT+ again: reset`. Press **OCT+** again and it is done: `settings reset` shows, and you are back on the view.
Anything else cancels: OCT-, HOME, turning SELECT to another entry, closing the Options.

- **Reset:** everything in the Options (Play Style back to Simple, Motion, LEDs, Hold Time, the MIDI channels and
  clock, USB Record and USB Level, Dual Core and the rest), Key Mode with its tonic, scale and Transpose, Single
  Notes and the split point, the KEY layer's keys, Key plays and Keyboard, the LOCK layer's two settings, the
  performance mode with all its parameters and whether it is on, LATCH, the effects being on, the tempo, the
  metronome, the looper's options (length, record mode, quantize, count-in, level, what its keys do), the bass
  behaviour and register, the view and its lock, and which sound each part plays: the chord part is on TINE EP
  again, the bass on DEEP SUB, and every engine is back at its first preset.
- **Kept:** the sounds you saved and every patch and bank you loaded, the loops in their slots and the slot you were
  on, and the panel calibration. Reset Settings never touches those; SAFE MODE's Flash Data is the entry that erases
  data, and Calibrate the one that changes the panel table.
- **What happens to what you were doing:** it ends like a PANIC: every note stops, a playing loop stops (it stays in
  its slot; a take you were recording is dropped), LOCK and LATCH go off, the octave shift and the voicing return to
  0, the bass is off as after switching on, and a change to a sound you had not saved is gone.
- **Dual Core** is the one setting that waits for a restart: if it was On, the message reads
  `settings reset · restart` and the second core keeps running until you switch off and on.

**Dual Core.** With Dual Core Off, the default since 0.16, ChoralRoot runs on one core as the stock firmware does, and
the chord and the bass share one budget of voices: a very full chord with the bass can cost a voice. With it On, the
bass part plays on the second core with its own voices, so a full chord and the bass both sound whole. It is off by
default because firmwares that run both cores are reported to crash on some FM-1s; ChoralRoot raises the core voltage
to the chip maker's level and falls back to one core if the second misbehaves, but the margin varies from unit to
unit. Turn it on if you want the extra voices; if your FM-1 ever crashes or resets while playing, it is the first
thing to turn off. The value reads `On · restart` until you switch off and on. The Geek Out view shows the state:
`c1 off`, `c1 ok ...`, or `c1 gave up · Options>Dual Core`. An update from 0.14 or 0.15, and a backup made on them,
sets it Off; switch it on again if you used it. More: [DUALCORE.md](DUALCORE.md).

### What the FM-1 remembers

Settings are stored by themselves a moment after the instrument falls quiet, and never while a loop plays. Give the
FM-1 a second or two of silence before you switch off.

| Remembered when you switch off | Not remembered |
| --- | --- |
| everything in Options | the bass being on (it always starts off) |
| Key Mode, the tonic, the scale, Transpose | LOCK and what it latched |
| the performance mode, its parameters, and whether it is on | the octave shift |
| LATCH, and the effects being on or off | the voicing on KNOB 1 |
| the tempo, the metronome and its time signature | the selected effect and the FX layer's KNOB 1 to KNOB 3 |
| the bass behaviour and register | the parts' Level and Pan, the bass level |
| the looper's settings and its slot; the KEY layer's keys (pick or play), Key plays and Keyboard; the LOCK layer's two settings | changes to a sound you did not save |
| which sounds the two parts play, and each engine's last place | where you were in the editor |
| the view and its lock | |
| saved sounds; loops (each time a loop stops) | |

More: [SETTINGS.md](SETTINGS.md).

## 15. MIDI and USB

Over USB the computer sees one device, **ChoralRoot FM-1**: a MIDI port of that name, and an audio input,
**ChoralRoot In**.

### MIDI out (USB)

| What | Channel (Options) | Carries |
| --- | --- | --- |
| Perform | MIDI Perform, ch 1 | what the chord sound plays: the chord, or the performance mode's notes |
| Bass | MIDI Bass, ch 2 | the bass line |
| Raw Chord | MIDI Raw Chord, Off | the plain chord as a block, whatever the performance mode does |

With MIDI Clock set to Out (the default) the FM-1 also sends MIDI clock at the tempo, a start when the loop starts
playing and a stop when it stops. PANIC sends all notes off (CC 123).

### MIDI in (USB and the TRS jack)

| Message | Effect |
| --- | --- |
| notes on the MIDI Perform channel | play the chord sound directly, as an ordinary synth (no chords are built) |
| notes on the MIDI Bass channel | play the bass sound |
| program change | the sound's place in its engine's pool (chord: 0 = INIT; bass: 0 = off, 1 = INIT, and so on) |
| CC 7 | the part's level |
| CC 91, 93, 94 | the part's reverb, chorus and delay amounts |
| CC 64, pitch bend, CC 1, aftertouch | sustain, bend, and the modulation sources of the engines that use them |
| clock, start, continue, stop | with MIDI Clock set to In: the tempo follows the clock and the loop follows start / continue / stop |

A part whose channel is Off ignores MIDI in.

### USB audio recording

**ChoralRoot In** is a class-compliant audio input: no driver is needed. It has six channels at 44.1 kHz, 16 bit.

| Channels | Signal |
| --- | --- |
| 1-2 | Master: what you hear, without the metronome click |
| 3-4 | the chord sound alone, before the reverb, chorus and delay, at half level |
| 5-6 | the bass sound alone, the same way |

- Set the computer or the DAW to 44.1 kHz.
- Options > USB Level = Fixed records at full level while MASTER only turns the speaker and headphones down.
- Options > USB Record = Off removes the audio input. When you change it the FM-1 leaves USB for about a second
  (`USB RECONNECTING`); the MIDI port goes with it and comes back.
- The computer cannot play audio through the FM-1.

More: [USB-AUDIO.md](USB-AUDIO.md).

### The web device manager

A page on the ChoralRoot site, next to the installer
(<https://quixotic7.github.io/ChoralRootFM1/webapp/installer/manager.html>, Chrome or Edge), that shows what is
stored on the FM-1 beside a library on the computer:

- the pool of every engine as the FM-1 has it, and the ten loop slots;
- a library: sounds kept in the browser, files, and the factory sounds as packs;
- Place (library to FM-1), Keep (FM-1 to library), Swap, Rename, Delete, drag to reorder, Undo;
- selecting a row on the FM-1 side switches the FM-1 to that sound, so you hear it (this needs 0.16);
- an on-screen keyboard, and Back up everything / Install a backup.

It will not write while a loop plays ("Stop the loop on the FM-1 first"). Adding `?sim=1` to the address runs it
against a simulated FM-1. More: [DEVICE-MANAGER.md](DEVICE-MANAGER.md).

### Backup and restore

The installer page (<https://quixotic7.github.io/ChoralRootFM1/>) has **Back up** and **Restore**:

- A backup is one `.json` file with everything stored on the FM-1: the settings, the 32 saved sounds with their
  patches, the ten loops, the FM6 voice bank and the CZ-1 banks.
- Restore checks the file, writes it back and restarts the FM-1. Stop any loop first.
- Install saves a backup before it writes, unless you skip it.
- The page's Sounds section exports, imports, renames and deletes single sounds as files (FM6 voices also as DX7
  `.syx`, CZ-1 tones as Casio dumps).

The same from the command line: `tools/fm1_install.py` (see the [README](../README.md) and [SOUNDS.md](SOUNDS.md)).

## 16. Updating and recovery

### Before every update

The FM-1 updates on its own battery, and the installer cannot read how full it is.

1. **Charge the FM-1 fully.**
2. Plug it **straight into the computer** with a cable that carries data. No hub.
3. Keep the computer awake.
4. Leave the cable alone until the installer says Done.

### Updating

Open <https://quixotic7.github.io/ChoralRootFM1/> in Chrome or Edge, connect the FM-1 and press **Install**. The page
saves a backup first, switches the FM-1 to update mode, writes the firmware and restarts it. Your settings, sounds and
loops are kept. Options > Version shows what is installed.

Updating to 0.16 sets Options > Dual Core to Off.

### If an update is interrupted

From 0.16 on, ChoralRoot checks at every start that the firmware was written completely. If an update was cut off
(the cable, the battery, the computer going to sleep):

1. Switch the FM-1 off and on again.
2. Some of the key lights flash **three short times** and the screen stays black. The FM-1 is in update mode.
3. Open the installer and press **Install** again. It finishes the write.

If the computer side merely stopped while the FM-1 stayed on, the FM-1 is still in update mode: replug the cable and
press Install again.

The update **to** 0.16 from an older version is the last one without this protection during its first second,
because the part that does the checking is itself being written then.

### Power-on and held gestures

| Gesture | Result |
| --- | --- |
| **OCT- alone, held while switching on** | update mode on purpose: three short flashes, black screen. Open the installer and Install, or switch off and on to start ChoralRoot again (nothing was written) |
| **OCT- and OCT+ held while switching on** | the panel calibration (below). Let go as soon as the screen says `let go` |
| **OCT- and OCT+ held for 5 s while ChoralRoot runs** | after 2 s a red countdown (`update mode: let go to cancel`); at 5 s the FM-1 goes to the chip's own USB boot: a black screen, and the computer shows a disk named `WL82 UBOOT1.00`. The installer cannot use that mode. Switch off and on to start ChoralRoot again |

Holding OCT- alone while switching on is new in 0.16. Hold it until the lights have flashed. Recovery after an
interrupted update does not depend on the key.

**One long flash instead of three** means the FM-1 went to the chip's USB boot (the `WL82 UBOOT1.00` disk) instead of
update mode. Switch it off for 10 seconds and try once more: a second OCT- start straight after a first one ends
there. If it is one long flash again, no update loader is stored on the unit; switch off and on to start ChoralRoot.

### Calibration

The calibration teaches ChoralRoot which physical button and knob is which. You only need it if buttons or knobs do
the wrong jobs, or a knob turns backwards.

1. Start it with Options > Calibrate, OCT+ (or hold OCT- and OCT+ while switching on).
2. The screen names each control in turn: press the 14 buttons one by one as it asks, then turn each of the seven
   knobs to the right. The names are the printed ones; the SEL button is called `SCL`.
3. At `done`, OCT+ keeps the result and OCT- throws it away. Thirty seconds without input cancels.

### SAFE MODE

If ChoralRoot crashes twice in a row within 30 seconds of starting (switching off and on does not count), it starts
in SAFE MODE: a yellow `SAFE MODE` screen, and `Safe mode` in red on the top line.

- It plays with the factory sounds and default settings. What is stored on the FM-1 (settings, saved sounds, loops)
  is ignored and left untouched, and nothing can be saved (`safe mode: no saving`).
- The installer and Back up still work, so you can save a backup.
- Switching off and on tries the stored data again.
- Options opens on two extra entries. **Safe Mode** is information. **Flash Data**, OCT+ twice, erases the stored
  settings, sounds and loops (not the firmware) and restarts.
- If SAFE MODE itself crashes twice in a row, the FM-1 falls back to the chip's USB boot (the `WL82 UBOOT1.00`
  disk); switch off and on.

### If the FM-1 stays dark

A black screen with a `WL82 UBOOT1.00` disk on the computer, or nothing on USB at all, is beyond this manual: follow
"If the FM-1 no longer starts" in the [README](../README.md) and the checklist in
[INSTALL-COMPAT.md](INSTALL-COMPAT.md), which lead to
[MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker). Do not format the disk.

### Back to the stock firmware

The installer's "Return to official V15" section installs M-VAVE's own firmware, which you download yourself from
M-VAVE. It saves a backup first: the stock firmware cannot use ChoralRoot's sounds, loops or settings. To bring them
back, install ChoralRoot again and press Restore.

More: [BOOT-SAFETY.md](BOOT-SAFETY.md), [INSTALL-COMPAT.md](INSTALL-COMPAT.md), the
[0.16 release notes](releases/0.16.md).

## 17. Troubleshooting

| What you see or hear | Likely cause | What to do |
| --- | --- | --- |
| Keys or knobs do something unexpected | a layer, the Options or the editor is open (a button blinks, or OPT is lit) | HOME |
| The FM-1 behaves strangely and you cannot tell which setting it is | some option is not at its default | Options > Reset Settings, OCT+ twice: every setting back to default; your sounds, loops and the calibration stay ([Options](#14-options)) |
| A layer will not go away | layers stay open by design | OCT- or HOME |
| Plain root keys play chords | Key Mode is on (KEY lit, `Key: C`) | tap KEY |
| Chords come out in the wrong key | Transpose is set (Geek Out shows `Trans`) | KEY layer, KNOB 3 back to 0 |
| Low single notes jump down an octave | Single Notes is Split Oct | KEY layer, KNOB 4: Full |
| Some roots play a bass note instead of a chord | Single Notes is Split Bass | KEY layer, KNOB 4: Full |
| The chord keys play notes, B3 does not lock | Keyboard is Full | KEY layer, SELECT to page 2, KNOB 2: Chords |
| Some keys do nothing | Key Mode is on with Key plays = Notes, or on the Full keyboard: they are outside the scale | tap KEY, or KEY layer page 2 |
| Chord keys stay on | LOCK is on (B3 lit) | tap B3 |
| Notes keep sounding | LATCH is on, or Strum's Hold | tap LATCH; PANIC stops everything |
| The chord sound is silent, the bass plays | Bass Behaviour is Solo (`Bass Solo`) | BASS layer, another behaviour, or tap BASS |
| No bass | the bass is off after every power-on | tap BASS |
| The bass layer's keys make no sound | in that layer the white keys pick the behaviour | close the layer to play |
| The loop plays but is silent | its Level is 0 | PLAY menu, KNOB 3 |
| REC replaced my loop | the record mode is Overwrite | PLAY menu, ALGORITHM: Overdub. Before the first chord, REC again cancels |
| In step entry the cursor does not move on | LOCK is on with a chord latched | OCT+ after each chord, or LOCK off |
| HOME does not change the view | the view is locked | HOME held, KNOB 1: Lock Off |
| The tempo moves by itself | MIDI Clock is In | Options > MIDI Clock |
| The effects settings changed after PRESETS | each sound brings its own amounts | save the sound with your amounts |
| My reverb type or delay time is back to normal | the effect parameters are not stored | set them again after switching on |
| My edit is gone | unsaved changes are lost with the next sound or at power-off | SAVE |
| `no free slot` | all 32 saved sounds are used | delete one (SAVE held in the save dialog), or use the device manager |
| A setting did not survive switching off | settings are written a moment after the instrument falls quiet, never while a loop plays | stop the loop, wait a second or two |
| The bass level, or a part's Level or Pan, is back to normal | they are not stored | set them again after switching on |
| Every note sounds doubled and louder | Raw Chord Sound is On with no performance mode | Options > Raw Chord Sound: Off |
| The screen shows the logo, or is black | three minutes, or ten, without input | touch anything |
| `Safe mode` on the top line | SAFE MODE | see [SAFE MODE](#safe-mode) |
| The computer shows no audio input | USB Record is Off | Options > USB Record: On |
| The FM-1 drops off USB for a second | USB Record was changed (`USB RECONNECTING`) | wait |
| The FM-1 became a `WL82 UBOOT1.00` disk | OCT- and OCT+ were held for 5 s | switch off and on |
| The FM-1 crashes or resets while playing | with Dual Core On, possibly the second core | Options > Dual Core: Off, switch off and on; please report it either way |
| Buttons or knobs are mixed up | the panel table is wrong | [Calibration](#calibration) |

## 18. Quick reference

### Buttons

A held layer button opens its layer and it stays; holding the same button again closes it (so do OCT- and HOME).

| Button (printed label) | ChoralRoot | Tap | Hold |
| --- | --- | --- | --- |
| FX | FX | effects on / off (in its layer too) | FX layer: SELECT effect · KNOB 1-3 parameters · KNOB 4 amount |
| SEL | KEY | Key Mode on / off | KEY layer: a root = tonic · MAJ / MIN = scale · KNOB 1 Tonic · 2 Scale · 3 Transpose · 4 Single (Full / Split Oct / Split Bass) · SELECT: page 2: KNOB 1 Key plays (Chords / Notes) · 2 Keyboard (Chords / Full) · GLO: the keys pick / play |
| ENV | BASS | bass on / off | BASS layer: SELECT behaviour · KNOB 1 Behaviour · 2 Register · 3 Sound · 4 Level. Held + EDIT: edit the bass sound. Held + SAVE: save it |
| LFO | LATCH | latch on / off | |
| EDIT | EDIT | the sound editor; in it: leave | the engine picker: white roots or SELECT engine · KNOB 1 presets · KNOB 2 init · KNOB 4 roots · OCT+ keep · OCT- cancel |
| GLO | OPT | Options (SELECT setting · KNOB 1 value). In the PLAY menu: Keys. In the KEY layer: its keys pick / play. In the editor: SHIFT latch | + PRESETS: engine · + ALGORITHM: bass level · + KNOB 1: split point · + SELECT: click level. In the editor: fine steps; + EDIT: chord / bass sound |
| HOME | HOME | back; on the view: next view | 0.6 s: the view menu (SELECT view · KNOB 1 lock) |
| SAVE | SAVE | the save dialog: KNOB 1 Overwrite / Save as new · OCT+ takes it · held 1 s: delete / reset | while held: loops: white roots slot · SELECT Save / Load / Delete · OCT+ does it |
| ARP | PERF | performance on / off (in its layer too) | PERF layer: SELECT mode · KNOB 1-4 its parameters |
| SEQ | METRO | metronome on / off | METRO layer: SELECT time signature · KNOB 1 Click |
| PLAY | LOOP | play / stop the loop; ends a take | PLAY menu: PRESETS slot · ALGORITHM record mode · SELECT length · KNOB 1 Quantize · 2 Count-in · 3 Level · 4 Keys |
| REC | REC | record by the record mode; ends a take; cancels when armed | undo the last layer |
| OCT- | octave down | roots down an octave (to -2). In layers, menus, dialogs: back / cancel (closes). In step entry: back a step | in the editor, + a knob: clear that parameter's modulation |
| OCT+ | octave up | roots up an octave (to +2). In layers, menus, dialogs: confirm (keep, save, do it); where there is nothing to confirm it closes, as OCT-. In step entry: a rest | |
| OCT- + OCT+ | PANIC | all notes off | 5 s: the chip's USB boot (switch off and on to leave) |

### In the sound editor

| Button (printed label) | In the editor | Tap | Hold |
| --- | --- | --- | --- |
| FX | OSC | the group; again: its next screen | |
| SEL | FILT | the same | |
| ENV | ENV | the same | + a knob: that envelope modulates the knob's parameter (VA) |
| LFO | LFO | the same | + a knob: that LFO modulates it (VA) |
| SEQ | MOD | the same | |
| PLAY | FX | the group | |
| REC | MIX | the group | |
| GLO | SHIFT | latch fine steps | fine steps while held; + EDIT: chord / bass sound |
| EDIT | EDIT | leave | the engine picker |
| HOME | HOME | leave | the view menu |
| SAVE | SAVE | the save dialog | loops, as outside |
| ARP | PERF | performance on / off | the PERF layer |

SELECT: the next lane. KNOB 1 to KNOB 4: the lane's values. PRESETS: the engine picker.

### Knobs

| Knob | Turn | OPT + turn | In a layer | In the editor |
| --- | --- | --- | --- | --- |
| MASTER | volume | | volume | volume |
| SELECT | tempo | click level | the layer's picker (KEY layer: its page; LOCK layer: tempo) | the lane |
| PRESETS | chord sound | chord engine | chord sound (PLAY menu: slot) | the engine picker |
| ALGORITHM | bass sound | bass level | bass sound (PLAY menu: record mode) | bass sound |
| KNOB 1 | voicing (Full keyboard: the octave) | split point | the layer's first cell | the lane's first value |
| KNOB 2 | bass register | | second cell | second value |
| KNOB 3 | performance parameter | | third cell | third value |
| KNOB 4 | FX amount | | fourth cell | fourth value |

### Keys

| Key | Job |
| --- | --- |
| F#3 G#3 A#3 C#4 | DIM MIN MAJ SUS |
| F3 G3 A3 C4 | 6 m7 M7 9 |
| B3 | LOCK (tap: on / off; held: the LOCK layer: KNOB 1 Type key Clears / Keeps · KNOB 2 Ext keys Toggle / Pushed). With LOCK on, the only latched type pressed again alone: off (single notes, the extensions kept) |
| F3 to G5, Keyboard = Full | 27 single notes (no chord keys, no LOCK); the split at F4 |
| D4 to G5 | roots |
| white roots D4 to G5, engine picker | FM6, FM TONE, VA, CZ-1, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE |
| white roots D4 to F5, PLAY menu with Keys = Loops, or SAVE held | loop slots 1 to 10 |
| D#4, F#4, PLAY menu with Keys = Loops | clear the loop (hold 1 s), undo |
| white roots, naming a sound | letters and digits; D#4 a space; F#4 delete |
| white roots D4 to A4, then B4, view menu | Chord, Keyboard, Notes, Geek Out, Scope; Lock |
| white roots, Options | the first eleven settings |
| white roots D4 to G4, BASS layer | the bass behaviour |
| white roots D4 to F4, METRO layer | 4/4, 3/4, 6/8 |

### At power-on

| Held while switching on | Result |
| --- | --- |
| OCT- | update mode (three flashes); new in 0.16 |
| OCT- + OCT+ | the panel calibration |

## 19. Where to read more

| Subject | Document |
| --- | --- |
| The sound editor in full | [EDITOR.md](EDITOR.md) |
| The engines | [VA.md](VA.md), [FM6.md](FM6.md), [QUAD.md](QUAD.md) (FM TONE), [CZ1.md](CZ1.md) |
| Presets and saved sounds | [PRESETS.md](PRESETS.md), [SOUNDS.md](SOUNDS.md) |
| The looper | [LOOPER.md](LOOPER.md), [LOOPER-MODES.md](LOOPER-MODES.md) |
| What is stored, and how | [SETTINGS.md](SETTINGS.md) |
| The second core | [DUALCORE.md](DUALCORE.md) |
| USB audio | [USB-AUDIO.md](USB-AUDIO.md) |
| The device manager | [DEVICE-MANAGER.md](DEVICE-MANAGER.md) |
| Updating, recovery, which firmware to install over | [BOOT-SAFETY.md](BOOT-SAFETY.md), [INSTALL-COMPAT.md](INSTALL-COMPAT.md) |
| What changed in each version | [releases/](releases/) |
| The design, and the emulator | [PLAN.md](../PLAN.md), [tools/emu/README.md](../tools/emu/README.md) |

Where one of these documents and this manual disagree about how the instrument behaves, the instrument is right;
please report it.

The screenshots in this manual are from the emulator, which runs the firmware's own drawing code:
[manual/shots.txt](manual/shots.txt) is the script that makes them.
