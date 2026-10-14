// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// fm1dnsnd.js (docs/SOUNDS.md ".dnsnd import") with .dnsnd files built here by our own encoder (a ZIP stored or
// deflated, an LZ4 block, the payload's header and trailer) holding made-up parameters: every mapped field lands with
// its conversion, clamping, the errors and warnings, the import into a slot, the blob checked by the firmware's own
// quad_blob_ok (build/host/sound_templates --quad-check, built by tests/run_cr_tests.sh; skipped when missing).
//   node web/test_dnsnd.mjs
import { deflateRawSync } from "node:zlib";
import { existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { parseDnsnd, dnsndToQuad, DNSND_FIELDS, DN_QUAD_RANGES, DN_QP, DN_RATIO_A, dnLz4Block } from "./fm1dnsnd.js";
import { soundFromDnsnd, recordValid, SOUND_IDS, placeSound, parseSoundObjects, firstFreeSlot } from "./fm1sounds.js";
import { libraryItemsFromDnsnd } from "./fm1manager.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const rejects = async (p, re) => { try { await p; return false; } catch (e) { return re.test(e.message) ? true : (console.log(`  (message: ${e.message})`), false); } };

/* ------------------------------------------------------------------ the encoder --- */
const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ c >>> 1 : c >>> 1; return c >>> 0; });
const crc32 = (b) => { let c = ~0; for (const x of b) c = crcT[(c ^ x) & 255] ^ c >>> 8; return ~c >>> 0; };
const le = (n, v) => Array.from({ length: n }, (_, i) => v >>> 8 * i & 255);
function zip(entries, deflate) {             // [[name, bytes]] -> a ZIP (local headers, central directory, end)
  const parts = [], cd = [];
  let off = 0;
  for (const [name, data] of entries) {
    const nm = new TextEncoder().encode(name), body = deflate ? new Uint8Array(deflateRawSync(data)) : data, m = deflate ? 8 : 0;
    const head = [...le(4, 0x04034b50), ...le(2, 20), 0, 0, ...le(2, m), 0, 0, 0, 0, ...le(4, crc32(data)), ...le(4, body.length), ...le(4, data.length), ...le(2, nm.length), 0, 0];
    parts.push(Uint8Array.from(head), nm, body);
    cd.push(Uint8Array.from([...le(4, 0x02014b50), 20, 0, 20, 0, 0, 0, ...le(2, m), 0, 0, 0, 0, ...le(4, crc32(data)), ...le(4, body.length), ...le(4, data.length), ...le(2, nm.length), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, off)]), nm);
    off += head.length + nm.length + body.length;
  }
  const cdLen = cd.reduce((n, p) => n + p.length, 0);
  const end = Uint8Array.from([...le(4, 0x06054b50), 0, 0, 0, 0, ...le(2, entries.length), ...le(2, entries.length), ...le(4, cdLen), ...le(4, off), 0, 0]);
  const all = [...parts, ...cd, end], out = new Uint8Array(all.reduce((n, p) => n + p.length, 0));
  let p = 0; for (const x of all) { out.set(x, p); p += x.length; }
  return out;
}
function lz4Literal(d) {                      // one sequence of literals only
  const l = d.length, out = [l >= 15 ? 0xF0 : l << 4];
  if (l >= 15) { let r = l - 15; while (r >= 255) { out.push(255); r -= 255; } out.push(r); }
  return Uint8Array.from([...out, ...d]);
}
// a made-up sound: the record from field values {name: raw byte or [coarse, fine]}
function record(name, set = {}, { version = 1, magic = [0xbe, 0xef, 0xba, 0xce] } = {}) {
  const d = new Uint8Array(302);
  d.set(magic, 0); d.set([version >>> 24, version >>> 16 & 255, version >>> 8 & 255, version & 255], 4);
  for (let i = 0; i < name.length; i++) d[12 + i] = name.charCodeAt(i);
  for (const [k, off] of DNSND_FIELDS.map((f) => [f[0], f[1]])) {
    const v = set[k];
    if (v === undefined) continue;
    if (Array.isArray(v)) { d[off] = v[0]; d[off + 1] = v[1]; } else d[off] = v;
  }
  return d;
}
function dnsnd(rec, { deflate = false, size = 302, man = {}, hdrMagic = [0xac, 0x11, 0xd3, 0x03], payload = "MY TEST", block } = {}) {
  const hdr = new Uint8Array(0x23);
  hdr.set(hdrMagic, 0); hdr.set([0x30, 0x30, 0x39, 0x37], 9); hdr.set(le(2, size), 0x1c);
  const blk = block || lz4Literal(rec);
  const tr = Uint8Array.from([0, 0, 0, 0, 1, 2, 3, 4, 0, 0, 0, 0x2a, 0xaa, 0xa1, 0xda, 0xaa]).subarray(2);
  const pl = new Uint8Array(hdr.length + blk.length + tr.length);
  pl.set(hdr); pl.set(blk, hdr.length); pl.set(tr, hdr.length + blk.length);
  const m = { FormatVersion: 1, ProductType: ["24", "30"], Payload: payload, FileType: "Sound", FirmwareVersion: "1.42A", MetaInfo: { Tags: ["test"] }, ...man };
  return zip([["manifest.json", new TextEncoder().encode(JSON.stringify(m))], [payload, pl]], deflate);
}

