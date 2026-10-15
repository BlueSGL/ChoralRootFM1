// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot FM-1 layout tool (docs/LAYOUT-TOOL.md, step 2: a viewer): the model. Pure code, no DOM: the page
// (web/layout.html) and node (web/test_layout.mjs, tools/layout/dump_catalogs.mjs) use the same functions.
//   wrap(ex)            the preview module's exports (build/layout-web/crlayout.wasm: lay_*) as plain calls
//   tiles(catalog)      every screen x lane of a catalog, in the editor's order
//   formatLayout(cat)   a catalog as the text of its layout file (design/layouts/<engine>.json)
//   layoutFileName, ruleNotes, lintCounts, sheetGeometry, GROUP_KNOB_COLOURS ..

export const SCREEN = 240;
export const FORMAT = "choralroot-layout";
export const FORMAT_VERSION = 1;
// the unit's knob caps, KNOB 1..4 (docs/EDITOR.md: blue, orange, white, green)
export const KNOB_COLOURS = ["blue", "orange", "white", "green"];
export const SHOW_ERRORS = { "-1": "no such engine", "-2": "no such sound", "-3": "the engine has no such group",
                             "-4": "no such screen", "-5": "no such lane" };

// ---------------------------------------------------------------- the module ---
// ex: the raw exports (CRLayoutModule.load()). Strings are C strings in the module's memory, JSON answers are parsed.
export function wrap(ex) {
  const mem = () => new Uint8Array(ex.memory.buffer);
  const cstr = (p) => {
    const m = mem();
    let e = p;
    while (m[e]) e++;
    let s = "";
    for (let i = p; i < e; i++) s += String.fromCharCode(m[i]);   // (the module's JSON is ASCII: · escapes)
    return s;
  };
  const json = (p) => JSON.parse(cstr(p));
  const catalogs = new Map();
  let version = null;
  return {
    ex,
    version() { return version || (version = json(ex.lay_version())); },
    // today's built-in layout of an engine (kept: the tables do not change while the module lives). The module walks
    // it from its reset state, so the editor is closed afterwards: show() again
    catalog(engine) {
      if (!catalogs.has(engine)) catalogs.set(engine, json(ex.lay_catalog(engine)));
      return catalogs.get(engine);
    },
    presets(engine) { return json(ex.lay_presets(engine)).presets || []; },
    // the real editor on that sound at that place, from the reset instance; -> the screen's lanes, or throws
    show(engine, preset, group, screen, lane) {
      const n = ex.lay_show(engine, preset, group, screen, lane);
      if (n < 0) throw new Error(`lay_show(${engine}, ${preset}, ${group}, ${screen}, ${lane}): ${SHOW_ERRORS[n] || n}`);
      return n;
    },
    knob(k, detents) { ex.lay_knob(k, detents); },
    select(detents) { ex.lay_select(detents); },
    step(ms) { ex.lay_step(ms); },
    reset() { ex.lay_reset(); },
    // the picture: 240 x 240 RGBA, a copy
    render() { const p = ex.lay_render(); return new Uint8ClampedArray(ex.memory.buffer, p, SCREEN * SCREEN * 4).slice(); },
    // the LCD as the firmware wrote it: RGB565 big-endian, a copy (the emulator's web_fb, for comparisons)
    fb() { return new Uint16Array(ex.memory.buffer, ex.lay_fb(), SCREEN * SCREEN).slice(); },
    lint() { return json(ex.lay_lint()); },
    state() { return json(ex.lay_state()); },
    // a packed design (packDesign) for its engine's classic pages: in effect from the next show(); unload(): none
    load(bytes) {
      const b = bytes || new Uint8Array(0), n = Math.min(b.length, ex.lay_buf_size());
      mem().set(b.subarray(0, n), ex.lay_buf());
      return json(ex.lay_load(ex.lay_buf(), n));
    },
    unload() { return this.load(new Uint8Array([76, 2, 255])); },
    // a parameter of the editor's sound by its built-in address (page, col; page 255: a part parameter, col its id)
    pget(page, col) { return ex.lay_pget(page, col); },
    ptext(page, col) { return cstr(ex.lay_ptext(page, col)); },
    pstep(page, col, n) { return ex.lay_pstep(page, col, n); },
    // a free page drawn by the firmware's primitives under its title line: ops (freeOps) -> 240 x 240 RGBA, a copy
    draw(title, page, ops) {
      const buf = ex.lay_buf(), STR = 60000;
      let at = STR;
      const put = (t) => {   // a C string (Latin-1) in the module's buffer (each call copies it)
        const b = toLatin1(t || "");
        if (at + b.length + 1 > 63900) at = STR;
        const p = buf + at;
        mem().set(b, p);
        mem()[p + b.length] = 0;
        at += b.length + 1;
        return p;
      };
      ex.lay_draw_begin(put(title), put(page));
      for (const o of ops) {
        if (o.op === "text") ex.lay_draw_text(o.bold ? 1 : 0, o.px, o.x, o.y, put(o.s), o.col, o.align || 0, o.maxw || 0);
        else if (o.op === "rect") ex.lay_draw_rect(o.x, o.y, o.w, o.h, o.col, o.r || 0, o.line || 0);
        else if (o.op === "glyph") ex.lay_draw_glyph(o.g, o.x, o.y, o.w, o.h, o.col, o.pct || 0, o.pct2 || 0, o.flags || 0);
        else if (o.op === "knob") ex.lay_draw_knob(o.cx, o.cy, o.size, o.col, o.pct || 0, o.bip ? 1 : 0);
        else if (o.op === "p5knob") ex.lay_draw_p5knob(o.cx, o.cy, o.size, o.face, o.rim, o.tick, o.ptr, o.pct || 0, o.bip ? 1 : 0, o.from || 0);
        else if (o.op === "disc") ex.lay_draw_disc(o.cx16, o.cy16, o.r16, o.col);
        else if (o.op === "wave") ex.lay_draw_wave(o.kind, o.x, o.y, o.w, o.h, o.col);
        else if (o.op === "hbar") ex.lay_draw_hbar(o.x, o.y, o.w, o.h, o.col, o.pct || 0, o.bip ? 1 : 0);
        else if (o.op === "band") {
          const p = buf + 64000;
          mem().set(Uint8Array.from((o.wv || []).concat(new Array(20).fill(0)).slice(0, 20)), p);
          ex.lay_draw_band(o.kind, p);
        }
      }
      const p = ex.lay_draw_end();
      return new Uint8ClampedArray(ex.memory.buffer, p, SCREEN * SCREEN * 4).slice();
    },
    textW(s, px, bold) {
      const p = ex.lay_buf() + 60000, b = toLatin1(s);
      mem().set(b, p);
      mem()[p + b.length] = 0;
      return ex.lay_text_w(p, px, bold ? 1 : 0);
    },
    // the band values of the editor's screen model now (cr_screen_t.wv)
    wv() { return Array.from(new Uint8Array(ex.memory.buffer, ex.lay_wv(), 20)); },
    snapshotBytes() { return ex.lay_snapshot_bytes(); },
  };
}

// ------------------------------------------------------------- the catalog ---
// every screen x lane, in the editor's order: [{group, groupName, screen, lane, title, kind, band, cells, key}]
export function tiles(catalog) {
  const out = [];
  for (const g of catalog.groups || [])
    for (const s of g.screens)
      for (const l of s.lanes)
        out.push({ group: g.group, groupName: g.name, button: g.button, screen: s.screen, lane: l.lane, lanes: s.lanes.length,
                   title: l.title, kind: s.kind, type: s.type, band: s.band, tall: s.tall, plan: s.plan ?? null,
                   headings: s.headings || null, pages: s.pages, cells: l.cells,
                   key: `${g.group}.${s.screen}.${l.lane}` });
  return out;
}
export const tileLabel = (t) => `${t.groupName} ${t.screen + 1}${t.lanes > 1 ? "." + (t.lane + 1) : ""} · ${t.title}`;

