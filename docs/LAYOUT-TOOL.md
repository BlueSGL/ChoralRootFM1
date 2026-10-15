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

It is built in five steps. **Steps 2, 3 and the design side of 4 exist today**: the viewer, and the designer, in
which today's pages can be rearranged and new free screens laid out, previewed live and exported as a design file.
Nothing is built from a design yet: the firmware support for free pages and the build-time generator are the
remaining step ("The five steps", below).

## The owner's rules

Verbatim (2026-10-13):

> Main rule for pages is they should all have the same consistent header across pages. None of the pages should use
> the main keybed for shortcuts. While sound designing I want to always be able to play notes. I design it, then we
> figure out how to build it. New UI's are in the build not on the fly changeable.

What follows from them:

1. **One shared header on every editor page: the editor's title line.** The owner (2026-10-13): "The current header
   that shows the preset name and the page name is good, no need to change it." So the shared header IS the title
   line the editor draws on every page today (`cr_draw.c` `cr_ed_title`: the sound's name left, the page's name
   right, rows 0 .. 27). Every design screen carries it automatically: it is not an element, it cannot be moved or
   edited, and a free screen's design area is the 240 x 212 under it. (The step-2 viewer's "no shared header" note
   was wrong and is gone; the catalog's `"header": null` means only that the header is not part of a layout.)
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

## The designer (steps 3 and 4)

"Viewer looks good, let's build the designer." (the owner, 2026-10-13). **Design** in the top bar; the engine buttons
work in both modes. The design is kept in the browser (localStorage, per engine; the page works without it) and is
exported as a file.

- **Top**: the design's name; its *Style* (flat or p5: "The p5 style", below); *open a shipped design* (the files of
  `design/layouts/designs/`, e.g. `p5-osc-panel`, `p5-panel`); *New from today* (today's layout, every screen classic); *Import* / *Export JSON* (a file pick and a
  download: no server); *PNG* of the screen; *Contact sheet*; the sound; *Undo* / *Redo* (Ctrl+Z, Ctrl+Shift+Z).
- **Left**: the groups (the editor's buttons: OSC, FILT, ENV, LFO, MOD, FX, MIX) and their screens. The current
  screen has earlier / later, duplicate, delete and *move to* another group. *New screen*: a group and a kind (free,
  classic edit8, classic stack), or *Copy today's* page as a classic screen.
- **Centre**: the live 240 x 240 at 2x with SELECT and the four knobs, as in the viewer, and a badge under it:
  "buildable today" (the firmware's editor draws it as designed) or **"design: not buildable yet"** (the badge's
  title says why). Over a free screen an editing layer on the 4 px grid: drag an element from the strip above the
  screen (frame, label, knob, switch, led, bar, glyph, band) onto it; click to select, Shift+click to add, drag to
  move, the yellow corner to resize; arrows nudge 4 px (Shift 16); Delete; Ctrl+D duplicates, Ctrl+A selects all;
  align left / top / right, spread. **Copy and paste between screens**: Ctrl+C / Cmd+C (or *copy*) keeps the
  selected elements in the page's clipboard (and puts them on the system's as JSON text when the browser lets it);
  Ctrl+V / Cmd+V (or *paste*) puts them on the current free screen at their places (8 px off where an identical box
  is already), with new ids, everything else kept (the parameter too: the lint then says it is twice, for you to
  rebind), selected and in no lane, one undo step (`pasteElements`). The shortcuts do nothing while typing. Each element in a lane wears its "lane·knob" tag in the knob's colour.
- **Lanes**: four coloured slots a lane (KNOB 1..4); SELECT moves between lanes. On a free screen select an element,
  then click a slot (it leaves any other slot); on a classic screen drag a parameter onto a slot, or click the slot
  and pick it in the inspector. Lanes can be added (up to 8), moved up, deleted; a classic lane has its title.
- **Right**: the inspector (a free element's type, parameter, label or text, x y w h, switch positions, led glyph,
  glyph, band, dial size, label size and alignment, colour: `auto` = the editor's lane colours, or a palette token;
  a classic screen's title, kind, band, and the selected slot's parameter and label) and the **parameter palette**:
  the engine's parameters grouped by today's pages, searchable, "placed" on those the design uses; drag one onto a
  slot or an element, or click it to bind the selection.
- **Lint** under the screen: the firmware's text lint (a text cut, off the screen, two texts overlapping) and the
  design's checks: a parameter twice on a screen or in the design, a bound element in no lane or in two, a lane of
  more than four, two elements overlapping (frames excepted), outside the area under the header, more than 24
  elements or 8 lanes, a title wider than the header's right side (112 px), and the "not buildable yet" reasons.
- **Contact sheet**: per group, today's screens x lanes beside the design's; a click on a design tile opens it; one
  PNG of the whole sheet.

