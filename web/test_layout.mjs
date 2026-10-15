// SPDX-License-Identifier: GPL-3.0-only
// The layout tool's preview module and viewer (docs/LAYOUT-TOOL.md, step 2). node >= 20:
//   node web/test_layout.mjs [--no-page] [--no-emu]
// (after sh tools/layout/build_layout.sh; the comparison with the emulator needs sh tools/emu/web/build_web.sh too)
// 1. build/layout-web/crlayout.wasm: lay_version, lay_presets, lay_catalog of every engine (every cell bound to a
//    parameter has a label and a range; P5: 19 pages, 56 cells), lay_load refusing what it cannot read.
// 2. Every screen x lane of P5, VA and FM TONE on the engine's first factory sound: not blank, the same bytes when
//    drawn twice and when shown again; a knob turn changes its cell's value and the picture and marks the cell hot
//    (and 800 ms later the mark is gone); SELECT moves the lane.
// 3. The same frames from the browser emulator (build/emu-web/choralroot.wasm: the firmware without the layout API,
//    driven by its panel alone: GLO + PRESETS to the engine, EDIT, the group buttons, SELECT, a knob): the LCD's
//    RGB565 must be identical, pixel for pixel, on every screen x lane of the three engines.
// 4. design/layouts/*.json are what the firmware says today (tools/layout/dump_catalogs.mjs).
// 6. The designer (steps 3 and 4): the design format's round trip and its validation; lay_load of a classic design
//    that only relabels (every frame today's but the relabelled text), one that swaps two parameters (the knobs turn
//    the right ones); the free-screen draw API on design/layouts/designs/p5-osc-panel.json (deterministic, lint clean,
//    the knobs turn its parameters); the p5 style on design/layouts/designs/p5-panel.json (the same checks, all 56
//    parameters placed, a knob turn on every lane, a knob's value inside its face, a lit led's red dot); and the page in
//    Design mode (?selftest=design: a scripted drag adds an element,
//    the export equals the model, the contact sheet has today's and the design's tiles).
// 5. The page: web/layout.html made into TEST_TMP/page (tools/layout/make_page.py), opened from file:// by headless
//    Chromium with ?selftest=1; the page checks itself (the canvas is drawn, a knob click changes it, the contact
//    sheet has every screen x lane) and paints a square per check, read here from the screenshot.
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { execFileSync, spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import * as L from "./fm1layout.js";
import { staleLayoutFiles } from "../tools/layout/dump_catalogs.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..");
const OUT = path.join(ROOT, "build/layout-web");
const TMP = process.env.TEST_TMP || path.join(ROOT, "build/layout-web-test");
const args = new Set(process.argv.slice(2));
fs.mkdirSync(TMP, { recursive: true });

let fails = 0;
const ok = (c, msg) => { console.log(`  ${c ? "ok  " : "FAIL"}  ${msg}`); if (!c) fails++; return c; };

if (!fs.existsSync(path.join(OUT, "crlayout.wasm"))) {
  console.log("FAIL: build/layout-web/crlayout.wasm missing (sh tools/layout/build_layout.sh)");
  process.exit(1);
}
await import(path.join(OUT, "crlayout.js"));
const wasm = fs.readFileSync(path.join(OUT, "crlayout.wasm"));

// ===================================================================================================== 1 ===
console.log(`the preview module: build/layout-web/crlayout.wasm (${wasm.length} B)`);
ok(Buffer.from(globalThis.CRLayoutModule.bytes()).equals(wasm), "crlayout.js carries the same bytes as crlayout.wasm");
let t0 = performance.now();
const m = L.wrap(await globalThis.CRLayoutModule.load(wasm));
const tLoad = performance.now() - t0;
t0 = performance.now();
m.reset();
const tBoot = performance.now() - t0;
const ver = m.version();
ok(/^ChoralRoot \d/.test(ver.firmware) && ver.api === 2 && ver.format === L.FORMAT && ver.format_version === L.DESIGN_VERSION,
   `lay_version: ${ver.firmware}, api ${ver.api}, ${ver.format} v${ver.format_version}`);
const names = ver.engines.map((e) => e.name);
ok(["FM6", "FM TONE", "VA", "CZ-1", "P5", "WHEEL"].every((n) => names.includes(n)), `engines: ${names.join(", ")}`);
ok(ver.engines.every((e) => /^[0-9a-f]{16}$/.test(e.params_hash)) && new Set(ver.engines.map((e) => e.params_hash)).size === ver.engines.length,
   "every engine has its own params_hash");
ok(true, `instantiated in ${tLoad.toFixed(0)} ms, power-on + snapshot (${m.snapshotBytes()} B of static data) in ${tBoot.toFixed(0)} ms (node, this machine)`);
ok(m.load().ok === false && m.load(Uint8Array.from([76, 2, 1])).ok === false && m.unload().ok === true,
   "lay_load: refuses what is not a packed design or names an engine without pages; unloads");
ok(ver.engines.every((e) => m.ex.lay_show(e.index, 0, 9, 0, 0) === -3) && m.ex.lay_show(1, 0, 0, 0, 0) === -1 &&
   m.ex.lay_show(ver.engines[0].index, 999, 0, 0, 0) === -2 && m.ex.lay_show(ver.engines[0].index, 0, 0, 99, 0) === -4 &&
   m.ex.lay_show(ver.engines[0].index, 0, 0, 0, 99) === -5, "lay_show refuses a place that does not exist (-1 .. -5)");

const cats = {};
for (const e of ver.engines) {
  const c = (cats[e.name] = m.catalog(e.index));
  const ts = L.tiles(c), n = L.counts(c), bound = ts.flatMap((t) => t.cells).filter((x) => x.bind === "deep" || x.bind === "track");
  const good = c.format === L.FORMAT && c.version === 1 && c.engine.index === e.index && c.params_hash === e.params_hash &&
    ts.length > 0 && bound.length > 0 &&
    bound.every((x) => x.label && x.short && Number.isInteger(x.min) && Number.isInteger(x.max) && x.max >= x.min && x.def >= x.min && x.def <= x.max && typeof x.fmt === "string" && x.fmt !== "?") &&
    ts.every((t) => t.cells.length === 4 && typeof t.title === "string" && t.title.length > 0 && t.kind !== "?" && t.band !== "?") &&
    bound.filter((x) => x.bind === "deep").every((x) => c.pages[x.page] && c.pages[x.page].cols[x.col] && c.pages[x.page].cols[x.col].short === x.short) &&
    m.presets(e.index).length >= 2 && L.checkLayout(c, ver) === null;
  ok(good, `lay_catalog ${e.name}: ${n.groups} groups, ${n.screens} screens, ${n.lanes} lanes, ${n.cells} bound cells; ${n.pages} pages (${n.pageCells} cells); ${m.presets(e.index).length} sounds`);
  // every cell of the engine's pages is on some screen (nothing the editor cannot reach)
  const seen = new Set(bound.filter((x) => x.bind === "deep").map((x) => `${x.page}.${x.col}`));
  const all = c.pages.flatMap((p) => p.cols.map((col, i) => (col ? `${p.page}.${i}` : null)).filter(Boolean));
  const lost = all.filter((k) => !seen.has(k));
  ok(lost.length === 0, `${e.name}: every page cell is on a screen` + (lost.length ? ` (not shown: ${lost.join(" ")})` : ""));
}
ok(cats.P5.pages.length === 19 && cats.P5.page_cells === 56, `P5: ${cats.P5.pages.length} pages, ${cats.P5.page_cells} cells (19 / 56)`);
ok(L.checkLayout({ ...cats.P5, params_hash: "0".repeat(16) }, ver) !== null && L.checkLayout({ ...cats.P5, version: 2 }, ver) !== null,
   "checkLayout refuses another params_hash and another format version");
