// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Elektron Digitone sound files (.dnsnd) read for FM TONE (engine 15, eng_quad.c), docs/SOUNDS.md ".dnsnd import".
// Clean-room: the container was decoded from files, no Elektron code or data is used. Pure: no DOM; the files are read
// on this computer, nothing is uploaded.
//
// A .dnsnd is a ZIP of manifest.json ({FormatVersion, ProductType, Payload, FileType "Sound", FirmwareVersion,
// MetaInfo.Tags}) and the payload named by Payload: a 0x23-byte header (magic ac 11 d3 03, ASCII "0097" at 9, the
// decompressed size u16le at 0x1c = 302), an LZ4 block, a 14-byte trailer (.. end magic aa a1 da aa). The 302-byte
// record: be ef ba ce, u32be version 1, u32 tag bits, the name (16 bytes, NUL padded) at 12, then the parameters as
// byte pairs from 30 (the coarse value, the fine byte on the hi-res ones).
//
//   parseDnsnd(bytes)   -> Promise<{name, tags, firmware, product, version, params, confidence, warnings, record}>
//   dnsndToQuad(parsed) -> {blob: Uint8Array(80) FM TONE version 3, pan, notes: [what was lost / approximated]}

// ---- FM TONE's patch (eng_quad.c QP_*: [min, max, def] of each value, `sound_templates --quad-ranges` prints it)
export const DN_QUAD_RANGES = [[1,8,1],[0,18,3],[0,63,3],[0,18,3],[-26,26,0],[0,127,0],[0,120,0],[-64,63,0],[-100,100,0],[-100,100,0],[-100,100,0],[-100,100,0],[0,127,0],[0,127,60],[0,127,64],[0,127,48],[0,127,0],[0,127,60],[0,127,0],[0,127,0],[0,127,0],[0,1,1],[0,1,1],[0,4,1],[0,127,0],[0,1,1],[0,1,1],[0,127,64],[0,127,0],[0,127,0],[0,127,0],[0,127,64],[0,127,0],[0,127,40],[0,127,127],[0,127,0],[0,3,1],[-64,63,0],[0,127,0],[0,127,0],[0,127,0],[0,127,127],[0,127,0],[0,127,64],[0,127,127],[0,127,40],[0,127,100],[-64,63,16],[0,23,3],[-64,63,0],[0,39,0],[0,6,0],[0,127,0],[0,4,0],[-64,63,0],[-64,63,16],[0,23,3],[-64,63,0],[0,39,0],[0,6,0],[0,127,0],[0,4,0],[-64,63,0],[-64,63,16],[0,23,3],[-64,63,0],[0,39,0],[0,6,0],[0,127,0],[0,4,0],[-64,63,0],[0,18,3],[0,127,0]];
export const DN_QP = { ALGO: 0, RC: 1, RA: 2, RB1: 3, HARM: 4, DTUN: 5, FDBK: 6, MIX: 7, AATK: 12, ADEC: 13, AEND: 14, ALEV: 15,
  BATK: 16, BDEC: 17, BEND: 18, BLEV: 19, ADLY: 20, ATRIG: 21, ARST: 22, BDLY: 24, BTRIG: 25, BRST: 26,
  FATK: 30, FDEC: 31, FSUS: 32, FREL: 33, FREQ: 34, RESO: 35, FTYPE: 36, FDEPTH: 37, BASE: 40, WIDTH: 41,
  EATK: 42, EDEC: 43, ESUS: 44, EREL: 45, LEVEL: 46, RB2: 71 };
const DN_QL = { SPEED: 0, MULT: 1, FADE: 2, DEST: 3, WAVE: 4, PHASE: 5, TRIG: 6, DEPTH: 7 };
const dnQlfo = (k, f) => 47 + k * 8 + DN_QL[f];
export const DN_QUAD_NP = 73, DN_QUAD_BLOB = 80;
const DN_QF = { OFF: 0, LP12: 1, HP12: 2, LP24: 3 };