/* ----------------------------------------------------------------------- tests --- */
const firmwareOk = (blobs) => {
  const bin = new URL("../build/host/sound_templates", import.meta.url).pathname;
  if (!existsSync(bin)) return null;
  return execFileSync(bin, ["--quad-check"], { input: blobs.map((b) => Buffer.from(b).toString("hex")).join("\n") + "\n" }).toString().trim().split("\n");
};
const ranges = (() => {
  const bin = new URL("../build/host/sound_templates", import.meta.url).pathname;
  return existsSync(bin) ? JSON.parse(execFileSync(bin, ["--quad-ranges"]).toString()) : null;
})();
if (ranges) ok(JSON.stringify(ranges) === JSON.stringify(DN_QUAD_RANGES), "FM TONE's ranges = the firmware's (sound_templates --quad-ranges)");
else console.log("(build/host/sound_templates missing: the firmware checks are skipped)");

// a sound with every mapped field set to a made-up value
const SET = {
  "lfo1.speed": [64 + 20, 128], "lfo2.speed": [64 - 30, 0], "lfo1.mult": 14, "lfo2.mult": 5, "lfo1.fade": 64 + 9, "lfo2.fade": 64 - 7,
  "lfo1.dest": 0, "lfo2.dest": 0, "lfo1.wave": 4, "lfo2.wave": 5, "lfo1.phase": 33, "lfo2.phase": 0, "lfo1.mode": 3, "lfo2.mode": 1,
  "lfo1.depth": [64 + 40, 200], "lfo2.depth": [64 - 12, 0], algo: 5, ratioC: 7, ratioA: 10, harm: [63 + 11, 0], dtun: [40, 129],
  fdbk: 100, mix: 64 - 20, "envA.atk": 11, "envA.dec": 22, "envA.end": 33, "envA.lev": 44, "envB.atk": 55, "envB.dec": 66,
  "envB.end": 77, "envB.lev": 88, "envA.delay": 5, "envA.trig": 0, "envA.reset": 1, "envB.delay": 6, "envB.trig": 1, "envB.reset": 0,
  "filter.type": 2, "filter.freq": [90, 0], "filter.reso": [12, 255], "filter.env": [64 + 25, 0], "filter.atk": 3, "filter.dec": 70,
  "filter.sus": 80, "filter.rel": 90, "filter.base": [10, 0], "filter.width": 100, "amp.atk": 7, "amp.hold": 9, "amp.dec": 57,
  "amp.sus": [99, 0], "send.delay": [20, 0], "amp.pan": 64 - 5, "amp.vol": [101, 0], "send.reverb": [0, 0], "send.chorus": [3, 0], "amp.rel": [61, 0],
};
const file = dnsnd(record("MY TEST TONE", SET));
const p = await parseDnsnd(file);
ok(p.name === "MY TEST TONE" && p.version === 1 && p.firmware === "1.42A" && p.tags.join() === "test" && !p.warnings.length && p.record.length === 302,
   "parse: name, version, firmware, tags, no warnings");
ok(p.params.algo === 6 && p.params["lfo1.speed"] === 20.5 && p.params.harm === 11 && p.confidence.algo === "inferred" && p.confidence["filter.env"] === "guess",
   "parse: the params in the Digitone's units, with their confidence");