// the cells bound to a parameter, and the deep-page cells among them
export function counts(catalog) {
  const ts = tiles(catalog);
  const cells = ts.flatMap((t) => t.cells);
  return { groups: (catalog.groups || []).length, screens: (catalog.groups || []).reduce((n, g) => n + g.screens.length, 0),
           lanes: ts.length, cells: cells.filter((c) => c.bind === "deep" || c.bind === "track").length,
           pages: (catalog.pages || []).length, pageCells: catalog.page_cells || 0 };
}

export const cellRange = (c) => (c.min === undefined ? "" : `${c.min} .. ${c.max} (default ${c.def})`);
export function cellBinding(c) {
  if (c.bind === "deep") return `page ${c.page} "${c.page_title}", column ${c.col}`;
  if (c.bind === "track") return `part parameter ${c.param}`;
  if (c.bind === "text") return `fixed text "${c.text}"`;
  return c.bind === "dash" ? "a dash (no parameter in this lane)" : "empty";
}

// The owner's rules for editor pages (docs/LAYOUT-TOOL.md "The owner's rules"), checked against a catalog as far as
// a catalog can say. Rule 1 holds: the shared header IS the editor's title line (the sound's name left, the page's
// right), drawn on every editor page today; the owner: "The current header ... is good, no need to change it." Rule 2
// holds by construction (no cell or element can be bound to a key). Nothing to report.
export function ruleNotes(catalog, tile) {   // eslint-disable-line no-unused-vars
  return [];
}

export function lintCounts(lint) {
  const c = { "off-screen": 0, overlap: 0, ellipsis: 0 };
  for (const f of (lint && lint.findings) || []) c[f.kind] = (c[f.kind] || 0) + 1;
  return c;
}
// the findings that are faults (an ellipsis is a note: the text was cut on purpose)
export const lintFaults = (lint) => ((lint && lint.findings) || []).filter((f) => f.kind !== "ellipsis");

// ---------------------------------------------------------- the layout file ---
export const layoutFileName = (name) => name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") + ".json";

// the catalog as text: two-space indents, a list of plain values or a cell on one line (a diff shows a cell a line)
export function formatLayout(catalog) {
  const plain = (v) => v === null || typeof v !== "object";
  const flat = (v) => Array.isArray(v) ? v.every(plain) : Object.values(v).every((x) => plain(x) || (Array.isArray(x) && x.every(plain)));
  const one = (v) => {
    if (plain(v)) return JSON.stringify(v);
    if (Array.isArray(v)) return "[" + v.map(one).join(", ") + "]";
    return "{" + Object.entries(v).map(([k, x]) => JSON.stringify(k) + ": " + one(x)).join(", ") + "}";
  };
  const fmt = (v, ind) => {
    if (plain(v) || flat(v)) return one(v);
    const pad = ind + "  ";
    if (Array.isArray(v)) return "[\n" + v.map((x) => pad + fmt(x, pad)).join(",\n") + "\n" + ind + "]";
    return "{\n" + Object.entries(v).map(([k, x]) => pad + JSON.stringify(k) + ": " + fmt(x, pad)).join(",\n") + "\n" + ind + "}";
  };
  return fmt(catalog, "") + "\n";
}

// is a layout file for this firmware's engine? -> null, or why not
export function checkLayout(file, version) {
  if (!file || file.format !== FORMAT) return `not a ${FORMAT} file`;
  if (file.version !== FORMAT_VERSION) return `format version ${file.version}, this tool reads ${FORMAT_VERSION}`;
  const e = (version.engines || []).find((x) => x.index === (file.engine || {}).index);
  if (!e) return `engine ${(file.engine || {}).name} is not in ${version.firmware}`;
  if (e.params_hash !== file.params_hash)
    return `made for other parameters of ${e.name} (params_hash ${file.params_hash}, the firmware's ${e.params_hash})`;
  return null;
}

// ------------------------------------------------------- the contact sheet ---
// n tiles of 240 x 240 at `scale`, `cols` a row, a caption line under each, a title line on top
export function sheetGeometry(n, cols, scale = 1) {
  const gap = 12, cap = 22, top = 34, w = SCREEN * scale;
  const rows = Math.ceil(n / cols);
  return { cols, rows, gap, cap, top, tile: w, width: gap + cols * (w + gap), height: top + rows * (w + cap + gap) + gap,
           at: (i) => ({ x: gap + (i % cols) * (w + gap), y: top + Math.floor(i / cols) * (w + cap + gap) }) };
}

// pixels of b that differ from a (two RGBA or RGB565 pictures of one size)
export function diffCount(a, b) {
  let n = 0;
  if (a.length === SCREEN * SCREEN * 4) {
    for (let i = 0; i < a.length; i += 4) n += a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2];
  } else {
    for (let i = 0; i < a.length; i++) n += a[i] !== b[i];
  }
  return n;
}
// pixels that are not the picture's most common colour (a blank screen: 0)
export function inkCount(rgba) {
  const seen = new Map();
  const px = new Uint32Array(rgba.buffer, rgba.byteOffset, rgba.length >> 2);
  for (const p of px) seen.set(p, (seen.get(p) || 0) + 1);
  let top = 0;
  for (const v of seen.values()) top = Math.max(top, v);
  return px.length - top;
}
export function toLatin1(s) {
  const out = [];
  for (const ch of String(s)) { const c = ch.codePointAt(0); out.push(c < 256 ? c : 63); }
  return Uint8Array.from(out);
}

// ================================================================ the designer (steps 3 and 4) ===
// A DESIGN (docs/LAYOUT-TOOL.md "The design format, version 2"): an engine's groups -> ordered screens, each either
// CLASSIC (one of today's kinds, lanes of four cells bound to parameters by id) or FREE (placed elements on a 4 px grid
// under the shared header, and lanes of up to four bound elements = KNOB 1..4, SELECT between lanes). Pure code: the
// page and the tests share it. Rendering goes through the module (wrap above): classic screens by lay_load + the real
// editor, free screens by the preview-only draw API.
export const DESIGN_VERSION = 2;
export const GRID = 4;
export const HEADER_H = 28;                  // the title line: rows 0 .. 27 are the header, the design area is below
export const MAX_LANES = 8, MAX_ELEMENTS = 24, LANE_SLOTS = 4;   // the firmware's cell[8][4] budget
export const HEADER_RIGHT_MAX = 112;         // the header's right text (the page's name, 11 px bold) at most this wide
// cr_screen.h CR_COL_*: the named colours and the palette's tokens (the only colours a design may use)
export const COLOURS = { white: 1, red: 2, blue: 3, yellow: 4, orange: 5, green: 6, grey: 7, black: 8, bg: 9, surf: 10,
                         text: 11, theme: 12, accent: 13, mid: 14, dim: 15, line: 16 };
export const KNOB_COL = [COLOURS.blue, COLOURS.orange, COLOURS.white, COLOURS.green];
// cr_screen.h CR_G_* in order
export const GLYPHS = ["none", "knob", "bar", "wave", "saw", "square", "steps", "dots", "morph", "noise", "room", "moon",
  "echoes", "lfo", "clip", "mix", "gate", "range", "arrow", "shift", "harm", "detune", "lfowave", "ratio", "tail", "pre",
  "cut", "width", "spread", "sync", "ping"];