// ---- the Digitone's tables (from its manual; the step tables marked as assumed are not confirmed by an export)
const DN_RATIO_C = [0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];   // = FM TONE's (QUAD_RCB)
// RATIO A, 36 steps (assumed: 0.25 .. 2 in quarters, then 2.5 .. 16 in halves; index 3 = 1.00 holds)
export const DN_RATIO_A = [...Array.from({ length: 8 }, (_, i) => (i + 1) / 4), ...Array.from({ length: 28 }, (_, i) => 2.5 + i / 2)];
const DN_WAVE = ["TRI", "SINE", "SQR", "SAW", "EXP", "RMP", "RND"];
const DN_WAVE_TO_QUAD = [0, 1, 2, 3, 5, 4, 6];                    // FM TONE: TRI SINE SQR SAW RAMP EXP RAND
const DN_FTYPE = ["LP4", "LP2", "BP", "HP2", "HP4"];           // (order inferred: 1 = LP2 is the commonest)
const DN_FTYPE_TO_QUAD = [DN_QF.LP24, DN_QF.LP12, DN_QF.LP12, DN_QF.HP12, DN_QF.HP12];
// the Digitone's LFO destination numbers -> FM TONE's (eng_quad.c QD_*): none confirmed yet, so only 0 (none); add
// a pair here once a controlled export shows a number (docs/SOUNDS.md "Refining the table")
export const DN_LFO_DEST = { 0: 0 };

// ---- the field table: [name, offset, hi-res, confidence, decode -> the Digitone's value, apply(value, out)]
// confidence: verified (the decoding proves it), inferred (corpus ranges / means), guess, unknown (not used)
const dnLo = (d, o) => d[o], dnHi = (d, o) => d[o] + d[o + 1] / 256;
const dnR = Math.round;
const dnTo = (qp, f = (v) => dnR(v)) => (v, out) => out.set(qp, f(v));
const dnLfo = (k) => [
  [`lfo${k + 1}.speed`, 30 + 2 * k, 1, "inferred", (d, o) => dnHi(d, o) - 64, dnTo(dnQlfo(k, "SPEED"))],
  [`lfo${k + 1}.mult`, 34 + 2 * k, 0, "inferred", dnLo, dnTo(dnQlfo(k, "MULT"))],        // 0..11 synced, 12..23 fixed: = FM TONE's
  [`lfo${k + 1}.fade`, 38 + 2 * k, 0, "guess", (d, o) => d[o] - 64, dnTo(dnQlfo(k, "FADE"))],
  [`lfo${k + 1}.dest`, 42 + 2 * k, 0, "inferred", dnLo, (v, out) => {
    const q = DN_LFO_DEST[v];
    out.set(dnQlfo(k, "DEST"), q ?? 0);
    if (q === undefined) out.note(`LFO ${k + 1} destination #${v} not mapped yet: off`);
  }],
  [`lfo${k + 1}.wave`, 46 + 2 * k, 0, "inferred", dnLo, (v, out) => out.set(dnQlfo(k, "WAVE"), DN_WAVE_TO_QUAD[v] ?? 0)],
  [`lfo${k + 1}.phase`, 50 + 2 * k, 0, "guess", dnLo, dnTo(dnQlfo(k, "PHASE"))],
  [`lfo${k + 1}.mode`, 54 + 2 * k, 0, "inferred", dnLo, dnTo(dnQlfo(k, "TRIG"))],          // FREE TRG HLD ONE HLF: = FM TONE's
  [`lfo${k + 1}.depth`, 58 + 2 * k, 1, "inferred", (d, o) => dnHi(d, o) - 64, dnTo(dnQlfo(k, "DEPTH"))],
];
const dnEnv = (x, base) => ["atk", "dec", "end", "lev"].map((f, i) => [`env${x}.${f}`, base + 2 * i, 0, "inferred", dnLo,
  dnTo(DN_QP[`${x}${f.toUpperCase()}`])]);