Turning a knob changes the preview's sound only; any edit of the design re-opens the screen from the reset
instance, so earlier turns are undone then (the viewer's "Reload sound" behaviour). No element can be bound to a
key, and a design file that tries (any `"key"`) is refused: the 27 keys always play.

### How a design is drawn

**Classic screens: through the real editor.** `packDesign` (web/fm1layout.js) packs the design's classic screens into
the firmware's own tables and `lay_load` hands them to the editor through `cp_deep_hook`: an `eng_deep_t` whose pages
hold, per column, a copy of the built-in descriptor the design binds there, and whose `get` / `set` / `desc` /
`mod_dst` go through to the built-in (page, column), so the engine sees its own patch addresses. Labels come through a
second preview-only hook, `cr_edit.c` `ce_label_hook`. `ce_scr`, `ce_view`, `ce_screen` and `cr_draw` then draw the
design exactly as the unit would, and the knobs and SELECT are the unit's. Two ways:

- **in place**, when every group's classic screens have today's structure (as many, the same kinds and lane counts):
  today's page titles are kept (so the editor derives the same screens, bands and titles), with the design's
  parameter and label in every column. Moves, swaps and relabels within that structure; tested: a relabel changes
  nothing but the label's pixels, a swap makes the knobs turn the swapped parameters.
- **plan**, when the structure changed (screens added, removed, reordered or moved between groups): OSC .. MOD become a
  screen plan (`core.h` `eng_screen_t`, as FM TONE's): one page per lane, an edit8 screen of one or two lanes, its
  band (none, env, filter, algo, ade2) and its title.

What neither can express is flagged **"not buildable yet"** and today's screen is shown in its place: a stack or a
screen of more than two lanes in a changed group, a part parameter moved onto an engine page, a changed platform
screen (FX, MIX, a platform MOD), a lane title or band the in-place way cannot change (a note). A group with free
screens only keeps today's tables, unseen.

**Free screens: the firmware's primitives, not its editor.** The firmware has no free pages yet, so a free screen is
drawn by preview-only calls of `layout_web.c` that use the firmware's own drawing code in the renderer's own six
strips: the header by `cr_ed_title` (the real title line, the sound's name from the editor), text by `cr_text` /
`cr_text_fit` (the unit's faces, sizes and ellipsis), the dial by `cr_cglyph` `CR_G_KNOB` (the 270-degree track and
value arc, bipolar from 12 o'clock), bars by `cr_hbar`, waveforms and pictograms by `cr_cglyph`, the big bands by
`cr_wide`, rectangles by `cr_fill` / `cv_rrect`, colours from `cr_rgb` (the named colours and the palette tokens).
Values are the real engine's: `lay_pget` / `lay_ptext` (the editor's value text) and `lay_pstep` (the editor's own
step, through the engine's `set`), so a knob turn moves the dial; the active lane is in the knob colours (blue,
orange, white, green), the others grey, a turned value on its block for 800 ms, as the editor does. How faithful: the
pixels of each primitive are the firmware's; the composition (where a knob's label, dial and value sit, how a switch
or an led is drawn) is the tool's proposal (`freeOps`), which the firmware will copy when it gets free pages. A band
element takes its values from the classic screen that has that band when the screen opens and does not follow later
turns.

### The draw API (preview only)

| function | does |
| --- | --- |
| `lay_load(ptr, len)` | a packed design (below) for its engine's classic pages: `{"ok", "engine", "pages", "screens"}` or `{"ok": false, "error"}`; engine 255 unloads. Kept across `lay_show` (the tables live outside the snapshot) |
| `lay_pget(page, col)`, `lay_ptext(page, col)`, `lay_pstep(page, col, n)` | a parameter of the editor's sound by its built-in address (page 255: a part parameter, col its `P_` id): the value, the editor's text, n detents by the editor's step (30 ms pass) |
| `lay_draw_begin(title, page)` | a new free page under the title line (the sound's name, the page's name) |
| `lay_draw_text(bold, px, x, y, s, col, align, maxw)` | text on baseline y (align 0 left, 1 centre, 2 right; maxw > 0: cut with an ellipsis) |
| `lay_draw_rect(x, y, w, h, col, r, line)` | filled (r: rounded) or an outline `line` px wide (r: its corners quarter arcs, `cr_arc`); r + 64: the left corners square, r + 128: the right ones (a pair's caps) |
| `lay_draw_glyph(g, x, y, w, h, col, pct, pct2, flags)` | a `CR_G_*` glyph in the box (pct Q8 of 255; `CR_G_LFOWAVE`: the wave in pct2) |
| `lay_draw_knob(cx, cy, size, col, pct, bip)` | the dial |
| `lay_draw_p5knob(cx, cy, size, face, rim, tick, ptr, pct, bip, from)` | the p5 style's knob in a size px box: the 0..10 scale (11 ticks over the 270 degrees, `cr_poly`), the face (`cr_disc`, radius size / 2 - 6, a 1 px rim), the pointer from `from` % (0: 30) to 95 % of the face's radius at CR_G_KNOB's angle |
| `lay_draw_disc(cx16, cy16, r16, col)` | a filled disc (`cr_disc`), centre and radius Q4 (1/16 px) |
| `lay_draw_wave(kind, x, y, w, h, col)` | one cycle of a wave in the box as a 1.5 px line (`cr_poly`), no baseline: 0 saw, 1 triangle, 2 pulse, 3 sine, 4 noise (the p5 led's icon) |
| `lay_draw_hbar(x, y, w, h, col, pct, bip)` | the editor's small level bar |
| `lay_draw_band(kind, wv)` | a `CR_W_*` band at its own rows (24 .. 120) with 20 values |
| `lay_draw_end()` | draws it all in strips, to the LCD; the 240 x 240 RGBA; `lay_lint` then answers for it |
| `lay_text_w(s, px, bold)`, `lay_wv()` | a text's advance; the editor's band values now |

The packed form: `'L' 2 engine npages`, per page `tlen title` and four times `srcpage srccol llen label` (srcpage 255:
an empty column), `section[8]`, `nscreens`, per screen `sec pg0 pg1 band tlen title`.

## The design format, version 2

A design is a "choralroot-layout" file of version 2 with `"kind": "design"`, in
`design/layouts/designs/<engine>-<name>.json` (the generated `design/layouts/<engine>.json` stay version 1: today's
layouts). It binds parameters by **stable id**: `"<page title>:<short>"` for an engine page's column (`"FILTER:CUT"`,
`"OSC B+:LOFREQ"`), `"part:<P_ id>"` for a part parameter (`"part:12"`); the designer's palette and
`paramList(catalog)` list them. `params_hash` ties the file to the parameters as in version 1.

```
{
  "format": "choralroot-layout", "version": 2, "kind": "design",
  "name": "osc-panel",
  "style": "flat",                        optional: how free screens look, "flat" (the default) or "p5" (below)
  "engine": {"index": 16, "name": "P5"}, "params_hash": "f5941e824048848d",
  "header": {"kind": "title-line"},       the shared header: the editor's title line, fixed (rule 1)
  "groups": [                             the editor's groups by name (OSC FILT ENV LFO MOD FX MIX), screens in order
    {"group": 1, "name": "FILT", "screens": [
      {"type": "classic",                 (a) one of today's kinds
       "kind": "edit8",                   edit8 (two rows under a band) or stack
       "band": "filter",                  none env filter dx cz algo ade2
       "title": "FILTER",
       "lanes": [                         1 .. 8 lanes of four cells, KNOB 1..4; null = an empty cell
         {"title": "FILTER", "cells": [
           {"param": "FILTER:RES"},
           {"param": "FILTER:CUT", "label": "Bright"},       a label of the design's own (else the editor's)
           {"param": "FILTER:FENV"}, {"param": "FILTER:KTRK"}]},
         ..]}]},
    {"group": 0, "name": "OSC", "screens": [
      {"type": "free",                    (b) placed elements under the header (x 0 .. 239, y 28 .. 239, 4 px grid)
       "title": "OSC A",                  the header's right text
       "elements": [
         {"id": "fa", "type": "frame", "x": 4, "y": 36, "w": 232, "h": 80, "text": "OSC A"},
         {"id": "a_freq", "type": "knob", "x": 12, "y": 44, "w": 56, "h": 64, "param": "OSC A:FREQ", "label": "Freq", "size": 32},
         {"id": "a_saw", "type": "led", "x": 80, "y": 56, "w": 40, "h": 12, "param": "OSC A:SAW", "glyph": "saw"},
         {"id": "a_sync", "type": "switch", "x": 184, "y": 48, "w": 48, "h": 44, "param": "OSC A+:SYNC", "label": "Sync", "positions": 2},
         {"id": "m_a", "type": "bar", "x": 12, "y": 144, "w": 68, "h": 28, "param": "MIXER:MIX A", "label": "Osc A"},
         ..],
       "lanes": [["a_freq", "a_saw", "a_pulse", "a_pw"], ["a_sync", null, null, null], ..]}   ids, KNOB 1..4
    ]}
  ]
}
```

Elements: `frame` (a titled section box: `text`), `label` (static `text`, `size`, `align`, `bold`), `knob` (dial,
label, value: `size` the dial's height), `switch` (`positions` 2 or 3, the current one filled; `names` optional),
`led` (an on/off square with its label, or a small waveform `glyph`: saw, square, triangle, sine, noise), `bar`
(level), `glyph` (any `CR_G_*` pictogram by name, its fill from the bound parameter if any), `band` (`band`: env,
filter, dx, cz, algo, ade2, at its own rows). `param`, `label` and `colour` (a palette token: white red blue yellow
orange green grey black bg surf text theme accent mid dim line; default `auto`) where they apply. Every bound element
must be in exactly one lane; up to 4 a lane, 8 lanes and 24 elements a screen (the firmware's `cell[8][4]`).
`validateDesign` lists what is wrong; the page refuses a file with errors and opens one with warnings. The design's
`style` (absent, `"flat"` or `"p5"`; anything else is an error) changes how frame, knob, led and switch look and their
new sizes (p5: knob 56 x 64 with a dial of 36, led 40 x 32, switch 56 x 64), not what they bind.

### The p5 style

The owner (2026-10-14): "For the prophet 5, I would like the sound editor UI to better resemble the real prophet 5
interface." and "I'd like update glyph designs to match with the Prophet 5 knobs and layout, lets do mockups and
Designer pages first." `"style": "p5"` composes a free screen's elements as the Prophet-5's panel (`freeOps` →
`p5Op`, drawn by the same primitives; the classic screens and the other elements are unchanged):

- **frame**: a rounded outline (`r`, default 3; 1 px, `line` or its colour), its text in capitals, 10 px bold `mid`,
  centred in the top border on a `bg` block, the border showing either side: the panel's section titles. A frame's
  `r` (0 .. 12; the inspector's *Corners*, 0 .. 12 in steps of 2) rounds it in either style (flat: square without);
  the owner's panel uses 6.
- **knob**: the dial box (`size`, default 36; 28 .. 44) at the cell's top: a printed scale of 11 ticks (0..10) over the
  270 degrees at the box's edge (`line`, the end ticks 2 px), a face of radius size / 2 - 6 in `surf` with a `mid` rim,
  a 2 px pointer in the element's colour (the lane's knob colour when its lane is active, else `dim`). The value (the
  editor's text) sits inside the face at 9 px when it fits (2r - 4; for a parameter of value names, when the widest
  name fits, so it never jumps rows), and the pointer is then short (70 .. 95 % of the radius) so it clears the value;
  else the value goes on the row under the label (10 px, the hot block as flat's). Hot (just turned): the face fills
  with the element's colour, the value and pointer in `bg`. The label under the dial at size + 12, as written.
- **led**: the panel's SHAPE button. On top (10 px) its icon or its label; the icon is ONE cycle of the wave as a thin
  line, no baseline, as the panel prints it (`lay_draw_wave`, 16 x 10: the saw a ramp to 70 % and straight down, the
  triangle one peak, the pulse one ⊓, the sine one hump, the noise a jag), in the lane's colour when its lane is active,
  else `mid` when on and `dim` when off. Under a 2 px gap the cap, 18 x 18 rounded (r 2, `surf`, a 1 px `mid` rim): a
  `black` window band (its top 8 px inside the rim) over a 1 px `line` row, the pad under it, and the LED centred in the
  window: a 5 px `red` disc when on, a 3 px `dim` dot when off. The owner (2026-10-14), with a photo of the buttons:
  "I'd like the buttons to look more like these and also have simple waveshape designs that match." The icon's drawn
  extent (its points' box) is centred over the LED, not its 16 x 10 box (the saw's drop would pull it aside).
  **Two LEDs** (`"leds": 2`, 44 x 32; only for a parameter of two values, onoff or an enum of two, and no `glyph`):
  one wide cap (30 x 18) with two LEDs at 1/4 and 3/4 of its window, the left `red` (the minimum), the right `yellow`
  (the maximum), the current one lit, the other a `dim` dot; the two names (the enum's, else `names`, else OFF / ON),
  9 px, over their LEDs, the current one in the element's colour; a cell 44 high or more prints the label under the
  cap. The panel's FILTER REV. **A pair** (`"pair": "left"` / `"right"` on two single leds that touch: the left's x + w
  is the right's x, the same y and h; `validateDesign` warns otherwise): the left's cap sits at its cell's right edge,
  the right's over the shared rim, their inner corners square, so they read as one wide cap with two windows, each
  icon over its own LED: the LFO's saw | triangle button. The inspector has LEDs (1, 2) and Pair selects. The owner:
  "Good except some buttons don't have the waveform centered, example photo. The prophet also has some buttons with 2
  LEDS: Filter REV; LFO Shape; Sample And Hold LFO." (P5 has no sample-and-hold LFO parameter: no such button.)
  **The label above or below** (`"labelAt"`, the inspector's *Label at*): above (the default; a two-LED button's
  default is below, its names being on top) or below: the cap moves up (to the cell's top + 2, or under the icon or
  the names) and the label is printed under it, 10 px bold, centred. With a glyph the icon stays on top and only an
  explicit label goes under. OSC A's SYNC is so, as the owner asked.
- **switch**: the label on top, a slot (8 px wide, 12 px a position, rounded `line` outline) with a 6 x 8 lever in the
  element's colour at the current position, the positions' names beside it (the current one in the element's colour).
- label, bar, glyph and band as flat, but a label's baseline is its box's bottom edge.

**One baseline rule** (`P5_UNDER`): a text under a graphic (a knob's dial, a led's cap) sits 12 px under its bottom
edge, and a label element's baseline is its box's bottom edge, so elements placed on the 4 px grid put their labels'
baselines on the grid and in line: a knob of size 36 at y 44, a led at y 60 with its label below (cap y + 2 .. y + 20,
label at y + 32: a 32-high cell's bottom) and a label box at y 80, 12 high, all have their baseline at 92. The owner:
"One thing I don't love is that the labels across different parameters and labels don't vertically align, picture for
example "-Shape-" is a couple pixels up off grid, same with SYNC while FREQ and PULSE W are right on the grid." (A
label above a cap, a switch's texts and a two-LED button's names are as before.)

`design/layouts/designs/p5-panel.json` is P5's whole engine in that style: eight free screens over OSC .. MOD (OSC A
with the mixer, OSC B, FILTER with the filter envelope, AMPLIFIER, LFO with its SHAPE, WHEEL-MOD, POLY-MOD, VOICE with
GLIDE), all 56
parameters placed once, KNOB 1..4 a lane, FX and MIX classic as today; every screen x lane lint clean. Since
2026-10-15 it is the owner's own (edited in the page: ENV 2 in FILT, his frames' titles, SHAPE labels, VOICE
rearranged, the filter envelope on the FILTER screen in a third lane), with frames of r 8. The wheel-mod's SOURCE MIX is `LFO:NOISE`: P5's LFO noise amount IS the Prophet's
wheel-mod source mix (LFO to noise; docs/PROPHET.md, the LFO page "Noise (its mix)"), so the WHEEL screen's "SRC MIX"
knob is bound to it and the LFO screen has no NOISE knob; the palette shows it with the hint "(wheel-mod source mix)"
(`PARAM_HINTS`). Its contact
sheet, `design/choralroot-fm1-p5-panel-screens.png`, is made by

```
node tools/layout/render_sheet.mjs design/layouts/designs/p5-panel.json design/choralroot-fm1-p5-panel-screens.png
```

(any design: every screen x lane at 2x with its caption, drawn by the module; it prints each tile's lint). Like every
free screen it is "not buildable yet": the composition is the tool's proposal, which the firmware will copy when it
gets free pages.

**Not buildable yet** means: the design is valid and previewed, but the firmware cannot show it on a unit as it is.
Every free screen is so today; a classic screen is so when the in-place and plan ways above cannot express it.

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
| `lay_buf()`, `lay_buf_size()`, `lay_load(ptr, len)` | a packed design's classic pages for its engine (the designer, below) |
| `lay_snapshot_bytes()` | the size of the snapshot (a number for tests) |

`web/fm1layout.js` wraps these (`wrap(exports)`: `catalog`, `show`, `knob`, `render`, `lint` ..) and holds the
pure code the page and the tests share (`tiles`, `formatLayout`, `checkLayout`, `ruleNotes`, `sheetGeometry`).

**The hooks.** `firmware/src/cr_pages.c` `cp_deep()`, the one place the editor asks for an engine's pages, calls
`cp_deep_hook` when `CR_LAYOUT_PREVIEW` is defined; `cr_edit.c` `ce_label()` asks `ce_label_hook` for a design's own
label. The module's hooks return the loaded design's table and labels, else the built-in. The macro is the preview
module's alone: the device image is byte-identical with and without them (sha256 of `build/choralroot.bin` before and
after: equal), and the emulators do not define it. `lay_version`'s `api` is 2 (the design API) and `format_version` 2.

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
  "header": null,                     (the shared header is the title line: not part of a layout)
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
  check, which the test reads from the screenshot;
- **the designer**: a design's JSON reads back to the same model; today's layout of every engine is a valid design;
  `validateDesign` catches each of 19 broken files (format, version, kind, params_hash, engine, header, group, screen
  type, a lane of three cells, a parameter, a band, an element type, a position, an id twice, a switch of four, an
  led glyph, a colour not a token, a lane naming nothing, a key binding); today's design loaded by `lay_load` gives
  all 22 P5 frames identical to the built-in tables'; a relabel changes only the label's pixels; a swap of Cutoff and
  Reso makes KNOB 1 turn the resonance and KNOB 2 the cutoff; an added screen packs as a plan and shows; the P5
  example renders every screen x lane lint clean and the same bytes twice, its knobs turn its parameters, SELECT runs
  across its lanes and screens, its header is the title line; `validateDesign` refuses a style that is not flat or
  p5; the p5 panel (`p5-panel.json`) valid with no warning, every screen x lane lint clean and deterministic, no
  parameter twice, all 56 placed, KNOB 1 on each of its free lanes turns its parameter and changes the picture, a
  knob's value text inside its dial, a lit led's dot the firmware's red, a saw led's one-cycle icon drawn over its cap
  and the cap's window darker than its pad, every wave icon's ink centred over its LED within 1 px, REV's two LEDs
  (yellow at 3, red at 1/2), a pair's caps touching; `validateDesign` refuses two LEDs on a 0..127 parameter and warns
  on a pair that does not touch; SYNC's label (labelAt below) under its cap; a frame at r 12 leaves its corner pixel
  the background (r 13 refused); the source mix bound once, on WHEEL, in a lane, with its hint; `pasteElements` (new
  ids, no lane, 8 px off an identical box, the parameter then twice);
- the page in Design mode (`?selftest=design`): the shipped example opens, a free screen is drawn and marked, a knob
  turns a dial, **a scripted pointer drag from the element strip adds a knob on the grid**, the export equals the
  model and validates, the contact sheet has today's and the design's tiles, the shipped p5 panel opens in the p5
  style and draws its knobs, a copied element pasted on another screen is in the export with a new id, selected, in
  no lane, one undo step.

`?demo=osc|classic|sheet` opens the places the screenshots show. Not tested: the wheel handler, moving and resizing
by drag, the keyboard shortcuts, import, the downloads (Export / PNG write a file the test does not read), the
autosave, any browser but Chromium.

## What the lint says about today's pages

Every screen x lane of every engine on every sound of its pool (6374 frames, 2026-10-13):

- **no text off the screen and no two texts overlapping**, on any engine;
- **CZ-1, FILT screen 1 ("DCW")**: four labels are cut with an ellipsis on every sound: "DCW key 1", "DCW key 2",
  "DCW vel 1" in the top row and "DCA key 1" in the bottom row do not fit a cell;
- **VA, OSC screen 2 (the "+" stack)**: the column heading "Morph/Shape" (two sounds) or "Shape/Density" (one
  sound) is cut when the four oscillators are in different modes;
- FM6, FM TONE, P5, WHEEL: nothing.

And against the rules: every page has the shared header, the title line (rule 1). Rule 2: by docs/EDITOR.md today's
editor uses buttons, SELECT and the knobs only, and the keys play; a design cannot bind a key.

## The five steps

1. **The generator.** A build script that turns `design/layouts/p5.json` into P5's page tables, reproducing
   today's 19 pages byte-identically (the device image unchanged): the proof that a layout file can be the source.
   Needs the (page, column) to patch-parameter table in the file.
2. **This viewer.** The preview module, the read-only page, the seed files, the tests.
3. **Editing classic pages, and export** (done): the designer; `lay_load` shows the edited layout at once through
   the real editor; export the design JSON.
4. **Free pages.** The design side is done: the element vocabulary (frame, label, knob, switch, led, bar, glyph,
   band), lanes of up to four bound elements (the four knobs + SELECT grammar; no key ever: rule 2), the title line
   as the shared header on every screen (rule 1), "not buildable yet" flags instead of refusals (rule 3), and the
   first example, `design/layouts/designs/p5-osc-panel.json` (P5's oscillators and mixer on two free screens in the
   spirit of the original panel: OSC A with Freq, Saw / Pulse leds, PW, Sync; the mixer; OSC B with Freq, Fine,
   Saw / Tri / Pulse leds, PW, Lo Freq and Keyboard switches), and the p5 style with `p5-panel.json`, the whole P5
   engine as its panel ("The p5 style"). **Remaining**: firmware support for free pages (a
   free-page kind in `cr_screen.h` / `cr_draw.c` drawing the vocabulary as `freeOps` does, the editor's lanes over
   bound elements) and a **build-time generator** that turns a design file into the engine's tables (pages, screen
   plan, labels) and the free pages' element tables, so a design is compiled into the build (rule 4).
5. **The other engines.** FM6, FM TONE, VA, CZ-1, WHEEL through the generator; FM TONE's screen plan
   (`eng_screen_t`) becomes a layout file like the rest.

In every step the layout is compiled into the build (rule 4).

## Step 4, the firmware: a plan

Written 2026-10-15, when the owner signed off his P5 panel: "I made my final changes and reviewed all the knob lanes
and updated them. I'm happy with this layout, so good to build it." No firmware code exists for it yet. The design is
`design/layouts/designs/p5-panel.json`: eight free screens over OSC .. MOD (73 elements: 29 knobs, 26 leds, 10
frames, 7 labels, 1 switch; 19 lanes; all 56 P5 parameters), FX and MIX the platform's, style p5.

### (a) The compiled form

`tools/gen_layout.py` (a step of `tools/build.py`'s generate list, beside `gen_tables.py`) writes
`build/gen/cr_layouts.h` from the designs the build ships.

- **Which designs.** A list in the generator: `BUILD = {"P5": "design/layouts/designs/p5-panel.json"}`. Not a field in
  the design: the page exports designs, and a file the owner exports must not decide what goes on a unit; what is
  built is the build's business (rule 4), reviewed where the build is. One design an engine.
- **Validation at build time, without the firmware's FNV.** The generator reads `design/layouts/<engine>.json` (the
  version 1 file of today's tables) and requires the design's `params_hash` to equal that file's. That file is already
  proved to be the firmware's (`web/test_layout.mjs` 4 regenerates it from the module and compares), so the chain
  design -> layout file -> firmware holds without porting the hash to Python. The generator resolves every stable id
  (`"FILTER:CUT"`) through the same file's pages to its built-in address (page, col), or `part:<id>` to (0xFF, id), and
  refuses: a `validateDesign` error (the same rules, in Python: a test runs both on every shipped and broken design), a
  classic screen in OSC .. MOD (the plan form exists for those, later), a band element, a flat style, an element over
  the budget. Value names (a switch's positions, a two-LED button's, a knob's named values) are taken from the layout
  file's descriptors here, so the renderer never asks the engine.
- **The records** (packed, no pointers but one table base):
  - an element, 14 B: type (1), x y w h (4, px), page col (2), size (1: the dial), r (1), colour (1), flags (2: glyph
    3 bits, leds, pair 2, labelAt, align 2, positions 2, bold), label (2: an offset in the string pool, 0xFFFF none);
  - a screen, 38 B: group (1), first element (1), element count (1), lane count (1), title (2: an offset), lanes
    `[8][4]` of element indices within the screen (32, 0xFF empty);
  - an engine's layout: engine index, screen count, the screens, the elements, the string pool (labels, frame texts,
    titles, value names, each once).
- **P5's cost, counted from the file:** 73 x 14 = 1022 B of elements, 8 x 38 = 304 B of screens, a string pool of
  about 460 B (50 distinct labels and texts, 387 B; 8 titles, 55 B; the names of REV and KEYBOARD), a header of 8 B:
  **about 1.8 KB of flash**, no RAM.

### (b) The firmware

- **The editor (`cr_edit.c`).** A new screen type `CE_S_FREE` in `ce_scr`: for an engine with a compiled layout, OSC ..
  MOD's screens are the layout's screens of that group (in order), `n` its lane count; FX and MIX stay the platform's
  (classic, as today). `ce_view` fills `ref[lane][slot]` from the lanes: element -> (page, col) -> `ce_r(CE_R_DEEP,
  page, col)` (a part parameter: `CE_R_TRK`). Everything that runs on `ref` is then untouched: `ce_knob` (the
  descriptor's step and acceleration, OPT snap, SHIFT, a ratio's slow hand), the hot mark (`hot_r` / `hot_c` = lane /
  slot), SELECT across lanes and screens, the modulation marks, `cu_edited` and the sound's blob (the parameters are the
  engine's own, through `get` / `set` / `desc`). `ce_screen` fills `cell[lane][slot]` with `ce_cell` as for edit8
  (label, the value's text, pct, flags), and two new bytes in `cr_screen_t`: `fl_eng`, `fl_scr` (the layout and screen,
  + 1; 0 none).
- **The renderer (`cr_draw.c`).** `CR_K_FREE`: a port of `freeOps` + `p5Op` + `layout_web.c`'s `ld_*` to C, drawing
  from the const table (as it draws from the fonts: no engine state) and the screen's cells: the title line
  (`cr_ed_title`), then per element, frames first: the frame (`ld_rline`: sides and `cr_arc` corners, r 0 .. 12; the
  title in capitals on a `bg` block in the border); the p5 knob (11 ticks by `cr_poly`, the face by `cr_disc`, the
  pointer from 30 % or 70 % of the radius, the value inside the face when the widest of its texts fits, else on the
  row, the hot face); the LED button (the cap with its window and rim, square corners for a pair's inner side, one LED
  or two (red / yellow), the one-cycle wave icons as point tables for `cr_poly`, labels above or below by the baseline
  rule); the lever switch; label; and bar and glyph by today's `cr_hbar` / `cr_cglyph`. Colours: the active lane's
  elements in the knob colours by slot, other bound ones `dim`, unbound `text` (the lanes are in the table). An
  element's on / off and position come from its cell's pct; its texts from its cell and the pool.
- **The strips.** The renderer draws in six strips of 40 rows (`CR_STRIP_H`); every primitive already clips to the
  strip (`cr_row0` / `cr_row1`), so an element across a boundary (a 64-high knob cell always is) is drawn in each
  strip it touches, as `ld_play` does today. Per strip, elements whose box (grown 6 px: a frame's title, a descender)
  misses it are skipped. Which strips to compose: a free screen's lanes are not rows of the screen, so the row hashes
  of edit8 do not apply; a screen or lane change composes all six strips, and a knob turn only the strips that the
  turned element's box touches (and the box of the element that was hot before), with the tile hashes
  (`cr_hash_tiles`) blitting only the tiles that changed. The cost of both is measured on the emulator before stage 2
  ends (edit8's full compose on a screen change is the reference; the owner's rule is responsiveness first).
- **The flat style is not compiled**: the firmware draws the p5 look only (it is the owner's, and one renderer is
  half the code). The tool keeps "flat" for sketching and marks a flat free screen "not buildable" with that reason.
- **FX and MIX stay classic** (the platform's screens, shared by every engine), and so do P5's classic pages in the
  tables: `eng_page_t` stays, it is what `get` / `set` / `desc` and the blob address.

### (c) The tool after step 4

`lay_load` packs a design's free screens into the same records (`packDesign` gains the free half, byte for byte the
generator's output: a host test compares the two on `p5-panel.json`) and installs them through a preview-only hook
beside `cp_deep_hook` (`cr_layout_hook`, under `CR_LAYOUT_PREVIEW`). The editor and the renderer then show a design's
free screens as the unit will, so the preview is the unit's picture again, knobs and SELECT included. `freeOps`, `p5Op`
and the `lay_draw_*` ops go away (the design checks stay, in JS, for the lint list). The emulator comparison of
`test_layout.mjs` (3) then covers free pages: the browser emulator built with P5's compiled design, driven by its panel
alone (GLO + PRESETS to P5, EDIT, the group buttons, SELECT through every screen x lane, a knob turn on each lane), its
LCD compared with the module's `lay_fb` after `lay_load` of the same file, pixel for pixel: the strongest proof that
the tool and the unit agree.

### (d) The budget

Measured: the rn-1 device build of 2026-10-14 (9698e52, `./build.sh`; `build/choralroot.elf`): `.text` 511808 B,
`.ram_text` 6680, `.c1_text` 128, `.data` 11884, `.bss` 65640, `.pool` 304600: **XIP 530500 B of 573440 (92.5 %)**,
RAM 77524 B of 98304 (78.9 %), POOL 304600 B of 344064 (88.5 %). Free XIP: 42.9 KB. (`tests/target_budget.txt` is the
DSP loops' cost, not the size; the size line is `tools/build.py`'s.) Estimate: P5's tables 1.8 KB; the renderer's
free kind 4 .. 6 KB (the knob, the LED button with its icons and pairs, the frame's arcs, the switch, the dispatcher;
the primitives exist); the editor's `CE_S_FREE` and the generator's lookup 0.5 .. 1 KB: **about 7 KB, XIP to about
93.7 %**, no RAM beyond `cr_screen_t`'s two bytes, no POOL. If XIP goes over the line the release allows: the strings
in one pool already shared; the wave icons as 16 point tables instead of code; the knob's ticks as a precomputed table
of 11 angle pairs; and the free renderer reusing `cr_cglyph`'s knob arcs where it can. P5's classic derivation is
generic code every engine uses: it does not shrink.

### (e) Tests and proof

- **Host:** `tests/gen_layout_test.py`: the generator on every shipped design and on `test_layout.mjs`'s broken ones
  (the same errors as `validateDesign`), the byte cost printed; `tests/run_cr_draw.sh`: goldens of every P5 free screen
  x lane, the hot element, a two-LED button at both values, a pair, from mock-up states (as `tests/gen_cr_screens.py`
  cuts them) with the generated table; every existing golden unchanged; `tests/run_cr_tests.sh` (`cr_edit`): every lane
  slot of the P5 design turned through the editor (the parameter moves, `cu_edited`, the hot mark on that element,
  SELECT across lanes and screens); the size line in HISTORY.
- **Browser:** `test_layout.mjs` (3): the emulator == the module on every P5 screen x lane, as (c).
- **The image:** stage 1 changes no device byte (the tables are only in host builds: sha256 of `build/choralroot.bin`
  before and after, equal, as for `CR_LAYOUT_PREVIEW` in 3y47). From stage 2 the image changes (new code); the other
  engines are proved unchanged by their goldens, `tools/emu/test_cr.sh` and the emulator comparison of VA, FM TONE
  and the rest (every frame identical to before), and P5's sound by `cr_engine_test` (the same blobs).

### (f) Stages (a commit each)

1. **The generator and the tables** (about 400 lines: `gen_layout.py`, the record structs in a new `cr_layout.h`, the
   host test): `build/gen/cr_layouts.h` from `p5-panel.json`; compiled in the host tests only; the device image
   unchanged (sha256). **Done (3y67)**: P5's panel is 73 x 14 + 8 x 38 + a pool of 453 + 8 = **1787 B**; value names
   follow their label in the pool (flag `CR_LF_NAMES`), `size` is also a label's px and a glyph's `CR_G_*`
   (`firmware/src/cr_layout.h`); tests `tests/gen_layout_test.py`, `tests/cr_layout_test.c`.
2. **The editor and the renderer on P5's free screens** (about 900 lines of C: `CE_S_FREE` in `cr_edit.c`, `CR_K_FREE`
   in `cr_draw.c`, two bytes in `cr_screen_t`): P5's editor is the owner's panel on the unit; goldens, the `cr_edit`
   test, a device build with the size line, the emulator walked through every screen. Not flashed by a subagent.
3. **The tool on the firmware's renderer** (about 300 lines: `packDesign`'s free half, the hook, `lay_load`; minus
   `freeOps` / `p5Op` / `lay_draw_*`): the preview is the unit's; the emulator comparison over free pages.
4. **Docs**: this file (step 4 done, the plan replaced by what was built), HISTORY, the P5 sections of docs/EDITOR.md
   and docs/PROPHET.md (the panel's screens, their lanes and knobs), the manual's P5 page.
