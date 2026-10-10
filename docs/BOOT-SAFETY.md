# Boot safety: a way back to UBOOT when a firmware upload fails (design, 2026-10-07)

The user's ask after the dark unit: *there should always be a quick, simple way to get the device back to UBOOT if an
upload fails*, and the current "hold OCT- and OCT+ for 10 s" is too long. This page is the design; the pieces that
need the real unit (the dump, two experiments) are marked. **Decided 2026-10-07: wait for the Transporter's dump and the two experiments before building any of it** (the SPL's behaviour may change the design). **Built 2026-10-10** after both: the last section, "As built", is what the code does (layout, marker, the maths, the cut-point table, what is still open, the device test plan); the sections before it are the design's history.

## What we have today, and where it failed

| layer | what it does | survives a half-written app? |
| --- | --- | --- |
| the chip's ROM | UBOOT1.00 over USB (the WL82 disk) when the SPL asks for it, or when a USB_KEY is sent on the data lines at power-on (the Transporter) | yes, always; but only the Transporter can trigger it from outside |
| the SPL (`uboot.boot`, flash 0x0000..0x3FFF, never written) | boots the app at 0x4000; honours an UPDATA_PARM record (RAM at 0x01C7FD88; a flash copy at 0xE4F00 written by `ota.c`) to run the update loader at 0xE0000 instead | unknown on hardware: the dark unit says the flash record was not honoured, or the loader did not run (the dump decides) |
| the update loader (0xE0000..0xE4FFF, Felucca's, staged before every update) | USB-MIDI `ota-FM-1`, writes the app area 0x4000..0x93000 sector by sector from low to high, 8 s watchdog; clears the RAM record at its own start | yes, if the SPL starts it |
| the app (0x4000..0x93000) | arms the WDT first thing (`fm1_cstart`), the boot guard (two crash boots -> SAFE MODE, four -> UBOOT), OCT- + OCT+ at power-on = calibration, held 5 s in the main loop = UBOOT, the SysEx soft key | **no**: every one of these lives in the app. With sectors 0x4000..0x25FFF new and the rest old, the entry sector is valid-looking and the code behind it is a mix; the chip jumped into it and hung before anything we own ran (or the SPL rejected it and hung: see the experiments) |

So the only safety that worked was the hardware one, and it needs a board we did not have.

The second core (docs/DUALCORE.md, 0.14): the app starts it at power-on; every path into a reset or a loader holds it first
(`fm1_reboot`, `fm1_enter_uboot`, `fm1_core_reset` / `fm1_enter_update`), so it never runs old code while a loader writes;
the loader and the stub never start it.

## What the dump showed (2026-10-09, the dark unit read through the Transporter)

Two full reads, identical (sha256 `6c075920…`; `docs/TRANSPORTER-HANDOFF.md`, "Result"). Against the V15 package, the
package that was being installed (commit `23a8a75`, rebuilt: its hash is the one the PC had, `e59c47ee…`, so the build
is reproducible) and the build before it:

| region | finding |
| --- | --- |
| 0x0000..0x3FFF | identical to V15's head |
| 0x04000..0x24FFF (33 sectors) | the new package, bit-exact |
| 0x25000..0x92FFF (110 sectors) | the old firmware, every sector fully programmed: **no torn sector**. The loader died between finishing 0x24000 and erasing 0x25000 (33 sectors = the 274 requests, 8 reads per sector) |
| 0xE0000..0xE4FFF | the staged loader **intact**: outer header CRC valid, body byte-identical to `build/loader/ota.bin`, marker at 0xE004C |
| 0xE4F00 | the record **present and valid**: CRC, 0x5A0D / 0x5A01, magic 0x5441, `ota-FM-1_015`, area 0xE0000; the only record in 0x93000..0xFC000 |

**Experiment 1 is answered: the SPL does not start the loader from the flash record at 0xE4F00 on a cold boot.**
Everything the resume path needs was in flash and valid, and the unit never produced `ota-FM-1`. (The dump cannot
separate "the SPL ignores the record" from "the SPL runs the loader and the loader dies on a cold start", but the
result is the same: the flash record is not a resume path.) The RAM record is the only hand-over known to work, and a
power cycle clears it. `ota.c`'s flash copy of the record stays (it is harmless and `ota_boot_cleanup` erases it), but
nothing may rely on it. This settles the design below: the stub is the only safety that survives a power loss.

Decisions taken from this:

- `FELUCCA_OTA_RAMONLY` stays 0 (the flash record costs nothing), but the docs and the installer stop promising a
  resume after a power loss; "charge first, keep the computer awake" is the rule (`INSTALL-COMPAT.md`, README).
- The stub + write order (next section) is the work item, in the order of §5. The two experiments left (§4, 2 and 3)
  are now safe to run: the Transporter restores anything, and `MvaveFM1Unbricker/fm1_transporter_recover.py` does the
  dump / restore in one go. Run them before the stub's hand-over code is written (experiment 3 decides how the stub
  starts the loader).