const dnLost = (what) => (v, out) => { if (dnR(v)) out.note(`${what} ${dnR(v)} not imported: effects are global on the FM-1`); };
export const DNSND_FIELDS = [
  ...dnLfo(0), ...dnLfo(1),
  ["algo", 62, 0, "inferred", (d, o) => d[o] + 1, dnTo(DN_QP.ALGO)],
  ["ratioC", 64, 0, "inferred", dnLo, dnTo(DN_QP.RC)],                                         // the same 19 steps, 3 = 1.00
  ["ratioA", 66, 0, "guess", dnLo, (v, out) => {
    const r = DN_RATIO_A[v] ?? 1;
    out.set(DN_QP.RA, dnR(r * 4) - 1);                                                       // FM TONE: 0.25 .. 16 in quarters
    if (v !== 3) out.note(`RATIO A step ${v} read as ${r.toFixed(2)} (step table assumed)`);
  }],
  ["unk68", 68, 0, "unknown", dnLo, null],
  ["harm", 70, 1, "inferred", (d, o) => dnHi(d, o) - 63, dnTo(DN_QP.HARM)],
  ["dtun", 72, 1, "inferred", dnHi, dnTo(DN_QP.DTUN)],
  ["fdbk", 74, 0, "inferred", dnLo, dnTo(DN_QP.FDBK, (v) => dnR(v * 120 / 127))],               // 0..127 -> FM TONE's 0..120
  ["mix", 76, 0, "inferred", (d, o) => d[o] - 64, dnTo(DN_QP.MIX)],
  ["unk78", 78, 0, "unknown", dnLo, null],
  ...dnEnv("A", 82), ...dnEnv("B", 90),
  ["envA.delay", 98, 0, "guess", dnLo, dnTo(DN_QP.ADLY)],
  ["envA.trig", 100, 0, "guess", dnLo, dnTo(DN_QP.ATRIG)],
  ["envA.reset", 102, 0, "guess", dnLo, dnTo(DN_QP.ARST)],
  ["envB.delay", 104, 0, "guess", dnLo, dnTo(DN_QP.BDLY)],
  ["envB.trig", 106, 0, "guess", dnLo, dnTo(DN_QP.BTRIG)],
  ["envB.reset", 108, 0, "guess", dnLo, dnTo(DN_QP.BRST)],
  ["unk110", 110, 0, "unknown", dnLo, null],
  ["filter.type", 128, 0, "inferred", dnLo, (v, out) => {
    const q = DN_FTYPE_TO_QUAD[v] ?? DN_QF.LP12, n = DN_FTYPE[v] ?? `#${v}`;
    out.set(DN_QP.FTYPE, q);
    if (v !== 0 && v !== 1 && v !== 3) out.note(`filter type ${n} -> ${["OFF", "LP12", "HP12", "LP24"][q]}`);
  }],
  ["filter.freq", 130, 1, "inferred", dnHi, dnTo(DN_QP.FREQ)],
  ["filter.reso", 132, 1, "inferred", dnHi, dnTo(DN_QP.RESO)],
  ["filter.env", 134, 1, "guess", (d, o) => dnHi(d, o) - 64, dnTo(DN_QP.FDEPTH)],
  ["filter.atk", 136, 0, "inferred", dnLo, dnTo(DN_QP.FATK)],
  ["filter.dec", 138, 0, "inferred", dnLo, dnTo(DN_QP.FDEC)],
  ["filter.sus", 140, 0, "inferred", dnLo, dnTo(DN_QP.FSUS)],
  ["filter.rel", 142, 0, "inferred", dnLo, dnTo(DN_QP.FREL)],
  ["filter.base", 144, 1, "guess", dnHi, dnTo(DN_QP.BASE)],
  ["filter.width", 146, 0, "guess", dnLo, dnTo(DN_QP.WIDTH)],
  ["amp.atk", 148, 0, "inferred", dnLo, dnTo(DN_QP.EATK)],
  ["amp.hold", 150, 0, "guess", dnLo, (v, out) => { if (v) out.note(`AMP HOLD ${v} dropped (FM TONE has none)`); }],
  ["amp.dec", 152, 0, "guess", dnLo, dnTo(DN_QP.EDEC)],
  ["amp.sus", 154, 1, "guess", dnHi, dnTo(DN_QP.ESUS)],
  ["send.delay", 156, 1, "guess", dnHi, dnLost("delay send")],
  ["amp.pan", 158, 0, "inferred", (d, o) => d[o] - 64, (v, out) => { out.pan = Math.max(-64, Math.min(63, v)); }],
  ["amp.vol", 160, 1, "inferred", dnHi, dnTo(DN_QP.LEVEL)],
  ["send.reverb", 162, 1, "guess", dnHi, dnLost("reverb send")],
  ["send.chorus", 164, 1, "guess", dnHi, dnLost("chorus send")],
  ["amp.rel", 166, 1, "guess", dnHi, dnTo(DN_QP.EREL)],
  ["unk168", 168, 0, "unknown", dnLo, null],
];