const pd = await parseDnsnd(dnsnd(record("MY TEST TONE", SET), { deflate: true }));
ok(pd.record.every((x, i) => x === p.record[i]), "parse: a deflated ZIP reads the same");
const q = dnsndToQuad(p), v = q.values, L = (k, f) => 47 + k * 8 + { SPEED: 0, MULT: 1, FADE: 2, DEST: 3, WAVE: 4, PHASE: 5, TRIG: 6, DEPTH: 7 }[f];
ok(q.blob.length === 80 && q.blob[0] === 0x51 && q.blob[1] === 3 && q.blob.subarray(75).every((x) => !x), "blob: 'Q' 3, 73 values, zero padding");
ok(v.every((x, i) => q.blob[2 + i] === x - DN_QUAD_RANGES[i][0]), "blob: each byte = value - min");
const want = [
  [DN_QP.ALGO, 6], [DN_QP.RC, 7], [DN_QP.RA, DN_RATIO_A[10] * 4 - 1], [DN_QP.RB1, 3], [DN_QP.RB2, 3], [DN_QP.HARM, 11], [DN_QP.DTUN, 41],
  [DN_QP.FDBK, Math.round(100 * 120 / 127)], [DN_QP.MIX, -20], [DN_QP.AATK, 11], [DN_QP.ADEC, 22], [DN_QP.AEND, 33], [DN_QP.ALEV, 44],
  [DN_QP.BATK, 55], [DN_QP.BDEC, 66], [DN_QP.BEND, 77], [DN_QP.BLEV, 88], [DN_QP.ADLY, 5], [DN_QP.ATRIG, 0], [DN_QP.ARST, 1],
  [DN_QP.BDLY, 6], [DN_QP.BTRIG, 1], [DN_QP.BRST, 0], [DN_QP.FTYPE, 1], [DN_QP.FREQ, 90], [DN_QP.RESO, 13], [DN_QP.FDEPTH, 25],
  [DN_QP.FATK, 3], [DN_QP.FDEC, 70], [DN_QP.FSUS, 80], [DN_QP.FREL, 90], [DN_QP.BASE, 10], [DN_QP.WIDTH, 100], [DN_QP.EATK, 7],
  [DN_QP.EDEC, 57], [DN_QP.ESUS, 99], [DN_QP.EREL, 61], [DN_QP.LEVEL, 101],
  [L(0, "SPEED"), 21], [L(1, "SPEED"), -30], [L(0, "MULT"), 14], [L(1, "MULT"), 5], [L(0, "FADE"), 9], [L(1, "FADE"), -7],
  [L(0, "DEST"), 0], [L(0, "WAVE"), 5], [L(1, "WAVE"), 4], [L(0, "PHASE"), 33], [L(0, "TRIG"), 3], [L(1, "TRIG"), 1],
  [L(0, "DEPTH"), 41], [L(1, "DEPTH"), -12],
  [L(2, "SPEED"), 16], [L(2, "MULT"), 3], [23, 1], [27, 64],                       // LFO 3, PHRT, VEL: FM TONE's defaults
];
const bad = want.filter(([i, x]) => v[i] !== x);
ok(!bad.length, `convert: ${want.length} values land with their conversion`);
bad.forEach(([i, x]) => console.log(`  value ${i}: ${v[i]}, want ${x}`));
ok(DN_RATIO_A.length === 36 && DN_RATIO_A[3] === 1 && DN_RATIO_A[7] === 2 && DN_RATIO_A[8] === 2.5 && DN_RATIO_A[35] === 16, "convert: RATIO A's assumed table (1.00 at 3, 16 at 35)");
ok(q.pan === -5, "convert: PAN -> the track's PAN");
const has = (re) => q.notes.some((n) => re.test(n));
ok(has(/RATIO B unknown: set to 1\.00\/1\.00/) && has(/filter type BP -> LP12/) && has(/AMP HOLD 9 dropped/) && has(/delay send 20 not imported/) &&
   has(/chorus send 3/) && !has(/reverb send/) && has(/RATIO A step 10 read as 3\.50/), "notes: RATIO B, BP -> LP12, HOLD, the sends (0 sends say nothing), RATIO A");
// clamping: out-of-range raw values
const qc = dnsndToQuad(await parseDnsnd(dnsnd(record("CLAMP", { algo: 200, ratioC: 99, ratioA: 99, harm: [127, 255], fdbk: 255, "lfo1.mult": 90,
  "lfo1.wave": 40, "lfo1.mode": 77, "lfo1.dest": 5, "lfo1.depth": [127, 255], "envA.trig": 9, "filter.type": 9, "amp.pan": 250 }))));
const cv = qc.values;
ok(cv[DN_QP.ALGO] === 8 && cv[DN_QP.RC] === 18 && cv[DN_QP.RA] === 3 && cv[DN_QP.HARM] === 26 && cv[DN_QP.FDBK] === 120 && cv[L(0, "MULT")] === 23 &&
   cv[L(0, "WAVE")] === 0 && cv[L(0, "TRIG")] === 4 && cv[L(0, "DEST")] === 0 && cv[L(0, "DEPTH")] === 63 && cv[DN_QP.ATRIG] === 1 &&
   cv[DN_QP.FTYPE] === 1 && qc.pan === 63, "clamp: every value inside FM TONE's range");