- The Transporter's dump is also the test oracle for the loader's host model (`tests/ldr_test.c`): the "33 new, then
  old, nothing torn" state is the fixture the stub must turn into a rescue.

## Experiment 2, answered on the unit (2026-10-10): the SPL verifies nothing over the app body

A package of the 0.16 build with one byte of app.bin's 0xFF padding changed (flash 0x918DC, 0xFF -> 0x00) **after**
the CRCs were made: the code identical, `app.bin`'s CRC-16 in its directory entry and the `app_area_head` block CRC
both stale. Installed with the normal installer (neither step 1 nor the loader checks those CRCs: the loader verifies
each sector against the package). The unit restarted into ChoralRoot after the loader's reset, **and again after a
power cycle** (the user), core 1 and the rails as usual. So the SPL jumps into the app area without checking the
body: answer (a) of experiment 2. The dark unit of 2026-10-07 therefore ran its half-new code and hung before the
guard could act; a stub in the first sector does run on a half-written body, and the design below stands. It also
means the directory CRCs need not match for the unit to boot (they stay correct all the same: other tools read them).

Experiment 3 (the RAM record after a reset) is what every install already does from the app: `fm1_enter_update`
writes the record and core-resets, and the SPL starts the loader. The stub hands over the same way; what is new is
doing it right after a cold boot, which the first stub build shows on the unit (the Transporter at hand).

Revisions to the design below, decided with these answers:

- **The stub is written first, not last**, when it differs from flash: marker erased, stub, body, marker. A new
  stub over an old body without a marker is a rescue; the old order left the first install (from a firmware without
  a stub) unprotected until its very end. The only unprotected window is the stub sector's own erase and write.
- **No "looks like a Felucca entry" rule**: a stub exists only where our loader put it, and our loader always goes
  on to write the body and the marker, so the stub is strict: no valid marker, no boot.
- **The marker sits in app.bin's last whole sector** (0x91000..0x91FFF), not 0x92000: that sector also holds
  cfg_tool.bin and the cfg directory, which must not be erased first.
- **Sector 0x4000 must be byte-identical from build to build**, or "written only when it differs" is every time:
  it holds the `app_area_head` directory (0x4000..0x411F) with the two CRCs that change with every build. Four
  compensation bytes in the marker sector make both CRC-16s come out at fixed values, so the directory is constant
  and every CRC stays valid.
- **A rescue that does not reach the loader ends in UBOOT**: the stub counts its rescue attempts in a RAM word that
  survives a warm reset; a second arrival without a valid marker enters the ROM's UBOOT instead of trying again.

## The design: a stub that owns the first sector, and a write order that keeps it true

### 1. A boot stub in sector 0x4000, version-independent

The app's first 4 KiB sector becomes a small **stub** (our code, built once, changed rarely) that the SPL enters as
it enters the app today (the `app_area_head` / `app_dir_head` layout of `tools/fm1pkg_make.py` and Felucca's
`crt0`: the stub keeps that entry convention and the header fields the SPL reads). It:

1. arms the watchdog (as `fm1_cstart` does now, first instruction);
2. scans the key matrix for ~50 ms (Sloop's `recovery_key`) and reads the reset reason;
3. checks the **commit marker** of the app body: the body's last sector (0x92000..0x92FFF) holds `magic, version,
   length, CRC-32 of 0x5000..0x92000, CRC-16 of the marker`; the stub recomputes the CRC-32 over the body (about
   0x8D000 bytes from XIP: a few tens of ms);
4. decides:
   - marker valid and no rescue key -> jump to the app's real entry (the body's first sector, 0x5000);
   - **rescue key held** (OCT- alone at power-on, Sloop's gesture; the user's "quick, simple"; no 5 s or 10 s) or
     marker invalid -> **rescue**: if the loader area 0xE0000..0xE4FFF holds a valid loader (it has a header and a
     CRC: `ota.c ota_stage` checks them when it stages it; the stub checks the same) run it: `ota-FM-1` appears on
     USB-MIDI and the normal installer resumes or re-installs, no extra tool; else enter the ROM's UBOOT
     (`fm1_enter_uboot`: the WL82 disk, for the Unbricker / Transporter).

Nothing in the stub needs the app, the settings or the flash data. The stub is the one thing that has to be right,
so it is tiny, it has host tests (`tests/`, as `cr_bootguard_test.c`), and the loader writes it only when its bytes
differ from what is in flash (a stub change is a rare, announced event).

### 2. The loader's write order

Today: sectors 0x4000 -> 0x92000 in order, each verified. New order, so that **every interruption leaves a state the
stub turns into rescue**:

1. erase the marker sector 0x92000 first (the body is now "uncommitted": the stub would rescue);
2. write the body 0x5000 .. 0x91FFF, sector by sector, verified (as today);
3. write the marker sector last (CRC-32 of what was just verified);
4. only if the package's stub differs from flash: erase and write sector 0x4000, verified (the one window where an
   interruption leaves no stub; the installer says "do not unplug" with emphasis there, and it is a few hundred ms).

An interruption in 1..3 leaves the old stub + an invalid marker: power on -> rescue (the loader or UBOOT). A
power-on with an intact old app and no marker (a unit updated by an older loader, or Felucca / Melodee data) must
still boot: the stub treats "no marker sector at all, body CRC unknown" as **boot the app** only when the body's
first sector looks like a Felucca-family entry (its header magic); the first ChoralRoot install with the new loader
writes the marker and from then on the rule is strict. (To be decided with the experiments below: whether the SPL
itself verifies the `app.bin` CRC-16 in the app directory; if it does, the marker is already there in spirit and the
stub's check is what happens *after* the SPL accepts.)

### 3. The gestures, shortened

| gesture | today | proposed |
| --- | --- | --- |
| OCT- alone held while switching on | nothing | **rescue** (the stub, before the app): the loader on USB-MIDI, "RESCUE · connect USB · open the installer" on the screen if the LCD init is cheap enough in the stub, else the LEDs only |
| OCT- + OCT+ held while switching on | calibration | calibration (unchanged, the app's) |
| OCT- + OCT+ held 5 s in the main loop | countdown, then UBOOT | unchanged as a fallback, but no longer the documented way; the docs say "hold OCT- while switching on" |
| the SysEx soft key from the installer | UBOOT | unchanged |
| the boot guard | 2 crashes -> SAFE MODE, 4 -> UBOOT | unchanged, and now reachable only when the stub has handed over to a valid body |

### 4. What only the unit can tell us (with the Transporter at hand, the experiments are safe: it restores anything)

1. **The dump** (`docs/TRANSPORTER-HANDOFF.md` step 6): is the flash record at 0xE4F00 intact and valid? Is the
   loader at 0xE0000 intact? Which sectors are new? That says whether the SPL honours the flash record at all. If it
   does not, the RAM record is the only hand-over and resume-after-power-loss is impossible by that route, which
   makes the stub the only safety and settles the design. **Done 2026-10-09: it does not (see "What the dump showed").**
2. **What the SPL does with a bad app**: with a known-good flash and the Transporter connected, corrupt one body
   sector (not the first) and power on: does the SPL (a) jump anyway and the app crashes (then the WDT + guard path
   should have reached UBOOT in the dark unit, so something else happened), (b) refuse and hang (the dark unit's
   behaviour), (c) refuse and enter UBOOT? Then the same with the first sector corrupted. The answers fix whether the
   SPL checks the app directory's CRC-16 and what it does on failure.
3. **Does the SPL honour the RAM record after a WDT reset?** (It should: that is how the soft key works.) Needed to
   know whether the stub can hand over to the loader by writing the RAM record + core reset (the simplest way to run
   the loader: no need to parse and jump into it ourselves).

### 5. Work items (after the unit is back)

- `firmware/stub/`: the stub (C + crt0, `.ld`), its host test, and its place in `tools/fm1pkg_make.py` (sector 0x4000
  = the stub; the body from 0x5000; the marker sector at 0x92000; `app.bin`'s CRC-16 over the whole as today).
- `firmware/loader/ldr_core.c`: the write order above; a `--stub` log line; `tests/ldr_test.c` for the interruption
  cases (power loss after k sectors -> the stub rescues: a host model of stub + loader + SPL hand-over).
- `firmware/src/main.c`: the 5 s path kept, the docs changed; the install texts (page, CLI, README,
  INSTALL-COMPAT.md "If an FM-1 is dark") say "hold OCT- while switching on".
- The installers: "do not unplug" emphasised during step 4; a stub-changed warning.
- Felucca upstream: the write order and the stub are worth offering back (issue #61's and this unit's class).

### Non-goals

- A second firmware bank (A/B app slots): the flash map has no room for two apps beside the data.
- Changing the head 0x0000..0x3FFF (SPL, isd_config): never written, by every tool's rule.

## As built (2026-10-10; host-verified, not on a device yet)

Where this section and the older ones above differ, this is what the code does. Sources: `firmware/stub/`
(`stub_core.h` the decision, portable; `stub.c` the device side; `crt0_stub.S`, `stub.ld`),
`firmware/loader/ldr_core.c` (the order), `tools/fm1pkg_make.py` (the marker, the compensation), `tools/build.py`
(`build_stub`), tests `tests/cr_stub_test.c`, `tests/stub_pkg_test.py`, `tests/ldr_test.c`, `tests/ota_test.c`.

### Layout

| flash | XIP | what |
| --- | --- | --- |
| 0x0000..0x3FFF | | the head (SPL, isd_config): never written |
| 0x4000..0x411F | 0x02000000 | the `app_area_head` directory (its block CRC-16, `app.bin`'s CRC-16) |
| 0x4120..0x4FFF | 0x02000120 | **the stub**: app.bin's first 0xEE0 bytes, where the SPL jumps. 2208 B used (58 %), 0xFF behind |
| 0x5000..0x90FFF | 0x02001000 | **the body**: the app, linked from 0x02001000 (`app.ld` XIP 0x8C000 = 573440 B; the build is at 76.7 %) |
| 0x91000..0x91FFF | 0x0208D000 | **the marker's sector**: 0xFF, and in its last 32 bytes (0x91FE0) the marker and the compensation bytes |
| 0x92000..0x92FFF | | the end of app.bin's slot (0xFF), cfg_tool.bin, the cfg directory: the same bytes in every build |
| 0xE0000..0xE4FFF | 0x020DC000 | the staged loader (stored plain), the record's flash copy at 0xE4F00 |

`app.bin` in the directory is still the whole slot (0x4120, 0x8DFBC bytes): no tool that reads packages sees a new
format. `build/choralroot.bin` is app.bin as packed (the stub, then the app); the stub alone is `build/stub/stub.bin`.

RAM: the stub runs on the app's two stacks (0x01C7A000, 0x01C7C000; 4.3 KiB used, the CRC tables) and owns
**0x01C7C000..0x01C7C00F** (`app.ld` moved NOINIT up by 16 bytes; nothing in the app or the loader writes there):

| word | content |
| --- | --- |
| +0 | the rescue word: `0x52534300 \| attempts` ("RSC"); anything else is no count |
| +4 | info: `0x5342` ("SB"), the stub's version, its decision (1 boot, 2 loader, 3 UBOOT) |
| +8 | TIMER4 ticks from the stub's start to its decision (24 per us) |
| +12 | detail: marker code, loader code << 8, rescue attempts found << 16, rescue key << 24 |

The app shows it: SysEx 73 DEBUG values `stub_ms stub_us stub_ver stub_decision stub_detail` (`cr_backup.c`,
`hal/fm1_sys.h fm1_stub_word`), and `fm1_install.py --debug` prints a line `boot stub v1: booted the app after
N ms; marker ok, ...`. The entry state the SPL left (icfg, psr, r0, sp, ssp) is saved to STATUS (0x01C07F20..) by the
stub's crt0 exactly as the app's crt0 did; the app's crt0 no longer writes it.

### The marker

20 bytes at flash 0x91FE0 (plain values; in flash they are SFC-encrypted like the rest of the app area):

| offset | field |
| --- | --- |
| +0 | magic 0x4D425243 ("CRBM") |
| +4 | format 1 |
| +8 | the body's length from flash 0x5000: a multiple of 4, 4..0x8C000 (the image, not the 0xFF behind it) |
| +12 | CRC-32 (zlib's) of those bytes |
| +16 | CRC-32 of the 16 bytes above |
| +20 | four compensation bytes (not covered, not read by the stub) |

It sits in the sector's last page, which the loader programs last; the rest of the sector is 0xFF. A torn marker page
fails the marker's own CRC (`cr_stub_test`: 20000 random partial programmings, none accepted).

### The stub's decision (`stub_core.h stub_decide`, the same code on the device and in the tests)

1. The watchdog (8 s, as `fm1_cstart`), TIMER4, the key matrix pins.
2. The rescue key: OCT- (column 0, row PA8) and OCT+ (column 1) sampled about once a ms. OCT- open in the first 4
   samples: no key, go on (the normal boot spends 3.3 ms here). Otherwise 32 samples; held = OCT- closed in at
   least 28 and OCT+ in fewer than 4 (OCT- + OCT+ stays the app's calibration).
3. No key: the marker (magic, own CRC, format, length), then the CRC-32 of the body through XIP. All good: the
   rescue word cleared, return to crt0, `goto 0x02001000`. Strict: nothing else boots.
4. Otherwise rescue. A rescue word that already counts an attempt: **UBOOT** (the word cleared first, so the
   power-on after the ROM boot starts fresh). Else the plain XIP window is set (as `fl_plain_window_init`) and the
   staged loader checked: the head `ota_stage_head` wrote (CRC, 0xE0000, 0x41, "LOADER.BIN", the length), the inner
   head (CRC, load address 0x01C0A800, size), "FELUCCA-LOADER-1" where `ota_stage` looks for it, and the LZ4 block
   chain from the first block to the stated end with the sizes adding up to the image. Good: the rescue word =
   1 attempt, three short LED flashes, the record `ota_session` builds (byte-identical: `ota_test`, `cr_stub_test`)
   written to 0x01C7FD88, core reset: **the SPL runs the loader**. Not good: one long flash, **UBOOT**.

Not checked: the blocks' contents (the package's CRC of the compressed body is not kept in flash, and the image's
CRC needs the LZ4 decoder). A loader that is whole in structure and bad inside is what the rescue count is for: the
SPL or the watchdog brings the stub back, and the second arrival is UBOOT. The official loader is refused (it would
rewrite the head and the stub).

LEDs, no LCD: the first key column's LEDs, 3 x 80 ms before the loader, 1 x 600 ms before UBOOT. The LCD needs
SPI1, the panel's init and a font: it does not fit comfortably and nothing of it is needed for the rescue.

What the stub does not touch: the reset-reason registers (the boot guard reads them after it, as before:
`cr_bootguard_test` gained the boot after a rescue), the settings, any flash data, the app's .noinit, the second
core (the two hold writes of `fm1_core_reset` before a reset, nothing else).

### Sector 0x4000 is the same in every build: the compensation

The sector holds the directory (0x4000..0x411F) and the stub. Two of the directory's fields change with the body:
`app.bin`'s CRC-16 (over the 0x8DFBC bytes of the slot) and the `app_area_head` block CRC-16 (over the entries, the
padding to 0x120, app.bin and cfg_tool.bin).

The CRC-16 here (poly 0x1021, init 0, no final xor) is linear over GF(2): for messages of equal length
`crc(a ^ b) = crc(a) ^ crc(b)`. Writing four bytes `v` at offset `p` of app.bin (zero before) changes the CRC by
`E(v) = crc(v followed by zeros to the end)`, a linear map of 32 bits to 16. `fm1pkg_make.py compensate` evaluates
`E` on the 32 basis bits, eliminates, and solves `E(v) = crc(app.bin with v = 0) ^ 0x4352`; so `app.bin`'s CRC-16
is **0x4352 in every package with a stub**.

One constraint is enough, not two: the block CRC continues from the state after app.bin, and that state is
`crc(app.bin) ^ (S0 * x^(8 * len) mod P)`, where `S0` is the state after the entries and the padding. The entries
hold `crc(app.bin)` (now a constant) and constants, so `S0` is a constant, the state after app.bin is a constant,
and cfg_tool.bin behind it is the same file: the block CRC is a constant too (with the SDK's cfg_tool.bin and the
stub's layout: the same value for any stub, any body). `fm1pkg_make.py` recomputes all four CRCs of the directory
bit by bit and stops if one does not hold; `stub_pkg_test.py` builds four bodies and compares the sectors.
`build.py` prints the sector's hash (`sector 0x4000 ... sha256`): it changes only with the stub (or the SDK files).

The stub is built from `firmware/stub/` alone (its register sequences are copies of the HAL's, each naming its
source, not includes), so a change elsewhere cannot change it. `firmware/stub/stub.lock` holds the version and hash
of the stub that ships: `build.py` warns when the built stub differs (a release build stops), because installing
such a build rewrites sector 0x4000. A deliberate change: bump `STUB_VERSION`, `FM1_STUB_LOCK=update ./build.sh`,
and say it in the release notes.

### The loader's order (`ldr_core.c`)

For a package with the marker (one extra read finds out):

1. the first time a sector differs from the flash, the marker's sector is erased first (and read back blank; a
   blank one, as in a resumed install, is left alone). An install of the identical package erases nothing;
2. sector 0x4000, only if it differs: the package's sector is in RAM before the erase; erase, 16 pages, read back;
3. the body 0x5000..0x90FFF and 0x92000..0x92FFF, each sector as before (skipped when equal, verified, one retry);
4. the marker's sector, last.

A package without the marker (never ours from here on) is written in address order as before. The hosts are not
affected by the order: the web page counts requests; `fm1_install.py` showed the highest address read, which would
have jumped (the marker probe comes first), and now shows the bytes served. "Do not unplug" is in both already; the
stub sector's window cannot be told from the host (it is the first second of "writing"), so no extra emphasis was
added.

### The interruption model (`tests/ldr_test.c`)

The install is cut after every erase and every programmed page; the flash as it stands goes to `stub_decide`
(marker and body read through the same cipher XIP applies, the staged loader as step 1 leaves it), and the install
is resumed from that state to a bit-exact end.

| old state -> this build | cut points | rescue | boot | old firmware whole | unprotected |
| --- | --- | --- | --- | --- | --- |
| the same stub, another body (every later install) | 1854 | 1853 | 1 (the last) | 0 | **0** |
| no stub: the 0.16 layout (the first install of a stub) | 1871 | 1853 | 1 (the last) | 1 | **16** (cuts 2..17) |
| the dark unit: 33 sectors of one firmware, then another's, no stub | 1871 | 1853 | 1 (the last) | 0 | **17** (cuts 1..17) |

The 16: the stub sector's erase and its first 15 pages (the 16th completes the stub: rescue from there on). Cut 1,
the marker sector's erase, leaves a 0.16 app whole (it does not use that sector); an old firmware that does use it
has 17. The dark unit's state holds no stub, so no code of ours runs on it and it stays dark until the hardware
route revives it; the row says what the new loader does once one runs there (it is unprotected until the stub is
written, as any first install). The same accident with a stub in place (cut 33 sectors in) is a rescue: tested.

In time: a sector erase is about 45 ms typical (400 ms worst) and a page about 1 ms, so the window is roughly
0.1..0.5 s, once per unit (and once more per stub change), against a write of 1 to 2 minutes.

### Size and time

Stub: 2208 B of 3808 (`build.py` stops above 3552). The loader grew by 435 B (ota.bin 7157 B).

Boot delay (estimate; the device's own figure is `stub_ms`):

- key sampling 3.3 ms (4 samples: 2 x 100 us settle each, 800 us apart);
- CRC tables 0.1 ms; the body: 439624 B = 109906 words x 21 instructions (the disassembly's loop) = 2.3 M
  instructions, at the SPL's ~360 MHz and 1 to 2 cycles each: **6 to 13 ms of CPU**;
- the flash read, which is the unknown: 13738 cache lines of 32 B, each a read through the SFC. Per line about 104
  clocks in a 4-wire mode, 168 in a 2-wire one, 296 in plain fast read; at 48 MHz that is 2.2 / 3.5 / 6.2 us, so
  **30 / 48 / 85 ms** for the body (twice that at 24 MHz). Nobody has measured the unit's XIP read rate.

So 40 to 100 ms is the expectation and about 190 ms the worst case of these assumptions. If `stub_ms` is above
~150, `STUB_BODY_CRC 0` (`stub_core.h`) makes the stub trust the marker alone: the loader's order already makes the
marker the commit (it is erased before the first write and written after the last verify), the body CRC only adds
protection against flash that changed by itself. That is a stub change (version 2, sector 0x4000 rewritten once).

### What the stub assumes about the hardware

Each is a copy of code that runs on the unit; the function it was taken from is named in `stub.c`.

| assumption | taken from |
| --- | --- |
| P33 access, the watchdog's four writes | `hal/fm1_sys.h fm1__p33_xfer / fm1__p33_op / fm1_wdt_arm(0x0D)`, first thing in `fm1_cstart` |
| TIMER4 at 24 MHz | `hal/fm1_time.h fm1_time_init` |
| the key matrix pins, the 595 word, OCT- / OCT+ on columns 0 / 1, row PA8 | `hal/fm1_input.h fm1_input_init, fm1__sr_bits, fm1__sr_latch, fm1__rows, FM1_KEYMAP`; the column word is `fm1_input_scan`'s; the 100 us settle is the TIMER5 scan's tick |
| LEDs lit with one column latched | `fm1__led_lines`; a column stays lit for the length of a flash erase in the app today (`main.c`, the TIMER5 comment) |
| the app area reads decrypted through XIP at 0x02000000 + offset - 0x4000 | the app executes there |
| flash from 0x93000 reads plain after the SFCENC window is set, the cache lines flushed | `hal/fm1_flash.h fl_plain_window_init` (without its cli / sti: interrupts stay off) |
| the record at 0x01C7FD88 + a core reset starts the staged loader | `fm1_enter_update`, `fm1_core_reset`; every install does it |
| "usb_update_mode" at 0x01C7FD80 + the P33 reset enters the ROM's UBOOT | `fm1_enter_uboot` |
| the debug write limits may be on (a warm reset) and are dropped before the mailbox writes | the same two functions |
| the stacks at 0x01C7A000 / 0x01C7C000 | `crt0.S`, `app.ld` |

**Open, for the device:**

1. **The rescue word at 0x01C7C000 must survive a core reset, the SPL and the loader.** .noinit starts there in
   every Felucca-family firmware and the boot guard relies on it across resets, but nobody has shown it on a unit
   for this path. If it does not survive, the stub works except for the rescue count: a hand-over that fails would
   repeat instead of ending in UBOOT. Step 8 of the plan answers it.
2. **The hand-over right after a cold boot.** From the app (USB up, clocks set) it works at every install. From the
   stub, a few ms after the SPL, it is new: does the SPL honour the RAM record then, and does the loader's USB come
   up? Step 6. If the loader starts and hangs while feeding its watchdog, nothing resets and no count helps: that
   is the case for the Transporter.
3. **The XIP read rate** (above): `stub_ms`, steps 1 and 2.
4. **UBOOT entered this early.** `fm1_cstart` does it at the same point of a boot (the boot guard's fourth failure),
   but that path has not been seen on a unit either. Step 4.
5. Entering the app with interrupts off (the stub's cli) instead of whatever icfg the SPL left: the app's crt0 did
   cli as its third instruction, so nothing should depend on it.

### Device test plan (the orchestrator flashes; the Transporter and a V15 / 0.16 image at hand, a full battery)

"Stop" in any step = no LED flash, no USB device of any kind (FM-1, ota-FM-1, the WL82 disk) 30 s after switching
on, with and without OCT- held; or a flash pattern that repeats every few seconds for a minute. Then the
Transporter writes the last good image; note what was seen.

1. **First flash**: `build/choralroot.fwsc` over 0.16 with the normal installer. This is the one install with the
   unprotected window (its first second of writing). Expect the usual restart into ChoralRoot.
   `fm1_install.py --debug`: `boot stub v1: booted the app after N ms; marker ok, staged loader not checked, rescue
   attempts before this boot 0, rescue key not held`. Write N down (open question 3; the user should not notice a
   longer start than before). Dark instead: stop.
2. **Cold boot**: off 10 s, on. The same line again; the console's `boot` (or GEEK OUT) says power-on, no SAFE MODE.
3. **OCT- + OCT+ at power-on**: the calibration, as before.
4. **The rescue key, no loader stored**: off, hold OCT- alone, on. One long flash (0.6 s), the screen black, the
   "WL82 UBOOT1.00" disk appears. Off, on without a key: ChoralRoot starts. (It starts with the key held: the key is
   not read; `--debug`'s "rescue key" says which. Nothing appears: stop.)
5. **A body the stub refuses**: `python3 tools/make_stub_test_pkg.py bad-marker`, install
   `build/stub-test-bad-marker.fwsc`. The write runs to 100 %; then three short flashes, the screen black, and the
   FM-1 is `ota-FM-1` (the installer says it did not come back as FM-1_920: expected; `--info` says update loader).
   This is the rescue from a warm reset.
6. **The dark unit's case**: in that state, off 10 s, on, no key. Three flashes, `ota-FM-1` again (open question
   2). One long flash and the disk instead: the rescue word outlived the 10 s (try 60 s) or the loader check failed.
   Nothing at all: wait 20 s (the watchdog), then stop.
7. **The rescue count** (optional): from update mode install bad-marker again. After the write: one long flash, the
   WL82 disk (the second arrival). Off, on: three flashes, `ota-FM-1`. (Update mode straight after the write
   instead: the rescue word does not survive; note it, go on.)
8. **Finish**: from update mode install `build/choralroot.fwsc`. ChoralRoot starts; `--debug` says `rescue attempts
   before this boot 1` (open question 1 answered yes; 0: no).
9. **The real thing**: install a build with another body (any later build, the same stub), pull the cable at about
   30 %, switch off and on: three flashes, `ota-FM-1`, Install again finishes it. With `--debug` afterwards.
10. **The rescue key with a loader stored** (optional, right after step 5 or 6 instead of installing): OCT- held at
    power-on gives the same three flashes; `stub_detail`'s key byte is seen only after the next good boot, where it
    is 0 again, so this step shows no more than 6.