ok(JSON.stringify(JSON.parse(L.formatLayout(cats.VA))) === JSON.stringify(cats.VA), "formatLayout: the file's text parses back to the catalog");

// ===================================================================================================== 2 ===
console.log("every screen x lane of P5, VA and FM TONE, on the first factory sound");
const THREE = ["P5", "VA", "FM TONE"];
const eq = (a, b) => a.length === b.length && L.diffCount(a, b) === 0;
const frames = {};   // engine name -> [{tile, fb}]
const lintAll = {};
t0 = performance.now();
let nframes = 0;
for (const e of ver.engines) {
  const ts = L.tiles(cats[e.name]);
  const sum = { "off-screen": 0, overlap: 0, ellipsis: 0, notes: [] };
  let blank = [], unstable = [], reshown = [], wrong = [];
  frames[e.name] = [];
  for (const t of ts) {
    const nl = m.show(e.index, 1, t.group, t.screen, t.lane);
    const a = m.render(), fb = m.fb(), lint = m.lint(), s = m.state();
    nframes++;
    for (const f of lint.findings) {
      sum[f.kind]++;
      sum.notes.push(`${L.tileLabel(t)}: ${f.kind} "${f.text}" [${f.box}]` + (f.other ? ` / "${f.other.text}" [${f.other.box}]` : ""));
    }
    if (!THREE.includes(e.name)) continue;
    if (L.inkCount(a) < 500) blank.push(t.key);
    if (!eq(a, m.render())) unstable.push(t.key);
    if (nl !== t.lanes || !s.editor || s.group !== t.group || s.screen !== t.screen || s.lane !== t.lane || s.engine !== e.index || s.page !== t.title) wrong.push(t.key);
    frames[e.name].push({ tile: t, fb, rgba: a });
  }
  lintAll[e.name] = sum;
  if (!THREE.includes(e.name)) continue;
  for (const f of frames[e.name]) {   // shown again, after every other place: the same bytes (the instance is reset)
    m.show(e.index, 1, f.tile.group, f.tile.screen, f.tile.lane);
    if (!eq(f.rgba, m.render())) reshown.push(f.tile.key);
  }
  ok(!blank.length, `${e.name}: ${ts.length} frames, none blank` + (blank.length ? ` (blank: ${blank.join(" ")})` : ""));
  const distinct = new Set(frames[e.name].map((f) => Buffer.from(f.rgba.buffer).toString("latin1"))).size;
  ok(distinct === ts.length, `${e.name}: ${distinct} different pictures for ${ts.length} places`);
  ok(!wrong.length, `${e.name}: lay_state says the place and the title the catalog has` + (wrong.length ? ` (not: ${wrong.join(" ")})` : ""));
  ok(!unstable.length && !reshown.length, `${e.name}: deterministic (drawn twice, and shown again later: identical)` +
     (unstable.length + reshown.length ? ` (differ: ${[...unstable, ...reshown].join(" ")})` : ""));
}
ok(true, `${nframes} lay_show + lay_render + lay_lint in ${(performance.now() - t0).toFixed(0)} ms`);
for (const e of ver.engines) {
  const s = lintAll[e.name];
  console.log(`  lint  ${e.name}: ${s["off-screen"]} off-screen, ${s.overlap} overlap, ${s.ellipsis} cut texts`);
  for (const n of s.notes.slice(0, 12)) console.log(`          ${n}`);
  if (s.notes.length > 12) console.log(`          .. ${s.notes.length - 12} more`);
}
ok(ver.engines.every((e) => lintAll[e.name]["off-screen"] === 0 && lintAll[e.name].overlap === 0),
   "lint: no text off the screen or overlapping on today's pages (cut texts are notes)");

// the knobs: on each lane of the three engines, the first knob with a parameter is turned
for (const name of THREE) {
  const e = ver.engines.find((x) => x.name === name);
  let turned = 0, bad = [];
  for (const f of frames[name]) {
    const t = f.tile, k = t.cells.findIndex((c) => c.bind === "deep" || c.bind === "track");
    if (k < 0) continue;
    m.show(e.index, 1, t.group, t.screen, t.lane);
    const s0 = m.state();
    if (!s0.cells[k].on) continue;   // (a cell this sound hides: an unused MOD slot)
    m.render();
    m.knob(k + 1, 2);
    let s1 = m.state(), px = m.render();
    if (s1.cells[k].value === s0.cells[k].value) {   // at the top of its range: down
      m.knob(k + 1, -4);
      s1 = m.state();
      px = m.render();
    }
    const changed = s1.cells[k].value !== s0.cells[k].value, moved = L.diffCount(f.rgba, px) > 0, hot = s1.hot === k + 1;
    m.step(900);
    const cool = m.state().hot === 0;
    turned++;
    if (!changed || !moved || !hot || !cool) bad.push(`${L.tileLabel(t)} K${k + 1} (${s0.cells[k].value} -> ${s1.cells[k].value}${moved ? "" : ", same picture"}${hot ? "" : ", not hot"}${cool ? "" : ", still hot after 900 ms"})`);
  }
  ok(!bad.length && turned > 5, `${name}: a knob turned on ${turned} lanes: the value, the picture and the hot cell change; the mark ends` +
     (bad.length ? `\n          ${bad.join("\n          ")}` : ""));
}
{
  const e = ver.engines.find((x) => x.name === "P5"), t = L.tiles(cats.P5).find((x) => x.lanes > 1 && x.lane === 0);
  m.show(e.index, 1, t.group, t.screen, 0);
  const a = m.render();
  m.select(1);
  const s = m.state();
  ok(s.lane === 1 && s.screen === t.screen && L.diffCount(a, m.render()) > 0, `SELECT +1 on P5 ${L.tileLabel(t)}: lane 2, the picture changes`);
  m.select(-1);
  ok(eq(a, m.render()), "SELECT -1: the first lane's picture again");
}