export const BANDS = ["none", "env", "filter", "dx", "cz", "algo", "ade2"];             // CR_W_*
const PLAN_BAND = { none: 0, env: 1, filter: 2, algo: 4, ade2: 5 };                     // ENG_B_*
export const ELEMENT_TYPES = ["frame", "label", "knob", "switch", "led", "bar", "glyph", "band"];
export const BINDABLE = new Set(["knob", "switch", "led", "bar", "glyph"]);
// an led's small waveform: the glyph and (lfowave) its wave
export const LED_GLYPHS = { saw: [4, 0], square: [5, 0], triangle: [22, 0], sine: [3, 0], noise: [9, 0] };
export const ELEMENT_SIZE = { frame: [112, 96], label: [48, 12], knob: [56, 64], switch: [56, 64], led: [52, 12],
                              bar: [56, 28], glyph: [40, 24], band: [240, 96] };
// a design's element style ("style": absent = "flat", the editor's look; "p5" = the Prophet-5's panel: framed sections
// titled in the border, knobs with a 0..10 scale, LED buttons, lever switches; docs/LAYOUT-TOOL.md "The p5 style")
export const STYLES = ["flat", "p5"];
const P5_SIZE = { knob: [56, 64], switch: [56, 64], led: [40, 32] };
export const P5_LED2_SIZE = [44, 32];   // a p5 led of two LEDs ("leds": 2)
// the p5 led's icon: one cycle of its wave (lay_draw_wave's kinds)
const P5_WAVE = { saw: 0, triangle: 1, square: 2, sine: 3, noise: 4 };
export const elementSize = (type, style) => (style === "p5" && P5_SIZE[type]) || ELEMENT_SIZE[type];
export const KNOB_SIZES = { flat: [24, 28, 32, 36, 40], p5: [28, 32, 36, 40, 44] };
export const knobSize = (e, style) => e.size || (style === "p5" ? 36 : 32);
export const CLASSIC_KINDS = ["edit8", "stack"];
const DIM = COLOURS.dim, MID = COLOURS.mid, LINE = COLOURS.line, BG = COLOURS.bg, TEXT = COLOURS.text, SURF = COLOURS.surf, RED = COLOURS.red;

// ------------------------------------------------------------- parameters ---
const cap = (s) => (s ? s[0] + s.slice(1).toLowerCase() : "");
// every parameter of the engine a design can bind: its stable id ("<page title>:<short>", "part:<P_ id>"), its
// built-in address (page, col; page 255 = a part parameter), the descriptor, the label today's editor shows, and the
// group / screen it is on today
export function paramList(cat) {
  const out = [], seen = new Map(), today = new Map();
  for (const t of tiles(cat))
    for (const c of t.cells) {
      const k = c.bind === "deep" ? `d${c.page}.${c.col}` : c.bind === "track" ? `t${c.param}` : null;
      if (k && !today.has(k)) today.set(k, { label: c.label, where: tileLabel(t), group: t.groupName, cell: c });
    }
  const uniq = (id) => { const n = (seen.get(id) || 0) + 1; seen.set(id, n); return n > 1 ? `${id}#${n}` : id; };
  for (const p of cat.pages || [])
    p.cols.forEach((c, i) => {
      if (!c) return;
      const t = today.get(`d${p.page}.${i}`);
      out.push({ id: uniq(`${p.title}:${c.short}`), page: p.page, col: i, pageTitle: p.title, short: c.short,
                 label: t ? t.label : cap(c.short), fmt: c.fmt, min: c.min, max: c.max, def: c.def, names: c.names,
                 unit: c.unit, where: t ? t.where : "", group: t ? t.group : "", part: false });
    });
  for (const [k, t] of today)
    if (k[0] === "t") {
      const c = t.cell;
      out.push({ id: uniq(`part:${c.param}`), page: 255, col: c.param, pageTitle: t.group, short: c.short,
                 label: c.label, fmt: c.fmt, min: c.min, max: c.max, def: c.def, names: c.names, unit: c.unit,
                 where: t.where, group: t.group, part: true });
    }
  return out;
}
export const paramIndex = (params) => new Map(params.map((p) => [p.id, p]));
// what a parameter is on the original instrument when its id does not say (the designer's palette: its search and
// its second line only)
export const PARAM_HINTS = { "LFO:NOISE": "wheel-mod source mix" };
const addrKey = (page, col) => `${page}.${col}`;

// ------------------------------------------------------------ new designs ---
const HEADER = { kind: "title-line", note: "the editor's title line: the sound's name left, the page's name right (fixed)" };
// today's layout as a design: every screen classic, as it is (the "start from today" of the designer)
export function designFromCatalog(cat, name = "today") {
  const params = paramList(cat), byAddr = new Map(params.map((p) => [p.part ? `t${p.col}` : addrKey(p.page, p.col), p.id]));
  const cell = (c) => c.bind === "deep" ? { param: byAddr.get(addrKey(c.page, c.col)) } :
                      c.bind === "track" ? { param: byAddr.get(`t${c.param}`) } :
                      c.bind === "text" ? { fixed: "text", text: c.text } : null;
  return {
    format: FORMAT, version: DESIGN_VERSION, kind: "design", name,
    engine: { index: cat.engine.index, name: cat.engine.name }, params_hash: cat.params_hash, header: { ...HEADER },
    groups: cat.groups.map((g) => ({
      group: g.group, name: g.name,
      screens: g.screens.map((s) => classicFromToday(s, cell)),
    })),
  };
}
function classicFromToday(s, cell) {
  return { type: "classic", kind: s.kind, band: s.band, title: s.lanes[0] ? s.lanes[0].title : "",
           lanes: s.lanes.map((l) => ({ title: l.title, cells: l.cells.map(cell) })) };
}
// a copy of today's screen k of group g (the "start from today's page")
export function classicScreenFromToday(cat, group, k) {
  const d = designFromCatalog(cat);
  const g = d.groups.find((x) => x.group === group);
  return JSON.parse(JSON.stringify(g.screens[k]));
}
export function newClassicScreen(kind = "edit8", title = "NEW") {
  const n = kind === "stack" ? 4 : 2;
  return { type: "classic", kind, band: "none", title,
           lanes: Array.from({ length: n }, () => ({ title, cells: [null, null, null, null] })) };
}
export function newFreeScreen(title = "NEW") { return { type: "free", title, elements: [], lanes: [] }; }
// a new element of a type at (x, y), snapped; id unique in the screen; its size the design's style's
export function newElement(screen, type, x, y, param = null, style = "flat") {
  const [w, h] = elementSize(type, style);
  let n = screen.elements.length + 1;
  while (screen.elements.some((e) => e.id === `e${n}`)) n++;
  const e = { id: `e${n}`, type, x: snap(type === "band" ? 0 : x), y: snap(type === "band" ? 24 : y), w, h };
  if (BINDABLE.has(type)) e.param = param;
  if (type === "frame" || type === "label") e.text = type === "frame" ? "SECTION" : "Label";
  if (type === "switch") e.positions = 2;
  if (type === "led") e.glyph = null;
  if (type === "knob") e.size = style === "p5" ? 36 : 32;
  if (type === "glyph") e.glyph = "wave";
  if (type === "band") e.band = "env";
  return e;
}
export const snap = (v) => Math.round(v / GRID) * GRID;
// copy and paste between screens: elements (copies) onto a screen at their own places, with fresh ids, in no lane; an
// element that would land exactly on an identical box moves 8 px right and down (again, until free). -> the new elements
export function pasteElements(screen, clip) {
  const made = [];
  let n = screen.elements.length + 1;
  for (const o of clip) {
    const e = JSON.parse(JSON.stringify(o));
    while (screen.elements.some((x) => x.id === `e${n}`)) n++;
    e.id = `e${n++}`;
    for (let k = 0; k < 30 && screen.elements.some((x) => x.x === e.x && x.y === e.y && x.w === e.w && x.h === e.h); k++) {
      e.x = Math.min(e.x + 8, SCREEN - e.w);
      e.y = Math.min(e.y + 8, SCREEN - e.h);
    }
    screen.elements.push(e);
    made.push(e);
  }
  return made;
}

