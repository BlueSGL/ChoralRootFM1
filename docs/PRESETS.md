# Presets: one pool per engine (the user's model, 2026-10-07)

**Status: done in the firmware 2026-10-07** (`cr_bank.c` the pools and the binding, `cr_ui.c` PRESETS / ALGORITHM /
OPT + PRESETS / the save dialog, settings v6, `tools/emu/scripts/cr_presets.txt`; docs/HISTORY.md 3e). Decided on the
way: Overwrite never renames; Overwrite on a user preset keeps its binding; the bound record of a loud factory preset
keeps that preset's trim; a delete loads what is then at the deleted place (the one before at the end); the CZ-1's
INIT TONE is the pool's INIT (its pool: INIT + Casio's 64); an engine not played yet lands on its first preset.
The Sounds page and the CLI show the binding ("over FM6 02 FM BELL" / "added").

Replaces the sound model of PLAN.md §3 / §7 and docs/INTEGRATION.md §7 ("Sounds"): the curated 48-row ChoralRoot
bank on PRESETS followed by 32 user slots, with the engines' own factory presets reachable only inside the editor
(EDIT held, KNOB 1). The user found it confusing (three lists, a saved sound landing at position 49 with no sign of
it, no way to save over a factory sound). Mock-ups: `design/choralroot-fm1-preset-screens.png`
(`design/make_preset_mockups.py`).

## The model

- **A pool per engine.** Each engine's pool starts with **INIT** at position 00 (the engine's init sound, `cu_sound_init`; it cannot be overwritten: Overwrite on it is Save as new), then its factory presets (`engine_t.presets`: FM6 25 (F1..F24, PIANO),
  FM TONE 16 (engine 15, `eng_quad.c`, code name QUAD), VA 25, CZ-1 64, PROPHET 200 (engine 16, 0.22 in development:
  below), WHEEL 5: the picker's order, six
  engines; five from 0.18 until PROPHET; PHASE 6, LOFI 5, VOICE 4, TRIO 5, PHYS 9 and NOISE 4 came between and after them until then) followed by
  the presets the user added. ANALOG (engine 0, 12 presets) is retired
  since 0.14 (`FELUCCA_ANALOG` 0): no pool; a user record of ANALOG is an added preset of the VA's pool and plays there
  with its CUT / RES / DRIVE / MIX / DTN, filter envelope and ATK / REL on the VA's macros of the same name (`cr_ui.c`
  `cu_analog_va`; its binding to an ANALOG factory preset is dropped), another retired engine's record (SAMPLE, GRAIN,
  DRUM) the VA's init sound. PHASE, LOFI, VOICE, TRIO, NOISE and PHYS (engines 2, 3, 5, 6, 11, 9) are retired since 0.18
  (`FELUCCA_PHASE` / `LOFI` / `VOICE` / `TRIO` / `NOISE` / `PHYS` 0): no pool; a user record of one is an added preset of the
  VA's pool under its name (its binding to a factory preset of its engine is dropped) and loads as the VA's init
  sound with the record's sends (DIST CHO DLY REV) and its envelope's ATK / REL on the VA's macros of that name: a
  stand-in, with the message `PHASE retired: stand-in` (`cr_ui.c cu_load_user`); the record is not rewritten.
  Every entry is editable: **saving over a factory preset is allowed** and the factory one stays recoverable (reset).
