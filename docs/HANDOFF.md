# Handoff: ChoralRoot FM-1, the current state

The current state only. What was done, when and why (the lettered items 3b .. 3y13, with the owner's words) is in
[`HISTORY.md`](HISTORY.md): new work is recorded there, at the end; this file changes only when the state does.

## State in three sentences

Released: v0.1 and v0.11 .. v0.18 (`git tag`; v0.18 is the latest, notes in `docs/releases/`). The 0.19 work is the
dev tree (`FELUCCA_VERSION` "ChoralRoot 0.19" in `firmware/src/choralroot.c`; draft notes `docs/releases/0.19.md`). The
owner's unit runs the latest verified dev build of main, flashed by the orchestrator: check with
`python3 tools/fm1_install.py --info`.

## Repos and folders (all under `/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/`)

| folder | what | remote |
| --- | --- | --- |
| `ChoralRootFM1/` | the firmware, the Mac emulator, the web installer, manager and site | github.com/Quixotic7/ChoralRootFM1, main, tags v0.1 .. v0.17 |
| `ChoralRootFM1Designer/` | the mock-up designer (MIT); PNG sheets in Git LFS | github.com/Quixotic7/MvaveFM1-Designer |
| `MvaveFM1Unbricker/` | the recovery tools, `TRANSPORTER-GUIDE.md` | github.com/Quixotic7/MvaveFM1Unbricker |
| `FM-1-transporter/`, `jl-uboot-tool/`, `MVaveOfficial/`, `Felucca/`, `melodee/`, `sloop-fm1/`, `fm1-x0x/` | upstream tools and references | read-only |
| `FM1VST/`, `fm1-backups/`, `fx-review/`, `UI Inspiration/` | the plug-in plan; the unit's backups; the effects' review WAVs and plots; the look's references | |

Read first: `PLAN.md` (§3 .. §5, §7), `docs/INTEGRATION.md`, the area's own document in `docs/`,
`tools/emu/README.md`.

## Working rules (the owner's)

- The orchestrator designs, briefs and verifies; Opus subagents write the code, each in its own worktree with files
  no other agent has; every report is checked by running the suites.
- Only the orchestrator flashes (`python3 tools/fm1_install.py build/choralroot.fwsc --yes`), one `--info` before a
  flash, never from two sessions, never while the owner uses another firmware: "hold off" until he says the unit is
  back. A full battery or USB power, an awake Mac.
- Never tag or publish unless the owner says "tag it" / "publish"; commit verified batches.
- Headless only: nothing that takes the focus (`build/host/emu --headless --script`; the designer through the
  preview pane, launch entry `fm1-designer`).
- The dcg hook: no `rm -rf`, no `git checkout --`, no branch deletion, no force push, no `>` to a path in a
  variable. Move aside with `mv`, write with the Write tool.
- Never raise the core rails above SYSVDD 14 / VDC14 4 (`hal/fm1_power.h`).
- PA2 must never be driven high: it switches the key matrix off with the backlight (`hal/fm1_lcd_hw.h`).
- The boot stub's bytes are pinned (`firmware/stub/stub.lock`): a stub change rewrites sector 0x4000 and is a
  decision, not a side effect.
- A cost on the unit is counted from the device's disassembly at about 205 instructions a microsecond (docs/FX.md
  "The cost on the unit"), never taken from the emulator's estimate.
- A change that must not alter the sound is proven bit-exact (the goldens, WAV compares).
- Saving is manual (SAVE held; GLO + SAVE reverts) except the Options and the calibration (docs/SETTINGS.md).
- Never two agents in `tools/emu/test_cr.sh` at once in one tree (it wipes `build/emu/test`).
- Taste: one big thing per screen, pickers not lists, role colours (white chords, blue voicing, orange bass, red
  loop, yellow key, green FX), the layers' knob-row grammar, OCT- back / OCT+ confirm / the layer's button held again
  closes.

## Verification

In a fresh worktree `./build.sh` comes first (Docker running; the site and installer tests read
`build/choralroot.fwsc`). No counts here: they go stale.

```
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
./build.sh                  # "stub: ... version 3, sha256 baee55ed142fe1cc" (= stub.lock), the size line, the package
sh tests/run_cr_tests.sh    # exit 0
sh tests/run_cr_draw.sh     # exit 0
sh tools/emu/build.sh && sh tools/emu/test_cr.sh && sh tools/emu/test_persist.sh     # each ends PASS
sh tools/emu/perf.sh        # estimates only: compare with main's, never quote as the unit's cost
python3 tests/install_test.py; for t in backup web sounds installer manager; do node web/test_$t.mjs; done
sh tests/run_tests.sh       # ends ALL HOST TESTS PASSED
```

POOL is the tight resource: read it off the size line.

## Tools for the device