// ---------------------------------------------------------- the design file ---
export const formatDesign = (d) => formatLayout(d);
export const designFileName = (d) => `${layoutFileName(d.engine.name).replace(/\.json$/, "")}-${layoutFileName(d.name || "design")}`;
// is a design well formed, and for this catalog? -> {errors: [..], warnings: [..]} (texts)
export function validateDesign(d, cat) {
  const errors = [], warnings = [];
  const err = (t) => errors.push(t), warn = (t) => warnings.push(t);
  if (!d || typeof d !== "object") return { errors: ["not a JSON object"], warnings };
  if (d.format !== FORMAT) err(`not a ${FORMAT} file`);
  if (d.version !== DESIGN_VERSION) err(`format version ${d.version}: a design is version ${DESIGN_VERSION}`);
  if (d.kind !== "design") err(`"kind" must be "design"`);
  if (!d.engine || d.engine.index !== cat.engine.index) err(`engine ${(d.engine || {}).name}: this catalog is ${cat.engine.name}`);
  if (d.params_hash !== cat.params_hash) err(`made for other parameters (params_hash ${d.params_hash}, the firmware's ${cat.params_hash})`);
  if (d.style !== undefined && !STYLES.includes(d.style)) err(`style "${d.style}" (${STYLES.join(", ")}; none = flat)`);
  if (!d.header || d.header.kind !== "title-line") err(`"header" must be {"kind": "title-line"}: the shared header is the editor's title line`);
  if (!Array.isArray(d.groups)) { err(`"groups" must be a list`); return { errors, warnings }; }
  const pidx = paramIndex(paramList(cat)), gnames = new Set(cat.groups.map((g) => g.name));
  const seenG = new Set();
  d.groups.forEach((g, gi) => {
    const where = `group ${gi + 1} (${g && g.name})`;
    if (!g || !gnames.has(g.name)) { err(`${where}: not a group of ${cat.engine.name} (${[...gnames].join(" ")})`); return; }
    if (seenG.has(g.name)) err(`${where}: the group twice`);
    seenG.add(g.name);
    if (!Array.isArray(g.screens)) { err(`${where}: "screens" must be a list`); return; }
    g.screens.forEach((s, si) => {
      const at = `${g.name} screen ${si + 1}`;
      if (!s || (s.type !== "classic" && s.type !== "free")) { err(`${at}: "type" must be "classic" or "free"`); return; }
      if (typeof s.title !== "string") err(`${at}: "title" must be a text`);
      if (JSON.stringify(s).includes('"key"')) err(`${at}: nothing may be bound to a key (rule 2: the keys always play)`);
      if (s.type === "classic") {
        if (!CLASSIC_KINDS.includes(s.kind)) err(`${at}: kind "${s.kind}" (${CLASSIC_KINDS.join(", ")})`);
        if (!BANDS.includes(s.band)) err(`${at}: band "${s.band}" (${BANDS.join(", ")})`);
        if (!Array.isArray(s.lanes) || !s.lanes.length || s.lanes.length > MAX_LANES) { err(`${at}: 1 .. ${MAX_LANES} lanes`); return; }
        s.lanes.forEach((l, li) => {
          if (!l || !Array.isArray(l.cells) || l.cells.length !== LANE_SLOTS) { err(`${at} lane ${li + 1}: four cells (KNOB 1..4; null = empty)`); return; }
          l.cells.forEach((c, ci) => {
            if (c === null) return;
            if (c.fixed) return;
            if (!pidx.has(c.param)) err(`${at} lane ${li + 1} knob ${ci + 1}: no parameter "${c.param}"`);
            if (c.label !== undefined && c.label !== null && typeof c.label !== "string") err(`${at} lane ${li + 1} knob ${ci + 1}: a label is a text`);
          });
        });
        return;
      }
      if (!Array.isArray(s.elements)) { err(`${at}: "elements" must be a list`); return; }
      if (!Array.isArray(s.lanes)) { err(`${at}: "lanes" must be a list`); return; }
      const ids = new Set();
      s.elements.forEach((e, ei) => {
        const et = `${at} element ${ei + 1} (${e && e.id})`;
        if (!e || typeof e.id !== "string" || !e.id) { err(`${et}: an "id" text`); return; }
        if (ids.has(e.id)) err(`${et}: the id twice`);
        ids.add(e.id);
        if (!ELEMENT_TYPES.includes(e.type)) { err(`${et}: type "${e.type}" (${ELEMENT_TYPES.join(", ")})`); return; }
        for (const k of ["x", "y", "w", "h"]) if (!Number.isInteger(e[k]) || e[k] < 0) err(`${et}: ${k} a whole number >= 0`);
        if (["x", "y", "w", "h"].some((k) => Number.isInteger(e[k]) && e[k] % GRID)) warn(`${et}: not on the ${GRID} px grid`);
        if (BINDABLE.has(e.type) && e.param !== null && e.param !== undefined && !pidx.has(e.param)) err(`${et}: no parameter "${e.param}"`);
        if (e.type === "switch" && ![2, 3].includes(e.positions)) err(`${et}: a switch has 2 or 3 positions`);
        if (e.type === "led" && e.labelAt !== undefined && e.labelAt !== "above" && e.labelAt !== "below") err(`${et}: "labelAt" is "above" or "below"`);
        if (e.type === "frame" && e.r !== undefined && !(Number.isInteger(e.r) && e.r >= 0 && e.r <= 12)) err(`${et}: a frame's corner radius "r" is 0 .. 12`);
        if (e.type === "led" && e.leds !== undefined && ![1, 2].includes(e.leds)) err(`${et}: "leds" is 1 or 2`);
        if (e.type === "led" && e.leds === 2) {
          const lp = pidx.get(e.param);
          if (lp && lp.max - lp.min !== 1) err(`${et}: two LEDs show a parameter of two values (${e.param}: ${lp.min} .. ${lp.max})`);
          if (e.glyph) err(`${et}: two LEDs have their names above them, no glyph`);
        }
        if (e.type === "led" && e.pair !== undefined) {
          if (e.pair !== "left" && e.pair !== "right") err(`${et}: "pair" is "left" or "right"`);
          else {
            const o = s.elements.find((x) => x && x.type === "led" && x.pair === (e.pair === "left" ? "right" : "left") &&
              x.y === e.y && x.h === e.h && (e.pair === "left" ? e.x + e.w === x.x : x.x + x.w === e.x));
            if (!o) warn(`${et}: a ${e.pair} half with no ${e.pair === "left" ? "right" : "left"} half touching it (same y and h): its cap stands alone`);
          }
        }
        if (e.type === "led" && e.glyph && !LED_GLYPHS[e.glyph]) err(`${et}: led glyph "${e.glyph}" (${Object.keys(LED_GLYPHS).join(", ")})`);
        if (e.type === "glyph" && !GLYPHS.includes(e.glyph)) err(`${et}: glyph "${e.glyph}"`);
        if (e.type === "band" && !BANDS.includes(e.band)) err(`${et}: band "${e.band}"`);
        if (e.colour !== undefined && e.colour !== "auto" && !(e.colour in COLOURS)) err(`${et}: colour "${e.colour}" is not a palette token`);
      });
      s.lanes.forEach((l, li) => {
        if (!Array.isArray(l)) { err(`${at} lane ${li + 1}: a list of element ids`); return; }
        for (const id of l) if (id !== null && !ids.has(id)) err(`${at} lane ${li + 1}: no element "${id}"`);
      });
    });
  });
  return { errors, warnings };
}