// ===================================================================================================== 3 ===
if (!args.has("--no-emu")) {
  console.log("against the browser emulator (build/emu-web/choralroot.wasm, driven by its panel)");
  const emuPath = path.join(ROOT, "build/emu-web/choralroot.wasm");
  if (!fs.existsSync(emuPath)) console.log("  skip  build/emu-web/choralroot.wasm missing (sh tools/emu/web/build_web.sh)");
  else {
    const B = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, SEQ: 9, PLAY: 10, REC: 11 }, E = { SELECT: 0, PRESETS: 2, K1: 3 };
    const emuBytes = fs.readFileSync(emuPath);
    // P5 on the unit is the owner's panel (the compiled layout, docs/LAYOUT-TOOL.md "Step 4"): the module's frames with
    // the layouts on, every screen x lane of p5-panel.json (its free screens in OSC .. MOD, FX and MIX as today)
    const panelTiles = () => {
      const pd = JSON.parse(fs.readFileSync(path.join(ROOT, "design/layouts/designs/p5-panel.json"), "utf8"));
      const BTN = ["FX", "SEL", "ENV", "LFO", "SEQ", "PLAY", "REC"], e = ver.engines.find((x) => x.name === "P5");
      m.layouts(true);
      const out = L.designTiles(pd).map((d) => {
        const g = pd.groups[d.gi].group, tile = { group: g, groupName: d.group, button: BTN[g], screen: d.si, lane: d.lane,
                                                  lanes: d.lanes, title: d.title, key: d.key };
        m.show(e.index, 1, g, d.si, d.lane);
        return { tile, fb: m.fb(), free: m.state().kind === "free" };
      });
      m.layouts(false);
      return out;
    };
    for (const name of THREE) {
      const e = ver.engines.find((x) => x.name === name), rank = ver.engines.indexOf(e);
      const x = await globalThis.CRLayoutModule.load(emuBytes);
      x.web_boot();
      const tap = (b) => { x.web_buttons(1 << B[b]); x.web_step(100); x.web_buttons(0); x.web_step(300); };
      const fb = () => new Uint16Array(x.memory.buffer, x.web_fb(), 240 * 240).slice();
      x.web_step(1000);
      x.web_buttons(1 << B.GLO);            // OPT held + PRESETS: the engine (its pool position 1), on OPT's release
      x.web_step(100);
      x.web_enc(E.PRESETS, rank);
      x.web_step(100);
      x.web_buttons(0);
      x.web_step(1500);
      tap("EDIT");
      let diff = [], n = 0, group = 0, worst = 0;
      const walk = name === "P5" ? panelTiles() : frames[name];
      if (name === "P5")
        ok(walk.filter((f) => f.free).length === 19 && walk.length === 22,
           `P5: the compiled panel's ${walk.length} places, ${walk.filter((f) => f.free).length} of them its free screens' lanes`);
      for (const f of walk) {
        const t = f.tile;
        if (t.group !== group) { tap(t.button); group = t.group; }   // (a group opens on its first screen and lane)
        const d = L.diffCount(f.fb, fb());
        n++;
        if (d) { diff.push(`${L.tileLabel(t)} (${d} px)`); worst = Math.max(worst, d); }
        x.web_enc(E.SELECT, 1);             // the next lane; past the last: the next screen's first
        x.web_step(100);
      }
      ok(!diff.length, `${name}: ${n} frames identical to the emulator's LCD (RGB565, 57600 pixels each)` +
         (diff.length ? `\n          differ: ${diff.slice(0, 8).join("; ")}${diff.length > 8 ? " .." : ""}` : ""));
      // a knob turn, the hot cell included: FILT's first screen, KNOB 1 +1
      const g1 = L.tiles(cats[name]).find((t) => t.group === 1);
      tap("SEL");
      x.web_enc(E.K1, 1);
      x.web_step(30);
      m.layouts(name === "P5");                // (P5: KNOB 1 of the FILTER screen's first lane is REV)
      m.show(e.index, 1, 1, 0, 0);
      m.knob(1, 1);
      m.render();
      m.layouts(false);
      const d = L.diffCount(m.fb(), fb());
      ok(d === 0 && m.state().hot === 1, `${name}: ${L.tileLabel(g1)} after KNOB 1 +1 (the hot cell): identical to the emulator's` + (d ? ` (${d} px differ)` : ""));
    }
  }
}

// ===================================================================================================== 4 ===
console.log("design/layouts: today's layouts in the file format");
{
  const { files, stale } = await staleLayoutFiles();
  for (const s of stale) console.log(`          ${s.file}: ${s.why}`);
  ok(stale.length === 0 && files.length === ver.engines.length,
     `${files.length} files are what the firmware says (else: node tools/layout/dump_catalogs.mjs)`);
}


