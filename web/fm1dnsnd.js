// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Elektron Digitone sound files (.dnsnd) read for FM TONE (engine 15, eng_quad.c), docs/SOUNDS.md ".dnsnd import".
// Clean-room: the container was decoded from files, no Elektron code or data is used. Pure: no DOM; the files are read
// on this computer, nothing is uploaded.
//
// A .dnsnd is a ZIP of manifest.json ({FormatVersion, ProductType, Payload, FileType "Sound", FirmwareVersion,
// MetaInfo.Tags}) and the payload named by Payload: a 0x23-byte header (magic ac 11 d3 03, ASCII "0097" at 9, the
// decompressed size u16le at 0x1c = 302), an LZ4 block, a 14-byte trailer (.. end magic aa a1 da aa). The 302-byte
// record: be ef ba ce, u32be version (1: the factory sounds; 5: a sound saved on OS 1.44, the owner's exports), u32 tag
// bits, the name (16 bytes, NUL padded) at 12, then the parameters as byte pairs from 30 (the coarse value, the fine
// byte on the hi-res ones). Slot s (offset 28 + 2 s) is also LFO destination number s (proved by the owner's controlled
// exports, 2026-10-14: docs/SOUNDS.md ".dnsnd import").
//
//   parseDnsnd(bytes)   -> Promise<{name, tags, firmware, product, version, params, confidence, warnings, record}>
//   dnsndToQuad(parsed) -> {blob: Uint8Array(80) FM TONE version 3, pan, drive, notes: [what was lost / approximated]}

// ---- FM TONE's patch (eng_quad.c QP_*: [min, max, def] of each value, `sound_templates --quad-ranges` prints it)
export const DN_QUAD_RANGES = [[1,8,1],[0,18,3],[0,63,3],[0,18,3],[-26,26,0],[0,127,0],[0,120,0],[-64,63,0],[-100,100,0],[-100,100,0],[-100,100,0],[-100,100,0],[0,127,0],[0,127,60],[0,127,64],[0,127,48],[0,127,0],[0,127,60],[0,127,0],[0,127,0],[0,127,0],[0,1,1],[0,1,1],[0,4,1],[0,127,0],[0,1,1],[0,1,1],[0,127,64],[0,127,0],[0,127,0],[0,127,0],[0,127,64],[0,127,0],[0,127,40],[0,127,127],[0,127,0],[0,3,1],[-64,63,0],[0,127,0],[0,127,0],[0,127,0],[0,127,127],[0,127,0],[0,127,64],[0,127,127],[0,127,40],[0,127,100],[-64,63,16],[0,23,3],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-64,63,0],[-64,63,16],[0,23,3],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-64,63,0],[-64,63,16],[0,23,3],[-64,63,0],[0,57,0],[0,6,0],[0,127,0],[0,4,0],[-64,63,0],[0,18,3],[0,127,0]];
export const DN_QP = { ALGO: 0, RC: 1, RA: 2, RB1: 3, HARM: 4, DTUN: 5, FDBK: 6, MIX: 7, OFSC: 8, OFSA: 9, OFSB1: 10, OFSB2: 11,
  AATK: 12, ADEC: 13, AEND: 14, ALEV: 15, BATK: 16, BDEC: 17, BEND: 18, BLEV: 19, ADLY: 20, ATRIG: 21, ARST: 22, PHRT: 23,
  BDLY: 24, BTRIG: 25, BRST: 26, FATK: 30, FDEC: 31, FSUS: 32, FREL: 33, FREQ: 34, RESO: 35, FTYPE: 36, FDEPTH: 37,
  FDLY: 38, BASE: 40, WIDTH: 41, EATK: 42, EDEC: 43, ESUS: 44, EREL: 45, LEVEL: 46, RB2: 71 };
const DN_QL = { SPEED: 0, MULT: 1, FADE: 2, DEST: 3, WAVE: 4, PHASE: 5, TRIG: 6, DEPTH: 7 };
const dnQlfo = (k, f) => 47 + k * 8 + DN_QL[f];
export const DN_QUAD_NP = 73, DN_QUAD_BLOB = 80;
const DN_QF = { OFF: 0, LP12: 1, HP12: 2, LP24: 3 };
export const DN_VERSIONS = [1, 5];                // the record versions read (others: "not supported")