// ---- the container
const DN_SIZE = 302, DN_HDR = 0x23;
const dnU16 = (b, o) => b[o] | b[o + 1] << 8, dnU32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const DN_NOT = "not a Digitone sound file";
// a raw deflate stream -> its bytes: the browser's DecompressionStream, else node's zlib
export async function dnInflateRaw(bytes) {
  if (typeof DecompressionStream !== "undefined") {
    const s = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
    return new Uint8Array(await new Response(s).arrayBuffer());
  }
  const zlib = await import("node:zlib");
  return new Uint8Array(zlib.inflateRawSync(bytes));
}
// the ZIP's entries (the central directory): name -> Promise<bytes>
export async function dnReadZip(b) {
  let e = -1;
  for (let i = b.length - 22; i >= Math.max(0, b.length - 22 - 65535); i--) if (dnU32(b, i) === 0x06054b50) { e = i; break; }
  if (e < 0) throw new Error(`${DN_NOT} (not a ZIP archive)`);
  const n = dnU16(b, e + 10), out = new Map();
  let p = dnU32(b, e + 16);
  for (let k = 0; k < n; k++) {
    if (p + 46 > b.length || dnU32(b, p) !== 0x02014b50) throw new Error(`${DN_NOT} (a damaged ZIP directory)`);
    const method = dnU16(b, p + 10), cs = dnU32(b, p + 20), nl = dnU16(b, p + 28), xl = dnU16(b, p + 30), cl = dnU16(b, p + 32), lh = dnU32(b, p + 42);
    const name = new TextDecoder().decode(b.subarray(p + 46, p + 46 + nl));
    if (lh + 30 > b.length || dnU32(b, lh) !== 0x04034b50) throw new Error(`${DN_NOT} (a damaged ZIP entry ${name})`);
    const start = lh + 30 + dnU16(b, lh + 26) + dnU16(b, lh + 28), data = b.subarray(start, start + cs);
    if (data.length !== cs) throw new Error(`${DN_NOT} (the ZIP entry ${name} is cut short)`);
    if (method !== 0 && method !== 8) throw new Error(`${DN_NOT} (ZIP method ${method})`);
    out.set(name, method === 8 ? data : data.slice());
    out.get(name).deflated = method === 8;
    p += 46 + nl + xl + cl;
  }
  return { names: [...out.keys()], get: async (name) => { const d = out.get(name); return d && (d.deflated ? dnInflateRaw(d) : d); } };
}
// an LZ4 block from src[p] -> want bytes (the payload's compression)
export function dnLz4Block(src, p, want) {
  const o = new Uint8Array(want); let n = 0;
  const byte = () => { if (p >= src.length) throw new Error(`${DN_NOT} (the sound data is cut short)`); return src[p++]; };
  while (n < want) {
    const t = byte(); let l = t >> 4;
    if (l === 15) { let b; do { b = byte(); l += b; } while (b === 255); }
    if (n + l > want) throw new Error(`${DN_NOT} (the sound data does not decompress)`);
    for (let i = 0; i < l; i++) o[n++] = byte();
    if (n >= want) break;
    const off = byte() | byte() << 8;
    let m = (t & 15) + 4;
    if ((t & 15) === 15) { let b; do { b = byte(); m += b; } while (b === 255); }
    if (!off || off > n || n + m > want) throw new Error(`${DN_NOT} (the sound data does not decompress)`);
    for (let i = 0; i < m; i++, n++) o[n] = o[n - off];
  }
  return { data: o, end: p };
}