// ===================================================================================================== 6 ===
console.log("the designer: the design format, lay_load, the free-screen draw API");
{
  const P5 = ver.engines.find((x) => x.name === "P5"), cat = cats.P5, params = L.paramList(cat), pidx = L.paramIndex(params);
  ok(params.length === 56 + L.tiles(cat).flatMap((t) => t.cells).filter((c) => c.bind === "track").length && pidx.size === params.length,
     `paramList P5: ${params.length} parameters, ids unique ("${params[0].id}" ..)`);
  // the round trip
  const today = L.designFromCatalog(cat, "today");
  const back = JSON.parse(L.formatDesign(today));
  ok(JSON.stringify(back) === JSON.stringify(today) && L.validateDesign(back, cat).errors.length === 0 && L.validateDesign(back, cat).warnings.length === 0,
     "a design's JSON reads back to the same model, valid");
  for (const name of ["VA", "FM TONE", "FM6", "CZ-1", "WHEEL"]) {
    const d = L.designFromCatalog(cats[name]);
    const v = L.validateDesign(JSON.parse(L.formatDesign(d)), cats[name]);
    if (v.errors.length) ok(false, `${name}: today's design not valid: ${v.errors[0]}`);
  }
  ok(true, "today's layout of every engine as a design: valid");
  // the validation: each broken file caught
  const free = () => JSON.parse(fs.readFileSync(path.join(ROOT, "design/layouts/designs/p5-osc-panel.json"), "utf8"));
  const broken = {
    "another format": (d) => { d.format = "x"; },
    "version 1": (d) => { d.version = 1; },
    "not a design": (d) => { d.kind = "layout"; },
    "another params_hash": (d) => { d.params_hash = "0".repeat(16); },
    "another engine": (d) => { d.engine.index = 3; },
    "no title-line header": (d) => { d.header = null; },
    "an unknown group": (d) => { d.groups[1].name = "ARP"; },
    "a screen of no type": (d) => { d.groups[1].screens[0].type = "panel"; },
    "a classic lane of three cells": (d) => { d.groups[1].screens[0].lanes[0].cells.pop(); },
    "an unknown parameter": (d) => { d.groups[1].screens[0].lanes[0].cells[0] = { param: "FILTER:NOPE" }; },
    "an unknown band": (d) => { d.groups[1].screens[0].band = "rainbow"; },
    "an unknown element type": (d) => { d.groups[0].screens[0].elements[1].type = "slider"; },
    "a position not a number": (d) => { d.groups[0].screens[0].elements[1].x = "12"; },
    "two elements of one id": (d) => { d.groups[0].screens[0].elements[2].id = d.groups[0].screens[0].elements[1].id; },
    "a switch of four positions": (d) => { d.groups[0].screens[0].elements.find((e) => e.type === "switch").positions = 4; },
    "an led glyph that does not exist": (d) => { d.groups[0].screens[0].elements.find((e) => e.type === "led").glyph = "star"; },
    "a colour that is not a token": (d) => { d.groups[0].screens[0].elements[1].colour = "#ff0000"; },
    "a lane naming no element": (d) => { d.groups[0].screens[0].lanes[0][0] = "nothing"; },
    "an element bound to a key": (d) => { d.groups[0].screens[0].elements[1].key = 12; },
    "an unknown style": (d) => { d.style = "x"; },
    "two LEDs on a 0..127 parameter": (d) => { Object.assign(d.groups[0].screens[0].elements.find((e) => e.type === "led"), { leds: 2, glyph: null, param: "OSC A:FREQ" }); },
  };
  const missed = Object.entries(broken).filter(([, f]) => { const d = free(); f(d); return L.validateDesign(d, cat).errors.length === 0; }).map(([k]) => k);
  ok(!missed.length, `validateDesign catches each of ${Object.keys(broken).length} broken designs` + (missed.length ? ` (missed: ${missed.join(", ")})` : ""));

  // lay_load: today's design packed in place: every frame today's
  const frameAll = () => L.tiles(cat).map((t) => { m.show(P5.index, 1, t.group, t.screen, t.lane); return { t, rgba: m.render(), lint: m.lint() }; });
  m.unload();
  const base = frameAll();
  const pk0 = L.packDesign(today, cat);
  ok(pk0.mode === "in-place" && m.load(pk0.bytes).ok, `today's design packs in place (${pk0.pages} pages, ${pk0.bytes.length} B) and loads`);
  const same = frameAll();
  ok(same.every((f, i) => L.diffCount(f.rgba, base[i].rgba) === 0), `loaded as today's: all ${same.length} P5 frames identical to the built-in tables'`);
  // a relabel
  const rel = L.designFromCatalog(cat, "relabel");
  rel.groups[1].screens[0].lanes[0].cells[1].label = "Bright";      // FILT, lane 1, KNOB 2 (Reso)
  ok(m.load(L.packDesign(rel, cat).bytes).ok, "a design that relabels FILT's Reso loads");
  const relf = frameAll();
  const bad = [];
  relf.forEach((f, i) => {
    const t = f.t, d = L.diffCount(f.rgba, base[i].rgba);
    if (t.group !== 1 || t.screen !== 0) { if (d) bad.push(`${L.tileLabel(t)}: ${d} px`); return; }
    const old = base[i].lint.texts.find((x) => x.text === "Reso"), now = f.lint.texts.find((x) => x.text === "Bright");
    let out = 0;
    for (let p = 0; p < 240 * 240; p++) {
      const a = p * 4;
      if (f.rgba[a] === base[i].rgba[a] && f.rgba[a + 1] === base[i].rgba[a + 1] && f.rgba[a + 2] === base[i].rgba[a + 2]) continue;
      const x = p % 240, y = (p / 240) | 0, inb = (b) => b && x >= b.box[0] - 2 && x < b.box[2] + 2 && y >= b.box[1] - 2 && y < b.box[3] + 2;
      if (!inb(old) && !inb(now)) out++;
    }
    if (!d || !now || out) bad.push(`${L.tileLabel(t)}: ${d} px differ, ${out} outside the label's box${now ? "" : ", no \"Bright\""}`);
  });
  ok(!bad.length, "relabelled: every frame is today's but FILT 1's, which differs only where \"Reso\" became \"Bright\"" + (bad.length ? ` (${bad.join("; ")})` : ""));
  // a swap: Cutoff and Reso change knobs; KNOB 1 now turns the resonance
  const sw = L.designFromCatalog(cat, "swap");
  const c0 = sw.groups[1].screens[0].lanes[0].cells;
  [c0[0], c0[1]] = [c0[1], c0[0]];
  ok(m.load(L.packDesign(sw, cat).bytes).ok, "a design that swaps FILT's Cutoff and Reso loads");
  m.show(P5.index, 1, 1, 0, 0);
  const pre = m.render(), cut = pidx.get("FILTER:CUT"), res = pidx.get("FILTER:RES");
  const lab = m.state().cells.map((c) => c.label);
  const r0 = m.pget(res.page, res.col), u0 = m.pget(cut.page, cut.col);
  m.knob(1, 3);
  const r1 = m.pget(res.page, res.col), u1 = m.pget(cut.page, cut.col);
  ok(lab[0] === "Reso" && lab[1] === "Cutoff" && r1 !== r0 && u1 === u0 && L.diffCount(pre, m.render()) > 0,
     `swapped: KNOB 1 is "${lab[0]}", KNOB 2 "${lab[1]}"; KNOB 1 +3 turns the resonance (${r0} -> ${r1}), the cutoff stays (${u0})`);
  m.knob(2, -2);
  ok(m.pget(cut.page, cut.col) !== u0, `KNOB 2 turns the cutoff (${u0} -> ${m.pget(cut.page, cut.col)})`);
  // a structural change: the plan (a screen added to FILT, two screens of ENV merged into one lane each)
  const pl = L.designFromCatalog(cat, "plan");
  pl.groups[1].screens.push(L.newClassicScreen("edit8", "MORE"));
  pl.groups[1].screens[1].lanes[0].cells[0] = { param: "FILTER+:REV" };
  const pk = L.packDesign(pl, cat);
  ok(pk.mode === "plan" && !pk.unbuildable.length && m.load(pk.bytes).ok && m.show(P5.index, 1, 1, 1, 0) > 0 && m.state().page === "MORE",
     `a screen added: packed as a screen plan (${pk.screens} screens, ${pk.pages} pages), the new screen shows ("${m.state().page}")`);
  m.unload();

  // the example (item 4) and the free-screen draw API
  const ex = free();
  const v = L.validateDesign(ex, cat);
  ok(!v.errors.length && !v.warnings.length, "design/layouts/designs/p5-osc-panel.json: valid, no warning");
  const s = L.designSession(m, cat, ex, 1);
  s.reload();
  const shots = [], findings = [];
  for (const t of L.designTiles(ex)) {
    s.open(t.gi, t.si, t.lane);
    const r = s.render();
    shots.push(r.rgba);
    for (const f of r.lint.findings) findings.push(`${t.group} ${t.si + 1}.${t.lane + 1}: ${f.kind} "${f.text}"`);
    for (const c of r.checks) findings.push(`${t.group} ${t.si + 1}.${t.lane + 1}: ${c.kind} ${c.text}`);
    if (t.type === "free" && (L.inkCount(r.rgba) < 500 || !r.unbuildable)) findings.push(`${t.key}: blank or not marked`);
  }
  ok(!findings.length && L.usedTwice(ex).length === 0, `the example: ${shots.length} screens x lanes rendered, lint clean (text, overlap, lanes, budget, header), no parameter twice` +
     (findings.length ? `\n          ${findings.slice(0, 10).join("\n          ")}` : ""));
  const again = L.designTiles(ex).map((t) => { s.open(t.gi, t.si, t.lane); return s.render().rgba; });
  ok(again.every((a, i) => L.diffCount(a, shots[i]) === 0), "the example drawn again: the same bytes (deterministic)");
  s.open(0, 0, 0);
  const free0 = s.render().rgba, fq = pidx.get("OSC A:FREQ"), f0 = m.pget(fq.page, fq.col);
  s.knob(1, 2);
  const f1 = m.pget(fq.page, fq.col), hotShot = s.render();
  s.cool();
  ok(f1 !== f0 && L.diffCount(free0, hotShot.rgba) > 0 && hotShot.ops.some((o) => o.op === "knob") && L.diffCount(hotShot.rgba, s.render().rgba) > 0,
     `free OSC A: KNOB 1 +2 turns Osc A's frequency (${f0} -> ${f1}), the dial and value move, the hot value ends`);
  s.select(1);
  ok(s.at.lane === 1 && s.lane()[0] && s.lane()[0].label === "Sync", "SELECT +1: lane 2, KNOB 1 is Sync");
  s.select(5);
  ok(s.at.si === 1, `SELECT on past the last lane: the next screen (OSC B), lane ${s.at.lane + 1}`);
  const st0 = s.render();
  ok(/^ITS|\w/.test(st0.state.sound) && st0.lint.texts.some((x) => x.text === "OSC B") && st0.lint.texts.some((x) => x.text === st0.state.sound),
     `the shared header on a free screen: the editor's title line ("${st0.state.sound}" | "OSC B")`);
  m.unload();

  // the p5 style: design/layouts/designs/p5-panel.json, P5's whole engine on nine free screens in the panel's look
  const pan = JSON.parse(fs.readFileSync(path.join(ROOT, "design/layouts/designs/p5-panel.json"), "utf8"));
  const pv = L.validateDesign(pan, cat);
  ok(pan.style === "p5" && !pv.errors.length && !pv.warnings.length, "design/layouts/designs/p5-panel.json: style p5, valid, no warning" +
     (pv.errors.length || pv.warnings.length ? ` (${[...pv.errors, ...pv.warnings].slice(0, 3).join("; ")})` : ""));
  const ps = L.designSession(m, cat, pan, 1);
  ps.reload();
  const pshots = [], pfind = [];
  for (const t of L.designTiles(pan)) {
    ps.open(t.gi, t.si, t.lane);
    const r = ps.render();
    pshots.push(r.rgba);
    for (const f of r.lint.findings) pfind.push(`${t.group} ${t.si + 1}.${t.lane + 1}: ${f.kind} "${f.text}"`);
    for (const c of r.checks) pfind.push(`${t.group} ${t.si + 1}.${t.lane + 1}: ${c.kind} ${c.text}`);
    if (t.type === "free" && (L.inkCount(r.rgba) < 500 || !r.unbuildable)) pfind.push(`${t.key}: blank or not marked`);
  }
  const frees = pan.groups.flatMap((g) => g.screens.filter((x) => x.type === "free"));
  const placed = frees.flatMap((x) => x.elements.filter((e) => e.param).map((e) => e.param));
  const engineIds = params.filter((p) => !p.part).map((p) => p.id);
  ok(!pfind.length && !L.usedTwice(pan).length, `the p5 panel: ${pshots.length} screens x lanes rendered, lint clean, no parameter twice` +
     (pfind.length ? `\n          ${pfind.slice(0, 10).join("\n          ")}` : ""));
  ok(frees.length >= 8 && placed.length === 56 && engineIds.length === 56 && engineIds.every((id) => placed.includes(id)),
     `the p5 panel: ${frees.length} free screens place all ${engineIds.length} P5 parameters once (${placed.length} bound elements)`);
  const pagain = L.designTiles(pan).map((t) => { ps.open(t.gi, t.si, t.lane); return ps.render().rgba; });
  ok(pagain.every((a, i) => L.diffCount(a, pshots[i]) === 0), "the p5 panel drawn again: the same bytes (deterministic)");
  const still = [];
  let turned = 0;
  for (const t of L.designTiles(pan).filter((x) => x.type === "free")) {
    ps.open(t.gi, t.si, t.lane);
    // the lane's first bound slot and its knob (a lane may start with empty slots)
    const sc = pan.groups[t.gi].screens[t.si], k = sc.lanes[t.lane].findIndex((x) => x !== null);
    const e = sc.elements.find((x) => x.id === sc.lanes[t.lane][k]), p = pidx.get(e.param);
    const a = ps.render().rgba, v0 = m.pget(p.page, p.col);
    ps.knob(k + 1, v0 >= p.max ? -1 : 1);
    const v1 = m.pget(p.page, p.col), b = ps.render().rgba;
    if (v1 === v0 || !L.diffCount(a, b)) still.push(`${t.group} ${t.si + 1}.${t.lane + 1} KNOB ${k + 1} ${e.id}`); else turned++;
  }
  ok(!still.length, `the p5 panel: the first bound knob of each of ${turned} free lanes turns its parameter and changes the picture` + (still.length ? ` (not: ${still.join(", ")})` : ""));
  // a p5 knob's value is inside its face; a lit led's dot is red
  const sA = pan.groups[0].screens[0], kn = sA.elements.find((x) => x.type === "knob"), kp = pidx.get(kn.param);
  ps.open(0, 0, 0);
  const rA = ps.render(), vt = m.ptext(kp.page, kp.col), size = L.knobSize(kn, "p5"), kcx = kn.x + (kn.w >> 1);
  const box = rA.lint.texts.find((x) => x.text === vt && x.box[1] < kn.y + size);
  const mid = box && [(box.box[0] + box.box[2]) / 2, (box.box[1] + box.box[3]) / 2];
  ok(!!mid && Math.abs(mid[0] - kcx) <= size / 2 && mid[1] >= kn.y && mid[1] <= kn.y + size,
     `a p5 knob's value "${vt}" inside its dial (${kn.id}: box ${box ? box.box.join(" ") : "none"}, the dial ${kcx - size / 2} ${kn.y} .. ${kcx + size / 2} ${kn.y + size})`);
  const red = m.draw("", "", [{ op: "rect", x: 100, y: 100, w: 8, h: 8, col: L.COLOURS.red }]).slice((104 * 240 + 104) * 4, (104 * 240 + 104) * 4 + 3);
  const lit = sA.elements.find((x) => x.type === "led" && m.pget(pidx.get(x.param).page, pidx.get(x.param).col) > pidx.get(x.param).min);
  const pix = (x, y) => Array.from(rA.rgba.slice((y * 240 + x) * 4, (y * 240 + x) * 4 + 3));
  const px4 = lit && pix(lit.x + (lit.w >> 1), lit.y + 17);   // (the LED's centre: the cap's window, 5 px under its top)
  ok(!!lit && px4.join() === Array.from(red).join(), `a lit p5 led's dot is red (${lit ? lit.id : "no lit led"}: ${px4 ? px4.join(" ") : ""}, red ${Array.from(red).join(" ")})`);
  // a p5 led's icon: one cycle of its wave over the cap (not blank); the cap's window darker than its pad
  const saw = sA.elements.find((x) => x.type === "led" && x.glyph === "saw"), scx = saw.x + (saw.w >> 1), bgp = pix(0, 239).join();
  let ink = 0;
  for (let y = saw.y; y < saw.y + 10; y++) for (let x = scx - 8; x < scx + 8; x++) ink += pix(x, y).join() !== bgp;
  const lum = (c) => c[0] * 0.3 + c[1] * 0.59 + c[2] * 0.11, win = pix(scx - 7, saw.y + 14), pad = pix(scx, saw.y + 25);
  ok(ink >= 10 && lum(win) < lum(pad), `a p5 led's saw icon is drawn over its cap (${ink} px of ink), its window darker than its pad (${win.join(" ")} < ${pad.join(" ")})`);
  // every wave icon's ink (in its own 16 x 10 box: a pair's icons are neighbours) centred over its LED within a pixel
  const offc = [];
  let icons = 0;
  for (const [gi, si] of [[0, 0], [3, 0]]) {
    ps.open(gi, si, 0);
    const r = ps.render(), q = (x, y) => Array.from(r.rgba.slice((y * 240 + x) * 4, (y * 240 + x) * 4 + 3)), b0 = lum(q(0, 239));
    r.ops.forEach((o, i) => {
      if (o.op !== "wave" || o.kind > 2) return;
      const d = r.ops.slice(i).find((x) => x.op === "disc");
      let x0 = 999, x1 = -1;
      for (let y = o.y; y < o.y + o.h; y++) for (let x = o.x; x < o.x + o.w; x++) if (lum(q(x, y)) - b0 > 20) { x0 = Math.min(x0, x); x1 = Math.max(x1, x); }
      icons++;
      if (!d || Math.abs((x0 + x1 + 1) / 2 - d.cx16 / 16) > 1) offc.push(`${["saw", "triangle", "square"][o.kind]} ink ${x0}..${x1}, LED ${d ? d.cx16 / 16 : "?"}`);
    });
  }
  ok(icons >= 5 && !offc.length, `the p5 leds' wave icons (${icons}: saw, triangle, square) centred over their LEDs within 1 px` + (offc.length ? ` (${offc.join("; ")})` : ""));
  // two LEDs for one parameter: FILTER+:REV, the left red at 1/2, the right yellow at 3
  const yel = m.draw("", "", [{ op: "rect", x: 100, y: 100, w: 8, h: 8, col: L.COLOURS.yellow }]).slice((104 * 240 + 104) * 4, (104 * 240 + 104) * 4 + 3).join();
  const fsc = pan.groups[1].screens[0], rev = fsc.elements.find((x) => x.leds === 2), rp = pidx.get(rev.param), rcx = rev.x + (rev.w >> 1);
  const rl = fsc.lanes.findIndex((l) => l.includes(rev.id)), rk = fsc.lanes[rl].indexOf(rev.id) + 1;
  ps.open(1, 0, rl);
  ps.knob(rk, rp.max - m.pget(rp.page, rp.col));
  let rr = ps.render().rgba;
  const ledAt = (rgba, x) => Array.from(rgba.slice(((rev.y + 17) * 240 + x) * 4, ((rev.y + 17) * 240 + x) * 4 + 3)).join();
  const atMax = [m.ptext(rp.page, rp.col), ledAt(rr, rcx + 7), ledAt(rr, rcx - 7)];
  ps.knob(rk, -1);
  rr = ps.render().rgba;
  const atMin = [m.ptext(rp.page, rp.col), ledAt(rr, rcx - 7), ledAt(rr, rcx + 7)];
  ok(atMax[1] === yel && atMax[2] !== atMax[1] && atMin[1] === Array.from(red).join() && atMin[2] !== atMin[1],
     `two LEDs (${rev.id}): at "${atMax[0]}" the right one yellow (${atMax[1]}), at "${atMin[0]}" the left one red (${atMin[1]}), the other off`);
  // a pair's caps meet as one: the shared rim and the top edge across the seam are drawn, not the background (the LFO's
  // SAW and TRI made a pair in a copy: the owner's panel has them as single buttons)
  const paired = JSON.parse(JSON.stringify(pan)), lsc = paired.groups[3].screens[0];
  const pL = lsc.elements.find((x) => x.param === "LFO+:SAW"), pR = lsc.elements.find((x) => x.param === "LFO+:TRI"), seam = pL.x + pL.w;
  Object.assign(pL, { pair: "left" });
  Object.assign(pR, { pair: "right", x: seam, y: pL.y, h: pL.h });
  const pss = L.designSession(m, cat, paired, 1);
  pss.reload(); pss.open(3, 0, 0);
  const rq = pss.render().rgba, q2 = (x, y) => Array.from(rq.slice((y * 240 + x) * 4, (y * 240 + x) * 4 + 3)).join(), bgq = q2(0, 239);
  ok(q2(seam - 1, pL.y + 26) !== bgq && q2(seam - 1, pL.y + 12) !== bgq && q2(seam, pL.y + 12) !== bgq && q2(seam - 2, pL.y + 12) !== bgq,
     `a pair's two caps touch (${pL.id} | its right half at x ${seam}: the shared rim ${q2(seam - 1, pL.y + 26)}, not the background ${bgq})`);
  ok(!L.validateDesign(paired, cat).warnings.length, "validateDesign: a pair that touches, no warning");
  const apart = JSON.parse(JSON.stringify(paired));
  apart.groups[3].screens[0].elements.find((x) => x.pair === "right").x += 4;
  ok(L.validateDesign(apart, cat).warnings.some((w) => /half/.test(w)), "validateDesign warns on a pair whose halves do not touch");
  // the p5 baseline rule: OSC A's FREQ and PULSE W (knobs at y 44), "-Shape-" (a label box ending at 92) and SYNC (a led
  // at y 60, its label below) on one baseline; a led at y 60 and a knob at y 44 give the same label baseline
  ps.open(0, 0, 0);
  const rb = ps.render(), words = ["FREQ", "PULSE W", "-Shape-", "SYNC"];
  const bases = words.map((t) => { const o = rb.ops.find((x) => x.op === "text" && x.s === t); return o ? o.y : null; });
  const inkEnd = ["PULSE W", "SYNC"].map((t) => { const b = rb.lint.texts.find((x) => x.text === t); return b ? b.box[3] : null; });   // (no descenders: the ink ends on the baseline)
  ok(bases.every((v) => v === bases[0]) && inkEnd.every((v) => v === bases[0]),
     `the p5 baseline rule on OSC A: ${words.join(", ")} on baseline ${bases.join(", ")} (the ink of PULSE W and SYNC ends at ${inkEnd.join(", ")})`);
  const probe = { type: "free", title: "T", elements: [
    { id: "k", type: "knob", x: 8, y: 44, w: 56, h: 64, param: "OSC A:FREQ", label: "AAA", size: 36 },
    { id: "l", type: "led", x: 68, y: 60, w: 56, h: 32, param: "OSC A:SAW", label: "BBB", labelAt: "below" },
    { id: "t", type: "label", x: 128, y: 80, w: 56, h: 12, text: "CCC" }], lanes: [["k", "l", null, null]] };
  const pops = L.freeOps(probe, pidx, () => ({ v: 0, text: "0" }), { style: "p5", measure: (t, px, b) => m.textW(t, px, b) });
  const bl = ["AAA", "BBB", "CCC"].map((t) => pops.find((o) => o.op === "text" && o.s === t).y);
  ok(bl.every((v) => v === 92), `the rule by construction: a knob at y 44 (size 36), a led at y 60 (label below), a label box at y 80 h 12: baselines ${bl.join(", ")}`);
  const frs = pan.groups.flatMap((g) => g.screens.filter((x) => x.type === "free").flatMap((x) => x.elements.filter((e) => e.type === "frame")));
  ok(frs.length > 0 && frs.every((e) => e.r === 8), `the owner's panel: all ${frs.length} frames at r 8`);
  // a led's label below its cap (labelAt): OSC A's SYNC
  const syn = sA.elements.find((x) => x.labelAt === "below");
  ps.open(0, 0, 0);
  const sb = syn && ps.render().lint.texts.find((x) => x.text === syn.label);
  ok(!!sb && sb.box[1] >= syn.y + 20 && sb.box[3] <= syn.y + syn.h + 3,
     `labelAt "below": ${syn ? syn.id : "no element"}'s "${syn && syn.label}" under its cap (text rows ${sb ? sb.box[1] + " .. " + sb.box[3] : "?"}, the cap ends at ${syn ? syn.y + 20 : "?"})`);
  // a frame's corner radius: at r 12 the corner pixel is the background, the side is drawn
  const r12 = JSON.parse(JSON.stringify(pan)), fr = r12.groups[0].screens[0].elements.find((x) => x.type === "frame");
  fr.r = 12;
  const rs = L.designSession(m, cat, r12, 1);
  rs.reload(); rs.open(0, 0, 0);
  const rr12 = rs.render(), q12 = (x, y) => Array.from(rr12.rgba.slice((y * 240 + x) * 4, (y * 240 + x) * 4 + 3)).join();
  ok(L.validateDesign(r12, cat).errors.length === 0 && q12(fr.x + 2, fr.y + 2) === q12(0, 239) && q12(fr.x, fr.y + (fr.h >> 1)) !== q12(0, 239) &&
     L.validateDesign({ ...r12, groups: [{ ...r12.groups[0], screens: [{ ...r12.groups[0].screens[0], elements: [{ ...fr, r: 13 }] }] }] }, cat).errors.length > 0,
     `a frame's corners at r 12 (${fr.id}): the corner pixel is the background, the side drawn; r 13 refused`);
  // the wheel-mod source mix: LFO:NOISE on WHEEL, in a lane, not on the LFO screen; the palette's hint
  const srcm = pan.groups.flatMap((g) => g.screens.filter((x) => x.type === "free").map((x) => ({ x, e: x.elements.find((e) => e.param === "LFO:NOISE") }))).filter((t) => t.e);
  ok(srcm.length === 1 && srcm[0].x.title === "WHEEL" && srcm[0].x.lanes.some((l) => l.includes(srcm[0].e.id)) && /source mix/.test(L.PARAM_HINTS["LFO:NOISE"]),
     `the wheel-mod source mix: LFO:NOISE once, on ${srcm[0] ? srcm[0].x.title : "?"} ("${srcm[0] ? srcm[0].e.label : ""}"), in a lane; the palette's hint "${L.PARAM_HINTS["LFO:NOISE"]}"`);
  // paste: copies onto another screen, fresh ids, no lane, an identical box moved 8 px
  const pd = JSON.parse(JSON.stringify(pan)), from = pd.groups[0].screens[0], to = pd.groups[1].screens[0];
  const clip = from.elements.filter((x) => x.id === "a_freq" || x.id === "a_sync");
  const pasted = L.pasteElements(to, clip), again2 = L.pasteElements(to, clip.slice(0, 1));
  const idsTo = to.elements.map((x) => x.id);
  ok(pasted.length === 2 && pasted.every((x, i) => x.param === clip[i].param && x.x === clip[i].x && x.y === clip[i].y && x.labelAt === clip[i].labelAt) &&
     new Set(idsTo).size === idsTo.length && !to.lanes.flat().some((x) => pasted.some((y) => y.id === x)) &&
     again2[0].x === clip[0].x + 8 && again2[0].y === clip[0].y + 8 && L.usedTwice(pd).some((u) => u.param === "OSC A:FREQ"),
     `pasteElements: ${pasted.map((x) => x.id).join(", ")} on FILTER at their places, in no lane; the same again lands 8 px off (${again2[0].x}, ${again2[0].y}); the lint sees the parameter twice`);
  m.unload();
}

