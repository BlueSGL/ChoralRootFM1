# The flash diet (2026-10-16): what left the firmware's slot, what it cost, what was rejected

The firmware slot is fixed: XIP 573 440 B, flash 0x5000 .. 0x91000 (the boot stub and the data objects around it,
docs/BOOT-SAFETY.md). The GRAIN engine, built on its own branch (claude/grain-engine-f724f9), fills the slot to 99.3 %
when merged with main: the owner chose a diet over growing the slot ("Lets roll with the diet"). The rules of the
diet: the sound and the screens stay as they are, proven bit for bit (the 313 golden renders of `tests/regress.c`
and the engines' own tests; the 313 pixel-exact draw goldens of `tests/run_cr_draw.sh`); a table computed at run
time must not cost the audio ISR anything (the heavy pads have no headroom on the unit: docs/INTEGRATION.md
"Measured on the unit"); a table moved from flash to RAM or POOL needs room there, so shrinking is preferred to
moving; one change a commit, its size line before and after in the commit message; costs on the unit counted from
the listing (`build/choralroot.dis`, about 205 instructions a microsecond), not estimated.

The baseline: main 646abb5 (v0.23 + HISTORY 3y123), `XIP 552 860 B of 573 440 (96.4 %), RAM 78 752 (80.1 %), POOL
239 180 (69.5 %)`.

## What was done

| HISTORY | change | XIP | RAM | POOL | audio ISR | proof |
| --- | --- | --- | --- | --- | --- | --- |
| 3y124 | FM TONE's 27 HARM waves stored as their first half (odd symmetry, exact), whole cycles built into POOL at power-on (`eng_quad.c quad_init`) | -13 728 | 0 | +27 704 | none: `quad_render` byte for byte the same (it reads SRAM now) | `cr_quad_test` 31 466 (+ the mirror check), 313 goldens |
| 3y125 | FM6's `FM6_SIN` built at power-on by Dexed's integer rotation; `FM6_MKI_LOG` / `FM6_OPL_LOG` mirrored from stored quarters (`fm6_tables_init`) | -6 512 | 0 (`.data` -> `.bss`) | 0 | none: the per-sample loops the same length | `fm6_test` (+ the checksum check), `cr_fm6_test`, 313 goldens |
| 3y126 | CZ-1's 64 factory tones packed, a bank of 16 an LZ4 block (`tools/lz4blk.py`), unpacked where a bank or a tone is read (`cz_patch.h cz_lz4_block`, `eng_cz.c cz_factory_bank`) | (see HISTORY) | 0 | 0 | none (the unpack runs in the main loop's load paths, ~2.3 KB of its stack) | `cr_cz_test` 128 of 128 tones, `cr_backup_test`, 313 goldens |

## What was rejected, and why

| candidate | what it would save | why not |
| --- | --- | --- |
| HARM read with a mirror (the second half read reversed and negated a sample at a time, bit for bit the same values) | XIP -12.1 KB with nothing moved to POOL | `quad_render` +1.7 KB; the two HARM presets' host count +26 % (GLASS PAD 1209 -> 1520, DRONE 1183 -> 1503 instructions a sample): FM TONE's 8-note chords on the unit have no room for it while recording |
| `CZ_RECIP` as two halves (x >= 1024 in 16 bits less 32 768) | XIP -1 632 (the table -2 044, the code +412) | the ramped loops of `cz_native_line_mv` grew 4 .. 6 instructions a sample (7 340 -> 7 690 B; `cz_native_noise` 554 -> 612): about 50 us a half for 8 voices with their DCW moving, for 1.6 KB |
| the value-name lists of FM TONE's RATIO / OFFSET columns (`N_QUAD_OFS` 2 KB) formatted at run time | ~2.3 KB | the F_INT-with-names path is read in `params.c`, `cr_pages.c`, `cr_edit.c` and the editor protocol's catalog (`web/test_web.mjs` compares it): too wide a change for 2 KB |
| `eng_page_t` / `param_desc_t` packed tighter (12 KB of `*_PAGES`) | ~3 KB | the struct is already 20 B a column (int16 ranges, a byte format); only `unit` could go, into a union with `names` |
| S and M with fewer phases, CRX at a smaller master size, S Huffman-coded | 17 KB, ~2 KB, 1.5 KB | pixel changes (the phases place every native label; CRX is resampled from its 104 px master); the S decode would slow the most-drawn labels |
| `cr_ring` (6.1 KB) and `CZ_CARRIER` (4 KB) built at power-on into POOL | 10 KB | POOL: with GRAIN's two 32 KB buffers and the HARM cycles it stands at 96.6 %; RAM (80 %) has no room for 10 KB either |