// ------------------------------------------------------ classic: lay_load ---
// the classic screens of a group, with their index among them (lay_show's screen)
const classicOf = (g) => g.screens.map((s, i) => ({ s, i })).filter((x) => x.s.type === "classic");
const sameCells = (a, b) => JSON.stringify(a.map((c) => (c && (c.param || c.text)) || null)) ===
                            JSON.stringify(b.map((c) => (c && (c.param || c.text)) || null)) && a.every((c) => !c || !c.label);
// Pack a design's classic screens for lay_load. Two ways the firmware's own hook can show them:
//   "in-place": every group's classic screens have today's structure (the same number, kinds and lanes): the engine's
//       page tables are kept, with the design's parameter in each column and its labels (moves, swaps, relabels);
//   "plan": the structure changed (screens added, removed, reordered, moved between groups): OSC .. MOD become a
//       screen plan (core.h eng_screen_t: edit8, one or two lanes a screen, a band, a title), one page per lane.
// What neither can express is listed in `unbuildable` and shown as today's ("not buildable yet").
// -> {bytes, mode, show: Map("gi.si" -> {group, screen}), unbuildable: [{gi, si, why}], notes: [..]}
export function packDesign(d, cat) {
  const params = paramIndex(paramList(cat));
  const today = new Map(cat.groups.map((g) => [g.name, g]));
  const unbuildable = [], notes = [], show = new Map();
  const groups = d.groups.map((g, gi) => ({ g, gi, t: today.get(g.name) })).filter((x) => x.t);
  // in place?
  let inPlace = true;
  for (const { g, t } of groups) {
    const cl = classicOf(g);
    if (!cl.length) continue;
    if (cl.length !== t.screens.length) { inPlace = false; break; }
    cl.forEach(({ s }, k) => {
      const ts = t.screens[k];
      if (s.kind !== ts.kind || s.lanes.length !== ts.lanes.length) inPlace = false;
    });
  }
  const colOf = (c) => {
    if (!c || c.fixed) return null;
    const p = params.get(c.param);
    return p && !p.part ? { src: [p.page, p.col], label: c.label || "" } : null;
  };
  let pages = [], screens = [], sec = [];
  const mode = inPlace ? "in-place" : "plan";
  if (inPlace) {
    pages = cat.pages.map((p) => ({ title: p.title, cols: p.cols.map((c, i) => (c ? { src: [p.page, i], label: "" } : null)) }));
    for (const { g, gi, t } of groups) {
      classicOf(g).forEach(({ s, i }, k) => {
        const ts = t.screens[k];
        show.set(`${gi}.${i}`, { group: t.group, screen: k });
        const deepLanes = ts.type === "edit" || ts.type === "page" || ts.type === "stack";
        if (!deepLanes) {
          if (!s.lanes.every((l, li) => sameCells(l.cells, designFromCatalog(cat).groups.find((x) => x.name === g.name).screens[k].lanes[li].cells)))
            unbuildable.push({ gi, si: i, why: `today's ${ts.type} screen: its cells cannot be moved by a design yet` });
          return;
        }
        s.lanes.forEach((l, li) => {
          const pg = ts.pages[li];
          l.cells.forEach((c, ci) => {
            if (c && c.param && params.get(c.param) && params.get(c.param).part)
              unbuildable.push({ gi, si: i, why: `${c.param}: a part parameter on an engine page` });
            pages[pg].cols[ci] = colOf(c);
          });
        });
        if (s.band !== ts.band) notes.push(`${g.name} ${i + 1}: the band follows the page titles in place ("${ts.band}"); "${s.band}" needs the plan`);
      });
    }
    for (const t of cat.groups.filter((x) => x.group <= 4)) {
      const ps = t.screens.flatMap((s) => s.pages);
      if (ps.length) sec[t.group] = Math.min(...ps);
    }
  } else {
    for (const gno of [0, 1, 2, 3, 4]) {
      const t = cat.groups.find((x) => x.group === gno);
      if (!t || !t.screens.some((s) => s.pages.length)) continue;
      const dg = groups.find((x) => x.t.group === gno);
      sec[gno] = pages.length;
      const cl = dg ? classicOf(dg.g) : [];
      if (!cl.length) pages.push({ title: t.name, cols: [null, null, null, null] });   // (a group of free screens only)
      cl.forEach(({ s, i }, k) => {
        show.set(`${dg.gi}.${i}`, { group: gno, screen: k });
        if (s.kind !== "edit8" || s.lanes.length > 2)
          unbuildable.push({ gi: dg.gi, si: i, why: `a ${s.kind} of ${s.lanes.length} lanes in a changed group: the screen plan has edit8 screens of one or two lanes` });
        const lanes = s.lanes.slice(0, 2), pg = [];
        for (const l of lanes) {
          pg.push(pages.length);
          pages.push({ title: (l.title || s.title || t.name).slice(0, 15), cols: l.cells.map(colOf) });
        }
        if (!(s.band in PLAN_BAND)) notes.push(`${dg.g.name} ${i + 1}: band "${s.band}" is not a plan band`);
        screens.push({ sec: gno, pg: [pg[0], pg.length > 1 ? pg[1] : 255], band: PLAN_BAND[s.band] || 0, title: (s.title || "").slice(0, 15) });
      });
    }
    for (const { g, gi, t } of groups)
      if (t.group > 4)
        classicOf(g).forEach(({ s, i }, k) => {
          show.set(`${gi}.${i}`, { group: t.group, screen: k });
          const ts = designFromCatalog(cat).groups.find((x) => x.name === g.name).screens[k];
          if (!ts || !s.lanes.every((l, li) => ts.lanes[li] && sameCells(l.cells, ts.lanes[li].cells)))
            unbuildable.push({ gi, si: i, why: `today's platform screen: its cells cannot be moved by a design yet` });
        });
  }
  // the bytes ('L' 2 engine npages pages.. section[8] nscreens screens..)
  const b = [76, 2, cat.engine.index, pages.length];
  const str = (s) => { const u = toLatin1((s || "").slice(0, 15)); b.push(u.length, ...u); };
  for (const p of pages) {
    str(p.title);
    for (let c = 0; c < 4; c++) {
      const x = p.cols[c];
      if (x) { b.push(x.src[0], x.src[1]); str(x.label); } else { b.push(255, 0); str(""); }
    }
  }
  const secs = [0, 1, 2, 3, 4].map((g) => sec[g]).filter((v) => v !== undefined);
  for (let i = 0; i < 8; i++) b.push(i < secs.length ? secs[i] : 255);
  b.push(screens.length);
  for (const s of screens) { b.push(s.sec, s.pg[0], s.pg[1], s.band); str(s.title); }
  return { bytes: Uint8Array.from(b), mode, show, unbuildable, notes, pages: pages.length, screens: screens.length };
}

