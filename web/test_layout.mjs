// SPDX-License-Identifier: GPL-3.0-only
// The layout tool's preview module and viewer (docs/LAYOUT-TOOL.md, step 2). node >= 20:
//   node web/test_layout.mjs [--no-page] [--no-emu]
// (after sh tools/layout/build_layout.sh; the comparison with the emulator needs sh tools/emu/web/build_web.sh too)
// 1. build/layout-web/crlayout.wasm: lay_version, lay_presets, lay_catalog of every engine (every cell bound to a
//    parameter has a label and a range; P5: 19 pages, 56 cells), lay_load's reserved answer.
// 2. Every screen x lane of P5, VA and FM TONE on the engine's first factory sound: not blank, the same bytes when
//    drawn twice and when shown again; a knob turn changes its cell's value and the picture and marks the cell hot
//    (and 800 ms later the mark is gone); SELECT moves the lane.
// 3. The same frames from the browser emulator (build/emu-web/choralroot.wasm: the firmware without the layout API,
//    driven by its panel alone: GLO + PRESETS to the engine, EDIT, the group buttons, SELECT, a knob): the LCD's
//    RGB565 must be identical, pixel for pixel, on every screen x lane of the three engines.
// 4. design/layouts/*.json are what the firmware says today (tools/layout/dump_catalogs.mjs).
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
ok(/^ChoralRoot \d/.test(ver.firmware) && ver.api === 1 && ver.format === L.FORMAT && ver.format_version === L.FORMAT_VERSION,
   `lay_version: ${ver.firmware}, api ${ver.api}, ${ver.format} v${ver.format_version}`);
const names = ver.engines.map((e) => e.name);
ok(["FM6", "FM TONE", "VA", "CZ-1", "P5", "WHEEL"].every((n) => names.includes(n)), `engines: ${names.join(", ")}`);
ok(ver.engines.every((e) => /^[0-9a-f]{16}$/.test(e.params_hash)) && new Set(ver.engines.map((e) => e.params_hash)).size === ver.engines.length,
   "every engine has its own params_hash");
ok(true, `instantiated in ${tLoad.toFixed(0)} ms, power-on + snapshot (${m.snapshotBytes()} B of static data) in ${tBoot.toFixed(0)} ms (node, this machine)`);
ok(m.load().ok === false && m.load().error === "not implemented", "lay_load: reserved, \"not implemented\"");
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
      for (const f of frames[name]) {
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
      m.show(e.index, 1, 1, 0, 0);
      m.knob(1, 1);
      m.render();
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

console.log(fails ? `FAIL (${fails})` : "PASS");
process.exit(fails ? 1 : 0);
