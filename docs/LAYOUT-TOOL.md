# The layout tool

A web page for designing the pages of the sound editor: which parameters are on a screen, in which lane, on which
knob, and (later) how the screen looks. The owner's request: "a webapp tool in which I can mockup page layouts for
the synth engine sound editor interfaces".

Two things make it more than a drawing program:

- **The preview is the firmware.** The picture in the tool is drawn by the unit's own renderer (`cr_draw.c` on
  `gfx.c`) from the unit's own editor code (`cr_edit.c`) on the real engines and their factory sounds: the whole
  firmware compiled to WebAssembly, as the browser emulator is. What the tool shows is what the unit shows, to the
  pixel (tested: below).
- **Its output is a file the firmware build reads.** A layout is JSON in `design/layouts/`; the build turns it into
  the engine's tables. Layouts are compiled in: a unit never loads one.

It is built in five steps. **Step 2 is what exists today: a read-only viewer.** Nothing can be edited or exported
yet except today's built-in layouts.

## The owner's rules

Verbatim (2026-10-13):

> Main rule for pages is they should all have the same consistent header across pages. None of the pages should use
> the main keybed for shortcuts. While sound designing I want to always be able to play notes. I design it, then we
> figure out how to build it. New UI's are in the build not on the fly changeable.

What follows from them:

1. **One shared header on every editor page.** Every page gets the same header design. This conflicts with today's
   editor: `docs/EDITOR.md` says the editor takes the whole 240 x 240 with "no header bar, no footer hints", and
   `cr_edit.c` `ce_screen` sets `header = 0`. The rule changes that in step 4; the firmware is not changed now. The
   viewer shows the gap: the inspector and the lint list say "no shared header" on every page, and the layout files
   carry `"header": null`.
2. **The 27 keys always play.** No page may use a key as a switch or a shortcut. A switch on a page is operated by
   a knob or SELECT, nothing else.
3. **Design first, build second.** The tool is a design tool before it is a generator: the owner lays pages out
   freely, and how the firmware builds a design is worked out afterwards. So the later steps must let him place
   things the firmware cannot draw yet. Such an element is flagged "not buildable yet"; it is never refused.
4. **Layouts are in the build.** A layout is compiled into the firmware; nothing is loaded or changed at run time
   on a unit. (The preview module's `lay_load` is the tool's own door, in the browser only.)

## Build it and open it

```
sh tools/layout/build_layout.sh     # build/layout-web/crlayout.wasm and crlayout.js (Emscripten, ~/emsdk; 6 s)
python3 tools/layout/make_page.py   # build/layout-web/index.html (web/layout.html with web/fm1layout.js inlined)
open build/layout-web/index.html    # or: python3 -m http.server -d build/layout-web
node web/test_layout.mjs            # the tests (the emulator comparison needs sh tools/emu/web/build_web.sh)
node tools/layout/dump_catalogs.mjs # design/layouts/*.json again, after a change to an engine's pages
```

The page works from `file://`: `crlayout.js` carries the module as base64, so nothing is fetched. `crlayout.wasm`
beside it is the same bytes, for node and other tools. `web/make_site.py` puts the page into the site as
`webapp/layout/` (index.html, crlayout.js, crlayout.wasm) and links it from the landing page; without
`build/layout-web` it warns, leaves the link out and builds the rest. The Pages job (`.github/workflows/pages.yml`)
does not build the module yet, so the published site has no layout viewer until a step is added there.

A link opens a place: `?engine=P5&group=FILT&screen=1&lane=2&preset=1` (numbers from 1; the group by name or
index), `?view=sheet` for the contact sheet.

## The viewer (step 2)

- **Left**: the engines the unit shows (FM6, FM TONE, VA, CZ-1, P5, WHEEL), then the groups (with the button that
  opens each) and every screen and lane.
- **Centre**: the live 240 x 240 at twice the size. Under it SELECT and the four knobs in their cap colours (blue,
  orange, white, green): the mouse wheel, a drag, the arrow keys or the - / + buttons turn one detent, as on the
  unit, and the screen answers as the unit's does (the value, the band, the hot cell for 800 ms). The sound picker
  lists the engine's pool; "Reload sound" undoes every turn. The lint's findings are drawn over the picture (red: a
  text off the screen or over another; yellow dashes: a text cut with an ellipsis) and listed under it; "text boxes"
  outlines every text.