// ===================================================================================================== 7 ===
// The unit's free screens (firmware/src/cr_draw.c CR_K_FREE, the compiled p5-panel.json: lay_layouts on, the real
// editor) against the tool's composition of the same design (freeOps / p5Op through the draw API) in the same state:
// every screen x lane, and every bound slot turned (its element hot, REV both ways): identical pixels
console.log("the firmware's free screens against the tool's: P5's panel");
{
  const e = ver.engines.find((x) => x.name === "P5"), cat = cats.P5, params = L.paramIndex(L.paramList(cat));
  const pd = JSON.parse(fs.readFileSync(path.join(ROOT, "design/layouts/designs/p5-panel.json"), "utf8"));
  const measure = (s, px, b) => m.textW(s, px, b);
  const value = (p) => ({ v: m.pget(p.page, p.col), text: m.ptext(p.page, p.col) });
  let n = 0, hot = 0, notFree = [], differ = [];
  m.layouts(true);
  pd.groups.forEach((g, gi) => g.screens.forEach((s, si) => {
    if (s.type !== "free") return;
    for (let lane = 0; lane < s.lanes.length; lane++)
      for (const k of [0, 1, 2, 3, 4]) {
        if (k && !s.lanes[lane][k - 1]) continue;
        m.show(e.index, 1, g.group, si, lane);
        if (k) m.knob(k, k % 2 ? 1 : -1);
        const a = m.render(), st = m.state();
        if (st.kind !== "free" || st.page !== s.title || (k && st.hot !== k)) notFree.push(`${g.name} ${si + 1}.${lane + 1} k${k}`);
        const ops = L.freeOps(s, params, value, { lane, hot: k ? s.lanes[lane][k - 1] : null, measure, style: pd.style });
        const d = L.diffCount(a, m.draw(st.title, s.title, ops));
        n++;
        hot += k ? 1 : 0;
        if (d) differ.push(`${g.name} ${si + 1}.${lane + 1}${k ? ` K${k}` : ""} (${d} px)`);
      }
  }));
  m.layouts(false);
  ok(!notFree.length, `lay_layouts on: P5's OSC .. MOD are the panel's free screens (CR_K_FREE), the title its, the turned slot hot` +
     (notFree.length ? ` (not: ${notFree.slice(0, 6).join(", ")})` : ""));
  ok(n === 75 && !differ.length, `${n} frames (${n - hot} screen x lane, ${hot} with a slot turned and hot): the unit's pixels are the tool's` +
     (differ.length ? `\n          differ: ${differ.slice(0, 8).join("; ")}` : ""));
}