// --------------------------------------------------------- free: drawing ---
// where each element is on the knobs: id -> {lane, slot}; and the lane problems
export function laneMap(screen) {
  const at = new Map(), twice = [];
  (screen.lanes || []).forEach((l, li) => l.forEach((id, k) => {
    if (id === null) return;
    if (at.has(id)) twice.push(id); else at.set(id, { lane: li, slot: k });
  }));
  return { at, twice };
}
const pctOf = (p, v) => (p && p.max > p.min ? Math.max(0, Math.min(255, Math.round((v - p.min) * 255 / (p.max - p.min)))) : 0);
// the free screen as draw calls (the module's lay_draw_*). value(param) -> {v, text}; opts: {lane (the active one),
// hot (an element id: its value on a block), wv: {band: [20]}, measure(text, px, bold) -> px, style ("p5": p5Op)}
export function freeOps(screen, params, value, opts = {}) {
  const ops = [], { at } = laneMap(screen), active = opts.lane || 0;
  const measure = opts.measure || ((t, px) => Math.ceil(t.length * px * 0.62));
  const colour = (e) => {
    const a = at.get(e.id);
    if (e.colour && e.colour !== "auto") return COLOURS[e.colour];
    return a && a.lane === active ? KNOB_COL[a.slot] : a ? DIM : TEXT;
  };
  const ordered = [...screen.elements].sort((a, b) => (a.type === "frame" ? 0 : a.type === "band" ? 1 : 2) - (b.type === "frame" ? 0 : b.type === "band" ? 1 : 2));
  for (const e of ordered) {
    const p = e.param ? params.get(e.param) : null, val = p ? value(p) : { v: 0, text: "" };
    const col = colour(e), lab = e.label !== undefined && e.label !== null ? e.label : p ? p.label : "";
    const cx = e.x + (e.w >> 1), hot = opts.hot === e.id;
    if (opts.style === "p5" && p5Op(ops, e, { p, val, col, lab, cx, hot, measure, lit: at.get(e.id) && at.get(e.id).lane === active })) continue;
    switch (e.type) {
      case "frame": {
        ops.push({ op: "rect", x: e.x, y: e.y, w: e.w, h: e.h, col: e.colour && e.colour !== "auto" ? COLOURS[e.colour] : LINE, r: e.r || 0, line: 1 });
        if (e.text) {
          const tw = measure(e.text, 10, true);
          ops.push({ op: "rect", x: e.x + 4, y: e.y - 5, w: tw + 6, h: 10, col: BG });
          ops.push({ op: "text", bold: 1, px: 10, x: e.x + 7, y: e.y + 4, s: e.text, col: MID, maxw: e.w - 12 });
        }
        break;
      }
      case "label": {
        const align = e.align === "centre" ? 1 : e.align === "right" ? 2 : 0;
        const x = align === 1 ? cx : align === 2 ? e.x + e.w : e.x;
        // (p5: the baseline is the box's bottom edge, so labels on the 4 px grid line up with the knobs' and leds')
        ops.push({ op: "text", bold: e.bold === false ? 0 : 1, px: e.size || 11, x, y: e.y + e.h - (opts.style === "p5" ? 0 : 2), s: e.text || "",
                   col: e.colour && e.colour !== "auto" ? COLOURS[e.colour] : TEXT, align, maxw: e.w });
        break;
      }
      case "knob": {
        const size = e.size || 32;
        ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + 10, s: lab, col, align: 1, maxw: e.w - 2 });
        ops.push({ op: "knob", cx, cy: e.y + 13 + (size >> 1), size, col, pct: pctOf(p, val.v), bip: p && p.min < 0 && p.max > 0 });
        if (hot) ops.push({ op: "rect", x: e.x + 3, y: e.y + e.h - 17, w: e.w - 6, h: 17, col, r: 2 });
        ops.push({ op: "text", bold: 1, px: 12, x: cx, y: e.y + e.h - 4, s: val.text, col: hot ? BG : col, align: 1, maxw: e.w - 4 });
        break;
      }
      case "switch": {
        const n = e.positions === 3 ? 3 : 2, cur = p && p.max > p.min ? Math.round((val.v - p.min) * (n - 1) / (p.max - p.min)) : 0;
        ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + 10, s: lab, col, align: 1, maxw: e.w - 2 });
        for (let i = 0; i < n; i++) {
          const y = e.y + 16 + i * 14, on = i === cur;
          ops.push({ op: "rect", x: e.x + 4, y, w: 8, h: 8, col: on ? col : DIM, line: on ? 0 : 1 });
          ops.push({ op: "text", bold: 1, px: 10, x: e.x + 16, y: y + 8, s: switchName(e, p, n, i), col: on ? col : DIM, maxw: e.w - 18 });
        }
        break;
      }
      case "led": {
        const on = p ? val.v > p.min : false, lit = at.get(e.id) && at.get(e.id).lane === active ? col : on ? MID : DIM;
        ops.push({ op: "rect", x: e.x, y: e.y + ((e.h - 8) >> 1), w: 8, h: 8, col: on ? lit : DIM, line: on ? 0 : 1 });
        if (e.glyph && LED_GLYPHS[e.glyph]) {
          const [g, wave] = LED_GLYPHS[e.glyph];
          ops.push({ op: "glyph", g, x: e.x + 12, y: e.y, w: Math.min(e.w - 12, 2 * e.h), h: e.h, col: on ? lit : DIM, pct: g === 22 ? 0 : 128, pct2: g === 22 ? wave : 0 });
          if (e.label) ops.push({ op: "text", bold: 1, px: 10, x: e.x + 14 + Math.min(e.w - 12, 2 * e.h), y: e.y + e.h - 2, s: e.label, col: on ? lit : DIM, maxw: e.w - 16 - Math.min(e.w - 12, 2 * e.h) });
        } else {
          ops.push({ op: "text", bold: 1, px: 10, x: e.x + 12, y: e.y + e.h - 2, s: lab, col: on ? lit : DIM, maxw: e.w - 12 });
        }
        break;
      }
      case "bar": {
        ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + 10, s: lab, col, align: 1, maxw: e.w - 2 });
        ops.push({ op: "hbar", x: e.x + 4, y: e.y + 14, w: e.w - 8, h: 4, col, pct: pctOf(p, val.v), bip: p && p.min < 0 && p.max > 0 });
        if (hot) ops.push({ op: "rect", x: e.x + 3, y: e.y + e.h - 12, w: e.w - 6, h: 12, col, r: 2 });
        ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + e.h - 2, s: val.text, col: hot ? BG : col, align: 1, maxw: e.w - 4 });
        break;
      }
      case "glyph":
        ops.push({ op: "glyph", g: Math.max(0, GLYPHS.indexOf(e.glyph)), x: e.x, y: e.y, w: e.w, h: e.h, col, pct: p ? pctOf(p, val.v) : e.pct ?? 128 });
        break;
      case "band":
        ops.push({ op: "band", kind: Math.max(0, BANDS.indexOf(e.band)), wv: (opts.wv && opts.wv[e.band]) || [] });
        break;
    }
  }
  return ops;
}