`tools/fm1_install.py` (python-rtmidi only; `--help` is complete): `--info`; `--debug` (SysEx 73 counters, then 78
PEAK, worst cases since the last read, cleared by reading: `stale_halves`, the audible ones, must be 0; `pk_*` the
worst half and its stages; `fx_*` the effects' parts; `irq_gap_*` the longest interrupt mask and where; table in
docs/FX.md); `--stub test|rescue`; `--rescan-midi`; `--screen off|on`;
`--select PART ENGINE POS`; `--backup` / `--restore`; `--sounds` and the per-sound import / export.
`tools/uac_check.py` records "ChoralRoot In" and judges by the unit's counters. After restarts in quick succession
the FM-1's MIDI port can stay offline in CoreMIDI: the tool names it, `--rescan-midi` brings it back. `gh` is not
installed on the owner's Mac.

## Open items

1. **The effects' pops, round 3 on the unit.** The pending bit is cleared before the render and the effects run
   block-wise (docs/FX.md "Late is not slow"); not yet read on the unit: the owner plays CZ-1 with both effects up,
   then `--debug` (PEAK). Switches to hear: `CR_FX_LITE=1`, `CR_ACK_LATE=1` (HISTORY 3y12, 3y13).
2. **Performance work**, in other worktrees: CZ-1's cost (docs/FX.md "CZ-1"), key polling during redraws.
3. **More preset slots for FM6, FM TONE, VA, CZ-1**: asked for with PHYS's cut, not built: the owner's decision
   (HISTORY 3y16 has the facts and the options). There is no table of slots per engine to redistribute: the 32 user
   slots are one pool shared by all engines, a slot takes any engine's sound, and the owner's unit used 10 of them
   on 2026-10-10. More slots mean new flash objects, new backup ids and POOL for their mirrors.
4. **Memory**: the store mirrors read through XIP (HISTORY 3t), FM6's tables to flash.
5. **FM TONE presets by ear** with the owner (docs/QUAD.md).
6. **The designer**: stale PNG sheets, and the landing page's two editor panel pictures (HISTORY 3x, 3y4, 3y12).
7. **The v0.16 release page** may still begin "ChoralRoot 0.16 (in progress)": edit it on GitHub.
8. **The MIDI port offline in CoreMIDI** after rapid restarts: cause unknown (HISTORY 3y2).
9. **The owner's review list** (behaviour, his decision; still true in the code): the BASS layer's footer says "a
   root: preview" but the roots pick the Behaviour silently; BASS + EDIT only with BASS held, not with its layer
   locked, and in the editor ENV + EDIT jumps to the bass sound; Raw Chord Sound On with Perform off doubles every
   note; step entry with LOCK latched does not advance (OCT+ each chord); ALGORITHM one click from off gives 00 INIT,
   a BASS tap the last bass sound; "Key: C" is hidden while a loop plays; Advance outside the PLAY menu: "next: slot
   N" is replaced at once; calibration names the SEL button "SCL"; the Chord view is black after PANIC until a
   chord; the modulation mark is blue for every source but ENV and LFO (the manual names three colours).
10. **CI runs no test suite** (`release.yml` builds, `pages.yml` builds the site).
11. **The emulator's build flags drifted** from the device's: `tools/emu/emu_firmware.h` has `FELUCCA_SLICER` 1
    (`choralroot.c`: 0) and builds without `FELUCCA_UAC`.
12. **M7 / M8**: the sticker sheet; Orchid captures (secret-chord map, chromatic Key Mode, factory patterns).
13. **Hardware checks nobody has done**: the reset-reason bits (console `boot`), SAFE MODE on a real crash, Windows
    enumeration of "ChoralRoot In", the stub's second attempt (one long flash, UBOOT), the dual-core checklist
    (docs/DUALCORE.md), the LED levels' cost (console `inp`).

### For 0.19

