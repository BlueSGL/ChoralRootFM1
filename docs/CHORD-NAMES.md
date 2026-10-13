# ChoralRoot FM-1: chord names

Files: `firmware/src/cr_engine.c` (`cr_chord_name`, the one function every screen names a chord through;
`crx_name_std`, `crx_figure`), `firmware/src/cr_draw.c` (`cr_name`: how the three strings are drawn),
`firmware/src/cr_ui.c` (the Options entry), `tests/cr_engine_test.c` (`s_names_std`: this document's table, cell for
cell), `tests/cr_draw_test.c` ("chord names": the fit), `tools/emu/scripts/cr_chord_names.txt`.

**Options > Chord Names: Orchid / Standard** (0.20). Orchid is the default.

- **Orchid** names a chord by the buttons held, as the Telepathic Instruments Orchid (ORC-1) does: its manual's
  "Standard Chord Naming Framework" (one extension as a superscript, two in a row, three = `JAZZ`, four = `WTF`;
  "we don't ... turn a 6 into a 13"), with ChoralRoot's one departure since 0.18: a plain major triad reads `CM`,
  because `C` alone is a single note. This is what every version up to 0.19 showed, and it is unchanged, string for
  string (`tests/cr_engine_test.c`: 12 roots x 8 qualities x 16 extension sets against a copy of 0.19's function).
- **Standard** names the notes that sound, as a lead sheet would: `A9` for A + 7 + 9 (Orchid: `A7 9`), `Aadd9` for
  A + 9 (Orchid: `A9`), `A9sus4` for Asus + 7 + 9 (Orchid: `Asus m7 9`), `A13` for A + 7 + 6.

A user asked for it ("something simpler/more accurate would be better (e.g. A9 for A7 + 9th rather than A79, Aadd9
for A major triad + 9th which is incorrectly displayed as A9 atm, Asus9 for Asus + 7 + 9 etc...)"); the owner: "We
can add option for the chord names but I would prefer we match the ORC-1 by default."

**The name is display only.** Neither setting changes a note, a stored loop event (a step keeps its root, quality
and extension bits, and is named when it is shown), a MIDI byte or a saved sound. Switching the option renames what
is on the screen at once, the last chord included.

## What sounds

The names are derived from the engine's tables (`cr_engine.c`), not from the buttons' labels:

| | semitones above the root |
| --- | --- |
| DIM | 0 3 6 |
| MIN | 0 3 7 |
| MAJ | 0 4 7 |
| SUS | 0 5 7 (the **fourth**: a sus4) |
| MAJ + SUS (secret) | 0 4 8 (augmented) |
| DIM + SUS (secret) | 0 7 12 (the power chord: root, fifth, octave) |
| MIN + SUS (secret) | 0 3 5 (`m add4`: **no fifth**) |
| 6 | adds 9, under every chord (a major sixth: under a minor chord `m6`, under a diminished one the diminished seventh) |
| 7 (the m7 key) | adds 10, under every chord (a minor seventh: under a diminished chord that is half-diminished, `m7b5`, not `dim7`) |
| M7 | adds 11 |
| 9 | adds 14, always a major ninth, always in the octave above |

An extension is added once, in the octave the table says. The notes are then voiced (KNOB 1) without changing the
name.

## Standard's rules

1. **The triad.** `CM` (see "The major triad" below), `Cm`, `Cdim`, `Csus4`, `C+`, `C5`, `Cmadd4`.
2. **The figure** of the extensions, when the two sevenths are not both held:

   | held | figure | |
   | --- | --- | --- |
   | 6 | `6` | |
   | 9 | `add9` | no seventh under it: a `9` would promise one |
   | 6 + 9 | `6/9` | |
   | 7 | `7` | |
   | 7 + 9 | `9` | |
   | 7 + 6, 7 + 6 + 9 | `13` | the 6th above a seventh is the 13th; a 13th chord may or may not have its 9th |
   | M7, M7 + 9, M7 + 6 (+ 9) | `M7`, `M9`, `M13` | the major seventh is `M`, as the display already writes `M7` (and `maj` would not fit) |

3. **Where the figure goes.** After the triad for major, minor and augmented (`C9`, `Cm9`, `C+9`, `CmM7`); before
   `sus4` (`C7sus4`, `C9sus4`, `C6sus4`; `Csus4add9` when the 9th is the only extension); before `add4` on the
   Min + Sus chord (`Cm7add4`).
4. **Diminished.** The 6 key makes it `Cdim7`; the 7 key makes it half-diminished, `Cm7b5` (`Cm9b5`, `Cm13b5`); the
   major seventh is written on the triad (`CdimM7`, `CdimM9`, `CdimM13`).
5. **The power chord.** `C5`. With the 9th it is a sus2 chord (`Csus2`, `C7sus2`, `C6sus2`); without it the name is
   the figure and `no3` (`C7no3`, `C6no3`).
6. **Both sevenths at once** (7 + M7) have no lead-sheet name: those 28 cells (4 of each quality's 16) keep
   Orchid's name exactly: `C7 M7`, `JAZZ` with one more extension, `WTF` with both. Nowhere else does Standard say
   `JAZZ` or `WTF`: the 14 other `JAZZ` cells (7 + 6 + 9 and M7 + 6 + 9) are `13` and `M13`.
7. **Sharps**, as before: `C#`, `D#`, `F#`, `G#`, `A#`.

**The 13 names a sixth voiced inside the octave.** The engine plays the 6th where it always did, a major sixth above
the root, under the seventh; the Orchid does not raise it and this option changes no voicing. `C13` here is "a C7
with its 6th", not a promise of a note above the octave. See "Open questions".

**One name, one set of notes**, with one exception: `13` stands for the chord with and without its 9th (12 pairs:
six qualities, the minor and the major seventh). And one name says less than it could: the Min + Sus chord has no
fifth, and `Cmadd4` does not say so (neither does Orchid's `Cm add4`). `tests/cr_engine_test.c` reads every Standard
name with a small lead-sheet reader that knows nothing of the engine and compares the pitch classes it gets with
`cr_chord_base`'s, with exactly those two allowances.

## The table

Root C. The names are written as the looper's lines write them: root, quality and superscript in a row (on the big
views the last part is the small raised one: `Cm` with a raised `7 9`, `C` with a raised `9sus4`). "Sounds" is in
semitones above the root, before the voicing. 7 qualities x 16 extension sets = 112 chords; the eighth row of the
engine's table is the single note (a root with no chord key and Key Mode off), which reads `C` whatever is held, in
both settings: 128 cells.

### MAJ (0 4 7)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MAJ | 0 4 7 | `CM` | `CM` (the same) | kept: `C` alone is a single note on every screen |
| MAJ + 6 | 0 4 7 9 | `C6` | `C6` (the same) |  |
| MAJ + 7 | 0 4 7 10 | `C7` | `C7` (the same) |  |
| MAJ + 6 + 7 | 0 4 7 9 10 | `C7 6` | `C13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + M7 | 0 4 7 11 | `CM7` | `CM7` (the same) |  |
| MAJ + 6 + M7 | 0 4 7 9 11 | `CM7 6` | `CM13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + 7 + M7 | 0 4 7 10 11 | `C7 M7` | `C7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MAJ + 6 + 7 + M7 | 0 4 7 9 10 11 | `CJAZZ` | `CJAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + 9 | 0 4 7 14 | `C9` | `Cadd9` | no seventh: `add9`, not `9` |
| MAJ + 6 + 9 | 0 4 7 9 14 | `C6 9` | `C6/9` | 6th and 9th, no seventh |
| MAJ + 7 + 9 | 0 4 7 10 14 | `C7 9` | `C9` | seventh and 9th |
| MAJ + 6 + 7 + 9 | 0 4 7 9 10 14 | `CJAZZ` | `C13` | seventh, 6th and 9th: a 13th chord |
| MAJ + M7 + 9 | 0 4 7 11 14 | `CM7 9` | `CM9` | seventh and 9th |
| MAJ + 6 + M7 + 9 | 0 4 7 9 11 14 | `CJAZZ` | `CM13` | seventh, 6th and 9th: a 13th chord |
| MAJ + 7 + M7 + 9 | 0 4 7 10 11 14 | `CJAZZ` | `CJAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + 6 + 7 + M7 + 9 | 0 4 7 9 10 11 14 | `CWTF` | `CWTF` (the same) | both sevenths: Orchid's name stands |

### MIN (0 3 7)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MIN | 0 3 7 | `Cm` | `Cm` (the same) |  |
| MIN + 6 | 0 3 7 9 | `Cm6` | `Cm6` (the same) |  |
| MIN + 7 | 0 3 7 10 | `Cm7` | `Cm7` (the same) |  |
| MIN + 6 + 7 | 0 3 7 9 10 | `Cm7 6` | `Cm13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + M7 | 0 3 7 11 | `CmM7` | `CmM7` (the same) | minor triad, major seventh |
| MIN + 6 + M7 | 0 3 7 9 11 | `CmM7 6` | `CmM13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + 7 + M7 | 0 3 7 10 11 | `Cm7 M7` | `Cm7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MIN + 6 + 7 + M7 | 0 3 7 9 10 11 | `CmJAZZ` | `CmJAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + 9 | 0 3 7 14 | `Cm9` | `Cmadd9` | no seventh: `add9`, not `9` |
| MIN + 6 + 9 | 0 3 7 9 14 | `Cm6 9` | `Cm6/9` | 6th and 9th, no seventh |
| MIN + 7 + 9 | 0 3 7 10 14 | `Cm7 9` | `Cm9` | seventh and 9th |
| MIN + 6 + 7 + 9 | 0 3 7 9 10 14 | `CmJAZZ` | `Cm13` | seventh, 6th and 9th: a 13th chord |
| MIN + M7 + 9 | 0 3 7 11 14 | `CmM7 9` | `CmM9` | seventh and 9th |
| MIN + 6 + M7 + 9 | 0 3 7 9 11 14 | `CmJAZZ` | `CmM13` | seventh, 6th and 9th: a 13th chord |
| MIN + 7 + M7 + 9 | 0 3 7 10 11 14 | `CmJAZZ` | `CmJAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + 6 + 7 + M7 + 9 | 0 3 7 9 10 11 14 | `CmWTF` | `CmWTF` (the same) | both sevenths: Orchid's name stands |

### SUS (0 5 7: the fourth)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| SUS | 0 5 7 | `Csus` | `Csus4` | the engine's SUS is the fourth (5 semitones), so `sus4` |
| SUS + 6 | 0 5 7 9 | `Csus6` | `C6sus4` | the figure goes before `sus4` |
| SUS + 7 | 0 5 7 10 | `Csusm7` | `C7sus4` | Orchid's `m7` is the plain minor seventh: a dominant 7 with the fourth |
| SUS + 6 + 7 | 0 5 7 9 10 | `Csusm7 6` | `C13sus4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| SUS + M7 | 0 5 7 11 | `CsusM7` | `CM7sus4` |  |
| SUS + 6 + M7 | 0 5 7 9 11 | `CsusM7 6` | `CM13sus4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| SUS + 7 + M7 | 0 5 7 10 11 | `Csusm7 M7` | `Csusm7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| SUS + 6 + 7 + M7 | 0 5 7 9 10 11 | `CsusJAZZ` | `CsusJAZZ` (the same) | both sevenths: Orchid's name stands |
| SUS + 9 | 0 5 7 14 | `Csus9` | `Csus4add9` | fourth and ninth, no third, no seventh: `sus4` with the added 9th |
| SUS + 6 + 9 | 0 5 7 9 14 | `Csus6 9` | `C6/9sus4` | 6th and 9th, no seventh |
| SUS + 7 + 9 | 0 5 7 10 14 | `Csusm7 9` | `C9sus4` | seventh and 9th |
| SUS + 6 + 7 + 9 | 0 5 7 9 10 14 | `CsusJAZZ` | `C13sus4` | seventh, 6th and 9th: a 13th chord |
| SUS + M7 + 9 | 0 5 7 11 14 | `CsusM7 9` | `CM9sus4` | seventh and 9th |
| SUS + 6 + M7 + 9 | 0 5 7 9 11 14 | `CsusJAZZ` | `CM13sus4` | seventh, 6th and 9th: a 13th chord |
| SUS + 7 + M7 + 9 | 0 5 7 10 11 14 | `CsusJAZZ` | `CsusJAZZ` (the same) | both sevenths: Orchid's name stands |
| SUS + 6 + 7 + M7 + 9 | 0 5 7 9 10 11 14 | `CsusWTF` | `CsusWTF` (the same) | both sevenths: Orchid's name stands |

### DIM (0 3 6)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| DIM | 0 3 6 | `Cdim` | `Cdim` (the same) |  |
| DIM + 6 | 0 3 6 9 | `Cdim6` | `Cdim7` | the 6 key adds 9 semitones: the diminished seventh |
| DIM + 7 | 0 3 6 10 | `Cdim7` | `Cm7b5` | the 7 key adds 10 semitones, a minor seventh: half-diminished, not `dim7` |
| DIM + 6 + 7 | 0 3 6 9 10 | `Cdim7 6` | `Cm13b5` | half-diminished with the 6th as its 13th |
| DIM + M7 | 0 3 6 11 | `CdimM7` | `CdimM7` (the same) | diminished triad, major seventh |
| DIM + 6 + M7 | 0 3 6 9 11 | `CdimM7 6` | `CdimM13` | the triad's name and the figure of M7 + 6 (the 6 is the diminished seventh's note) |
| DIM + 7 + M7 | 0 3 6 10 11 | `Cdim7 M7` | `Cdim7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| DIM + 6 + 7 + M7 | 0 3 6 9 10 11 | `CdimJAZZ` | `CdimJAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + 9 | 0 3 6 14 | `Cdim9` | `Cdimadd9` | no seventh: `add9`, not `9` |
| DIM + 6 + 9 | 0 3 6 9 14 | `Cdim6 9` | `Cdim7add9` | the diminished seventh chord and a 9th |
| DIM + 7 + 9 | 0 3 6 10 14 | `Cdim7 9` | `Cm9b5` | half-diminished with the 9th |
| DIM + 6 + 7 + 9 | 0 3 6 9 10 14 | `CdimJAZZ` | `Cm13b5` | half-diminished with the 6th as its 13th |
| DIM + M7 + 9 | 0 3 6 11 14 | `CdimM7 9` | `CdimM9` | as `CmM9`, on the diminished triad |
| DIM + 6 + M7 + 9 | 0 3 6 9 11 14 | `CdimJAZZ` | `CdimM13` | the triad's name and the figure of M7 + 6 (the 6 is the diminished seventh's note) |
| DIM + 7 + M7 + 9 | 0 3 6 10 11 14 | `CdimJAZZ` | `CdimJAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + 6 + 7 + M7 + 9 | 0 3 6 9 10 11 14 | `CdimWTF` | `CdimWTF` (the same) | both sevenths: Orchid's name stands |

### MAJ + SUS, the secret augmented chord (0 4 8)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MAJ + SUS | 0 4 8 | `C+` | `C+` (the same) | `+` as Orchid's (short; `aug` is the same chord) |
| MAJ + SUS + 6 | 0 4 8 9 | `C+6` | `C+6` (the same) |  |
| MAJ + SUS + 7 | 0 4 8 10 | `C+7` | `C+7` (the same) | also written `C7#5` |
| MAJ + SUS + 6 + 7 | 0 4 8 9 10 | `C+7 6` | `C+13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + SUS + M7 | 0 4 8 11 | `C+M7` | `C+M7` (the same) |  |
| MAJ + SUS + 6 + M7 | 0 4 8 9 11 | `C+M7 6` | `C+M13` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MAJ + SUS + 7 + M7 | 0 4 8 10 11 | `C+7 M7` | `C+7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MAJ + SUS + 6 + 7 + M7 | 0 4 8 9 10 11 | `C+JAZZ` | `C+JAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + SUS + 9 | 0 4 8 14 | `C+9` | `C+add9` | no seventh: `add9`, not `9` |
| MAJ + SUS + 6 + 9 | 0 4 8 9 14 | `C+6 9` | `C+6/9` | 6th and 9th, no seventh |
| MAJ + SUS + 7 + 9 | 0 4 8 10 14 | `C+7 9` | `C+9` | seventh and 9th |
| MAJ + SUS + 6 + 7 + 9 | 0 4 8 9 10 14 | `C+JAZZ` | `C+13` | seventh, 6th and 9th: a 13th chord |
| MAJ + SUS + M7 + 9 | 0 4 8 11 14 | `C+M7 9` | `C+M9` | seventh and 9th |
| MAJ + SUS + 6 + M7 + 9 | 0 4 8 9 11 14 | `C+JAZZ` | `C+M13` | seventh, 6th and 9th: a 13th chord |
| MAJ + SUS + 7 + M7 + 9 | 0 4 8 10 11 14 | `C+JAZZ` | `C+JAZZ` (the same) | both sevenths: Orchid's name stands |
| MAJ + SUS + 6 + 7 + M7 + 9 | 0 4 8 9 10 11 14 | `C+WTF` | `C+WTF` (the same) | both sevenths: Orchid's name stands |

### DIM + SUS, the secret power chord (0 7 12)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| DIM + SUS | 0 7 12 | `C5` | `C5` (the same) |  |
| DIM + SUS + 6 | 0 7 9 12 | `C5 6` | `C6no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 7 | 0 7 10 12 | `C5 7` | `C7no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 6 + 7 | 0 7 9 10 12 | `C5 7 6` | `C13no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + M7 | 0 7 11 12 | `C5 M7` | `CM7no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 6 + M7 | 0 7 9 11 12 | `C5 M7 6` | `CM13no3` | root and fifth only: the figure, and `no3` says the third is missing |
| DIM + SUS + 7 + M7 | 0 7 10 11 12 | `C5 7 M7` | `C5 7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| DIM + SUS + 6 + 7 + M7 | 0 7 9 10 11 12 | `C5 JAZZ` | `C5 JAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + SUS + 9 | 0 7 12 14 | `C5 9` | `Csus2` | root, fifth, ninth: that is a sus2 chord |
| DIM + SUS + 6 + 9 | 0 7 9 12 14 | `C5 6 9` | `C6sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 7 + 9 | 0 7 10 12 14 | `C5 7 9` | `C7sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 6 + 7 + 9 | 0 7 9 10 12 14 | `C5 JAZZ` | `C13sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + M7 + 9 | 0 7 11 12 14 | `C5 M7 9` | `CM7sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 6 + M7 + 9 | 0 7 9 11 12 14 | `C5 JAZZ` | `CM13sus2` | with the 9th it is a sus2 chord; the figure names the rest |
| DIM + SUS + 7 + M7 + 9 | 0 7 10 11 12 14 | `C5 JAZZ` | `C5 JAZZ` (the same) | both sevenths: Orchid's name stands |
| DIM + SUS + 6 + 7 + M7 + 9 | 0 7 9 10 11 12 14 | `C5 WTF` | `C5 WTF` (the same) | both sevenths: Orchid's name stands |

### MIN + SUS, the secret chord `m add4` (0 3 5: no fifth)

| Buttons | Sounds | Orchid | Standard | Why |
| --- | --- | --- | --- | --- |
| MIN + SUS | 0 3 5 | `Cmadd4` | `Cmadd4` (the same) | as Orchid's, without the space; the fifth does not sound and the name does not say so |
| MIN + SUS + 6 | 0 3 5 9 | `Cmadd4 6` | `Cm6add4` |  |
| MIN + SUS + 7 | 0 3 5 10 | `Cmadd4 7` | `Cm7add4` |  |
| MIN + SUS + 6 + 7 | 0 3 5 9 10 | `Cmadd4 7 6` | `Cm13add4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + SUS + M7 | 0 3 5 11 | `Cmadd4 M7` | `CmM7add4` |  |
| MIN + SUS + 6 + M7 | 0 3 5 9 11 | `Cmadd4 M7 6` | `CmM13add4` | a seventh with the 6th: the 6th is its 13th (no 9th sounds) |
| MIN + SUS + 7 + M7 | 0 3 5 10 11 | `Cmadd4 7 M7` | `Cmadd4 7 M7` (the same) | both sevenths: no lead-sheet name, Orchid's literal stands |
| MIN + SUS + 6 + 7 + M7 | 0 3 5 9 10 11 | `Cmadd4 JAZZ` | `Cmadd4 JAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + SUS + 9 | 0 3 5 14 | `Cmadd4 9` | `Cmadd9add4` | the minor chord's name (`madd9`) and `add4` |
| MIN + SUS + 6 + 9 | 0 3 5 9 14 | `Cmadd4 6 9` | `Cm6/9add4` | 6th and 9th, no seventh |
| MIN + SUS + 7 + 9 | 0 3 5 10 14 | `Cmadd4 7 9` | `Cm9add4` | seventh and 9th |
| MIN + SUS + 6 + 7 + 9 | 0 3 5 9 10 14 | `Cmadd4 JAZZ` | `Cm13add4` | seventh, 6th and 9th: a 13th chord |
| MIN + SUS + M7 + 9 | 0 3 5 11 14 | `Cmadd4 M7 9` | `CmM9add4` | seventh and 9th |
| MIN + SUS + 6 + M7 + 9 | 0 3 5 9 11 14 | `Cmadd4 JAZZ` | `CmM13add4` | seventh, 6th and 9th: a 13th chord |
| MIN + SUS + 7 + M7 + 9 | 0 3 5 10 11 14 | `Cmadd4 JAZZ` | `Cmadd4 JAZZ` (the same) | both sevenths: Orchid's name stands |
| MIN + SUS + 6 + 7 + M7 + 9 | 0 3 5 9 10 11 14 | `Cmadd4 WTF` | `Cmadd4 WTF` (the same) | both sevenths: Orchid's name stands |

Counted (`tests/cr_engine_test.c`): of the 112 chords, 84 have a Standard name of their own and 28 (both sevenths)
keep Orchid's: 7 read `7 M7`, 14 `JAZZ`, 7 `WTF`. 16 of the 84 are the same in both settings: `CM`, `C6`, `C7`, `CM7`,
`Cm`, `Cm6`, `Cm7`, `CmM7`, `Cdim`, `CdimM7`, `C+`, `C+6`, `C+7`, `C+M7`, `C5`, `Cmadd4`.

## Choices

- **The major triad stays `CM`.** A lead sheet writes `C`. But a single note is named by its root alone, without an
  octave, on every screen that names both: the Chord, Keyboard, Notes, Scope and Geek Out views, the looper's name
  line and its playing name all go through `cr_chord_name`, which gives `C` for `CR_Q_NONE` (the octave appears
  only in the notes list and in the loop editor's Transpose cell). With `C` for the triad the two would read the
  same, which is what the owner's rule of 0.18 removed ("major chords should use capital M to differentiate from
  single notes. So C major would read CM"). So Standard keeps it. The owner's to change: see "Open questions".
- **`M` for the major seventh** (`CM7`, `CM9`, `CM13`, `CmM7`), not `maj`: Orchid mode already writes `M7`, and
  `G#mmaj13add4` would not fit anywhere.
- **`Cmadd9`**, no brackets (and `Cmadd4`, `Cdimadd9`, `Csus4add9`): the superscript sets the `add9` apart on the
  big views, and brackets cost width on the small ones.
- **`C6/9`**, with the slash: the superscript is drawn with the 40 px face, which has it (below).
- **`C+`** for the augmented chord, as in Orchid mode (`aug` is three letters wider); `C+7` is the chord also
  written `C7#5`.
- **`sus4`, not `sus`**: the engine's SUS is the fourth, and the power chord with a 9th is a `sus2`, so the number
  tells them apart. Orchid mode keeps `sus`.
- **`Csus4add9`** for SUS + 9 (root, 2nd, 4th, 5th), rather than `Csus2sus4`.
- **`no3`** for the power chord with a 6th or a seventh (`C7no3`): there is no third to call it major or minor.
- **`dim` + M7 + 6 is `CdimM13`**, not `Cdim7M7`: next to the fallback `Cdim7 M7` (DIM + 7 + M7, Orchid's name) the
  two would differ by a space.
- **The fallback is Orchid's whole name**, quality included: `Csusm7 M7` in Standard too (not `Csus4 ...`), so that
  a fallback is recognisable as one. Note that Orchid's `dim` + `7` there means the minor seventh (10 semitones),
  as everywhere in Orchid mode.

## How the name is drawn, and what fits

`cr_chord_name` writes three strings: `root` (3 bytes), `qual` (5) and `sup` (`CR_SUP_MAX`, 12). `cr_draw.c cr_name`
draws the root at the view's size, the quality at 0.58 of it on the same baseline, and `sup` as a superscript at
0.38, orange (red for a secret chord); the whole is squeezed horizontally to the room it has (224 px on the Chord,
Keyboard and arp views, 200 px in the PERF layer's arp band, 132 px on Geek Out, where the name is centred on x 70)
and never cut. The looper's name line, its playing name, the Scope view's header and the Notes view's title write
the three in a row as text.

**Glyphs.** The root and the quality of the Chord view (104 px, 60 px) come from the big face `CRX`, which has 37
characters only: the space and `# + - . 0 1 2 3 4 5 6 7 8 9 ? A B C D E F G I J M N P T W Z b d i m s u`
(`tools/gen_aa_font.py CRX_CHARS`; a string it does not cover falls back to the 40 px face scaled up). Standard's
qualities are `M`, `m`, `dim`, `sus4`, `sus2`, `+`: all in it. Everything else Standard writes (`add9`, `6/9`,
`no3`, `b5`, `M13`) is in the superscript, which at every size is the 40 px face `CRB`: full ASCII, so `/`, `a`,
`n`, `o` are there. No glyph was added and the font generator was not touched. (`°` and `ø` are in neither face:
`dim` and `m7b5` are spelled out.)

**The fit**, checked for all 112 chords under G# and A# (the widest roots) in both settings by
`tests/cr_draw_test.c` ("chord names": 0 lint findings, nothing ellipsised, nothing off the screen):

| where | room | Orchid's widest | Standard's widest |
| --- | --- | --- | --- |
| the Chord view's big name, 104 px | squeezed to 224 px | `G#m` `add4 JAZZ`, to 56 % | `G#m` `add9add4`, to 58 %; `G#sus4` `add9`, to 59 % |
| the Keyboard view (40 px), the arp (44 px), the PERF layer's arp band (20 px) | 224 / 224 / 200 px | fits unsqueezed | fits unsqueezed |
| Geek Out (40 px, centred on x 70) | 132 px | `G#m` `add4 JAZZ`, to 85 % | `G#m` `add9add4`, to 88 % |
| the looper's name line beside `step 64/64`, with a blue voicing `-12` | 15 px bold, what the step count and the voicing leave | `G#madd4 JAZZ` 110 px | `G#madd9add4` 105 px |
| the loop editor's playing name | 78 px, at the largest of 13, 11 and 10 px that fits | `G#madd4 JAZZ` 74 px at 10 px (8 of the 224 names need 10 px) | none of its own names needs 10 px (`G#madd9add4` would be 70 px there) |
| the Scope view's header, the Notes view's title | 15 px bold | 110 px | 105 px |

Standard's longest names are shorter than Orchid's longest (`G#madd4 JAZZ`, which Standard also shows for the both-
sevenths cells), so no layout had to change for Standard. Two things were found on the way, both in Orchid mode too:

- The screen's copy of the superscript (`cr_screen.h cr_name_t.sup`) was 8 bytes against the engine's 12: on the
  views, `Cm add4 JAZZ` showed as `Cm add4 JA`, `add4 7 6` as `add4 7` (the looper's lines were whole). It is 12 now
  (`cr_ui.c` asserts it against `CR_SUP_MAX`): 4 bytes a name, 8 bytes of POOL (the frame's screen holds two names)
  and 8 of RAM (the animation's two).
- With the superscript whole, the longest names ran off Geek Out's left edge (centred on x 70 with 224 px of room:
  `G#m add4 JAZZ` began at x -7). Geek Out's room is 132 px now; a name up to 132 px wide is drawn as before.

## Where the option is stored

Bit 3 of the settings block's `pick_roots` byte (`cr_settings.h CRS_NAMES_STD`; set = Standard), beside Output
(bit 1) and USB Rate (bit 2): docs/SETTINGS.md. No record version: the bit is 0 in every older record and 0 is the
default, Orchid. An Option, so it is saved by itself. The engine is told through `cr_chord_names_set` (a byte of its
own in `cr_engine.c`, not part of `cr_t`: nothing that plays reads it).

## Open questions (the owner's)

1. **The 6th an octave higher when a seventh is held.** The same user: "it would be nice if the 6th was played an
   octave higher if either 7th is enabled", which would make the `13` a real 13th (21 semitones above the root
   instead of 9, above the 9th's 14). Not built: it changes what sounds and what a stored loop step plays, the
   Orchid does not do it, and the owner did not ask for it. If wanted it is one line in `cr_chord_base` (the 6's
   interval 9 -> 21 when a seventh's bit is set) plus the goldens, and a question of its own: for both settings or
   for Standard only (a naming option that changes the sound would be a surprise).
2. **`C` for the major triad in Standard.** Kept as `CM` (above). `C` would need the single note to read
   differently where both are named (its octave, `C4`, as the notes list writes it).
3. **`13` with and without the 9th.** `C13` is both MAJ + 7 + 6 and MAJ + 7 + 6 + 9. `C7add13` for the first would
   tell them apart, at 7 characters of superscript.
4. **The Min + Sus chord's missing fifth.** `Cmadd4` (as Orchid's `Cm add4`); `Cm(add4,no5)` would be exact and
   would not fit.