// ===================================================================================================== 5 ===
function readPng(file) {   // 8-bit RGB / RGBA, not interlaced -> {w, h, at(x, y) -> [r, g, b]}
  const b = fs.readFileSync(file);
  let p = 8, w = 0, h = 0, ch = 3;
  const idat = [];
  while (p < b.length) {
    const len = b.readUInt32BE(p), type = b.toString("latin1", p + 4, p + 8);
    if (type === "IHDR") { w = b.readUInt32BE(p + 8); h = b.readUInt32BE(p + 12); ch = b[p + 17] === 6 ? 4 : 3; }
    if (type === "IDAT") idat.push(b.subarray(p + 8, p + 8 + len));
    p += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat)), stride = w * ch, px = Buffer.alloc(h * stride);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], src = y * (stride + 1) + 1, dst = y * stride;
    for (let i = 0; i < stride; i++) {
      const a = i >= ch ? px[dst + i - ch] : 0, u = y ? px[dst - stride + i] : 0, c = y && i >= ch ? px[dst - stride + i - ch] : 0;
      const pa = Math.abs(u - c), pb = Math.abs(a - c), pc = Math.abs(a + u - 2 * c);
      const pred = f === 0 ? 0 : f === 1 ? a : f === 2 ? u : f === 3 ? (a + u) >> 1 : pa <= pb && pa <= pc ? a : pb <= pc ? u : c;
      px[dst + i] = (raw[src + i] + pred) & 255;
    }
  }
  return { w, h, at: (x, y) => [px[y * stride + x * ch], px[y * stride + x * ch + 1], px[y * stride + x * ch + 2]] };
}
if (!args.has("--no-page")) {
  console.log("the page (headless Chromium, file://, ?selftest=1)");
  const chrome = "/Applications/Chromium.app/Contents/MacOS/Chromium";
  if (!fs.existsSync(chrome)) console.log("  skip  screenshot: no " + chrome);
  else {
    const dir = path.join(TMP, "page"), png = path.join(TMP, "selftest.png"), prof = path.join(TMP, `chrome-profile-${process.pid}`);
    execFileSync("python3", [path.join(ROOT, "tools/layout/make_page.py"), "--out", dir], { stdio: "pipe" });
    try { fs.unlinkSync(png); } catch {}
    const t1 = performance.now();
    const c = spawn(chrome, ["--headless=new", "--use-mock-keychain", "--password-store=basic", "--disable-gpu", "--no-sandbox", "--no-first-run",
      `--user-data-dir=${prof}`, "--hide-scrollbars", "--window-size=1280,1400", "--timeout=15000", "--virtual-time-budget=3000",
      `--screenshot=${png}`, `file://${path.join(dir, "index.html")}?engine=P5&selftest=1`], { stdio: "ignore" });
    for (let i = 0; i < 60 && !(fs.existsSync(png) && fs.statSync(png).size > 0); i++) await new Promise((r) => setTimeout(r, 500));
    await new Promise((r) => setTimeout(r, 500));
    c.kill();   // (by PID: never by pattern)
    await new Promise((r) => setTimeout(r, 300));
    try { fs.rmSync(prof, { recursive: true, force: true }); } catch {}   // (this run's own profile)
    if (!ok(fs.existsSync(png) && fs.statSync(png).size > 20000, `screenshot: ${path.relative(ROOT, png)} (${((performance.now() - t1) / 1000).toFixed(1)} s)`)) {
      // nothing to read
    } else {
      const img = readPng(png), sq = (i) => img.at(10 + 20 * i, 10);
      const green = (p) => p[0] < 40 && p[1] > 215 && p[2] < 40, blue = (p) => p[0] < 40 && p[1] < 40 && p[2] > 215;
      const what = ["the canvas is drawn (not blank)", "a click on KNOB 1's + changes the picture and marks the cell hot",
                    `the contact sheet has every screen x lane of P5 (${L.tiles(cats.P5).length} tiles), none blank`,
                    "the lint list, the version line and the inspector's four cells are filled"];
      ok(blue(sq(4)), "the page ran its self-test to the end (the end mark is there)");
      what.forEach((w, i) => ok(green(sq(i)), `page: ${w}`));
      const colours = new Set();
      for (let y = 300; y < img.h; y += 7) for (let x2 = 0; x2 < img.w; x2 += 7) colours.add(img.at(x2, y).join(","));
      ok(colours.size > 40, `the screenshot shows the sheet (${colours.size} colours sampled)`);
    }
  }
}