- **PRESETS turns inside the current engine's pool only** (the chord part's engine; ALGORITHM the bass part's). The
  popup: the number big, the name, the stripe meter over the pool, and two lines that depend on where it shows.
  **Where OPT held + the part's knob is its engine picker** (`cu_epk_ok(part)`, 0.19: wherever the knob's plain job
  is the part's sound: the home pages, the Options, the layers but the PLAY menu, and for the bass part the sound
  editor; in 0.18 the home pages only, `cu_pool_home`; 2026-10-11, the owner: "show above the preset number in orange the current engine name. Then at the bottom
  it currently says 'ENGINE - 20/25', let's change that to say 'Hold OPT to change engine', the numbers and the bar
  already communicate how many presets there are and which one is selected"): the part's engine over the number in
  the role orange (`cr_screen_t.line` / `line_col`, the meter's line over its value; the bass with Bass Behaviour
  Solo: `VA · solo`), and under the stripes `Hold OPT to change engine` for both parts (since 2026-10-12 OPT + ALGORITHM is the bass
  part's engine, as OPT + PRESETS the chord part's; until then the bass part's line read `BASS + EDIT, hold EDIT:
  engine` and OPT + ALGORITHM was the bass level, which is BASS held + ALGORITHM now). **Elsewhere**
  (the PLAY menu, the editor's picker and its preview, the save dialog; the chord part in the sound editor): nothing over the number and
  `FM6 · 05/25` under the stripes, as everywhere until then (the place and the number of presets: 00 = INIT, which
  is not counted as a preset, 01.. the factory presets, then the user's); an overwritten factory preset
  carries a small square mark after its name. The meter is a stripe per preset and place k lights k of them: none on
  INIT, every one on the last preset (`WHEEL · 05/05`: five stripes, all lit). A pool of more than 32 presets (the
  CZ-1's 64, FM6 with eight added) has 32 stripes: none on INIT, one from preset 01 on, every one only on the last
  (`cr_ui.c` `cu_pool_lit`); since 0.16 (before: INIT was counted, `FM6 · 05/26`, and the last preset left a stripe dark).
  A step moves the stripes from the place shown to the new one, up (lit) or down (darkened), a stripe every 30 ms, more
  than 14 of them in 420 ms (a wrap); the part's first read-out and another engine's fill from none (0.17; before, every
  step filled from none: docs/INTEGRATION.md section 5, `cu_animate`).
- **OPT + PRESETS changes the engine** outside the editor (a horizontal picker: the engine big, the pool size as the
  value); the part lands on the preset it last had in that pool (remembered per engine per part; the first preset
  before that). The editor's EDIT-held picker is unchanged (it is the same thing inside the editor).
- **Where the two engine shortcuts work (0.19; the owner, 2026-10-12: "If GLO + Algo is changing bass level
  anywhere, it should change bass engine, let's keep the UI consistent where we can.").** One rule for both knobs,
  `cr_ui.c cu_epk_ok(part)`: where the knob's plain job is the part's sound, OPT held makes it the part's engine;
  where the knob has a job of the place's own, OPT changes nothing about it. Before and after, per place:

  | place | OPT + PRESETS, 0.18 | OPT + PRESETS, 0.19 | OPT + ALGORITHM, to 0.17 | 0.18 | 0.19 |
  | --- | --- | --- | --- | --- | --- |
  | home pages (every view) | chord engine | chord engine | bass level | bass engine | bass engine |
  | the Options | chord engine | chord engine | bass level | bass engine | bass engine |
  | layers KEY, PERF, FX, BASS, SEQ, LOCK, the view menu (also over the sound editor) | chord sound (plain) | **chord engine** | bass level | bass sound (plain) | **bass engine** |
  | the sound editor's pages | the editor's picker opens (PRESETS's job there) | the same | bass level | bass sound (plain) | **bass engine** |
  | the PLAY menu | the slot (PRESETS's job there) | the same | bass level | the record mode (ALGORITHM's job there) | the same |
  | the loop editor | 8 steps a click | the same | Voicing Follow / 0 | the same | the same |
  | the editor's picker (`L_EDIT`) | its presets | the same | bass level | bass sound (plain) | the same |
  | the save dialog | chord sound (plain) | the same | bass level | bass sound (plain) | the same |

  The last two are kept out on purpose: each holds one sound (a preview OCT- takes back; the sound being named and
  stored), and another engine loaded under it by a second picker would be previewed, cancelled or saved in its
  place. The picker, the landing rule (`cs.pool_pos[part]`), the read-out and "a bass that is off stays off" are
  `cu_epk_step` / `cu_epk_commit` everywhere: one code path. The read-out's hint line follows the same predicate, so
  `Hold OPT to change engine` is shown exactly where it is true. `tools/emu/scripts/cr_glo_knobs.txt` walks every
  row.
- **SAVE tap**: a two-item picker, **Overwrite** (the current preset; saves at once, name kept) or **Save as new**
  (the pool's next free place, then the naming screen prefilled with the current name). The default is Overwrite when
  the current preset is a user one or an edited one, Save as new otherwise. OCT+ takes it, OCT- cancels. **SAVE held
  1 s in the dialog**: "reset to factory?" on an overwritten factory preset, "delete?" on a user-added one. SAVE held
  outside the dialog stays the loops' save / load / delete.
- The bass: the same model on ALGORITHM (the bass part's engine's pool). ALGORITHM position 0 = OFF as today. Its
  engine: OPT held + ALGORITHM on the home pages since 2026-10-12, and since 0.19 wherever ALGORITHM alone is the
  bass sound (the table above) (the chord part's picker in orange, the same
  engines, wrapping, the place last played in that engine, `cs.pool_pos[1]`; a bass that is off stays off), and as
  before BASS held + EDIT's picker. The bass level, OPT + ALGORITHM until then, is BASS held + ALGORITHM.
- The idea of "templates" is gone: what the editor's picker steps with KNOB 1 is the same pool PRESETS turns.

## PROPHET's pool (0.22 in development; docs/PROPHET.md)

- **00 INIT** is the engine's INIT PROPHET (its preset 0 is the pool's INIT, as the CZ-1's INIT TONE: `cr_bank.c
  pool_f0`); **01 .. 200** are Sequential's factory programs v1.03 in the bank's order (01 ITS PROPHET5 .. 200 CAT),
  with 12-character PRESETS names made from the programs' own (`tools/gen_prophet_factory.py`). A build with
  `FELUCCA_P5_FACTORY=0` has INIT alone. The read-out is `PROPHET · 03/200`; the stripe meter has 32 stripes, as every
  pool over 32.
- The programs are dry; they play with the part's sends, which no sound load touches (the model above).
- **It is the first pool that cannot grow yet: SAVE on a PROPHET part is refused** ("PROPHET: no save yet"): the
  engine has no store for its program (phase B). Its edits live in the part and in the session.
- Two limits the first pool over 127 places met, both left as they are for phase B: the settings record has no slot
  for PROPHET's per-engine pool position (`cr_settings.c crs_slot`), so after a power cycle a part that comes back
  to PROPHET from another engine lands on 01 (the sound a part was playing at power-off does come back: the record's
  chord / bass sound and the session), and the record's check would reset such a position over 127. A pool's place,
  an engine's preset count and a track's preset are bytes: 200 fits, 255 is the end.
- The pool read-out showed 11 of a name's 12 characters; it shows 12 with PROPHET in the build (`cu.pop.sub`).

## Storage (no flash layout change)

- The 32 user slots (`upreset.c` records, the VA / FM6 / CZ-1 stores beside them) hold every user-saved preset.
  A record **binds** to its place in a pool through two of the record's pattern bytes, unused by ChoralRoot (no
  sequencer): `note[15] = 0xA6` (the binding marker) and `flags[15]` = the factory index + 1 it overwrites, or 0 for
  a preset added to the pool. A record without the marker (saved by older firmware, or restored) is a user-added
  preset of its engine. The record's engine is the pool. `up_valid` is unchanged; the JSON sound files carry the
  binding with the record (docs/SOUNDS.md: the clients show it: "overwrites FM6 02 FM BELL").
- The pool of engine E = its factory presets, each replaced by a bound record where one exists (the first bound slot
  wins; a duplicate binding is reported and the later one is treated as added), then the added records of E in slot
  order. The list is built on demand from the 32 records (cheap: 32 comparisons).
- "Reset to factory" deletes the bound record (the factory preset reappears). "Delete" deletes an added record.
- The curated bank (`cr_bank.c` `CB_CHORD` / `CB_BASS`): its **trims** survive as a lookup by (engine, factory
  preset) applied when that factory preset loads (the loud CZ-1 presets; ANALOG's STRINGS row went with ANALOG in 0.14); its PIANO hybrid (FM6 TINE EP
  through the macros) becomes a factory-shipped **bound record**? No: ship it as a real FM6 factory preset entry
  (`FM6_PRESETS` gains PIANO as F25 with its macro values), so the pool stays pure. The bank's row order, names and
  the 48-row list go away; the default sound at power-on is FM6 TINE EP (F1); the bass's, VA DEEP SUB (ANALOG SUB BASS until 0.14).
- Settings (`cr_settings.h`, add-a-field procedure of docs/SETTINGS.md): `chord_sound` becomes (engine, pool index)
  and the per-engine last preset per part (11 bytes x 2) joins the record (version 6); `bass_sound` the same.
- The emulator's test_persist scripts and the Sounds page / CLI (docs/SOUNDS.md) need their expectations updated
  (the record's two binding bytes; `--sounds` lists "FM6 02 (over FM BELL)" or "FM6 +1").

## What changes where (the firmware brief)

- `cr_ui.c`: the sound list (`cu_list_load`, `cb_count`, `cb_slot_at`, `cu_sound_popup`) rebuilt on the pool; PRESETS /
  ALGORITHM steps; OPT + PRESETS the engine picker (a momentary layer while OPT is held, horizontal picker, the part
  switches on release); the save dialog (a picker page before the naming page), SAVE held 1 s in it; the editor's
  KNOB 1 steps the same pool.
- `cr_bank.c`: the trims table by (engine, preset); the bank rows removed (the tests that name bank rows: update).
- `upreset.c` glue (`cr_bank.c`'s wrappers): the binding bytes written by the save, read by the pool builder.
- `eng_fm6.c`: PIANO as a factory preset.
- Settings record v6; `tests/cr_settings_test.c`.
- Tests: `tools/emu/scripts/cr_presets.txt` (PRESETS stays in the engine; OPT + PRESETS changes it; the popup texts;
  save-as-new lands at the next place and PRESETS shows it; overwrite keeps the number; reset brings the factory
  name back; the per-engine memory across an engine change), `test_persist.sh` (a saved preset after a relaunch),
  `cr_draw` fixtures from the preset sheet, `install_test.py` / `test_sounds.mjs` for the binding bytes.
- Docs: PLAN.md §3 (PRESETS / ALGORITHM / SAVE rows, §4 "Sound editing"), docs/INTEGRATION.md §7, docs/EDITOR.md §7-§8,
  docs/SOUNDS.md (the record's binding), the release notes.

## Also in this pass (the user, 2026-10-07)

- **The PERF layer's root keys play the chord** as outside the layer, so a mode change is heard at once; SELECT picks
  the mode (it already moves the picker; KNOB 1..4 stay the mode's parameters). The same for the **FX layer** (SELECT
  picks the effect, the keys play). KEY (tonic), LOOP (slots), BASS (preview) keep their key maps. The footers change
  accordingly ("SELECT: mode · OCT-: back · HOME: home"). The LEDs on the roots in PERF / FX go back to the chord's.
- The LOOP knob row's **Quantize glyph at "none"** must read as no grid (no pulses), not a dense one: pct 0 draws an
  empty baseline; the grid densities start at 1/4.