// ---- the Digitone's tables (the manual OS 1.44 and the owner's controlled exports, 2026-10-14)
// RATIO C, B1, B2: 19 steps (= FM TONE's QUAD_RCB); exports: C 0 = 0.25, 3 = 1, 4 = 2, 18 = 16; B 0.25 0.5 1 16
export const DN_RATIO_CB = [0.25, 0.5, 0.75, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
// RATIO A, 36 steps. Verified anchors (exports): 0 = 0.25, 3 = 1, 9 = 2.5, 15 = 4, 25 = 8, 35 = 16. 0..15 in quarters
// fits every anchor; 16..24 and 26..34 are NOT proved: linear between the anchors (4.4 4.8 .. and 8.8 9.6 ..)
export const DN_RATIO_A = Array.from({ length: 36 }, (_, i) => Math.round((i <= 15 ? (i + 1) / 4 : i <= 25 ? 4 + (i - 15) * 0.4 : 8 + (i - 25) * 0.8) * 100) / 100);
export const DN_RATIO_A_ANCHORS = [0, 3, 9, 15, 25, 35];
const DN_WAVE = ["TRI", "SINE", "SQR", "SAW", "EXP", "RMP", "RND"];   // the manual's order (not proved by an export)
const DN_WAVE_TO_QUAD = [0, 1, 2, 3, 5, 4, 6];                       // FM TONE: TRI SINE SQR SAW RAMP EXP RAND
// filter TYPE. Version 5 (exports C110..C113): 0 OFF, 1 LP2, 2 HP2, 3 LP4 = the manual's order = FM TONE's, 1:1.
// Version 1 (the factory files) has five values 0..4: its order is NOT proved (guess: LP4 LP2 BP HP2 HP4)
const DN_FTYPE_V1 = ["LP4", "LP2", "BP", "HP2", "HP4"];
const DN_FTYPE_V1_TO_QUAD = [DN_QF.LP24, DN_QF.LP12, DN_QF.LP12, DN_QF.HP12, DN_QF.HP12];
// the LFO DEST numbers (each one an export, C042..C099; the number = the record slot of the target, (offset - 28) / 2)
// -> FM TONE's (eng_quad.c QD_*): every one, 1:1 (since 2026-10-14 FM TONE has the Digitone's whole list: LFO 1's own
// parameters for LFO 2, RATIO ALL, the AB macros, DRIVE, the three sends, the filter's ENV DELAY were added, numbers 40..57;
// its editor lists them in the Digitone's order). A number not in the table (none known): off, with a note
export const DN_LFO_DEST_NAMES = {
  0: "NONE", 1: "LFO1 SPEED", 3: "LFO1 MULT", 5: "LFO1 FADE", 9: "LFO1 WAVE", 11: "LFO1 SPH", 13: "LFO1 MODE", 15: "LFO1 DEPTH",
  17: "ALGO", 18: "RATIO C", 19: "RATIO A", 20: "RATIO B", 21: "HARM", 22: "DETUNE", 23: "FEEDBACK", 24: "MIX",
  27: "A ATTACK", 28: "A DECAY", 29: "A END", 30: "A LEVEL", 31: "B ATTACK", 32: "B DECAY", 33: "B END", 34: "B LEVEL",
  35: "A DELAY", 38: "B DELAY", 42: "PITCH ALL", 43: "PITCH A+B2", 44: "RATIO ALL", 45: "AB LEVEL", 46: "AB ATTACK",
  47: "AB DECAY", 48: "AB END", 49: "AB DELAY", 51: "FILTER FREQ", 52: "FILTER RESO", 53: "FILTER ENV DEPTH",
  54: "FILTER ATTACK", 55: "FILTER DECAY", 56: "FILTER SUSTAIN", 57: "FILTER RELEASE", 58: "FILTER BASE", 59: "FILTER WIDTH",
  60: "AMP ATTACK", 61: "AMP DECAY", 62: "AMP SUSTAIN", 63: "AMP RELEASE", 64: "DRIVE", 65: "PAN", 66: "VOLUME",
  67: "REVERB SEND", 68: "DELAY SEND", 69: "CHORUS SEND", 73: "RATIO C OFFSET", 74: "RATIO A OFFSET", 75: "RATIO B1 OFFSET",
  76: "RATIO B2 OFFSET", 77: "FILTER ENV DELAY",
};
const DN_QD = { NONE: 0, PITCH: 1, PAB2: 2, ALGO: 3, RC: 4, RA: 5, RB: 6, OFSC: 7, OFSA: 8, OFSB1: 9, OFSB2: 10, HARM: 11, DTUN: 12,
  FDBK: 13, MIX: 14, ALEV: 15, BLEV: 16, AATK: 17, ADEC: 18, AEND: 19, BATK: 20, BDEC: 21, BEND: 22, ADLY: 23, BDLY: 24, FREQ: 25,
  RESO: 26, FENV: 27, BASE: 28, WIDTH: 29, FATK: 30, FDEC: 31, FSUS: 32, FREL: 33, EATK: 34, EDEC: 35, ESUS: 36, EREL: 37, LEVEL: 38, PAN: 39,
  L1SPD: 40, L1MUL: 41, L1FAD: 42, L1WAV: 43, L1SPH: 44, L1MOD: 45, L1DEP: 46, RALL: 47, ABLEV: 48, ABATK: 49, ABDEC: 50, ABEND: 51,
  ABDLY: 52, DRIVE: 53, SREV: 54, SDLY: 55, SCHO: 56, FDLY: 57 };
export const DN_LFO_DEST = {
  0: DN_QD.NONE, 1: DN_QD.L1SPD, 3: DN_QD.L1MUL, 5: DN_QD.L1FAD, 9: DN_QD.L1WAV, 11: DN_QD.L1SPH, 13: DN_QD.L1MOD,
  15: DN_QD.L1DEP, 17: DN_QD.ALGO, 18: DN_QD.RC, 19: DN_QD.RA, 20: DN_QD.RB, 21: DN_QD.HARM, 22: DN_QD.DTUN, 23: DN_QD.FDBK,
  24: DN_QD.MIX, 27: DN_QD.AATK, 28: DN_QD.ADEC, 29: DN_QD.AEND, 30: DN_QD.ALEV, 31: DN_QD.BATK, 32: DN_QD.BDEC, 33: DN_QD.BEND,
  34: DN_QD.BLEV, 35: DN_QD.ADLY, 38: DN_QD.BDLY, 42: DN_QD.PITCH, 43: DN_QD.PAB2, 44: DN_QD.RALL,
  45: DN_QD.ABLEV, 46: DN_QD.ABATK, 47: DN_QD.ABDEC, 48: DN_QD.ABEND, 49: DN_QD.ABDLY, 51: DN_QD.FREQ, 52: DN_QD.RESO, 53: DN_QD.FENV,
  54: DN_QD.FATK, 55: DN_QD.FDEC, 56: DN_QD.FSUS, 57: DN_QD.FREL, 58: DN_QD.BASE, 59: DN_QD.WIDTH, 60: DN_QD.EATK, 61: DN_QD.EDEC,
  62: DN_QD.ESUS, 63: DN_QD.EREL, 64: DN_QD.DRIVE, 65: DN_QD.PAN, 66: DN_QD.LEVEL, 67: DN_QD.SREV, 68: DN_QD.SDLY, 69: DN_QD.SCHO,
  73: DN_QD.OFSC, 74: DN_QD.OFSA, 75: DN_QD.OFSB1, 76: DN_QD.OFSB2, 77: DN_QD.FDLY,
};

// ---- the field table: [name, offset, hi-res, confidence, decode(d, o) -> the Digitone's value, apply(value, out), since]
// confidence: "verified" (a controlled export changed exactly this field: offset and encoding proved), "slot" (the offset
// proved by the LFO destination numbering, the encoding inferred), "inferred" (corpus ranges, the manual), "guess",
// "unknown" (decoded for the diff tool, not imported). since: the first record version that has the field (default 1)
const dnLo = (d, o) => d[o], dnHi = (d, o) => d[o] + d[o + 1] / 256;
const dnR = Math.round;
const dnTimeI = (v) => v >= 127 ? 127 : Math.min(126, dnR(v));      // a release: 127 INF, a timed one (126.85) stays timed
const dnTo = (qp, f = (v) => dnR(v)) => (v, out) => out.set(qp, f(v));
const dnLfo = (k) => {
  const c = k ? "verified" : "slot";                             // LFO 2's SPEED / MULT were turned (C100..C109)
  return [
    [`lfo${k + 1}.speed`, 30 + 2 * k, 1, c, (d, o) => dnHi(d, o) - 64, dnTo(dnQlfo(k, "SPEED"))],   // -64.00..63.99
    [`lfo${k + 1}.mult`, 34 + 2 * k, 0, c, dnLo, dnTo(dnQlfo(k, "MULT"))],    // 0 = 1, 5 = 32, 21..23 = 512 1k 2k: = FM TONE's 24
    [`lfo${k + 1}.fade`, 38 + 2 * k, 0, "slot", (d, o) => d[o] - 64, dnTo(dnQlfo(k, "FADE"))],
    [`lfo${k + 1}.dest`, 42 + 2 * k, 0, "verified", dnLo, (v, out) => {
      const q = DN_LFO_DEST[v];
      out.set(dnQlfo(k, "DEST"), q ?? 0);
      if (q === undefined) out.note(`LFO ${k + 1} destination ${DN_LFO_DEST_NAMES[v] ?? `#${v}`}: FM TONE has none, off`);
    }],
    [`lfo${k + 1}.wave`, 46 + 2 * k, 0, "slot", dnLo, (v, out) => out.set(dnQlfo(k, "WAVE"), DN_WAVE_TO_QUAD[v] ?? 0)],
    [`lfo${k + 1}.phase`, 50 + 2 * k, 0, "slot", dnLo, dnTo(dnQlfo(k, "PHASE"))],
    [`lfo${k + 1}.mode`, 54 + 2 * k, 0, "slot", dnLo, dnTo(dnQlfo(k, "TRIG"))],          // FREE TRG HLD ONE HLF: = FM TONE's
    [`lfo${k + 1}.depth`, 58 + 2 * k, 1, "slot", (d, o) => dnHi(d, o) - 64, dnTo(dnQlfo(k, "DEPTH"))],
  ];
};
const dnEnv = (x, base) => ["atk", "dec", "end", "lev"].map((f, i) => [`env${x}.${f}`, base + 2 * i, 0, "verified", dnLo,
  dnTo(DN_QP[`${x}${f.toUpperCase()}`])]);
const dnLost = (what) => (v, out) => { if (v > 0) out.note(`${what} ${v.toFixed(2)} not imported: effects are global on the FM-1`); };
// a ratio offset (version 5 only, 64 = 0; its scale is not proved: +-64 read as +-1.00, FM TONE's OFS -100..100)
const dnOfs = (qp, what) => (v, out) => { out.set(qp, dnR(v * 100 / 64)); if (dnR(v * 100 / 64)) out.note(`${what} offset read as ${(v / 64).toFixed(2)} (its scale is not proved)`); };
export const DNSND_FIELDS = [
  ...dnLfo(0), ...dnLfo(1),
  ["algo", 62, 0, "slot", (d, o) => d[o] + 1, dnTo(DN_QP.ALGO)],                             // 0..7 -> ALGO 1..8
  ["ratioC", 64, 0, "verified", (d, o) => DN_RATIO_CB[Math.min(18, d[o])], (v, out) => out.set(DN_QP.RC, Math.max(0, DN_RATIO_CB.indexOf(v)))],
  ["ratioA", 66, 0, "verified", (d, o) => DN_RATIO_A[Math.min(35, d[o])], (v, out) => {
    out.set(DN_QP.RA, dnR(v * 4) - 1);                                                         // FM TONE: 0.25 .. 16 in quarters
    const i = DN_RATIO_A.indexOf(v);
    if (i > 15 && !DN_RATIO_A_ANCHORS.includes(i)) out.note(`RATIO A step ${i} read as ${v.toFixed(2)} (between the proved steps)`);
  }],
  // RATIO B: a u16be at 68 = 2 x (B1 step x 19 + B2 step) + a low bit (not known; ignored). The value is FM TONE's own
  // RATIO B pair (QP_RB1 x 19 + QP_RB2: B2 the fast hand, the manual's §11.3.4)
  ["ratioB", 68, 1, "verified", (d, o) => Math.min(360, (d[o] << 8 | d[o + 1]) >> 1), (v, out) => {
    out.set(DN_QP.RB1, Math.floor(v / 19)); out.set(DN_QP.RB2, v % 19);
  }],
  ["harm", 70, 1, "verified", (d, o) => dnHi(d, o) - 63, dnTo(DN_QP.HARM)],                    // 37 = -26, 63 = 0, 89 = +26
  ["dtun", 72, 1, "verified", dnHi, dnTo(DN_QP.DTUN)],
  ["fdbk", 74, 0, "verified", dnLo, dnTo(DN_QP.FDBK, (v) => dnR(v * 120 / 127))],              // stored 0..127 (MAX = 127)
  ["mix", 76, 0, "verified", (d, o) => d[o] - 64, dnTo(DN_QP.MIX)],                            // 0 = -64, 64 = 0, 127 = +63
  ["phrt", 78, 0, "inferred", dnLo, dnTo(DN_QP.PHRT)],                                         // 0..4 OFF ALL C A+B A+B2 = FM TONE's
  ...dnEnv("A", 82), ...dnEnv("B", 90),
  ["envA.delay", 98, 0, "slot", dnLo, dnTo(DN_QP.ADLY)],
  ["envA.trig", 100, 0, "inferred", dnLo, dnTo(DN_QP.ATRIG)],
  ["envA.reset", 102, 0, "inferred", dnLo, dnTo(DN_QP.ARST)],
  ["envB.delay", 104, 0, "slot", dnLo, dnTo(DN_QP.BDLY)],
  ["envB.trig", 106, 0, "inferred", dnLo, dnTo(DN_QP.BTRIG)],
  ["envB.reset", 108, 0, "inferred", dnLo, dnTo(DN_QP.BRST)],
  ["unk110", 110, 0, "unknown", dnLo, null],
  ["filter.type", 128, 0, "verified", dnLo, (v, out) => {
    if (out.version >= 5) { out.set(DN_QP.FTYPE, v); return; }                                 // version 5: 1:1
    const q = DN_FTYPE_V1_TO_QUAD[v] ?? DN_QF.LP12, n = DN_FTYPE_V1[v] ?? `#${v}`;
    out.set(DN_QP.FTYPE, q);
    if (v !== 0 && v !== 1 && v !== 3) out.note(`filter type ${n} (a version-1 sound: the type order is a guess) -> ${["OFF", "LP12", "HP12", "LP24"][q]}`);
  }],
  ["filter.freq", 130, 1, "verified", dnHi, dnTo(DN_QP.FREQ)],
  ["filter.reso", 132, 1, "verified", dnHi, dnTo(DN_QP.RESO)],
  ["filter.env", 134, 1, "verified", (d, o) => dnHi(d, o) - 64, dnTo(DN_QP.FDEPTH)],
  ["filter.atk", 136, 0, "verified", dnLo, dnTo(DN_QP.FATK)],
  ["filter.dec", 138, 0, "verified", dnLo, dnTo(DN_QP.FDEC)],                                  // 127 = INF (dnsndToQuad)
  ["filter.sus", 140, 0, "verified", dnLo, dnTo(DN_QP.FSUS)],
  ["filter.rel", 142, 0, "verified", dnLo, dnTo(DN_QP.FREL)],                                  // 127 = INF = FM TONE's
  ["filter.base", 144, 1, "slot", dnHi, dnTo(DN_QP.BASE)],
  ["filter.width", 146, 0, "slot", dnLo, dnTo(DN_QP.WIDTH)],
  ["amp.atk", 148, 0, "verified", dnLo, dnTo(DN_QP.EATK)],
  ["amp.dec", 150, 0, "verified", dnLo, dnTo(DN_QP.EDEC)],                                     // 127 = INF (dnsndToQuad)
  ["amp.sus", 152, 0, "verified", dnLo, dnTo(DN_QP.ESUS)],
  ["amp.rel", 154, 1, "verified", dnHi, dnTo(DN_QP.EREL, dnTimeI)],                            // 127 = INF = FM TONE's
  ["amp.drive", 156, 1, "verified", dnHi, (v, out) => { out.drive = Math.max(0, Math.min(127, dnR(v))); }],
  ["amp.pan", 158, 0, "verified", (d, o) => d[o] - 64, (v, out) => { out.pan = Math.max(-64, Math.min(63, v)); }],
  ["amp.vol", 160, 1, "verified", dnHi, dnTo(DN_QP.LEVEL)],
  ["send.reverb", 162, 1, "verified", dnHi, dnLost("reverb send")],
  ["send.delay", 164, 1, "verified", dnHi, dnLost("delay send")],
  ["send.chorus", 166, 1, "verified", dnHi, dnLost("chorus send")],
  ["amp.reset", 168, 0, "verified", dnLo, (v, out) => { if (out.version >= 5 && v === 0) out.note("AMP ENV RESET off not imported (FM TONE's amp envelope always resets)"); }],
  ["unk170", 170, 0, "unknown", dnLo, null, 5],
  ["unk172", 172, 0, "unknown", dnLo, null, 5],
  ["ofsC", 174, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSC, "RATIO C"), 5],
  ["ofsA", 176, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSA, "RATIO A"), 5],
  ["ofsB1", 178, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSB1, "RATIO B1"), 5],
  ["ofsB2", 180, 1, "slot", (d, o) => dnHi(d, o) - 64, dnOfs(DN_QP.OFSB2, "RATIO B2"), 5],
  ["filter.delay", 182, 0, "slot", dnLo, dnTo(DN_QP.FDLY), 5],
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
  if (!DN_VERSIONS.includes(version)) throw new Error(`a newer Digitone format (version ${version}): not supported`);
  const tail = pl.subarray(end);
  if (tail.length < 4 || dnU32(tail, tail.length - 4) !== 0xaadaa1aa) warnings.push("the payload's trailer is not the usual one");
  if (man.FirmwareVersion !== "1.42A") warnings.push(`made on Digitone firmware ${man.FirmwareVersion}: read as 1.42A`);
  if (JSON.stringify(man.ProductType) !== '["24","30"]') warnings.push(`product ${JSON.stringify(man.ProductType)}: read as a Digitone`);
  let name = "";
  for (let i = 12; i < 28 && d[i]; i++) name += String.fromCharCode(d[i]);
  const params = {}, confidence = {};
  for (const [k, o, , c, dec, , since = 1] of DNSND_FIELDS) if (version >= since) { params[k] = +dec(d, o).toFixed(2); confidence[k] = c; }
  const tags = Array.isArray(man.MetaInfo && man.MetaInfo.Tags) ? man.MetaInfo.Tags.map(String) : [];
  return { name, tags, firmware: man.FirmwareVersion ?? null, product: man.ProductType ?? null, version, params, confidence, warnings, record: d };
}