if (!args.has("--no-page") && fs.existsSync("/Applications/Chromium.app/Contents/MacOS/Chromium")) {
  console.log("the page in Design mode (headless Chromium, file://, ?selftest=design)");
  const chrome = "/Applications/Chromium.app/Contents/MacOS/Chromium";
  const dir = path.join(TMP, "page"), png = path.join(TMP, "selftest-design.png"), prof = path.join(TMP, `chrome-profile-d-${process.pid}`);
  try { fs.unlinkSync(png); } catch {}
  const c = spawn(chrome, ["--headless=new", "--use-mock-keychain", "--password-store=basic", "--disable-gpu", "--no-sandbox", "--no-first-run",
    `--user-data-dir=${prof}`, "--hide-scrollbars", "--window-size=1400,1400", "--timeout=15000", "--virtual-time-budget=3000",
    `--screenshot=${png}`, `file://${path.join(dir, "index.html")}?engine=P5&selftest=design`], { stdio: "ignore" });
  for (let i = 0; i < 60 && !(fs.existsSync(png) && fs.statSync(png).size > 0); i++) await new Promise((r) => setTimeout(r, 500));
  await new Promise((r) => setTimeout(r, 500));
  c.kill();   // (by PID)
  await new Promise((r) => setTimeout(r, 300));
  try { fs.rmSync(prof, { recursive: true, force: true }); } catch {}
  if (ok(fs.existsSync(png) && fs.statSync(png).size > 20000, `screenshot: ${path.relative(ROOT, png)}`)) {
    const img = readPng(png), sq = (i) => img.at(10 + 20 * i, 10);
    const green = (p) => p[0] < 40 && p[1] > 215 && p[2] < 40, blue = (p) => p[0] < 40 && p[1] < 40 && p[2] > 215;
    const what = ["Design mode opens the shipped P5 example (designs.js)", "a free screen is drawn and marked \"not buildable yet\"",
                  "KNOB 1's + turns a dial on the free screen", "a scripted drag from the element strip adds a knob on the 4 px grid (one undo step)",
                  "Export JSON equals the model (and validates)", "the contact sheet has today's tiles and the design's",
                  "the shipped p5-panel opens in the p5 style and its free screen draws P5 knobs",
                  "copy an element, paste it on another screen: the export has it with a new id, selected, in no lane (one undo step)"];
    ok(blue(sq(8)), "the Design self-test ran to the end (the end mark is there)");
    what.forEach((w, i) => ok(green(sq(i)), `page: ${w}`));
  }
}

console.log(fails ? `FAIL (${fails})` : "PASS");
process.exit(fails ? 1 : 0);