- **(a) Audio quality, GitHub issue 3: built, to be heard** (docs/AUDIO-QUALITY.md; HISTORY 2026-10-12 "the output
  chain" and "the look-ahead, Sloop 2.6"). Options > Output: Classic (0.18's master, bit for bit) / Clean (linear to
  -2.0 dBFS, a limiter that holds and looks 32 samples ahead: the output 0.73 ms late, no sample over its threshold;
  MASTER last into the 24-bit word), default Clean in the dev builds. Felucca's SPEAKER setting left behind by another
  firmware is held off. The harness is in the tree (`tests/master_measure.c`, `tools/master_*`); the listening files
  and tables in `../audio-0.19/`. **The owner's decisions**: Output's default for the release; Clean's trim (single
  notes are 2.4 LU quieter than Classic, loud chords up to 1.4 LU louder) or the make-up build (`CR_OUT_MAKEUP=1
  ./build.sh`: the DAC word's unused 6 dB, not heard on the unit yet); the part LEVEL / trims against Sloop's (the
  largest measured difference, untouched); the chorus's mono return; the limiter's hold (40 ms; longer takes 2 to
  5 dB off what is left on loud chords) and 32 or 64 samples of look-ahead; the click and the USB part pairs, which
  lead Clean's output by 0.73 ms. Measured against **Sloop 2.6** in the second round (its master now looks ahead):
  Classic is behind it on loud chords, Clean ahead; the level gap is 6.3 dB (Classic) / 8.8 dB (Clean) on a single
  note, none on the loudest chords. From Felucca 1.0.1 .. 1.5.1 one fix is missing on the live path: MASTER read
  before the audio starts (1.1.5, #137). Listening files: `../audio-0.19/lookahead/`. Not checked on the unit:
  anything (the emulator and the host only).
- **(b) CZ-1: the pops, the level, the click that was left: built, to be heard and read on the unit** (docs/CZ1.md
  "Pops (0.19)" with "The second round", and "Level"; HISTORY 2026-10-12 "CZ-1's pops" and "CZ-1's level and the
  click that was left"). The owner's report ("pop and dropout when holding chords without FX ... probably internal
  clipping") was not clipping: a sounding note struck again cut its own voice, and the DCW depth stepped at a block's
  edge (abb0b81). Since then: **the engine is 6 dB lower** (the owner: "CZ-1 trim sounds good"; a voice adds `* 2`,
  every sample exactly half; the three per-sound trims are gone; user CZ-1 sounds are 6 dB quieter with the rest),
  and **a sounding note struck again at another velocity no longer steps** (a loop's step velocities, MIDI in). The
  two clicks `wavclicks.py` counted on BRASS 1 struck again are not a step: the first bright window of a re-attack
  whose DCW is still open, as the chip model does; the 79 jumps left at C2 are the waves' own edges, each checked
  against its definition. **The owner's**: the level by ear against the other engines (`../audio-0.19/cz-trim/`),
  the per-sound trims proposed for INIT TONE, STRINGS 1, BRASS 1, PIPE ORGAN 2 and four quiet sounds (none
  applied), whether a re-attack should open from a closed DCW when the DCA has let go (not built). **Open in
  another file**: the session record restores a stored trim (`cr_settings.c css_part_put`), so a unit switched off
  on BRASS 1, STRINGS 2 or PIPE ORGAN 1 plays it 5, 3 or 6 dB too quiet until the sound is picked again. **Not
  done on the unit**: anything; `python3 tools/cz_pop_check.py --play` (late halves: the third possible cause of
  the first report) was never run.
- **(c) WHEEL**: the owner is on the fence about cutting it.
- **(d) USB recording at 48 kHz: built, nothing run on a host or the unit** (docs/USB-AUDIO.md "48 kHz"; HISTORY
  2026-10-12). **Opt-in since 2026-10-12: Options > USB Rate, `44.1 kHz` by default** (0.18's descriptors byte for
  byte, `bcdDevice` 3.22; `44.1 + 48 kHz`: 3.23), because the owner's Mac chose 48 kHz by itself. With it on,
  ChoralRoot In offers 44.1 and 48 kHz, the host chooses; 44.1 kHz is bit for bit 0.18's. At 48 kHz the
  six channels are resampled in the audio ISR: about 185 us a half by the listing, which the heaviest CZ-1 patches
  may not have. **The owner's**: the test plan there (macOS at both rates, a phone, Windows; the heavy case with
  `--debug`), then whether six channels at 48 kHz stay opt-in, get the 16-tap filter, become the default, or 48 kHz
  is dropped. The plan has both states of the option now.

## Facts that are easy to get wrong

- **Engines** (`engines.c`): indices are append-only, 0 .. 15; five are shown, in `ENGINE_ORDER`: FM6 (12), FM TONE
  (15, code name QUAD), VA (13), CZ-1 (14), WHEEL (7). Every other slot is `ENG_GONE` (PHYS, 9, since 0.18:
  `FELUCCA_PHYS` 0, its files kept); records of retired engines stay and load as stand-ins (docs/PRESETS.md).
- **Sounds**: a pool per engine = INIT + the factory presets (ROM: FM6 25, FM TONE 16, VA 25, CZ-1 64, WHEEL 5) + the
  user's. The user's are the 32 slots of `upreset.c` (`UP_SLOTS`, `core.h`), shared by all engines; a slot's deep
  patch sits at the same index in its engine's store (`va_store.c`, `fm6_ustore.c`, `cz_ustore.c`, `quad_store.c`).
- **Flash map**: the comment at the top of `storage.c` owns it. The FM TONE store is 0xB2000 / 0xB3000, the session
  0xB4000 / 0xB5000; 0xB6000 .. 0xC7FFF is free; the loops are 0xC8000 .. 0xDBFFF.
- **SAFE MODE's Flash Data** erases 58 sectors (`cr_ui.c cu_flash_erase`).
- **Backup object ids** (`cr_backup.c`'s head): 1 settings, 6 / 7 user banks, 8 the FM6 bank, 9 the VA store,
  10 / 11 FM6, 12 / 13 CZ-1, 14 .. 21 the CZ banks, 22 FM TONE, 40 .. 49 the loop slots. The session is not in a backup.
- **Settings**: the block is v15, 192 bytes, and never grows; the session record sits beside it. docs/SETTINGS.md
  owns both and the add-a-field procedure.
- **The package identity** is always FM-1_920; `./build.sh --release X.Y` sets only the version string and names.
- **Boot**: the stub is app.bin's first bytes, the commit marker at 0x91FE0; OCT- at power-on, or OCT- + OCT+ +
  HOME held 5 s, is update mode (docs/BOOT-SAFETY.md).
- **Audio**: a half is 128 frames (`HALF_FRAMES`, 2902 us), a control block `CTL` 32.