ok(qc.notes.some((n) => /LFO 1 destination #5 not mapped yet: off/.test(n)), "notes: an unmapped LFO destination is off");
const fw = firmwareOk([q.blob, qc.blob, dnsndToQuad(await parseDnsnd(dnsnd(record("ZERO")))).blob]);
if (fw) ok(fw.join() === "ok,ok,ok", "firmware: quad_blob_ok takes the three blobs");
// the import: a sound in the template's record, placed in the first free slot
const s = soundFromDnsnd(p);
ok(recordValid(s.record) && s.engine === 15 && s.kind === "quad" && s.name === "MY TEST TONE" && s.record[16 + 39] === 64 - 5 &&
   s.record[16 + 83] === 64 + 6 && s.record[16 + 84] === 64 + 7 && s.record[175] === 0xA6 && s.record[191] === 0, "import: an FM TONE record (name, PAN, macros, added)");
ok(soundFromDnsnd(await parseDnsnd(dnsnd(record("A NAME LONGER THAN 12")))).name === "A NAME LONGE" &&
   soundFromDnsnd(await parseDnsnd(dnsnd(record("")))).name === "DIGITONE", "import: a long name cut to 12, an empty one DIGITONE");
const blank = new Map(SOUND_IDS.map((id) => [id, new Uint8Array(0)]));
const slot = firstFreeSlot(blank), r = placeSound(blank, slot, s);
const row = parseSoundObjects(r.objs).slots[slot - 1];
ok(r.changed.map((c) => c.id).join() === "22,6" && row.used && row.engineName === "FM TONE" && row.patch === "quad" && row.name === "MY TEST TONE" &&
   r.objs.get(22).subarray(16 + (slot - 1) * 80, 16 + slot * 80).every((x, i) => x === q.blob[i]), "import: placed in U01 (the FM TONE store, then the bank)");
const items = await libraryItemsFromDnsnd("t.dnsnd", file);
ok(items.length === 1 && items[0].engine === 15 && items[0].patch && items[0].notes.length === q.notes.length && items[0].file.dnsnd.notes.length === q.notes.length,
   "manager: a library item with its notes");
// errors
const rec0 = record("X");
ok(await rejects(parseDnsnd(new Uint8Array([1, 2, 3, 4, 5])), /not a Digitone sound file \(not a ZIP/), "error: not a ZIP");
ok(await rejects(parseDnsnd(zip([["other.txt", new Uint8Array(3)]])), /no manifest\.json/), "error: no manifest");
ok(await rejects(parseDnsnd(zip([["manifest.json", new TextEncoder().encode("{x")]])), /not JSON/), "error: a manifest that is not JSON");
ok(await rejects(parseDnsnd(dnsnd(rec0, { man: { FileType: "Pattern" } })), /FileType "Pattern"/), "error: not a Sound");
ok(await rejects(parseDnsnd(dnsnd(rec0, { man: { Payload: "GONE" } })), /payload "GONE" is missing/), "error: the payload missing");
ok(await rejects(parseDnsnd(dnsnd(rec0, { hdrMagic: [1, 2, 3, 4] })), /payload's magic/), "error: the payload's magic");
ok(await rejects(parseDnsnd(dnsnd(rec0, { size: 400 })), /a newer Digitone format \(a 400-byte sound/), "error: another record size");
ok(await rejects(parseDnsnd(dnsnd(record("X", {}, { magic: [1, 2, 3, 4] }))), /the sound's magic/), "error: the record's magic");
ok(await rejects(parseDnsnd(dnsnd(record("X", {}, { version: 2 }))), /a newer Digitone format \(version 2\): not supported/), "error: record version 2");
ok(await rejects(parseDnsnd(dnsnd(rec0, { block: lz4Literal(rec0).subarray(0, 100) })), /cut short/), "error: the LZ4 block cut short");
ok(await rejects(parseDnsnd(dnsnd(rec0, { block: Uint8Array.from([0x10, 7, 9, 0]) })), /does not decompress/), "error: an LZ4 match before the start");
// warnings
const w = await parseDnsnd(dnsnd(rec0, { man: { FirmwareVersion: "1.50", ProductType: ["31"] } }));
ok(w.warnings.length === 2 && /firmware 1\.50/.test(w.warnings[0]) && /product \["31"\]/.test(w.warnings[1]), "warning: another firmware, another product (read)");
// the LZ4 decoder with a match (an overlapping copy)
const m = dnLz4Block(Uint8Array.from([0x26, 1, 2, 1, 0, 0x30, 7, 8, 9]), 0, 15).data;
ok([...m].join() === "1,2,2,2,2,2,2,2,2,2,2,2,7,8,9", "lz4: literals, an overlapping match, the last literals");

console.log(fails ? `DNSND TESTS FAILED (${fails})` : "dnsnd tests passed");
process.exit(fails ? 1 : 0);
