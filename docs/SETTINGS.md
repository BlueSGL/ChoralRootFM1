# ChoralRoot FM-1: settings persistence

Files: `firmware/src/cr_settings.h` (the record), `firmware/src/cr_settings.c` (import / apply / capture, and
in the firmware unit the flash side and the UI glue), `firmware/src/settings_persist.c` (Felucca's record,
now PER5), `tests/cr_settings_test.c`, the emulator's flash in `tools/emu/emu_hal_fw.h` and `tools/emu/emu_fw.c`,
and `tools/emu/test_persist.sh`.

Since 0.18 there are two records: the **settings block** (the options: 192 bytes inside Felucca's settings object,
unchanged in size since 0.1) and the **session** (the state of the instrument: both parts' sounds as they are, the
voicing, the octave, Bass on, LOCK, the FX layer; 1024 bytes in a flash object of its own). **Saving is manual**
(the owner, 2026-10-11): what the Options menu alone sets is still written by itself; everything else, in either
record, is written only by the **global save** (SAVE held), and the unit powers on as the last global save left it:
[Manual and automatic](#manual-and-automatic-018). The inventory of what is kept where is in
[The session](#the-session-018).

## Manual and automatic (0.18)

"Hold Save button to manually do a global save, instead of trying to auto-save settings at runtime. So if you power
cycle without global saving it will reset to what it was. You could also hold GLO + save to reload the global
settings and current preset, which would also make a nice feature for being able to change a bunch of settings then
quickly revert back or save." (the owner, 2026-10-11)

| gesture | does | the screen |
| --- | --- | --- |
| **SAVE held** past the hold time (Options > Hold Time, as the layer buttons), alone | the **global save** (`cr_ui.c cu_gsave` -> `cr_settings_commit` + `cu_loop_write`): the whole settings block, the session, and the loop in RAM if it changed | the message box `saved`, green (a flash error: `save error`, red) |
| SAVE held while a loop plays or a capture is open (armed, count-in, recording, overdub, step entry) | nothing is written (no flash erase then); the save waits (`cu.gsave_pend`) and happens when the loop has stopped and no capture is open | `saves at stop`, then `saved` at that moment |
| **OPT (GLO) + SAVE**, both held 1 s (`CU_REVERT_MS`; either one first) | the **revert** (`cu_revert` -> `cr_settings_revert`): what the last global save wrote, applied as a power-on applies it | `reverted`, white |
| SAVE tapped | the save-preset dialog, as ever (a preset is written at once) | |
| SAVE held in the naming dialog, 1 s | delete / reset the preset, as ever (no global save there) | |
| SAVE held + a key, a knob or another button | a combo: nothing (the loop slots' Save / Load / Delete map of 0.17 is gone: `L_SAVE` is retired as `L_RSV6`, the layer numbers stay) | |

No confirm for either ("No confirm, just a message to communicate what happened"). SAFE MODE: `safe mode: no saving`
/ `safe mode: no revert` (nothing is read or written there). After a backup's settings were restored and before the
restart: `restored: restart first`.

**Every setting and its side.** Automatic = written by `cr_settings_poll` when the unit is quiet (the rules below,
unchanged); manual = written only by the global save. The rule for a setting reachable both from the Options and
from a layer: it goes with the layer (manual).

| setting | where it is set | record | side |
| --- | --- | --- | --- |
| Play Style | Options | block `playstyle` | **automatic** |
| Extension Addition | Options | block `extadd` | **automatic** |
| Secret Chords | Options | block `secret` | **automatic** |
| Velocity | Options | block `vel` | **automatic** |
| MIDI Perform, MIDI Bass, MIDI Raw Chord | Options | block `midi_en[3]`, `midi_ch[3]` | **automatic** |
| Raw Chord Sound | Options | block `raw_sound` | **automatic** |
| MIDI Clock | Options | block `clock_mode` | **automatic** |
| Motion | Options | block `motion` | **automatic** |
| Chord Names (0.20) | Options | block `pick_roots` bit 3 | **automatic** |
| LEDs | Options | block `leds` | **automatic** |
| Hold Time | Options | Felucca's `bold` (HOLD), in the same flash object | **automatic** |
| USB Record, USB Level | Options | block `usb_in`, `usb_level` | **automatic** |
| USB Rate (0.19) | Options | block `pick_roots` bit 2 | **automatic** |
| Dual Core | Options | block `view` bit 6 | **automatic** |
| Output (0.19) | Options | block `pick_roots` bit 1 | **automatic** |
| the palette (no control: MOD) | | block `palette` | automatic |
| the panel calibration | Options > Calibrate, OCT- + OCT+ at power-on | Felucca's `panel` | **automatic** (written by the calibration itself) |
| Bass Behaviour | Options **and** the BASS layer (SELECT, KNOB 1) | block `bass_mode` | manual |
| Single Notes | Options **and** the KEY layer (KNOB 4) | block `single` | manual |
| Split Point | Options **and** OPT + KNOB 1 | block `split_pc` | manual |
| View | Options **and** HOME (tap, the view menu) | block `view` bits 0..2 | manual |
| the view's lock | the view menu | block `view` bit 7 | manual |
| Key Mode, tonic, scale | KEY tap, the KEY layer | block `key_on` bit 0, `tonic`, `scale` | manual |
| Key plays, Keyboard | the KEY layer's page 2 | block `key_on` bits 1 / 2 | manual |
| the KEY layer's keys (pick / play) | GLO in the KEY layer | block `loop_rec` bit 5 | manual |
| Transpose | the KEY layer | block `transpose` | manual |
| the LOCK layer's two settings | B3 held | block `loop_rec` bits 6 / 7 | manual |
| Perform on, the mode, its parameters | PERF tap, the PERF layer, KNOB 3 | block `perform_on`, `perform_mode`, `perf_sel`, `par` | manual |
| LATCH on | LATCH tap | block `sticky` | manual |
| the tempo | SELECT | block `bpm` | manual |
| the bass register | KNOB 2, the BASS layer | block `bass_voicing` | manual |
| each part's chosen sound, each engine's last place | PRESETS, ALGORITHM, the pickers | block `chord_sound`, `bass_sound`, `pool_pos` | manual |
| FX on | FX tap | block `fx_on` | manual |
| the looper: Length, Quantize, Count-in, Level, the record mode, the PLAY menu's keys | the PLAY menu | block `loop_sync`, `loop_quant`, `loop_count_in`, `loop_level`, `loop_rec` bits 0..4 | manual |
| the looper's Rate | the PLAY menu's KNOB 2 | session `loop_rate` | manual |
| the loop slot selected | the PLAY menu's PRESETS | block `loop_slot` | manual |
| the metronome: on, time signature, level | the PLAY menu's page 2 (KNOB 2), GLO + SEQ, the SEQ key's hold menu (metronome), OPT + SELECT | block `metro_on`, `metro_sig`, `metro_vol` | manual |
| the voicing, the octave | KNOB 1, OCT | session `voicing`, `octave` | manual |
| Bass on | BASS tap | session `bass_on` | manual |
| LOCK on, the chord keys it latched | B3 | session `lock`, `latched` | manual |
| the FX layer: the page picked, the effects' parameters, both parts' sends | the FX layer, KNOB 4, the editors | session `fx` | manual |
| both parts' sounds as they are (unsaved edits, deep patch, name), their Level and Pan | the editor, the layers | session `part[2]` | manual |
| the loop in RAM | REC, the editor, clear, undo | its slot's flash record | manual, **and** written when its slot is left (docs/LOOPER.md) |
| a preset saved (SAVE tap), patches and banks received | | their own flash objects | written at once, as ever |

In the block the automatic fields are 17 bytes and one bit of the 180 after the header (`cr_settings_auto`,
`tests/cr_settings_test.c t_auto`: every byte is one side or the other).

**How the two sides share one flash object.** The block is one 192-byte record inside one flash object, so an
automatic write cannot leave the manual fields out: it writes **flash's own block with the automatic fields as they
are now** (`crs_write(0)`: `crs_rec`, the mirror of what flash holds, with `cr_settings_auto` over it), and the manual
fields in flash stay what the last global save wrote. The global save writes the block as captured, whole
(`crs_write(1)`), then the session (`css_write`). The poll starts an automatic write only when an automatic field
(or Hold Time) changed (`cr_settings_poll`: the last state seen with the Options of now differs from it), so turning
a manual knob costs no erase, ever.

**The revert** applies `crs_rec` (flash's block) with the Options as they are now over it, reads the session object
again (`css_read`, the same "does it belong" rule as at power-on), puts both parts and the effects where `cr_ui_init`
leaves them (`crs_reinit`: an edit not saved is gone), and runs `cr_settings_load`: exactly the power-on path. The UI
(`cu_revert`) first quiets the unit as PANIC does without its screen (`cu_quiet`: notes off, the loop stopped, LATCH
off, the octave 0; LOCK off and its latch up), closes every layer, page and dialog (the engine picker's preview and
the loop editor with them), and afterwards reloads the loop: the slot the block names, from flash, its RAM changes
dropped (a loop staged by a slot switch while it played is written first: that save is the switch's). A unit that
never saved reverts to what it powered on as.

**Audio.** A flash erase holds every IRQ for ~45 ms with the audio buffer zeroed (`storage_hw.c st_erase`). The
global save writes at most three objects, one erase each, back to back (the settings object: 1 erase + 4 page
programs; the session: 1 + 5; the loop slot: 1 + up to 16) and only those that changed (`crs_write`'s compare,
`css_write`'s hash, `crl.dirty`): into silence nothing is heard; into a held chord or a tail, one 45 ms hole a
written object, the sound going on after each (`test_persist.sh` `session_hold.txt`: "audio stalled" in the dump).
It is not deferred to silence: the owner presses and expects it. It IS deferred while a loop plays or a capture is
open. The automatic write keeps main's rule: never while anything sounds above the tail floor.

**Power-on**: as the last global save left it, plus the Options as they were when the unit was last quiet. Never
saved: the defaults. **Migration**: none; what a 0.17 unit or an earlier 0.18 build stored is "the last save".
`cr_settings_save` (Felucca's `settings_save`: the power-on calibration, a backup's LIST, the update gesture) is the
automatic part alone: neither a backup nor the update gesture is a global save.

## Where it lives

Felucca keeps one flash object, `OBJ_SETTINGS`: an A/B sector pair at 0xFC000 / 0xFD000 (`storage.c`). Each
copy has a commit record with a sequence number and a CRC, so a torn write leaves the older copy in charge. Its
payload is `persist_t` (`settings_persist.c`), whose magic marks the layout version:

| magic | adds |
| --- | --- |
| PER1, PER2 | upstream: palette, lowcut, panel table |
| PER3 | `bold` (HOLD now lives there) |
| PER4 | favorites |
| **PER5** | **`cr_settings_t cr`: ChoralRoot's block, 192 bytes, last** |

A PER1 to PER4 record imports as before, and its `cr` block is zeroed. A zeroed block fails the block's magic,
so the ChoralRoot side reads defaults. A Felucca build (`felucca.c` → `project.c`) keeps the block exactly as
saved. Felucca's own fields stay where they were: the panel calibration table, the palette, HOLD in `bold`, and
LEDs in `zoom`.

## The ChoralRoot block (`cr_settings_t`, `CRS_SIZE` = 192 bytes)

The block has its own header:

- `magic` `CRS1`
- `version` (`CRS_VERSION`, currently 15)
- `size` (the writer's `CRS_SIZE`)
- `check`: FNV-1a over bytes 12..size

| field | range | default |
| --- | --- | --- |
| playstyle / extadd / secret | Simple–Free / Add Note, Play Chord / Off, Simple, All | Simple, Add Note, Off |
| key_on, tonic, scale | bit 0 Key Mode 0/1 (**v14: bit 1 the KEY layer's Key plays**, 0 Chords / 1 Notes, `CRS_KEY_NOTES`, `cs.key_notes`; **bit 2 its Keyboard**, 0 Chords / 1 Full, `CRS_KBD_FULL`, `cs.kbd_full`; `CRS_KEY_ON` / `CRS_KEY_PLAYS` / `CRS_KEYBOARD` / `CRS_KEY_BYTE`; a higher bit: the byte's default), 0..11, Major/Minor | off, Chords, Chords, C, Major; a v1..v13 record: its Key Mode, Chords, Chords |
| transpose | −24..24 | 0 |
| single, split_pc | 0 Full Octave / 1 Split Oct ("Split" until v14) / **v14: 2 Split Bass**, 0..11 (the Chords layout's split, a pitch class; the Full keyboard splits at F4, fixed, not stored) | Full Octave, F (5) |
| vel | 1..127 (the keys' velocity) | 100 |
| bass_on | 0/1 | **0**, and always written 0: until 0.17 the rule was "the bass is off at every power-on"; since 0.18 (v15) Bass on is kept, in the session (`bass_on` there), because a firmware older than v15 would switch the engine's bass on from this byte with its own UI still showing off |
| bass_mode, bass_voicing | Chords Only..Solo, −2..4 octaves | Chords Only, 0 |
| perform_on, perform_mode, perf_sel | 0/1, Strum..Harp, the PERF layer's entry 0..6 | off, Strum, 0 |
| sticky | latch | off |
| bpm | 20..300 | 120 |
| par[5][11] | per mode, the engine's `CR_PAR_MIN`..`MAX` | design.md §21 (= `cr_init`) |
| loop_sync, loop_quant, loop_count_in, loop_level | 0..5, 0..5, 0/1, 0..100 | Free, none, **on** (the count-in), 100 (the looper: docs/LOOPER.md) |
| midi_en[3], midi_ch[3] | per stream MAIN BASS RAW | on, on, **RAW off**; channels 1 / 2 / 3 |
| clock_mode | Off / **Out** / In | **Out** (Orchid sends clock) |
| raw_sound | RAW also plays part 0 | off |
| view, motion, leds | view bits 0..2: Chord / Keyboard / Notes / Geek Out / Scope (Options > View, HOME held's menu); **v9: bit 7 the view lock** (`CRS_VIEW` / `CRS_VIEW_LOCK` / `CRS_VIEW_BYTE`; locked: HOME tap never cycles the view); **v12: bit 6 Options > Dual Core On** (`CRS_DC_ON`, `CRS_DUALCORE`; set = On: core 1 started at power-on, docs/DUALCORE.md; v11, 0.15, had the bit the other way round: set = Off, On the default), another bit or view: the default; Full/Calm/Off, Glow/Stock | Chord, unlocked, Dual Core **Off**, Full, Glow; a v1..v8 record: its view, unlocked; a v1..v11 record: Dual Core Off, whatever its bit 6 |
| palette | gfx.c index, 0xFF = MOD | **MOD** |
| fx_on | 0/1 | on |
| chord_sound | v6: the chord part's sound, (engine << 8) \| its pool position (docs/PRESETS.md); 0xFFFF = the UI's default. v1..5: an old list position, read as the default | FM6 TINE EP |
| bass_sound | v6: the bass part's sound (what BASS tap brings), (engine << 8) \| pool position; 0xFFFF = the UI's default (v1..5: read as the default; v6 on ANALOG, engine 0, retired in 0.14: read as the default) | VA DEEP SUB (ANALOG SUB BASS until 0.14) |
| metro_on, metro_sig, metro_vol, loop_slot (v2) | the click, 4/4 3/4 6/8, 0..100, 0..9 | off, 4/4, 70, 0 |
| pick_roots (v3) | bit 0: RESERVED since 0.18 (it was: the engine picker's white roots, 1 choose engines, 0 play, KNOB 4 in the picker): the roots always play there now; written 0, read and ignored. **Bit 1 (0.19): Options > Output**, set = Classic (0.18's master), clear = Clean (`CRS_OUT_CLASSIC`, `CRS_OUT_CLEAN`; the UI's `cs.out_clean`; applied by `crs_usb_apply` at boot without a fade, by `cr_settings_load` and the Options with one: `fx.c fx_output_set`; docs/AUDIO-QUALITY.md). No version for it: the bit is 0 in every older record and 0 is the default. A firmware up to 0.18 finds 2 or 3 out of range and takes its own default for the byte, nothing else. **Bit 2 (0.19, 2026-10-12): Options > USB Rate**, set = ChoralRoot In offers 44.1 and 48 kHz, clear = 44.1 kHz only, 0.18's device (`CRS_USB_48K`, `CRS_USB48`; the UI's `cs.usb48`; to `usb.c` as `ua_off`'s `UA_OFF_48` by `crs_usb_apply` at boot, before `usb_start`, and by `cu_usb_apply` from the Options, which replugs: docs/USB-AUDIO.md "Opt-in"). No version for it either, by the same reasoning: 0 in every older record, and 0 is the default. A firmware up to 0.18 finds 4 .. 7 out of range and takes its own default for the byte, a 0.19 build of before the option (range 0 .. 3) does the same and so reads Output as Clean until it is set there. **Bit 3 (0.20): Options > Chord Names**, set = Standard (lead-sheet names of the notes that sound), clear = Orchid (the ORC-1's naming, the default) (`CRS_NAMES_STD`, `CRS_NAMES`; the UI's `cs.names_std`; to the engine as `cr_chord_names_set` by `cr_settings_load` and the Options: docs/CHORD-NAMES.md). No version, by the same reasoning: 0 in every older record, and 0 is the default. Values 0 .. 15 are in range now (`crs_sanitize`); 0.19 and older find 8 .. 15 out of range and take their own default for the byte: Output reads Clean and USB Rate 44.1 kHz there until they are set again, nothing else is touched | **1** in a record's defaults (0.17 reads it): Output Clean, USB Rate 44.1 kHz, Chord Names Orchid |
| loop_rec (v8; was rsv_usb, v4: usb_out) | v8: the looper's record mode (bits 0..3: 0 Overwrite, 1 Advance, 2 Overdub, 3 Replace, 4 Step; `CRS_LOOP_MODE`) and the PLAY menu's keys (bit 4: 0 Play, 1 Loops; `CRS_LOOP_KEYS`), docs/LOOPER-MODES.md; **v13: bit 5 the KEY layer's keys** (0 pick: a root = the tonic, MAJ / MIN = the scale; 1 play: the keys play as outside the layer; OPT tapped in the layer; `CRS_KEY_PLAY`, `CRS_KEY_KEYS`; the UI's `cs.key_play`); **v13 also: bits 6 and 7 the LOCK layer's styles** (B3 held): bit 6 Type key (0 Clears, 1 Keeps the latched extensions; `CRS_LOCK_KEEP`, `cs.lock_keep`), bit 7 Ext keys (0 Toggle, 1 Pushed; `CRS_LOCK_PUSH`, `cs.lock_push`); a mode out of range: the defaults (every bit of the byte has a job now). Before v8: retired in v5: v4's Options > USB Audio Out (the playback device, removed: docs/USB-AUDIO.md). A 0.14 dev build (still v6) kept the chord part's pool position on FM TONE (engine 15, `eng_quad.c`, code name QUAD) here; v6 → 7 moves it into `pool_pos` slot 0 and clears the byte: 0 since | 0 (Overwrite, Play, pick, Clears, Toggle); a v4 record's byte (1 by default there) is cleared on import; v1..v7 records read 0; a v8..v12 record: its mode and PLAY-menu keys, pick |
| usb_in (v4) | Options > **USB Record**: ChoralRoot In is presented to the computer (docs/USB-AUDIO.md); Off: the serial console instead | **1** (on); a v1..v3 record takes it (its zero would mean off) |
| usb_level (v4) | Options > USB Level: `CRS_USB_MASTER` (the recording follows MASTER) / `CRS_USB_FIXED` (recorded at the full level, MASTER after) | Master |
| pool_pos[2][10], pool_pos10_chord, pool_pos10_bass (v6) | per part (chord, bass), per engine slot (slots 0..9 in `pool_pos`, slot 10 in the two bytes that were `rsv0` / `rsv1`; `crs_pool_get` / `crs_pool_set`). The slots are fixed per engine in 0.13's display order, whatever the order shown (`cr_settings.c crs_slot`): 0 FM TONE (ANALOG's until v7), 1 FM6, 2 VA, 3 PHASE, 4 CZ-1, 5 LOFI, 6 VOICE, 7 TRIO, 8 WHEEL, 9 PHYS, 10 NOISE (3, 5, 6, 7, 9, 10: engines retired in 0.18, the slots reserved, their bytes kept): the pool position last played there, where OPT + PRESETS lands; 0..127, checked against the pool by the glue | 1 (each engine's first preset); a v1..v5 record takes the defaults |

The reserve is used up by v6 (`rsv[20]` became `pool_pos`), and v8 took the last free byte (`rsv_usb`, packed with two
fields), v9 put the view lock in the view's byte (bit 7; an older firmware reads a locked record's view as out of
range, Chord), v11 put Dual Core in bit 6 of the same byte, set = Off, and v12 (0.16) turned it round: set = On, so
that a fresh, erased or zeroed record reads Off, the default since 0.16 (the same caveat: an older firmware reads an
On record's view as Chord; 0.15 reads a v12 record's bit its own way, On as Off and Off as On, until it is set there),
and v13 (0.17) put the KEY layer's keys in bit 5 of `loop_rec`, next to the other "what do the keys do in this menu"
bit, and the LOCK layer's two styles in its bits 6 and 7 (the caveat here: a firmware older than v13 takes a record
with one of the three set for an unknown bit and gives the whole byte its defaults, so after a downgrade with the KEY
layer's keys left on play, or a LOCK style off its default, the record mode reads Overwrite and the PLAY menu's keys
Play until they are set again; nothing else is touched. With all three at their defaults an older firmware reads the
record as before). The byte is full. v14 (0.17) needed two more bits, for the KEY layer's page 2 (Key plays, Keyboard),
and a third value of Single: the bits ride in `key_on`'s byte (bits 1 and 2: Key Mode's own byte, where a 0 / 1 flag
left seven bits unused), 0 = the defaults, and Single's 2 is a new value of an existing byte, so the record did not
have to grow. The caveat is the same kind: **a firmware older than v14 checks `key_on` and `single` against 0..1**,
so after a downgrade with Key plays = Notes or Keyboard = Full it reads Key Mode as off (whatever it was), and with
Single = Split Bass it reads Full Octave; each until set again there, nothing else is touched, and with the three at
their defaults (and Single Full or Split Oct) an older firmware reads the record as before. Bits 3..7 of `key_on`
and the upper bits of the other 0 / 1 bytes (`scale`, `sticky`, `raw_sound`, `fx_on`, ...) are what is left for
flags; a field that needs a byte of its own goes into the session record (below), which has room.

**v15 (0.18) changed no field of the block.** What 0.18 needed (about 400 bytes a part for the edit buffers, the
voicing, the octave, Bass on, LOCK, the FX layer) did not go into a longer block, although "a larger `CRS_SIZE`" was
the procedure written here until 0.17, for three reasons found in the code: (1) `settings_persist.c`
`settings_import` takes a settings object only at its exact size (`n == sizeof *p`), so **0.17 and older would read
a longer object as no record at all**: every option at its default and the panel calibration table gone with it,
and their next save would write that over the unit's settings; (2) `web/fm1backup.js` and `tools/fm1_install.py`
know the block as 192 bytes (`CR_BLOCK`: a ChoralRoot settings object restored on Felucca, and back), and
`cr_backup.c` takes a restored settings object through the same exact-size import, so older backups and newer
firmware would stop meeting; (3) a session written every time a knob is turned should not share a sector pair with
the calibration table. So the block keeps its size and layout, an older firmware reads a v15 block exactly as it read
a v14 one (a newer version of the same size: its fields are taken, `tests/cr_settings_test.c t_v15`), and the version
number says one thing: this block was written by a firmware that keeps a session beside it.

**0.19 added two bits and no version**: Options > Output in bit 1 of `pick_roots` and Options > USB Rate in bit 2
(the table below). The version stays 15: an older record's bits are 0, which is the default of both (Clean; 44.1 kHz
only), so there is nothing to migrate, and a version bump would have said something the bytes do not. This is the
spare-bit case of "How to add a field" below: its steps 3, 5 and 6 (the range in `crs_sanitize`, the mapping in
`crs_capture` / `cr_settings_load`, a check in `tests/cr_settings_test.c`), not 1, 2 and 4 (no byte taken from the
reserve, no version, no migration line because zero is the default); and, being Options, a line in
`cr_settings_auto` (`CRS_PICK_AUTO`: the byte's automatic bits, its bit 0 stays manual and reserved).

**0.20 added one more the same way**: Options > Chord Names in bit 3 of `pick_roots` (Orchid, the default, is 0;
docs/CHORD-NAMES.md). The version stays 15. Steps 3, 5 and 6 again: `crs_sanitize`'s range is 0 .. 15,
`crs_capture` / `cr_settings_load` map the bit to `cs.names_std` (and tell the engine: `cr_chord_names_set`),
`tests/cr_settings_test.c` checks the round trip, every older byte (0 .. 7) and every older version reading Orchid,
and the bit being automatic; `CRS_PICK_AUTO` has it. Bits 4 .. 7 of the byte are free.

**Felucca's SPEAKER setting (0.19).** The object's head holds Felucca's own fields; one of them, `lowcut` (MENU >
SPEAKER there: 1 LOWCUT, 2 BASS+), is a stage of the master that ChoralRoot has no control for. Until 0.19 a unit
that had run Felucca with it on kept it under ChoralRoot, through every save, backup and Reset Settings. Since 0.19
`cr_settings_boot` sets `settings.lowcut` and `fx_lowcut` to 0 before `settings_init` applies them (and keeps what
it found in `crs_lowcut_found`, the emulator's dump), `crs_rec` keeps what flash holds so that the next write of
the object stores the 0, `cr_backup.c crb_commit` stores 0 from a restored object, and `cr_settings_reset` clears
it. docs/AUDIO-QUALITY.md "Left behind" lists every other value another firmware can leave.

### Import rules (`cr_settings_import`)

`cr_settings_import` returns 1 for a current record, 2 for a migrated one, and 0 when it falls back to defaults.

1. **No defaults path:** an absent or short block, a bad magic, version 0, a size that does not fit, or a bad
   checksum gives **all defaults**. It never loops and never hangs; the test feeds it 2000 garbage blocks.
2. **Older record:** an older version, or a shorter size, is copied over the defaults. Any field it lacks keeps
   its default.
3. **Newer record:** a newer, longer record (a downgrade) keeps its first 192 bytes, which hold our fields.
4. **Range check:** every field is checked against its range. A field out of range takes its default and the
   others are kept.
5. **Lists:** `palette`, `chord_sound` and `bass_sound` are checked against the lists by the glue.

Migrations by version (the `in.version < CRS_VERSION` block): v1 → 2 the metronome fields and the loop slot take
their defaults; v2 → 3 `pick_roots`; v3 → 4 the USB settings (`usb_in` on, `usb_level` Master; the zeros there would
switch the recording off); v4 → 5 the USB playback is removed: `rsv_usb` (v4's `usb_out`) is cleared, `usb_in` and
`usb_level` are kept; v5 → 6 presets per engine (docs/PRESETS.md): `chord_sound` / `bass_sound` (old list positions)
read as the defaults, every `pool_pos` 1; v6 → 7 (0.14: ANALOG retired, FM TONE takes its slot 0): slot 0's chord
place from `rsv_usb` when a 0.14 dev build wrote it there (1..127; a 0.13 record's 0: the first preset), its bass place
from `bass_sound` when the bass played FM TONE (else the first preset), `rsv_usb` cleared; a `chord_sound` /
`bass_sound` on ANALOG (engine 0) reads as the default (TINE EP / DEEP SUB). Both bytes of a 0.14 dev record are read,
the slot is what is written; v7 → 8 `loop_rec` (the byte `rsv_usb` was, 0 since v7) takes its default (Overwrite, Play); v9 → 10 every mode's Hold (`par[m][CR_P_HOLD]`) set to 0: the arp no longer
latches by default (it had no control in the ARP layer); LOCK (Sticky Keys) is the latch, STRUM KNOB 4 still sets Hold;
v11 → 12 (0.16: Dual Core is Off by default and bit 6 of `view` means On): bit 6 cleared in every older record,
whatever it said: **Dual Core Off** (a v11 record's On, bit 6 clear, cannot be told from 0.15's default, and its Off,
bit 6 set, would read as On); the view (bits 0..2) and its lock (bit 7) are kept. A v12 record keeps what it says;
v12 → 13 (0.17: the KEY layer's keys in bit 5 of `loop_rec`, LOCK's styles in bits 6 / 7): the three cleared in every
older record (no writer set them): **pick, Clears, Toggle**, the record mode and the PLAY menu's keys kept (`tests/cr_settings_test.c t_key_keys_v13`);
v13 → 14 (0.17: Key plays and Keyboard in bits 1 / 2 of `key_on`, Single's value 2): an older record's `key_on` is
its Key Mode alone (1 stays 1, anything else reads off: no older writer set the new bits) and a `single` above 1
reads Full Octave: **Chords, Chords**, Split kept as Split Oct (`t_page2_v14`);
v14 → 15 (0.18): nothing to migrate (no field changed; `t_v15` reads v12, v13 and v14 records). A
settings object restored from a backup goes through the same import (`cr_backup.c`), so a 0.14 / 0.15 backup restores
with Dual Core Off.

## The session (0.18)

"Too much is forgotten at power-off. We should get this to the point where you turn off, turn back on, pick up
exactly where you left off." (the owner, 2026-10-11, the session's first build: written by itself when the unit was
quiet. The same day's later decision, above: it is written by the global save alone; "0.18" below means "after a
global save".)

### The inventory

Every piece of state a player can see, whether it came back after a power cycle in 0.17, whether it does in 0.18,
and where it lives. "block" is `cr_settings_t` above, "session" is `cr_session_t` (`cr_settings.h`).

| state | 0.17 | 0.18 | where |
| --- | --- | --- | --- |
| every entry of Options (Play Style .. Dual Core, Hold Time) | yes | yes | block; Hold Time in Felucca's `bold` |
| the panel calibration | yes | yes | Felucca's `panel` in the settings object |
| Key Mode, tonic, scale, Transpose | yes | yes | block `key_on`, `tonic`, `scale`, `transpose` |
| the KEY layer: its keys (pick / play), Key plays, Keyboard, Single, the split | yes | yes | block `loop_rec` bit 5, `key_on` bits 1 / 2, `single`, `split_pc` |
| the KEY layer's page, the PLAY menu's page | no | **no, on purpose** | page 1 whenever the layer opens (`cu.kpage`, `cu.lpage`) |
| the performance mode, its parameters, Perform on | yes | yes | block `perform_mode`, `perf_sel`, `par`, `perform_on` |
| LATCH on | yes | yes | block `sticky` |
| the chord LATCH was holding, any sounding note | no | **no, on purpose** | |
| LOCK on | no | **yes** | session `lock` |
| the chord keys LOCK latched | no | **yes** | session `latched` (bit `cr_mod_t`), posted down to the engine at power-on as the keys post them |
| the LOCK layer's two styles | yes | yes | block `loop_rec` bits 6 / 7 |
| the voicing (KNOB 1) | no | **yes** | session `voicing` |
| the octave (OCT) | no | **yes** | session `octave` |
| Bass on | no (off at every power-on, by rule) | **yes** (the rule is superseded by the owner's sentence above) | session `bass_on` |
| which sound the bass part plays | yes | yes | block `bass_sound`, `pool_pos`; and the session's part |
| the bass behaviour, the bass register | yes | yes | block `bass_mode`, `bass_voicing` |
| the bass level (the BASS layer's Level, BASS held + ALGORITHM; OPT + ALGORITHM until 2026-10-12), each part's Level and Pan (MIX) | no ("bass level 102 came back as 92") | **yes** | session `part[k].p[P_LEVEL]`, `p[P_PAN]`: the part's own parameters travel with its record |
| FX on / off | yes | yes | block `fx_on` |
| the page picked in the FX layer (Sends, Reverb 1, Reverb 2, Chorus, Delay 1, Delay 2, Drive) | no | **yes** | session `fx.sel` (an effect tag; the second pages and Sends have their own) |
| the effects' own parameters, GLOBAL (the reverb's 7 and the delay's 9: [FX.md](FX.md); the chorus's rate and depth) | no | **yes** | session `fx.par[]` (tag, value) |
| the chord part's four sends (the FX layer's "Send" cells, KNOB 4) | no (the preset's again) | **yes** | session `fx.amt[]`. The sends are the PART's since 0.18: no sound load touches them (MANUAL section 8) |
| an FM TONE sound's Drive (its AMP screen; the Sends page's Drive while the part plays FM TONE) | no (the preset's again) | **yes**, with the part's sound as it is | with the SOUND: a user preset's record (`upreset.c`, its `P_DIST` field), a factory preset's table row; the session `part[].p[P_DIST]`. The part's own drive send stays beside it: `fx.amt[DRIVE]` (chord), tag 29 (bass). docs/FX.md "FM TONE's Drive" |
| the bass part's four sends (its editor's FX group, CC 91 / 93 / 94 on the bass channel) | no (the bass sound's again) | **yes** | session `fx.par[]`, tags 26..29 |
| which sound the chord part plays, each engine's last place | yes | yes | block `chord_sound`, `pool_pos` |
| each part's sound as it is: an edit not saved, the deep patch, the name, "edited", the pool entry | no | **yes** | session `part[0]`, `part[1]` (the edit buffers) |
| the tempo, the metronome (on, signature, level) | yes | yes | block `bpm`, `metro_*` (a metronome left on clicks at power-on, as before) |
| the looper's settings, its record mode, the PLAY menu's keys | yes | yes | block `loop_*`, `loop_rec` |
| the looper's Rate | n/a | **yes** | session `loop_rate` |
| the loop slot loaded, the loop in it | yes | yes | block `loop_slot`; the slot's own flash record, written by the global save and when the slot is left (docs/LOOPER.md; until 2026-10-11: each time a changed loop stopped) |
| a loop playing | no | **no, on purpose**: the slot is loaded and STOPPED, it never starts by itself | |
| a take in progress | no | **no, on purpose** | |
| the view and its lock | yes | yes | block `view` |
| an open layer, menu, dialog, the editor; the editor's place (group, screen, lane) | no | **no, on purpose**: the unit starts on the view | |
| what a PANIC cleared (octave 0, LATCH off, the latch empty) | n/a | stays cleared: the state left is the state kept | |
| saved sounds, patches, banks | yes | yes | their own flash objects |

`tools/emu/test_persist.sh` sets one recognisable state (`session_set.txt`: a loop in slot 3, an unsaved edit on each
part, Bass on with its Level down, the delay picked and set, Perform on with Arpeggiate, voicing +2, octave -1, LOCK
with MAJ + M7), checks that 4 s later nothing is in flash, holds SAVE, relaunches on the same flash file and checks
every line of it, that the loop is stopped, that nothing sounds, and that nothing is written again.

### The record (`cr_session_t`, `CSS_SIZE` = 1024 bytes)

Header: `magic` `CSS1`, `version` (`CSS_VERSION`, 1), `size`, `check` (FNV-1a over bytes 12..size), then `bind`
(below) and:

| field | range | default |
| --- | --- | --- |
| voicing, octave | -12..12, -2..2 | 0, 0 |
| bass_on | 0/1 | 0 |
| lock, latched | 0/1, bit `cr_mod_t` (cleared when `lock` is 0) | off, none |
| loop_rate | -7..3: the loop's Rate in steps from x1 (x1/16 x1/12 x1/8 x1/6 x1/4 x1/2 x3/4 **x1** x1.5 x2 x4; `cr_loop.h CRL_NRATE`; -7 .. -4 since 2026-10-11: a 0.18 dev build of before reads them as x1); the first byte of the old reserve, so a record written before it reads 0 = x1 with no migration | 0 (x1) |
| fx (`css_fx_t`, 108 bytes) | the FX sub-block, below | nothing stored |
| part[2] (`css_part_t`, 384 bytes each: chord, bass) | `valid`; `eng`, `preset`, `user`, `trim` (the part's `track_t` fields; **`trim` is written and, since 0.19, not read**: a part put back takes the trim of its pool entry from the running firmware's table, `cr_session_part_trim`: a factory preset its `CB_TRIM` row, a user preset bound to one that row, an added user preset and INIT none, as `cu_pool_load`, so a row removed or added by an update holds for a session saved before it); `edited`, `kind`, `fk`, `slot`, `name` (the UI's `cu_snd_t`: the pool entry the sound came from); `np` and `p[96]` (the writer's `P_COUNT`, 91 today, and every parameter: the engine's eight, the envelopes, the LFO, the modulation, the sends, **Level and Pan**); `nblob` and `blob[160]` (the engine's deep patch, `ENG_BLOB_MAX`: FM6 128, VA 110, CZ-1 144, FM TONE 80; none on the other engines) | not valid: the pool's sound (the block's `chord_sound` / `bass_sound`) |

**Reserve: 142 bytes of the 1024** (46 after the state, 20 after the FX block, 8 in each part, 60 at the end), 16 of
the FX block's 24 parameter entries and 5 of a part's 96 parameters; and the object holds 3840 bytes, so `CSS_SIZE`
itself can grow: the import takes a shorter (older) record over the defaults and a longer (newer) one by its prefix,
and no other firmware and no tool reads this object.

**The FX sub-block is versioned on its own** (`CSS_FX_VERSION`, 1) and stores by TAG, never by position, so that the
delay and the reverb can change their parameters without a new record: `sel` is an effect tag (`CSS_FXE_REVERB` 0,
`CHORUS` 1, `DELAY` 2, `DRIVE` 3, and for the FX layer's second pages `REVERB2` 4, `DELAY2` 5, for its Sends page `SENDS` 6: `sel` only, a reader
that does not know a tag keeps its default page; room for 8), `amt[tag]`
that effect's send from the chord part (0..127, `CSS_FX_NONE` = not stored: the sound's own send), and `par[]` up to
24 pairs of a parameter tag and its value. The rules, for whoever reworks an effect:

| Tag | Parameter | Range, default | |
| --- | --- | --- | --- |
| 1 `RSIZE`, 2 `RDAMP`, 3 `RTYPE` | Felucca's ROOM / SPRING reverb | | **retired** with it (0.18's new reverb): never reused, not read, gone from a record at its next save |
| 4 `CRATE`, 5 `CDEPTH` | the chorus's rate, depth (`song.g`) | 0..127, 40 / 60 | |
| 6 `DTIME`, 7 `DFDBK`, 8 `DCOLOR` | Felucca's mono delay | | **retired** with it |
| 9 `XD_TIME` | the first 0.18 build's free time (21 .. 1000 ms, the delay at half rate) | | **retired** (25) |
| 10 `XD_DIV` | .. its synced division (1/32 .. 1/2) | 0..12, 6 (1/8) | `XP_DDIV` |
| 11 `XD_SPREAD` | .. the free spread (x1.00 .. x0.50) | 0..127, 0 | `XP_DSPREAD` |
| 12 `XD_RATIO` | .. the synced spread (1:1 3:4 2:3 1:2) | 0..3, 0 | `XP_DRATIO` |
| 13 `XD_FDBK` | .. feedback | 0..127, 58 | `XP_DFDBK` |
| 14 `XD_SYNC` | .. Free / Tempo | 0..1, 1 | `XP_DSYNC` |
| 15 `XD_MODE` | .. Stereo / Ping-pong | 0..1, 1 | `XP_DMODE` |
| 16 `XD_TONE` | .. the loop's low-pass | 0..127, 70 | `XP_DTONE` |
| 17 `XD_WOW` | .. the time's wobble | 0..127, 24 | `XP_DWOW` |
| 18 `XR_SIZE` | the reverb's size | 0..127, 88 | `XP_RSIZE` |
| 19 `XR_DAMP` | .. damping | 0..127, 56 | `XP_RDAMP` |
| 20 `XR_DECAY` | .. decay (0.3 .. 72 s, 127 Inf) | 0..127, 52 (3 s) | `XP_RDECAY` |
| 21 `XR_PRE` | .. pre-delay (0 .. 138 ms) | 0..127, 16 | `XP_RPRE` |
| 22 `XR_MOD` | .. modulation | 0..127, 56 | `XP_RMOD` |
| 23 `XR_LOW` | .. low cut (20 .. 500 Hz) | 0..127, 60 | `XP_RLOW` |
| 24 `XR_WIDTH` | .. width | 0..127, 127 | `XP_RWIDTH` |
| 25 `XD_TIME2` | the delay's free time (16 .. 740 ms: the delay at the full rate) | 0..127, 91 | `XP_DTIME` |
| 26 `BS_REV`, 27 `BS_CHO`, 28 `BS_DLY`, 29 `BS_DRIVE` | the BASS part's sends (the chord part's are `amt[]`) | 0..127; 10, 0, 0, 0 | `trk[1].p[P_REV ..]`; 29 while the bass part plays FM TONE: `cu_bdrive` (`cr_ui.c cu_bdrive_p`: the part's send waits there, the track's `P_DIST` is the sound's Drive and is stored in `part[1].p[]`) |

22 entries are stored today (24 fit). A session written by 0.17 or by a 0.18 build before the new effects holds
tags 1..8: the chorus's two are read, the six retired ones are ignored, and the new delay and reverb start at
their defaults ([FX.md](FX.md), decision 13). `cr_settings.c` has a static assert on the numbers.

- a new parameter: a new `CSS_FXP_*` number and a line in `cr_settings.c` `CSS_FXG` (tag -> `song.g`'s `G_*`, or
  `cr_fx.c`'s `XP_*` with `x` 1);
- a parameter whose meaning or range changes: a NEW tag; the old number is retired and never reused;
- a parameter removed: its line goes, its number stays retired;
- a reader skips a tag it does not know, keeps its default for a tag it does not find, and for a value outside
  the parameter's range (`GP[g].min..max`, `XFP[id].min..max`);
- `CSS_FX_VERSION` goes up only if `css_fx_t`'s layout changes; a reader that sees another version takes the
  defaults for the whole block (the sounds' own sends, the effects' defaults) and keeps the rest of the session.

**Import** (`cr_session_import`, the block's rules): no record, a bad magic, version 0, a size that does not fit or
a bad checksum: the defaults, which are "nothing stored" (the unit powers on as 0.17 did); a shorter record: what it
has, zeros for the rest (0 is the default of every later field; another default needs a migration line); a longer
one: its first 1024 bytes; every field range-checked on its own; a part with more parameters or a longer blob than
the record can hold: no sound stored. The glue then refuses a part whose `np` is not this build's `P_COUNT`, whose
engine is gone or not a melodic one, whose blob is not that engine's size, or whose pool entry cannot be
(`css_part_put`): that part keeps the pool's sound. `tests/cr_settings_test.c t_session` (round trip of every item,
2000 garbage blocks, shorter, too-new, the FX block by tag and by version, the ranges).

### Where it lives, and its wear

`OBJ_SESSION` (`storage.c`): the A/B sector pair **0xB4000 / 0xB5000**, the first two of the 20 sectors that were
free after FM TONE's patch store (0xB2000 / 0xB3000) in what was Felucca's user sample slot 2 (ChoralRoot has no
SAMPLE engine; docs/HANDOFF.md's flash map and `storage.c`'s say 0xB4000..0xC7FFF free; 0xB6000..0xC7FFF still is).
It is inside `FL_DATA` (`hal/fm1_flash.h`: the HAL writes there), outside the firmware, the update's staging area and
every other object. The same commit protocol as every object: the other copy is erased and written, its header
last, so a write torn by the power switch leaves the previous session in charge (`t_session_flash`).

- **SAFE MODE** neither reads nor writes it (`ST_BLOCKED`), and Options > Flash Data erases it with the rest
  (`cr_ui.c cu_flash_erase`: 58 sectors now).
- **Backup**: it is NOT a backup object (no new id; `web/fm1backup.js`, the manager and `tools/fm1_install.py` are
  unchanged). A backup holds what you keep (settings, saved sounds, loops), not where the unit happened to be. A
  settings object restored over SysEx empties the session (`cr_backup.c crb_commit`: a zero-length object), so the
  restart comes up on the restored settings alone.
- **Wear**: a session save is one sector erase and five page programs. It happens only at a global save (SAVE held),
  and only if the state differs from what flash holds (a hash compare: a save with nothing changed writes nothing).
  The two copies alternate, so a sector is erased every second save. The part is rated for 100 000 erases a sector:
  200 000 session saves, which is 50 saves a day for eleven years. The settings object's pair: the same at a global
  save, plus an automatic write when an Option changed.

### `bind`: which settings a session belongs to

A session is used at power-on only next to the settings it was written beside:

- the settings block read as **current** (v15: this firmware wrote it; an older firmware in between would have left
  its own version there): used;
- any other block (none, the defaults, a migrated v1..v14 one): used only if the settings object in flash is still
  byte for byte the one the session was written beside (`bind` = the object's payload CRC then, 0 = none): a new
  unit that has only ever changed session state, or a unit just updated from 0.17 that has not changed an option
  since;
- otherwise (0.18, then 0.17 which saved a setting, then 0.18 again; another firmware's settings; sectors another
  firmware erased) it is left alone and the unit starts from the settings: `session: .. not these settings'` in the
  emulator's dump (`test_persist.sh`: the flash of the session run with its settings object rewritten as v14).

### Compatibility, both ways

- **0.18 on a 0.17 (or older) flash**: the block migrates as ever (v1..v14), there is no session: the unit powers on
  as 0.17 would, and from the first change on it keeps its session.
- **0.17 (or older) on a 0.18 flash**: it reads the v15 block as it read v14 (same size, same fields; `bass_on` is 0
  there, so no engine / UI mismatch), keeps the panel calibration and every option, and never looks at
  0xB4000 / 0xB5000: voicing 0, the bass off, the pool's sounds, as 0.17 always powered on. Its SAFE MODE erase does
  not cover the session's sectors; `bind` is what keeps that stale session from coming back later.
- **backups**: a 0.17 backup restores on 0.18 and a 0.18 backup on 0.17, the settings object being the same size.

### Runtime

`cr_settings_boot` reads the object (`css_read`) into the one RAM copy, `css_buf` (1024 bytes in POOL: the session at
power-on, or at a revert, until `cr_settings_load` has applied it, afterwards the scratch the state is captured into
for a write; what flash holds is kept as a 32-bit hash, so "unchanged: no erase" needs no second kilobyte). `cr_settings_load` applies it once, at power-on, after the block (`css_apply`): the two parts as
the engine picker's cancel restores a sound (`cu_pick_end`), the FX parameters by tag, the amounts, the voicing and
Bass on directly in the engine (IRQ off, nothing sounds yet), the octave, LOCK and its latch through the engine's
queue. The session is captured and written by `cr_settings_commit` (the global save) and by nothing else: until 2026-10-11
`cr_settings_poll` captured it every 200 ms and wrote a change when the unit was quiet (`css_poll`: gone). The
emulator's dump prints `session: saves N (at power-on: ..) now HASH flash HASH pending P ..` (pending 1: the state
differs from flash's, a global save would write it), the FX layer and each part's sound.

## How to add a field

A new option that fits a spare bit of the block (the list above): of the steps below, 3, 5 and 6, with zero as its
default so that 2 and 4 are not needed (Output and USB Rate, 0.19; Chord Names, 0.20); a field of its own: steps 1 to 6. **Anything that needs a byte:
the session record** (`cr_session_t`): take it from `rsv` (the state), `rsv_fx`, a part's `rsv` or `rsv_end`,
default 0, range in `css_sanitize`, capture and apply in `css_capture` / `css_apply`, a check in
`tests/cr_settings_test.c t_session`, a line in the inventory above; bump `CSS_VERSION` only if an older record's
zero there would be wrong (then a migration line in `cr_session_import`). An FX parameter: a tag (above), no version.

1. Take its bytes from the front of `rsv` in `cr_settings.h` and shrink `rsv` by that many. `CRS_SIZE` never
   changes, and the build checks it (`crs_size_ok`).
2. Bump `CRS_VERSION`.
3. Set its default in `cr_settings_defaults`, and its range in `crs_sanitize` (`CRS_FIX`).
4. If an older record's bytes there are not zero, or zero is not the default, add a line to the
   `in.version < CRS_VERSION` block of `cr_settings_import`: `if (in.version < 2) s->x = d.x;`.
5. If the field is the engine's, map it in `cr_settings_apply` / `cr_settings_capture`. If it is the UI's, map it
   in `crs_capture` / `cr_settings_load` (the glue part of `cr_settings.c`).
6. Add a check to `tests/cr_settings_test.c`.

Never reorder or remove a field. A retired field stays in place, unused.

## Runtime

| call | where | does |
| --- | --- | --- |
| `cr_settings_boot()` | `persist_boot` (cr_shim.c), the emulator's `emu_fw_init` | the flash object → Felucca's fields (panel table, palette, HOLD, LEDs) + the block; the USB settings to `usb.c` (`ua_off`) and `fx.c` (`fx_usb_fixed`) through `crs_usb_apply`, before `usb_start` builds the configuration the computer first reads |
| `cr_settings_load()` | the end of `cr_ui_init` (**patch below**); the emulator calls it after `cr_ui_init` until then | the block → engine (IRQ off), `cr_route`, the UI mirror `cs`, motion, palette, the sounds, tempo |
| `cr_settings_poll()` | every frame (`main.c` `settings_poll` → cr_shim.c; the emulator's `emu_fw_frame`) | captures the state (the block, and Felucca's HOLD beside it: `crs_hold_seen`, below); a change **of an automatic field** (`cr_settings_auto`; a manual one alone starts nothing) is written, as flash's block with the automatic fields of now, once nothing changed for 1.5 s and nothing has sounded for 1 s (no voice of the parts, no chord held or latched, no scheduled note, the master output under -60 dBFS: a flash erase stops the audio for ~45 ms, docs/INTEGRATION.md Performance), never while a loop plays (`CR_SETTINGS_BUSY()`), retried 1 s after a flash error, and skipped when the bytes are unchanged |
| `cr_settings_save()` | `settings_save` (cr_shim.c): the power-on calibration; a backup's LIST; the update gesture | the automatic part now |
| `cr_settings_commit()` | `cr_ui.c cu_gsave_now` (SAVE held), `cr_settings_reset` | the global save: the whole block and the session, now; 0, or -1 on a flash error |
| `cr_settings_revert()` | `cr_ui.c cu_revert` (GLO + SAVE) | flash's block (the Options of now over it) and flash's session applied as at power-on |
| `cr_settings_reset()` | Options > Reset Settings, OCT+ twice (`cr_ui.c cu_reset`) | the defaults: into the block (`cr_settings_factory`), applied (the default sounds, `cr_settings_load` again), saved; 0.18: the session with it (the parts' own parameters, the FX parameters and the effect picked back to `cr_ui_init`'s), so what is saved is a new unit's session |

The automatic saving is driven by change detection, so `cr_ui.c` needs no save call at each Options entry. The poll compares the
block as captured with the last one seen (`crs_seen`). One setting of the Options lives outside the block: **Hold
Time** is Felucca's HOLD (`panel.c` `settings_hold`, saved in `bold`), so the poll watches it beside the block
(`crs_hold_seen`) and a change of Hold Time alone is saved like any other; `crs_write`'s `settings_export` puts it
in the record. Felucca's other fields need no watch: the panel table is saved by the calibration
(`cr_settings_save`), and the palette, LOWCUT and Felucca's LEDS have no control in ChoralRoot's UI (its palette and
LEDs are the block's).

The only call `cr_ui.c` needs
is the load at init. **The patch for `cr_ui.c`**, at the end of `cr_ui_init()`, after `cu_set_tempo(...)` and
before `song.grid = 2;`:

```c
#if CR_HAVE_SETTINGS
    cr_settings_load();                           /* the saved settings over the defaults (docs/SETTINGS.md) */
#endif
```

`cr_settings_load` is defined after `cr_ui.c` in the unit, so `cr_ui.c` also needs a forward declaration near
its top: `static void cr_settings_load(void);`. That declaration must sit inside the same `#if CR_HAVE_SETTINGS`.
The emulator does not define `CR_HAVE_SETTINGS`; there it includes `cr_settings.c` from `emu_fw.c`. Once the
patch is in, define `CR_HAVE_SETTINGS 1` in `emu_firmware.h` too, or keep the emulator's guard: `emu_fw_init`
calls `cr_settings_load()` only `if (!crs_loaded)`.

## Defaults, and Options > Reset Settings

"Default" is one thing: `cr_settings_defaults()` (part 1, portable). Three ways lead to it:

- **no usable record at power-on** (`cr_settings_boot`): no settings object (a new unit, erased sectors), an object
  that is not PER1..PER5, a Felucca / Melodee PER1..PER4 object (`settings_import` zeroes the block), or a block
  without the `CRS1` magic, cut short, or with a bad checksum: `cr_settings_import` leaves the defaults. Play Style
  is Simple on every one of them (`tests/cr_settings_test.c t_fresh_playstyle`, `test_persist.sh`: a PER4 object and
  erased settings sectors made from a flash that had Advanced saved);
- **SAFE MODE**: the record is not read (the defaults for this power-on, nothing written), nor the session; its
  Options > Flash Data erases the data sectors, the settings and the session among them, and the next power-on is
  the first case;
- **Options > Reset Settings** (0.17, the last entry outside SAFE MODE; OCT+ asks, OCT+ again does it; another entry,
  OCT-, HOME or a closed menu drop the ask, as they now do for Flash Data's): `cu_reset` ends the session as a PANIC
  does without its screen (`cu_quiet`: notes off, the loop stopped, LATCH off, the octave 0; plus LOCK off and the
  voicing 0), then `cr_settings_reset()`: `cr_settings_factory(&crs_rec.cr)` (the defaults, keeping `loop_slot`: the
  loop in RAM is that slot's, and the loops are not the reset's), Felucca's HOLD to its default, the parts as
  `cr_ui_init` leaves them (TINE EP and DEEP SUB loaded, the part levels, the bass off), `cr_settings_load()` again
  (the block -> the engine, the routing, the UI mirror), the looper's configuration and USB Record re-applied,
  `cr_settings_commit()`: the reset resets both sides, the manual and the automatic, and writes at once (it is a
  deliberate act: no SAVE held is needed after it). The record then captured is byte for byte a fresh unit's (the emulator dump's
  `settings: now` checksum; `test_persist.sh`), and since 0.18 so is the session (`session: .. now`): the parts'
  Level and Pan and every other parameter a sound's load keeps, the effects' parameters, the effect picked, with
  LOCK, the voicing and the octave that `cu_reset` clears, and the bass off. An edit not saved is gone: the one place
  where it is, now that a power-off keeps it.

What the reset does not write: the panel table (`persist_t.panel`: `settings_export` writes the table in RAM, as
every save does), the favourites, and every other flash object but the session (saved sounds, VA / FM6 / CZ / FM
TONE patches and banks, the loops): `test_persist.sh` compares the flash outside the settings object and the
session's pair byte for byte and the panel table inside it. Dual Core: Off is saved; `cr_c1_off` is read at power-on only, so a running second core runs until
the restart (the message says `restart`). In SAFE MODE the entry works on the session only (`ST_BLOCKED`: no write).
Tests: `tests/cr_settings_test.c t_factory`, `reset_set.txt` / `reset_do.txt` / `reset_check.txt`.

## Power-on calibration

OCT− + OCT+ held at power-on still runs Felucca's HARDWARE CALIBRATION:

- In the ChoralRoot unit, `main.c` has its own port, `cr_panel_setup`, because `ui_input.c` is not in the unit.
  `main.c`'s `#define panel_setup cr_panel_setup` picks it, so cr_shim.c's empty stub goes unused.
- `settings_save` then writes the table into the same record.

## The emulator's flash

`tools/emu/emu_hal_fw.h` holds the 1 MiB NOR as a RAM image backed by a file. It is read at power-on, and every
erase and program is written through to the file, so a crash or kill keeps what was saved. NOR rules apply:

- an erase sets a 4 KiB sector to 0xFF;
- a program only clears bits and must not wrap a 256-byte page.

`emu_fw.c` builds Felucca's `storage.c` and `cr_settings.c` on it with `FELUCCA_FLASH 1`. The rest of the UI is
still built RAM-only by `emu_firmware.h`.

| option | meaning |
| --- | --- |
| `--flash PATH` | the image file (default `build/emu/flash.bin` in windowed runs) |
| (headless) | no file unless `--flash` is given: every scripted run starts fresh and stays deterministic |
| `--no-flash` | RAM only |
| `--save-on-exit` | save the automatic settings at exit, without waiting for the 1.5 s quiet time (`cr_settings_save`; a global save is a script's `btn SAVE 900`) |

`emu_fw.c` takes these options out of `argv` before `emu.c` parses them; they become `--demo`, which ChoralRoot
ignores. The dump (F12 / `dump`) prints the flash file, the number of writes and saves, and whether the record
read as current, migrated or defaults.

`sh tools/emu/test_persist.sh` checks it end to end:

1. SELECT +17 sets 137 BPM, the PLAY menu's mode and keys and Play Style are changed; SAVE held; the record is saved
   once.
2. A relaunch on the same file reads 137 BPM. The BPM meter is captured to `build/emu/test/persist_bpm.ppm`.
3. A run without `--flash` reads 120 BPM.
4. Manual and automatic (`gsave_none.txt`, `gsave_save.txt`): the tempo, the KEY layer's tonic and an Option changed
   and NOT saved: after a relaunch the Option is there, the tempo and the key are not; then SAVE held (`saved`), more
   changes, GLO + SAVE (`reverted`: the tempo, the voicing, Bass on, the chord sound, LATCH back as saved, nothing
   sounding, the Option untouched), GLO + SAVE let go early (nothing); the relaunch: as saved.
5. VA / FM6 / CZ-1 / FM TONE: a deep edit saved to U01, SAVE held; the relaunch powers on with U01 and its patch as saved.
6. Hold Time alone (an Option): set to 600 ms and nothing else: saved once, by itself; the relaunch reads 600 ms.
7. The KEY layer's keys, the LOCK layer's styles, the KEY layer's page 2: set, SAVE held: saved once; the relaunch has
   them in the UI and in the engine.
8. The loops by hand (`loop_persist_stop` / `_save` / `_switch` / `_revert` + `_back`: docs/LOOPER.md).
9. The session: `session_set.txt` / `session_check.txt`, the inventory's state set, nothing written for 4 s, SAVE
   held, found again after a relaunch, the loop stopped, nothing written again; the same flash with a v14 settings
   object: the session not applied; `session_storm.txt`: knobs turned and left alone write nothing, each SAVE held
   writes what changed, a second one nothing; `session_hold.txt`: SAVE held under a held chord writes at once (the
   erases' silence in the dump), an Option changed under it waits for silence; the reset runs: the session a new
   unit's.
