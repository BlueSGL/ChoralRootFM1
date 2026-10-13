// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
//
// Node checks of the device manager (web/fm1manager.js, web/manager.html; docs/DEVICE-MANAGER.md), no browser, no
// hardware: the simulated FM-1 of ?sim=1 (fm1manager.js makeSimFM1, web/test_installer.mjs's backup side) read and
// written through the session. Run from the repo root: node web/test_manager.mjs
// - the pools per engine in the firmware's order (INIT, factory, overwritten, added); the loop slots' headers
// - Place new / over a factory preset / over a user sound / of another engine; Place all; Keep; Swap (two user sounds,
//   a user sound and a factory preset, two loops); Rename; Delete (a factory preset comes back); loops placed, kept,
//   deleted; Undo restores each step (written back, read back); busy (rc 3) refused with "Stop the loop" and no undo step
// - the library: files (sound, loop, backup, .syx, pack), the store (in memory in node), the pack index and every pack
// - audition: program change and notes; the page's shape and its script as make_site.py inlines it

import { readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";
import { captureBackup, loopHeader } from "./fm1backup.js";
import { FACTORY_PRESETS, FACTORY_FIRST, readSoundFile, exportSyx, parseSoundObjects, recordValid } from "./fm1sounds.js";
import {
  selectOnUnit, selectRequest, playingText, managerDrop, factorySoundFile, factoryFiles, usedSlots, makePack, packFileName, userSoundsPack, renameSoundFile, MANAGER_ENGINES, managerPools, managerLoops, loopSummary, managerPlace, managerPlaceAll, managerSwap, managerRename, managerDelete,
  managerKeep, keepLoop, readLoopFile, placeLoop, deleteLoop, swapLoops, ManagerSession, makeSimFM1, simObjects, makeLoopRecord,
  libraryItemsFromFile, LibraryStore, loadPackIndex, loadPack, auditionMessages, noteMessage, midiText, writeErrorText, UNDO_DEPTH, retiredNote,
} from "./fm1manager.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(76)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const HERE = new URL(".", import.meta.url).pathname;
const sameObjs = (a, b) => [...a.keys()].every((id) => { const x = a.get(id), y = b.get(id) || new Uint8Array(0); return x.length === y.length && x.every((v, i) => v === y[i]); });
const names = (rows) => rows.map((r) => `${r.pos}:${r.name}:${r.type}${r.slot ? ":U" + r.slot : ""}`);

const P = (o) => managerPools(o, { empty: false });   // (the pools without the "Place here" row)
/* ------------------------------------------------------------- the pools --- */
const objs0 = simObjects();
const pools = P(objs0);
ok(MANAGER_ENGINES.every((e) => pools[e] && pools[e][0].type === "init" && pools[e][0].pos === 0), "pools: every engine's pool starts with 00 INIT");
ok(MANAGER_ENGINES.every((e) => pools[e].filter((r) => r.type !== "init" && r.f >= 0).length === FACTORY_PRESETS[e].length - FACTORY_FIRST[e]),
  "pools: the factory positions of each engine (CZ-1 without INIT TONE)");
const fm6 = pools[12];
ok(fm6[1].type === "factory" && fm6[1].name === "TINE EP" && fm6[2].type === "user" && fm6[2].name === "MY TINE" && fm6[2].overwrites === "FM BELL" &&
   fm6[2].slot === 1, "pools: FM6 02 is U01 MY TINE over FM BELL (overwritten)");
ok(fm6.length === 27 && fm6[26].name === "BELL TOO" && fm6[26].slot === 5 && fm6[26].f === -1, "pools: FM6 26 is the added U05 BELL TOO, after the 25 factory");
ok(pools[14][13].slot === 9 && pools[14][13].overwrites === "SYNTH.BASS" && pools[14].at(-1).name === "CZ GLASS", "pools: CZ-1 13 over SYNTH.BASS, CZ GLASS added at the end");
ok(pools[13].at(-1).name === "WARM VA" && pools[7][1].name === "WHEEL BOW" && pools[7][1].overwrites === "FULL ORGAN", "pools: VA's added, WHEEL 01 overwritten");
ok(pools.loops === undefined && pools[15].every((r) => r.type !== "user"), "pools: FM TONE has no user sound");

/* ------------------------------------------------------------- the loops --- */
const loops = managerLoops(objs0);
ok(loops.length === 10 && loops.filter((l) => l.used).map((l) => l.slot).join() === "1,4", "loops: ten slots, 1 and 4 hold loops");
const h1 = loops[0].header, h4 = loops[3].header;
ok(h1.valid && h1.len === 4 * 384 && h1.bars === 4 && h1.nlayers === 3 && h1.nev === 24 && h1.ppqn === 96 && h1.sigName === "4/4",
  "loops: slot 1 header (len 1536 ticks = 4 bars at 96 ppqn, 3 layers, 24 events)");
ok(h4.valid && h4.bars === 2 && h4.sigName === "3/4" && loopSummary(h4) === "2 bars 3/4 · 1 layer · 6 events", "loops: slot 4 in 3/4, its summary");
ok(loopHeader(new Uint8Array(0)) === null && !loopHeader(new Uint8Array(20)).valid && loopSummary(null) === "empty", "loops: empty and unreadable records");
const lf = keepLoop(objs0, 1, { created: "2026-10-10T00:00:00Z" });
ok(lf.format === "choralroot-loop" && lf.bars === 4 && lf.nlayers === 3 && readLoopFile(JSON.stringify(lf)).bytes.length === h1.size, "loops: Keep -> a choralroot-loop file, read back");

/* --------------------------------------------- the session on the simulator --- */
const sim = makeSimFM1();
const s = new ManagerSession(sim.request);
await s.read();
ok(sameObjs(objs0, s.objs) && !s.canUndo, "session: the simulated FM-1 read (sounds and loops), no undo yet");
const snaps = [new Map(s.objs)];
const step = async (what, res, check) => {
  const before = sim.log.length;
  await s.apply(res);
  snaps.push(new Map(s.objs));
  ok(sim.log.length > before && check(s.objs), `${what} (written: ${sim.log.slice(before).join(" ")}; read back)`);
};
const packIdx = await loadPackIndex(async (url) => JSON.parse(readFileSync(join(HERE, url), "utf8")));
const packs = {};
for (const p of packIdx) packs[p.engine] = await loadPack(async (url) => JSON.parse(readFileSync(join(HERE, url), "utf8")), p);
const vaPad = packs[13][0], fmBass = packs[12][2], czBrass = packs[14][0];

await step("place new: a VA pack sound with nothing selected -> U04, added to VA", managerPlace(s.objs, vaPad.file, null), (o) => {
  const r = P(o)[13].at(-1); return r.slot === 4 && r.name === "LUSH PAD" && r.f === -1;
});
await step("place over factory: FM BASS onto FM6 04 BRASS -> U07 bound to BRASS", managerPlace(s.objs, fmBass.file, P(s.objs)[12][4]), (o) => {
  const r = P(o)[12][4]; return r.type === "user" && r.slot === 7 && r.name === "FM BASS" && r.overwrites === "BRASS";
});
await step("place over user: a CZ-1 sound onto CZ-1 13 (U09) -> U09 replaced, still over SYNTH.BASS", managerPlace(s.objs, czBrass.file, P(s.objs)[14][13]), (o) => {
  const r = P(o)[14][13]; return r.slot === 9 && r.name === "BRASS 1" && r.overwrites === "SYNTH.BASS";
});
await step("place of another engine: a CZ-1 sound with FM6 02 selected -> its own pool, added", managerPlace(s.objs, packs[14][5].file, P(s.objs)[12][2]), (o) => {
  const p = P(o); return p[14].at(-1).name === packs[14][5].name && p[14].at(-1).f === -1 && p[12][2].name === "MY TINE";
});
const keep = managerKeep(s.objs, P(s.objs)[12][2], { firmware: "ChoralRoot 0.15" });
ok(readSoundFile(keep).name === "MY TINE" && keep.binding && keep.binding.overwrites === 2, "keep: FM6 02 -> a choralroot-sound file (binding FM6 02)");
await step("swap two user sounds: FM6 02 (U01) and FM6 26 (U05) exchange places", managerSwap(s.objs, P(s.objs)[12][2], P(s.objs)[12].find((r) => r.slot === 5)), (o) => {
  const p = P(o)[12]; return p[2].name === "BELL TOO" && p[2].overwrites === "FM BELL" && p.at(-1).name === "MY TINE";
});
await step("swap a user sound with a factory preset: the added MY TINE onto FM6 06 MARIMBA", managerSwap(s.objs, P(s.objs)[12].at(-1), P(s.objs)[12][6]), (o) => {
  const p = P(o)[12]; return p[6].name === "MY TINE" && p[6].overwrites === "MARIMBA" && p.length === 26;
});
await step("rename: FM6 06 -> SOFT TINE", managerRename(s.objs, P(s.objs)[12][6], "SOFT TINE"), (o) => P(o)[12][6].name === "SOFT TINE");
await step("delete an overwriting sound: FM6 06 resets to MARIMBA", managerDelete(s.objs, P(s.objs)[12][6]), (o) => {
  const r = P(o)[12][6]; return r.type === "factory" && r.name === "MARIMBA";
});
await step("loops: the kept loop placed into slot 2", placeLoop(s.objs, 2, lf), (o) => managerLoops(o)[1].used && managerLoops(o)[1].header.nev === 24);
await step("loops: swap slots 1 and 4", swapLoops(s.objs, 1, 4), (o) => managerLoops(o)[0].header.sigName === "3/4" && managerLoops(o)[3].header.bars === 4);
await step("loops: delete slot 2", deleteLoop(s.objs, 2), (o) => !managerLoops(o)[1].used && sim.objs.get(41).length === 0);
const free = parseSoundObjects(s.objs).slots.filter((x) => !x.used).length;
const all = managerPlaceAll(s.objs, packs[12].map((it) => it.file));
ok(all.placed === free && all.skipped === packs[12].length - free, `place all: ${all.placed} placed into the free slots, ${all.skipped} skipped`);
await step("place all: written, every user slot used", all, (o) => parseSoundObjects(o).slots.every((x) => x.used));
let fullErr = null; try { managerPlace(s.objs, vaPad.file, null); } catch (e) { fullErr = e; }
ok(fullErr && /All 32 user slots/.test(fullErr.message), "place new with 32 slots used: refused");

// busy: a write while the loop plays is refused, nothing kept for undo
sim.playing = true;
const depth = s.history.length;
let busy = null; try { await s.apply(managerRename(s.objs, P(s.objs)[12][2], "BUSY")); } catch (e) { busy = e; }
ok(busy && busy.rc === 3 && /Stop the loop on the FM-1 first/.test(writeErrorText(busy)) && s.history.length === depth, "busy (rc 3): refused, \"Stop the loop on the FM-1 first\", no undo step");
sim.playing = false;

// undo every step back to the start
let undoOk = true;
for (let k = snaps.length - 2; k >= 0; k--) {
  await s.undo();
  if (!sameObjs(snaps[k], s.objs) || !sameObjs(snaps[k], sim.objs)) { undoOk = false; console.log(`  undo to step ${k} differs`); }
}
ok(undoOk && !s.canUndo && sameObjs(objs0, sim.objs), `undo: ${snaps.length - 1} steps undone one by one, the FM-1 back to its first state`);
const s2 = new ManagerSession(makeSimFM1().request); await s2.read();
for (let k = 0; k < UNDO_DEPTH + 3; k++) await s2.apply(managerRename(s2.objs, P(s2.objs)[12][2], `N${k}`));
ok(s2.history.length === UNDO_DEPTH, `undo: at most ${UNDO_DEPTH} steps kept`);

/* ------------------------------------- capacity, packs of one's own, factory keep, reorder --- */
const fpacks = factoryFiles(MANAGER_ENGINES.map((e) => ({ engine: e, sounds: packs[e].map((it) => it.file) })));
const o30 = simObjects(30);
ok(usedSlots(o30) === 30 && MANAGER_ENGINES.every((e) => managerPools(o30)[e].at(-1).type === "empty"), "capacity: the 30-sound simulator; every pool ends with an empty \"Place here\" row");
const fit = managerPlaceAll(o30, packs[13].slice(0, 5).map((it) => it.file));
ok(fit.placed === 2 && fit.skipped === 3 && usedSlots(fit.objs) === 32, "capacity: Place all 5 into 2 free slots -> 2 placed, 3 did not fit");
let capErr = null; try { managerPlace(fit.objs, packs[7][0].file, managerPools(fit.objs)[7][2]); } catch (e) { capErr = e; }
ok(MANAGER_ENGINES.every((e) => managerPools(fit.objs)[e].at(-1).type !== "empty") && capErr && /All 32 user slots/.test(capErr.message),
  "capacity: 32 used -> no empty rows, Place over a factory preset refused (no free slot)");
ok(managerPlace(o30, packs[7][0].file, managerPools(o30)[7].at(-1)).where === "new", "capacity: Place onto the empty row -> a new added sound");
const tine = managerKeep(objs0, P(objs0)[12][1], { packs: fpacks });
ok(tine.factory === true && tine.engine === 12 && tine.position === 1 && readSoundFile(tine).name === "TINE EP", "factory keep: FM6 01 TINE EP -> its pack entry, factory: true, engine 12, position 1");
const back = managerPlace(objs0, tine, null);
ok(back.where === "over-factory" && P(back.objs)[12][1].type === "user" && P(back.objs)[12][1].overwrites === "TINE EP", "factory keep: placed with nothing selected -> back over its own position");
const sel = [P(objs0)[12][1], P(objs0)[12][2], P(objs0)[14][13]].map((r) => managerKeep(objs0, r, { packs: fpacks }));
const mine = makePack(sel, { name: "Picks", author: "me", created: "2026-10-10T00:00:00Z" });
const mineItems = libraryItemsFromFile(packFileName("Picks"), JSON.stringify(mine));
ok(mine.format === "choralroot-pack" && mineItems.length === 3 && mineItems.map((i) => i.name).join() === "TINE EP,MY TINE,CZ BASS 2", "pack: three selected rows (factory + user) saved as ONE pack, read back as 3 items");
const all6 = userSoundsPack(objs0, { created: "2026-10-10T00:00:00Z" });
const blank = new Map([...objs0.keys()].map((id) => [id, new Uint8Array(0)]));
const reloaded = managerPlaceAll(blank, libraryItemsFromFile("u.json", JSON.stringify(all6)).map((i) => i.file));
ok(all6.sounds.length === 6 && reloaded.placed === 6 && P(reloaded.objs)[12][2].name === "MY TINE" && P(reloaded.objs)[14][13].name === "CZ BASS 2",
  "pack: Save all user sounds -> 6 in one file; loaded back, overwrites return to their positions");
let rn = renameSoundFile(tine, "MY EP"); let longErr = null; try { renameSoundFile(tine, "THIRTEEN CHAR"); } catch (e) { longErr = e; }
ok(readSoundFile(rn).name === "MY EP" && longErr, "library rename: a file renamed (12-character rule)");
const two = managerPlace(objs0, packs[12][3].file, null).objs;          // FM6: U04 BRASS (added), U05 BELL TOO (added)
const pre = P(two)[12], a1 = pre.findIndex((r) => r.slot === 4), a2 = pre.findIndex((r) => r.slot === 5);
const d1 = managerDrop(two, pre[a2], pre[a1]).objs;
ok(P(d1)[12][a1].name === "BELL TOO" && P(d1)[12][a1].slot === 4 && P(d1)[12][a2].name === "BRASS", "reorder: an added sound dragged onto another -> their slots swap, the order follows");
const d2 = managerDrop(objs0, P(objs0)[12][2], P(objs0)[12][5]).objs;
ok(P(d2)[12][5].name === "MY TINE" && P(d2)[12][5].overwrites === "FM PAD" && P(d2)[12][2].type === "factory", "reorder: a user sound dragged onto a factory position -> bound there, its old place back to factory");
const d3 = managerDrop(objs0, P(objs0)[12][7], P(objs0)[12].at(-1)).objs;
ok(P(d3)[12][7].name === "BELL TOO" && P(d3)[12][7].overwrites === "FM ORGAN", "reorder: a factory row dragged onto an added sound -> the sound takes that position");
const st2 = new LibraryStore(null); const k2 = await st2.add(items0()); const it2 = (await st2.all())[0];
await st2.put({ ...it2, name: "MY EP", file: rn }); 
ok((await st2.all())[0].name === "MY EP" && (await st2.all()).length === 1, "library rename: This browser's entry renamed in place");
function items0() { return libraryItemsFromFile("k.json", JSON.stringify(tine))[0]; }

/* ---------------------------------------------------------- the library --- */
const items = libraryItemsFromFile("k.json", JSON.stringify(keep));
ok(items.length === 1 && items[0].type === "sound" && items[0].pool === 12 && items[0].name === "MY TINE", "library: a sound file -> one FM6 item");
ok(libraryItemsFromFile("l.json", JSON.stringify(lf))[0].header.bars === 4, "library: a loop file -> a loop item");
const bsim = makeSimFM1();
const backup = await captureBackup(bsim.request, "ChoralRoot 0.15");
const bItems = libraryItemsFromFile("b.json", JSON.stringify(backup));
ok(bItems.filter((i) => i.type === "sound").length === 6 && bItems.filter((i) => i.type === "loop").length === 2, "library: a whole backup -> its 6 sounds and 2 loops");
const syx = exportSyx(objs0, 2);
const sItems = libraryItemsFromFile("t.syx", syx.bytes);
ok(sItems.length === 1 && sItems[0].engine === 14 && sItems[0].patch, "library: a CZ-1 .syx -> a CZ-1 item with its tone");
let bad = null; try { libraryItemsFromFile("x.json", "{}"); } catch (e) { bad = e; }
ok(bad && /not a sound, loop, pack, backup or \.syx/.test(bad.message), "library: an unknown file is refused");
const store = new LibraryStore(null);
const key = await store.add(items[0]); await store.add(sItems[0]);
const got = await store.all();
await store.remove(key);
ok(got.length === 2 && (await store.all()).length === 1, "library: the store (in memory in node): add, list, remove");

/* ------------------------------------------------------------- the packs --- */
ok(packIdx.length === MANAGER_ENGINES.length && packIdx.map((p) => p.engine).join() === MANAGER_ENGINES.join(), "packs: the index lists one pack per engine, in the picker's order");
ok(MANAGER_ENGINES.every((e) => packs[e].length === FACTORY_PRESETS[e].length - FACTORY_FIRST[e] &&
   packs[e].every((it, k) => it.name === FACTORY_PRESETS[e][k + FACTORY_FIRST[e]] && it.engine === e && recordValid(readSoundFile(it.file).record))),
  "packs: every pack holds its engine's factory presets, named and valid");
ok([12, 13, 14, 15].every((e) => packs[e].every((it) => it.patch)) && [7].every((e) => packs[e].every((it) => !it.patch)),
  "packs: VA, FM6, CZ-1 and FM TONE carry their patches; the others are records (macros) only");
{ // 0.18: PHASE, LOFI, VOICE, TRIO, NOISE, then PHYS retired: no pool, no pack; a record of one is an added row of the VA's pool
  const old = { record: Uint8Array.from(readSoundFile(packs[7][0].file).record), name: "PD BRASS", engine: 2, kind: null, patch: null };
  old.record[2] = 2;
  const withOld = managerPlace(objs0, old, null), row = P(withOld.objs)[13].at(-1);
  ok(MANAGER_ENGINES.join() === "12,15,13,14,7" && [2, 3, 5, 6, 9, 11].every((e) => !(e in pools) && !(e in packs)) && Object.keys(packs).length === 5,
    "0.18: five engines (FM6, FM TONE, VA, CZ-1, WHEEL), five packs");
  { // .. and a PHYS sound (a 0.17 slot, an old pack's file): the same
    const ph = { record: Uint8Array.from(readSoundFile(packs[7][0].file).record), name: "HARP", engine: 9, kind: null, patch: null };
    ph.record[2] = 9;
    const withPh = managerPlace(objs0, ph, null), prow = P(withPh.objs)[13].at(-1);
    ok(withPh.where === "new" && prow.name === "HARP" && prow.engine === 13 && prow.recEngine === 9 && prow.f === -1 &&
       retiredNote(prow.recEngine) === "PHYS · retired" && parseSoundObjects(withPh.objs).slots[prow.slot - 1].engine === 9 &&
       managerKeep(withPh.objs, prow).engineName === "PHYS",
      "0.18: a PHYS sound: an added row of the VA's pool, \"PHYS · retired\", its record kept");
  }
  ok(withOld.where === "new" && row.name === "PD BRASS" && row.type === "user" && row.engine === 13 && row.recEngine === 2 && row.f === -1 &&
     retiredNote(row.recEngine) === "PHASE · retired" && retiredNote(13) === "" && retiredNote(undefined) === "" &&
     parseSoundObjects(withOld.objs).slots[row.slot - 1].engine === 2 && managerKeep(withOld.objs, row).engineName === "PHASE",
    "0.18: a PHASE sound: an added row of the VA's pool, \"PHASE · retired\", its record kept (Keep gives the PHASE file back)");
}

/* ------------------------------------------------------------ audition --- */
ok(midiText(auditionMessages(fm6[2])[0]) === "C0 02" && midiText(auditionMessages(fm6[2], { bass: true })[0]) === "C1 03",
  "audition: program change of the pool position (bass: channel 2, position + 1)");
ok(midiText(noteMessage(true, 60)) === "90 3C 64" && midiText(noteMessage(false, 60, { bass: true })) === "81 3C 00", "keyboard: note on ch 1 velocity 100, note off ch 2");

const ssim = makeSimFM1();
const pad = P(objs0)[12][5];
ok(midiText(selectRequest(pad)[1]) === "00 0C 05" && midiText(selectRequest(pad, { bass: true })[1]) === "01 0C 05", "select: SysEx 76 part, engine, position (chord 0, bass 1)");
const sel1 = await selectOnUnit(ssim.request, pad);
ok(sel1.ok && ssim.selected.engine === 12 && ssim.selected.pos === 5 && playingText(pad) === "FM6 · 05 FM PAD on the unit", "select: the unit switches to FM6 05 FM PAD; the page says what plays");
ok((await selectOnUnit(ssim.request, { engine: 4, pos: 0 })).rc === 1 && (await selectOnUnit(ssim.request, { engine: 12, pos: 99 })).rc === 2, "select: bad engine rc 1, bad position rc 2");
ssim.playing = true; const selBusy = await selectOnUnit(ssim.request, pad); ssim.playing = false;
ok(selBusy.rc === 3, "select: busy rc 3");
ssim.select = false;
ok((await selectOnUnit(ssim.request, pad)).unsupported === true, "select: an older firmware (no reply) -> unsupported (the program change fallback)");
ok(/manager_select|selectOnUnit/.test(readFileSync(join(HERE, "manager.html"), "utf8")) && readFileSync(join(HERE, "manager.html"), "utf8").includes("reselect("), "page: rows select on the unit, writes re-select the affected row");

/* -------------------------------------------------------------- the page --- */
const html = readFileSync(join(HERE, "manager.html"), "utf8");
const count = (sub) => html.split(sub).length - 1;
ok(count("/*LIB*/") === 1 && count("/*PACKS*/null") === 1 && count('<script type="module">') === 1, "page: /*LIB*/ and /*PACKS*/null once, one module script");
ok(["eng-tabs", "dev-rows", "lib-rows", "cap", "dev-fw", "keep-all", "sel-all", "save-pack", "save-users", "lib-src", "place", "keep", "place-all", "rename", "swap", "delete", "save-file", "backup", "restore", "undo", "kb", "bass"]
  .every((id) => html.includes(`id="${id}"`)), "page: the panes, Place / Keep, the bottom bar, the keyboard");
ok(html.indexOf('id="lib"') < html.indexOf('id="dev"') && html.includes("Place →") && html.includes("← Keep") && html.includes("draggable") && html.includes("shiftKey") && html.includes("metaKey") && html.includes("simObjects(30)"),
  "page: Library left, FM-1 right (Place →, ← Keep), multi-select, drag and drop, the 30-sound simulator");
ok(html.includes('"ChoralRoot FM-1"') && html.includes("onstatechange") && html.includes('get("sim") === "1"'), "page: the port name, hot-plug, ?sim=1");
ok(!/<link\s/i.test(html) && !/src="http/.test(html), "page: no external assets");
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const lib = ["fm1backup.js", "fm1sounds.js", "fm1manager.js"].map((f) => strip(readFileSync(join(HERE, f), "utf8"))).join("\n");
const script = html.slice(html.indexOf('<script type="module">') + 22, html.lastIndexOf("</script>")).replace("/*LIB*/", lib);
let compiled = null; try { new vm.Script(`(async () => {${script}\n})`); compiled = true; } catch (e) { compiled = e; }
ok(compiled === true, `page: the script as make_site.py inlines it compiles (no duplicate names)${compiled === true ? "" : ": " + compiled.message}`);
const site = readFileSync(join(HERE, "make_site.py"), "utf8");
ok(site.includes('"manager.html"') && site.includes("fm1manager.js") && site.includes('"packs"'), "make_site.py: emits manager.html with fm1manager.js and the packs");

if (failed) { console.log(`${failed} manager test(s) FAILED`); process.exit(1); }
console.log("manager tests passed");