// the parsed sound (or its params: read as version 1) -> FM TONE's blob, the track PAN and DRIVE (P_PAN, P_DIST: not in
// the patch), and what was lost or approximated
export function dnsndToQuad(parsed) {
  const params = parsed && parsed.params ? parsed.params : parsed;
  const p = DN_QUAD_RANGES.map((r) => r[2]), notes = [];
  const out = {
    pan: 0, drive: 0, version: (parsed && parsed.params && parsed.version) || 1,
    note: (s) => notes.push(s),
    set: (i, v) => {
      const [mn, mx] = DN_QUAD_RANGES[i], c = Math.max(mn, Math.min(mx, Number.isFinite(v) ? v : DN_QUAD_RANGES[i][2]));
      p[i] = c;
    },
  };
  for (const [k, , , c, , apply] of DNSND_FIELDS) if (apply && c !== "unknown" && params[k] !== undefined) apply(params[k], out);
  // the Digitone's filter and amp DEC 127 is INF (the owner's exports C126, C135): the level holds after the attack, i.e.
  // FM TONE's SUS at 127 (its DEC has no INF)
  if (params["filter.dec"] === 127) { p[DN_QP.FSUS] = 127; notes.push("filter DEC INF: FM TONE's filter SUS set to 127"); }
  if (params["amp.dec"] === 127) { p[DN_QP.ESUS] = 127; notes.push("AMP DEC INF: FM TONE's AMP SUS set to 127"); }
  const blob = new Uint8Array(DN_QUAD_BLOB);
  blob[0] = 0x51; blob[1] = 3;
  for (let i = 0; i < DN_QUAD_NP; i++) blob[2 + i] = p[i] - DN_QUAD_RANGES[i][0];
  return { blob, values: p, pan: out.pan, drive: out.drive, notes };
}