// The p5 style's one baseline rule: a text under a graphic (a knob's dial, a led's cap) sits P5_UNDER px under its
// bottom edge, and a label element's baseline is its box's bottom edge; so boxes on the 4 px grid put every such
// baseline on the grid too (a knob at y 44 of size 36, a led at y 60 with its label below, a label box ending at 92:
// one row)
export const P5_UNDER = 12;
// The p5 style: the Prophet-5's panel drawn by the same primitives (the tool's proposal, as freeOps). A frame is a
// rounded section box, its name in capitals in the top border; a knob a black face with a white pointer inside a printed
// 0..10 scale, its name under it, its value inside the face (or, a name too wide, on the row under the label); an led
// the panel's square button with its red LED, the waveform or name above; a switch a lever in a slot, the positions'
// names beside it. The rest as flat. -> true when it drew e
function p5Op(ops, e, { p, val, col, lab, cx, hot, measure, lit }) {
  switch (e.type) {
    case "frame": {
      ops.push({ op: "rect", x: e.x, y: e.y, w: e.w, h: e.h, col: e.colour && e.colour !== "auto" ? COLOURS[e.colour] : LINE, r: e.r ?? 3, line: 1 });
      if (e.text) {
        const t = e.text.toUpperCase(), tw = Math.min(measure(t, 10, true), e.w - 12);
        ops.push({ op: "rect", x: cx - (tw >> 1) - 3, y: e.y - 5, w: tw + 6, h: 10, col: BG });
        ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + 4, s: t, col: MID, align: 1, maxw: e.w - 12 });
      }
      return true;
    }
    case "knob": {
      const size = knobSize(e, "p5"), r = (size >> 1) - 6, cy = e.y + (size >> 1), vt = val.text || "";
      // inside when it fits (a parameter of value names: when the widest does, so the value never jumps rows)
      const inside = Math.max(...[vt, ...((p && p.names) || [])].map((t) => measure(String(t), 9, true))) <= 2 * r - 4;
      ops.push({ op: "p5knob", cx, cy, size, face: hot ? col : SURF, rim: MID, tick: LINE, ptr: hot ? BG : col,
                 pct: pctOf(p, val.v), bip: p && p.min < 0 && p.max > 0, from: inside ? 70 : 30 });   // (a short pointer clears the value)
      if (inside) ops.push({ op: "text", bold: 1, px: 9, x: cx, y: cy + 3, s: vt, col: hot ? BG : col, align: 1 });
      ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + size + P5_UNDER, s: lab, col, align: 1, maxw: e.w - 2 });
      if (!inside) {
        if (hot) ops.push({ op: "rect", x: e.x + 3, y: e.y + size + 15, w: e.w - 6, h: 12, col, r: 2 });
        ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + size + 24, s: vt, col: hot ? BG : col, align: 1, maxw: e.w - 4 });
      }
      return true;
    }
    case "led": {
      // a column: the rows over the cap (the icon, the two LEDs' names, the label when it is above: 12 px each), the cap
      // (2 px under them, or at the cell's top + 2 with none), the label under it when labelAt is "below" (a two-LED
      // button's label goes under by default: its names are on top)
      const on = p ? val.v > p.min : false, ink = lit ? col : on ? MID : DIM, two = e.leds === 2;
      const icon = !two && e.glyph && LED_GLYPHS[e.glyph], below = e.labelAt ? e.labelAt === "below" : two;
      const labTop = !!lab && !below && !icon;   // (above, an icon takes the label's place)
      const rows = (labTop ? 1 : 0) + (icon || two ? 1 : 0), y = rows ? e.y + 12 * rows : e.y + 2;
      // the cap, 18 high: a black window (the LEDs') over a 1 px line, the pad under it, the rim; sq: the corners on one
      // side square (lay_draw_rect r + 64 left, + 128 right: a pair's caps meet as one)
      const cap = (x, w, sq = 0) => {
        ops.push({ op: "rect", x, y, w, h: 18, col: SURF, r: 2 + sq });
        ops.push({ op: "rect", x: x + 1, y: y + 1, w: w - 2, h: 8, col: COLOURS.black });
        ops.push({ op: "rect", x: x + 1, y: y + 9, w: w - 2, h: 1, col: LINE });
        ops.push({ op: "rect", x, y, w, h: 18, col: MID, r: 2 + sq, line: 1 });
      };
      const led = (x, lit5, c) => ops.push({ op: "disc", cx16: x * 16, cy16: (y + 5) * 16, r16: lit5 ? 40 : 24, col: lit5 ? c : DIM });   // lit 5 px, off 3 px
      // one LED; a pair's halves sit flush (the left's cap at its cell's right edge, the right's over the shared rim)
      const cxL = two ? cx : e.pair === "left" ? e.x + e.w - 9 : e.pair === "right" ? e.x + 8 : cx;
      const labCol = two ? col : ink;
      if (labTop) ops.push({ op: "text", bold: 1, px: 10, x: cxL, y: e.y + 9, s: lab, col: labCol, align: 1, maxw: e.w - 2 });
      const top = e.y + (labTop ? 12 : 0);
      if (two) {   // one parameter of two values: a wide cap, two LEDs (min red, max yellow), the names above them
        const cur = on ? 1 : 0;
        cap(cx - 15, 30);
        [cx - 7, cx + 7].forEach((x, i) => {
          ops.push({ op: "text", bold: 1, px: 9, x, y: top + 9, s: switchName(e, p, 2, i).toUpperCase(), col: i === cur ? (lit ? col : MID) : DIM, align: 1 });
          led(x, i === cur, i ? COLOURS.yellow : RED);
        });
      } else {
        if (icon) ops.push({ op: "wave", kind: P5_WAVE[e.glyph], x: cxL - 8, y: top, w: 16, h: 10, col: ink });
        cap(cxL - 9, 18, e.pair === "left" ? 128 : e.pair === "right" ? 64 : 0);
        led(cxL, on, RED);
      }
      if (below && lab && (two || !icon || e.label))
        ops.push({ op: "text", bold: 1, px: 10, x: cxL, y: y + 18 + P5_UNDER, s: lab, col: labCol, align: 1, maxw: e.w - 2 });
      return true;
    }
    case "switch": {
      const n = e.positions === 3 ? 3 : 2, cur = p && p.max > p.min ? Math.round((val.v - p.min) * (n - 1) / (p.max - p.min)) : 0;
      const sx = e.x + 4, sy = e.y + 16;
      ops.push({ op: "text", bold: 1, px: 10, x: cx, y: e.y + 10, s: lab, col, align: 1, maxw: e.w - 2 });
      ops.push({ op: "rect", x: sx, y: sy, w: 8, h: 12 * n, col: LINE, r: 2, line: 1 });
      ops.push({ op: "rect", x: sx + 1, y: sy + 12 * cur + 2, w: 6, h: 8, col });
      for (let i = 0; i < n; i++)
        ops.push({ op: "text", bold: 1, px: 10, x: sx + 13, y: sy + 12 * i + 10, s: switchName(e, p, n, i), col: i === cur ? col : DIM, maxw: e.w - 18 });
      return true;
    }
  }
  return false;
}
// a switch's position i of n: the element's names, else the parameter's value names, else Off / On (Lo Mid Hi)
function switchName(e, p, n, i) {
  const v = p ? p.min + Math.round(i * (p.max - p.min) / (n - 1)) : i;
  return String((e.names && e.names[i]) || (p && p.names && p.names[v - (p.fmt === "enum" ? p.min : 0)]) || (n === 2 ? ["Off", "On"][i] : ["Lo", "Mid", "Hi"][i]));
}