// a .dnsnd's bytes -> the sound (throws with a clear message on anything that is not a Digitone sound version 1)
export async function parseDnsnd(bytes) {
  if (!(bytes instanceof Uint8Array)) bytes = Uint8Array.from(bytes || []);
  if (bytes.length < 4 || dnU32(bytes, 0) !== 0x04034b50) throw new Error(`${DN_NOT} (not a ZIP archive)`);
  const zip = await dnReadZip(bytes), warnings = [];
  const mt = await zip.get("manifest.json");
  if (!mt) throw new Error(`${DN_NOT} (no manifest.json)`);
  let man;
  try { man = JSON.parse(new TextDecoder().decode(mt)); } catch (_) { throw new Error(`${DN_NOT} (manifest.json is not JSON)`); }
  if (!man || man.FileType !== "Sound") throw new Error(`${DN_NOT} (FileType ${man && JSON.stringify(man.FileType)}, not "Sound")`);
  const pl = typeof man.Payload === "string" && await zip.get(man.Payload);
  if (!pl) throw new Error(`${DN_NOT} (the payload ${JSON.stringify(man.Payload)} is missing)`);
  if (pl.length < DN_HDR + 14 || dnU32(pl, 0) !== 0x03d311ac) throw new Error(`${DN_NOT} (the payload's magic)`);
  if (new TextDecoder().decode(pl.subarray(9, 13)) !== "0097") warnings.push("the payload header is not the 0097 kind: read as it");
  const size = dnU16(pl, 0x1c);
  if (size !== DN_SIZE) throw new Error(`a newer Digitone format (a ${size}-byte sound, not ${DN_SIZE}): not supported`);
  const { data: d, end } = dnLz4Block(pl, DN_HDR, size);
  if (!(d[0] === 0xbe && d[1] === 0xef && d[2] === 0xba && d[3] === 0xce)) throw new Error(`${DN_NOT} (the sound's magic)`);
  const version = (d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7]) >>> 0;
  if (version !== 1) throw new Error(`a newer Digitone format (version ${version}): not supported`);
  const tail = pl.subarray(end);
  if (tail.length < 4 || dnU32(tail, tail.length - 4) !== 0xaadaa1aa) warnings.push("the payload's trailer is not the usual one");
  if (man.FirmwareVersion !== "1.42A") warnings.push(`made on Digitone firmware ${man.FirmwareVersion}: read as 1.42A`);
  if (JSON.stringify(man.ProductType) !== '["24","30"]') warnings.push(`product ${JSON.stringify(man.ProductType)}: read as a Digitone`);
  let name = "";
  for (let i = 12; i < 28 && d[i]; i++) name += String.fromCharCode(d[i]);
  const params = {}, confidence = {};
  for (const [k, o, , c, dec] of DNSND_FIELDS) { params[k] = +dec(d, o).toFixed(2); confidence[k] = c; }
  const tags = Array.isArray(man.MetaInfo && man.MetaInfo.Tags) ? man.MetaInfo.Tags.map(String) : [];
  return { name, tags, firmware: man.FirmwareVersion ?? null, product: man.ProductType ?? null, version, params, confidence, warnings, record: d };
}

// the parsed sound (or its params) -> FM TONE's blob, the track PAN, and what was lost or approximated
export function dnsndToQuad(parsed) {
  const params = parsed && parsed.params ? parsed.params : parsed;
  const p = DN_QUAD_RANGES.map((r) => r[2]), notes = [];
  const out = {
    pan: 0,
    note: (s) => notes.push(s),
    set: (i, v) => {
      const [mn, mx] = DN_QUAD_RANGES[i], c = Math.max(mn, Math.min(mx, Number.isFinite(v) ? v : DN_QUAD_RANGES[i][2]));
      p[i] = c;
    },
  };
  for (const [k, , , c, , apply] of DNSND_FIELDS) if (apply && c !== "unknown" && params[k] !== undefined) apply(params[k], out);
  p[DN_QP.RB1] = 3; p[DN_QP.RB2] = 3;
  notes.unshift("RATIO B unknown: set to 1.00/1.00");
  const blob = new Uint8Array(DN_QUAD_BLOB);
  blob[0] = 0x51; blob[1] = 3;
  for (let i = 0; i < DN_QUAD_NP; i++) blob[2 + i] = p[i] - DN_QUAD_RANGES[i][0];
  return { blob, values: p, pan: out.pan, notes };
}