- **Right**: the inspector of the selected lane: the screen's kind and band and where they come from (the engine's
  screen plan, the page titles, or the platform's pages), and per knob the label, the parameter (page and column, or
  a part parameter), the range and default, the format, the value names, the value now.
- **Contact sheet**: every screen x lane of the engine on the chosen sound, each tile with its name, its lint fault
  count and a PNG download; one PNG of the whole sheet. A click on a tile opens it.
- **Download catalog JSON**: today's built-in layout of the engine in the layout file format (generated, read-only).

## The preview module

`tools/layout/layout_web.c` includes the firmware exactly as the browser emulator does (`tools/emu/emu_fw.c`: every
source file, the emulator's HAL) with `-DCR_LAYOUT_PREVIEW`, and adds the functions below. Standalone WebAssembly, no
Emscripten runtime, about 940 KB; instantiating it and powering it on takes a few tens of milliseconds, a screen
about 2 ms.

**The instance.** Power-on with no flash file (a fresh 0xFF flash in RAM: default settings, no user sounds), then
1000 ms of the device clock, then a snapshot of the module's whole static data (about 3.1 MB: every firmware
variable, the RAM flash, the LCD). `lay_reset` copies the snapshot back, and `lay_show` and `lay_catalog` begin with
it. So nothing one call changes (a knob turn, a send, a settings write) reaches a later `lay_show`: the same calls
give the same pixels. Time is simulated and moves only inside `lay_show`, `lay_knob`, `lay_select` and `lay_step`,
by fixed amounts. The wall clock is never read (the loader answers the clock call with 0).

**How the editor is driven.** `lay_show` calls the editor's own functions, the ones the unit's gestures end in:
`cu_pool_load` and `cu_sends_to_fx` (the sound, as PRESETS and the engine picker), `cu_edit_open` (EDIT tapped),
`ce_group` (a group button tapped: once for the group, once more per screen), `ce_select` (SELECT to the lane). Not
injected inputs: reaching any engine and pool position by gestures needs OPT + PRESETS, its commit on release and a
popup to wait out, and would tie the API to today's gestures. `lay_knob` and `lay_select` do inject: detents into
the HAL's encoders, as the emulator's `web_enc`, so the step sizes, the hot cell and the redraw are the unit's own
knob code. The test checks the first choice against the second: the emulator, driven by its panel alone, shows the
same pixels.