// ---------------------------------------------------------- design checks ---
const boxOf = (e) => [e.x, e.y, e.x + e.w, e.y + e.h];
const meet = (a, b) => a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
// the design's own checks of a screen (beside the firmware's text lint): [{kind, text, ids?}]
// opts.measure(text, px, bold): the header's right text width (the module's lay_text_w)
export function checkScreen(screen, params, opts = {}) {
  const f = [], add = (kind, text, ids) => f.push({ kind, text, ids });
  if (opts.measure && opts.measure(screen.title || "", 11, true) > HEADER_RIGHT_MAX)
    add("title", `the title "${screen.title}" is wider than the header allows (${HEADER_RIGHT_MAX} px)`);
  if (screen.type === "classic") {
    if ((screen.title || "").length > 15) add("title", `the title "${screen.title}" is longer than 15 characters`);
    const seen = new Map();
    screen.lanes.forEach((l, li) => l.cells.forEach((c) => {
      if (c && c.param) { if (seen.has(c.param)) add("twice", `${c.param} is on this screen twice`); seen.set(c.param, li); }
    }));
    if (screen.lanes.length > MAX_LANES) add("budget", `${screen.lanes.length} lanes: at most ${MAX_LANES}`);
    if (screen.kind === "edit8" && screen.lanes.length > 2) add("budget", `an edit8 screen shows two lanes`);
    return f;
  }
  const els = screen.elements, { at, twice } = laneMap(screen);
  if (els.length > MAX_ELEMENTS) add("budget", `${els.length} elements: at most ${MAX_ELEMENTS}`);
  if (screen.lanes.length > MAX_LANES) add("budget", `${screen.lanes.length} lanes: at most ${MAX_LANES}`);
  screen.lanes.forEach((l, li) => { if (l.filter((x) => x !== null).length > LANE_SLOTS) add("lane", `lane ${li + 1} has ${l.length} elements: four knobs`); });
  for (const id of new Set(twice)) add("lane", `${id} is in two lanes`, [id]);
  const used = new Map();
  for (const e of els) {
    if (BINDABLE.has(e.type) && e.param) {
      if (used.has(e.param)) add("twice", `${e.param} is bound twice (${used.get(e.param)}, ${e.id})`, [used.get(e.param), e.id]);
      used.set(e.param, e.id);
      if (!at.has(e.id)) add("lane", `${e.id} (${e.param}) is in no lane: no knob reaches it`, [e.id]);
      if (!params.has(e.param)) add("param", `${e.id}: no parameter ${e.param}`, [e.id]);
    }
    if (BINDABLE.has(e.type) && !e.param && e.type !== "glyph") add("param", `${e.id}: a ${e.type} bound to nothing`, [e.id]);
    if (e.y < HEADER_H || e.x < 0 || e.x + e.w > SCREEN || e.y + e.h > SCREEN) add("area", `${e.id} is outside the design area (under the header, rows ${HEADER_H} .. 239)`, [e.id]);
  }
  for (let i = 0; i < els.length; i++)
    for (let j = i + 1; j < els.length; j++) {
      const a = els[i], b = els[j];
      if (a.type === "frame" || b.type === "frame") continue;
      if (meet(boxOf(a), boxOf(b))) add("overlap", `${a.id} and ${b.id} overlap`, [a.id, b.id]);
    }
  return f;
}
// a parameter used on more than one screen of the design: [{param, where: [..]}]
export function usedTwice(d) {
  const at = new Map();
  d.groups.forEach((g) => g.screens.forEach((s, si) => {
    const ps = s.type === "classic" ? s.lanes.flatMap((l) => l.cells.filter((c) => c && c.param).map((c) => c.param))
                                    : s.elements.filter((e) => e.param).map((e) => e.param);
    for (const p of new Set(ps)) { if (!at.has(p)) at.set(p, []); at.get(p).push(`${g.name} ${si + 1}`); }
  }));
  return [...at].filter(([, w]) => w.length > 1).map(([param, where]) => ({ param, where }));
}
// every screen x lane of a design, in order: [{gi, si, lane, lanes, type, group, title, key}]
export function designTiles(d) {
  const out = [];
  d.groups.forEach((g, gi) => g.screens.forEach((s, si) => {
    const n = Math.max(1, s.lanes.length);
    for (let l = 0; l < n; l++)
      out.push({ gi, si, lane: l, lanes: n, type: s.type, group: g.name, title: s.title, key: `${gi}.${si}.${l}` });
  }));
  return out;
}

// ------------------------------------------------------------ a session ---
// The designer's live preview of one design on the module: classic screens through lay_load + the real editor (the
// knobs and SELECT are the unit's), free screens through the draw API (the knobs turn the active lane's parameters by
// the editor's own steps, SELECT moves the lane and runs on across the group's screens as the editor's does).
//   const s = designSession(m, cat, design, preset); s.open(gi, si, lane); s.render() -> {rgba, lint, checks, ..}
export function designSession(m, cat, design, preset = 1) {
  const params = paramIndex(paramList(cat)), E = cat.engine.index;
  let pack = null, at = { gi: 0, si: 0, lane: 0 }, hot = null, wv = {};
  const scr = () => design.groups[at.gi].screens[at.si];
  const self = {
    get at() { return { ...at }; },
    get pack() { return pack; },
    get hot() { return hot; },
    // the design changed: pack and load its classic pages again (the place is kept)
    reload() {
      pack = packDesign(design, cat);
      const r = m.load(pack.bytes);
      if (!r.ok) throw new Error(`lay_load: ${r.error}`);
      return self.open(at.gi, at.si, at.lane);
    },
    // a classic screen the firmware cannot show as designed: why (today's is shown), else null
    unbuildable(gi = at.gi, si = at.si) { const u = pack && pack.unbuildable.find((x) => x.gi === gi && x.si === si); return u ? u.why : null; },
    open(gi, si, lane = 0) {
      if (!pack) { pack = packDesign(design, cat); m.load(pack.bytes); }
      at = { gi, si, lane };
      hot = null;
      const s = scr();
      if (!s) return self;
      at.lane = Math.max(0, Math.min(lane, Math.max(1, s.lanes.length) - 1));
      if (s.type === "classic") {
        const p = pack.show.get(`${gi}.${si}`);
        m.show(E, preset, p ? p.group : design.groups[gi].group, p ? p.screen : 0, Math.min(at.lane, s.lanes.length - 1));
      } else {
        wv = {};
        for (const b of new Set(s.elements.filter((e) => e.type === "band").map((e) => e.band))) {
          const t = tiles(cat).find((x) => x.band === b);
          if (t) { m.show(E, preset, t.group, t.screen, 0); wv[b] = m.wv(); }
        }
        if (!Object.keys(wv).length) m.show(E, preset, 0, 0, 0);
      }
      return self;
    },
    // KNOB k (1..4) n detents
    knob(k, n) {
      const s = scr();
      if (s.type === "classic") { m.knob(k, n); return; }
      const id = (s.lanes[at.lane] || [])[k - 1], e = id && s.elements.find((x) => x.id === id), p = e && e.param && params.get(e.param);
      if (!p) return;
      m.pstep(p.page, p.col, n);
      hot = e.id;
    },
    // the 800 ms of a hot cell's mark have passed
    cool() { hot = null; m.step(800); },
    // SELECT n detents: the lane, on across the group's screens
    select(n) {
      const s = scr();
      if (s.type === "classic" && !n) return;
      let { gi, si, lane } = at;
      const g = design.groups[gi];
      for (let i = 0; i < Math.abs(n); i++) {
        const d = Math.sign(n), cur = g.screens[si], nl = Math.max(1, cur.lanes.length);
        if (lane + d >= 0 && lane + d < nl) lane += d;
        else if (si + d >= 0 && si + d < g.screens.length) { si += d; lane = d > 0 ? 0 : Math.max(1, g.screens[si].lanes.length) - 1; }
      }
      if (s.type === "classic" && si === at.si) { m.select(lane - at.lane); at.lane = lane; return; }
      self.open(gi, si, lane);
    },
    // the screen now: {rgba, lint (the firmware's text lint), checks (the design's), free, unbuildable, state}
    render() {
      const s = scr();
      if (s.type === "classic") {
        const rgba = m.render(), st = m.state();
        if (st.lane !== at.lane) at.lane = st.lane;
        return { rgba, lint: m.lint(), checks: checkScreen(s, params, { measure: (t, px, b) => m.textW(t, px, b) }), free: false,
                 unbuildable: self.unbuildable(), state: st };
      }
      const st = m.state();
      const value = (p) => ({ v: m.pget(p.page, p.col), text: m.ptext(p.page, p.col) });
      const ops = freeOps(s, params, value, { lane: at.lane, hot, wv, measure: (t, px, b) => m.textW(t, px, b), style: design.style });
      const rgba = m.draw(st.sound, s.title, ops);
      return { rgba, lint: m.lint(), checks: checkScreen(s, params, { measure: (t, px, b) => m.textW(t, px, b) }), free: true,
               unbuildable: "a free screen: the firmware has no free pages yet", state: st, ops };
    },
    // the values of the four knobs of the active lane: [{label, value} | null]
    lane() {
      const s = scr();
      if (s.type === "classic") return m.state().cells.map((c) => (c.on ? { label: c.label, value: c.value } : null));
      return [0, 1, 2, 3].map((k) => {
        const id = (s.lanes[at.lane] || [])[k], e = id && s.elements.find((x) => x.id === id), p = e && e.param && params.get(e.param);
        return p ? { label: e.label || p.label, value: m.ptext(p.page, p.col) } : null;
      });
    },
  };
  return self;
}
