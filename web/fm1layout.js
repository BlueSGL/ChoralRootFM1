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
    load(bytes) {   // reserved for step 3
      const b = bytes || new Uint8Array(0), n = Math.min(b.length, ex.lay_buf_size());
      mem().set(b.subarray(0, n), ex.lay_buf());
      return json(ex.lay_load(ex.lay_buf(), n));
    },
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
// a catalog can say. Today only rule 1 can be told from the data: no screen has the shared header yet.
export function ruleNotes(catalog, tile) {
  const notes = [];
  const headerless = tile ? !catalog.header : (catalog.groups || []).some((g) => g.screens.some((s) => !s.header));
  if (headerless)
    notes.push({ rule: 1, text: tile ? "No shared header on this page (rule 1): today's editor draws none." :
                 "No screen has the shared header (rule 1): today's editor draws none." });
  return notes;
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