**The API** (C functions; the names and meanings are the contract for the later steps; strings are C strings in the
module's memory, valid until the next call):

| function | answer |
| --- | --- |
| `lay_version()` | JSON: `firmware` (the version string), `api` (1), `format`, `format_version`, `engines`: per engine `index`, `name`, `params_hash`, `deep_pages`, in the unit's order |
| `lay_catalog(engine)` | JSON: the engine's built-in layout as a layout file (below). Starts from the reset instance |
| `lay_presets(engine)` | JSON: `presets`, the names of the engine's pool (INIT, then the factory sounds); `lay_show`'s preset is an index into it |
| `lay_show(engine, preset, group, screen, lane)` | opens the editor there (all from 0; group 0 OSC 1 FILT 2 ENV 3 LFO 4 MOD 5 FX 6 MIX). Returns the screen's number of lanes, or -1 no such engine, -2 sound, -3 group, -4 screen, -5 lane. Starts from the reset instance |
| `lay_knob(k, detents)` | KNOB k (1..4) turned, + clockwise; 30 ms pass |
| `lay_select(detents)` | SELECT turned (the lane, running on across the group's screens); 30 ms pass |
| `lay_step(ms)` | the clock on with no input (a hot cell's mark ends after 800 ms) |
| `lay_render()` | pointer to 240 x 240 RGBA: the screen drawn again from scratch. The clock does not move |
| `lay_fb()` | pointer to the LCD as the firmware wrote it: 240 x 240 RGB565, big-endian (the emulator's `web_fb`) |
| `lay_lint()` | JSON for the last `lay_render`: `findings` (`kind` "off-screen", "overlap" or "ellipsis"; `text`; `box` [x0, y0, x1, y1]; `other` for an overlap) and `texts` (every text's box): `tests/cr_draw_test.c`'s rules, from the same hook (`gfx.c` `GFX_HOOK_TEXT`) |
| `lay_state()` | JSON: where the editor is and what the active lane shows now (title line, kind, band, `hot`, the four cells' label, value, pct) |
| `lay_reset()` | the instance as after power-on |
| `lay_buf()`, `lay_buf_size()`, `lay_load(ptr, len)` | **reserved for step 3**: packed tables replacing an engine's built-in pages. Today `{"ok": false, "error": "not implemented"}` |
| `lay_snapshot_bytes()` | the size of the snapshot (a number for tests) |

`web/fm1layout.js` wraps these (`wrap(exports)`: `catalog`, `show`, `knob`, `render`, `lint` ..) and holds the
pure code the page and the tests share (`tiles`, `formatLayout`, `checkLayout`, `ruleNotes`, `sheetGeometry`).

**The hook for step 3.** `firmware/src/cr_pages.c` `cp_deep()`, the one place the editor asks for an engine's
pages, calls `cp_deep_hook` when `CR_LAYOUT_PREVIEW` is defined. The module installs a hook that returns the
built-in table (step 3 will return the loaded one). The macro is the preview module's alone: the device image is
byte-identical with and without the change (sha256 of `build/choralroot.bin` before and after: equal), and the
emulators do not define it.

## The file format, version 1

`design/layouts/<engine>.json` (`fm6`, `fm-tone`, `va`, `cz-1`, `p5`, `wheel`), written by
`tools/layout/dump_catalogs.mjs` from `lay_catalog`. In step 2 the files are **generated and read-only**: they are
today's layouts in the future format, the seed the later steps edit. `web/test_layout.mjs` regenerates them and
compares, so they cannot rot: after a change to an engine's pages, run the dump again and commit.

```
{
  "format": "choralroot-layout",
  "version": 1,
  "generated": "...",                 a note: who wrote the file (a hand-edited file will not have it)
  "engine": {"index": 16, "name": "P5"},
  "params_hash": "f5941e824048848d",  the parameters this layout is for (below)
  "pages": [                          the engine's parameter pages, as the firmware has them (core.h eng_page_t)
    {"page": 5, "title": "FILTER", "cols": [
      {"short": "CUT", "fmt": "int", "min": 0, "max": 127, "def": 0, "names": null, "unit": null},
      ..four; null = an empty column ]}
  ],
  "page_cells": 56,
  "header": null,                     the shared header (rule 1): none yet
  "groups": [                         the editor's groups, in button order
    {"group": 1, "name": "FILT", "button": "SEL", "screens": [
      {"screen": 0,
       "kind": "edit8",               the renderer's screen kind: "edit8" (two rows under a band) or "stack"
       "type": "edit",                how the editor made it: platform, stack, columns, mixer, edit, page
       "band": "filter",              the wide band: none, env, filter, dx, cz, algo, ade2
       "tall": false, "header": false,
       "plan": 3, "plan_band": "..",  only when the engine has a screen plan (FM TONE): its index, its band
       "headings": ["Wave", ..],      only on a stack: the column headings
       "pages": [5, 6],               the parameter pages behind the lanes
       "lanes": [
         {"lane": 0, "title": "FILTER", "cells": [     the title line's right text; four cells, KNOB 1..4
           {"knob": 1, "bind": "deep", "page": 5, "col": 0, "page_title": "FILTER",
            "label": "Cutoff", "short": "CUT", "fmt": "int", "min": 0, "max": 127, "def": 0,
            "names": null, "unit": null},
           ..]}
       ]}
    ]}
  ]
}
```

A cell's `bind` is `"deep"` (a column of an engine page: `page`, `col`), `"track"` (a part parameter: `param`, the
`P_*` id), `"text"` (a fixed text: the platform's MOD routes), `"dash"` or `"none"`. `label` is what the screen
shows (the long label, or the screen plan's), `short` the parameter's own. `style` is the screen plan's cell style
where there is one; `by_mode: true` says the engine may show another label or names by the sound's mode (the VA's
oscillator modes). Strings are Unicode (the firmware's Latin-1 middle dot is U+00B7).

The catalog is read at run time from the firmware's own tables through the editor's own derivation (`ce_scr`,
`ce_lanes`, `ce_view` on the engine's INIT sound); nothing is copied by hand. One rule is the catalog's own: a
column the view hides for the sound at hand (an unused MOD slot shows its source only) is listed as the page's
column.

**`params_hash`** is FNV-1a 64 over what a layout binds to: the engine's index and name, its eight EDIT descriptors
and page titles, every page (title, the four descriptors with their value names and units), the sections and the
screen plan. A layout made for one hash is for exactly those parameters; `checkLayout` refuses another. The
firmware's version is not in the file: the hash is the binding.

Not in version 1 yet, and expected to be added without breaking it: the element vocabulary of step 4, the header's
design, and the per-engine table that says which patch parameter a (page, column) is (P5's `P5_DKEY`, the VA's
`VA_MAP`, FM TONE's `QUAD_MAP`): the generator of step 1 needs it, the viewer does not.

## Tests

`node web/test_layout.mjs` (suite `layout_test` of `tests/run_all.sh`; `layout_build` builds the module, cached by
`tests/hostcc.sh`):

- the module: the version, every engine's catalog (every bound cell has a label and a range; every page cell is on
  some screen; P5 has 19 pages and 56 cells), the refusals, `lay_load`'s reserved answer;
- every screen x lane of P5, VA and FM TONE on the first factory sound: not blank, all different, the same bytes
  drawn twice and shown again later; a knob turn on every lane changes the value, the picture and the hot cell, and
  the mark ends; SELECT moves the lane and back;
- **the frames equal the emulator's.** The browser emulator's module (`build/emu-web/choralroot.wasm`: the same
  firmware without the layout API or the hook) is driven by its panel alone (GLO + PRESETS to the engine, EDIT, the
  group buttons, SELECT) through every screen x lane of the three engines, and its LCD (RGB565) is compared with
  `lay_fb` pixel for pixel: 88 frames, identical; and one more per engine after a knob turn, the hot cell included.
  This is the cheapest exact reference: `tests/run_cr_draw.sh`'s PPMs are drawn from mock-up states, not from the
  editor on a sound, so they have no frame for the same state;
- the lint on every screen x lane of every engine: no text off the screen or overlapping (cut texts are printed as
  notes);
- `design/layouts/*.json` are what the firmware says;
- the page, from `file://` in headless Chromium with `?selftest=1`: the page checks itself (the canvas is drawn, a
  click on a knob's + changes it, the contact sheet has every tile, the lists are filled) and paints a square per
  check, which the test reads from the screenshot.

Not tested: the wheel and drag handlers and the downloads in a browser (the self-test clicks buttons only); any
browser but Chromium.

## What the lint says about today's pages

Every screen x lane of every engine on every sound of its pool (6374 frames, 2026-10-13):

- **no text off the screen and no two texts overlapping**, on any engine;
- **CZ-1, FILT screen 1 ("DCW")**: four labels are cut with an ellipsis on every sound: "DCW key 1", "DCW key 2",
  "DCW vel 1" in the top row and "DCA key 1" in the bottom row do not fit a cell;
- **VA, OSC screen 2 (the "+" stack)**: the column heading "Morph/Shape" (two sounds) or "Shape/Density" (one
  sound) is cut when the four oscillators are in different modes;
- FM6, FM TONE, P5, WHEEL: nothing.

And against the rules: no page has the shared header (rule 1: the catalog says so). Rule 2 is not something the
tool can check yet; by docs/EDITOR.md today's editor uses buttons, SELECT and the knobs only, and the keys play.

## The five steps

1. **The generator.** A build script that turns `design/layouts/p5.json` into P5's page tables, reproducing
   today's 19 pages byte-identically (the device image unchanged): the proof that a layout file can be the source.
   Needs the (page, column) to patch-parameter table in the file.
2. **This viewer.** The preview module, the read-only page, the seed files, the tests.
3. **Editing classic pages, and export.** Move parameters between knobs, lanes and screens of today's page styles;
   rename labels; `lay_load` shows the edited layout at once; export the JSON the generator reads. The header of
   rule 1 can be designed here as a mock-up on the preview.
4. **Free pages.** A small element vocabulary (frame, label, knob, switch, led, bar, glyph, band) and lanes of up
   to four bound elements, so the four knobs + SELECT grammar is kept and no key is ever needed (rule 2: a switch is
   a knob's or SELECT's). Every free page carries the one shared header (rule 1): `docs/EDITOR.md`'s "no header
   bar" changes here. The owner places anything; what the firmware cannot draw yet is flagged "not buildable yet",
   never refused (rule 3). The first use: a P5 oscillator panel in the style of the original instrument.
5. **The other engines.** FM6, FM TONE, VA, CZ-1, WHEEL through the generator; FM TONE's screen plan
   (`eng_screen_t`) becomes a layout file like the rest.

In every step the layout is compiled into the build (rule 4).
