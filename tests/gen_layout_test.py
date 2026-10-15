#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""The layout generator (tools/gen_layout.py, docs/LAYOUT-TOOL.md "Step 4, the firmware: a plan" (e)):

  python3 tests/gen_layout_test.py          (from the repo root; needs nothing built; node for the JS comparison)

- the shipped designs: p5-panel.json compiles (8 screens, 73 elements, 19 lanes, all 56 P5 parameters once, the byte
  cost printed); p5-osc-panel.json is refused (flat, classic screens in OSC .. MOD);
- web/test_layout.mjs 6's broken designs, made the same way from p5-osc-panel.json: each refused with its own reason
  (an error the unbroken file does not have, matching the reason's words);
- the generator's own refusals (the firmware's rules: the grid, the area, the budgets, the lanes, a parameter once, a
  pair apart, a band element, a classic screen in OSC .. MOD, the flat style, FX and MIX as today ..) on p5-panel.json;
- the same rules as validateDesign: the generator's checks without the build's (validate(.., built=False)) give
  validateDesign's errors word for word on every design here, and with them validateDesign(.., {build: true})'s
  (node, web/fm1layout.js)."""
import copy
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_layout as G  # noqa: E402

checks = fails = 0


def ok(cond, what):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print("FAIL", what)


def load(name):
    return json.loads((ROOT / "design/layouts/designs" / name).read_text(encoding="utf-8"))


cat = G.load_catalog("P5")
panel, osc = load("p5-panel.json"), load("p5-osc-panel.json")


def refusals(d):
    try:
        G.compile_design(d, cat)
        return []
    except ValueError as e:
        return e.args[0]


# ------------------------------------------------------------------------------------- the shipped designs ---
c = G.compile_design(copy.deepcopy(panel), cat)
n = G.cost(c)
ok((n["screens"], n["elements"], n["lanes"]) == (8, 73, 19), f"p5-panel: 8 screens, 73 elements, 19 lanes ({n})")
addr = [(r[5], r[6]) for s in c["screens"] for r in s["elements"] if r[5] != G.NONE]
ok(len(addr) == 56 and len(set(addr)) == 56 and all(p < 19 and col < 4 for p, col in addr),
   f"p5-panel: {len(addr)} bound elements, all 56 P5 parameters once, each within its 19 pages")
total = n["bytes"] + n["pool"]
ok(n["bytes"] == 73 * 14 + 8 * 38 + 8 and 300 < n["pool"] < 700 and total < 2048,
   f"p5-panel: {total} B (the plan's estimate: about 1.8 KB)")
print(f"gen_layout_test: p5-panel {n['elements']} x 14 + {n['screens']} x 38 + pool {n['pool']} + 8 = {total} B of flash")
ok(c["screens"][3]["name"] == "ENV 1 · amp" and b"ENV 1 \xb7 amp\0" in c["pool"].data, "a title's middle dot as Latin-1 (0xB7)")
osc_base = refusals(copy.deepcopy(osc))
ok(any("p5 style only" in e for e in osc_base) and any("a classic screen in FILT" in e for e in osc_base),
   "p5-osc-panel: refused (the flat style, classic screens in OSC .. MOD)")
with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp) / "cr_layouts.h"
    r = subprocess.run([sys.executable, str(ROOT / "tools/gen_layout.py"), str(out)], capture_output=True, text=True)
    text = out.read_text(encoding="latin-1") if out.exists() else ""
    ok(r.returncode == 0 and "8 screens, 73 elements, 19 lanes" in r.stdout and f"= {total} B" in r.stdout and
       "static const cr_lay_elem_t CR_LAY_ELEMS[73]" in text and "#define CR_LAY_POOL_SIZE" in text,
       f"tools/gen_layout.py writes the header and its cost line ({r.stdout.strip() or r.stderr.strip()})")

# ------------------------------------------------- web/test_layout.mjs 6: the broken designs, the same way ---
def osc_el(d, pred):
    return next(e for e in d["groups"][0]["screens"][0]["elements"] if pred(e))


def two_leds(d):
    osc_el(d, lambda e: e["type"] == "led").update(leds=2, glyph=None, param="OSC A:FREQ")


def set_(path, v):
    def f(d):
        o = d
        for k in path[:-1]:
            o = o[k]
        o[path[-1]] = v
    return f


FS = ["groups", 0, "screens", 0]           # OSC's first (free) screen
CL = ["groups", 1, "screens", 0]           # FILT's (classic) screen
BROKEN = {   # name: (how, the reason's words)
    "another format": (set_(["format"], "x"), r"not a choralroot-layout file"),
    "version 1": (set_(["version"], 1), r"format version 1"),
    "not a design": (set_(["kind"], "layout"), r'"kind" must be "design"'),
    "another params_hash": (set_(["params_hash"], "0" * 16), r"made for other parameters"),
    "another engine": (set_(["engine", "index"], 3), r"this catalog is P5"),
    "no title-line header": (set_(["header"], None), r'"header" must be'),
    "an unknown group": (set_(["groups", 1, "name"], "ARP"), r"not a group of P5"),
    "a screen of no type": (set_(CL + ["type"], "panel"), r'"type" must be "classic" or "free"'),
    "a classic lane of three cells": (lambda d: d["groups"][1]["screens"][0]["lanes"][0]["cells"].pop(), r"four cells"),
    "an unknown parameter": (set_(CL + ["lanes", 0, "cells", 0], {"param": "FILTER:NOPE"}), r'no parameter "FILTER:NOPE"'),
    "an unknown band": (set_(CL + ["band"], "rainbow"), r'band "rainbow"'),
    "an unknown element type": (set_(FS + ["elements", 1, "type"], "slider"), r'type "slider"'),
    "a position not a number": (set_(FS + ["elements", 1, "x"], "12"), r"x a whole number"),
    "two elements of one id": (lambda d: set_(FS + ["elements", 2, "id"], d["groups"][0]["screens"][0]["elements"][1]["id"])(d),
                               r"the id twice"),
    "a switch of four positions": (lambda d: osc_el(d, lambda e: e["type"] == "switch").update(positions=4), r"2 or 3 positions"),
    "an led glyph that does not exist": (lambda d: osc_el(d, lambda e: e["type"] == "led").update(glyph="star"), r'led glyph "star"'),
    "a colour that is not a token": (set_(FS + ["elements", 1, "colour"], "#ff0000"), r"not a palette token"),
    "a lane naming no element": (set_(FS + ["lanes", 0, 0], "nothing"), r'no element "nothing"'),
    "an element bound to a key": (set_(FS + ["elements", 1, "key"], 12), r"bound to a key"),
    "an unknown style": (set_(["style"], "x"), r'style "x"'),
    "two LEDs on a 0..127 parameter": (two_leds, r"two LEDs show a parameter of two values"),
}
ok(len(BROKEN) == 21, f"{len(BROKEN)} broken designs, as web/test_layout.mjs 6")
designs = {"p5-panel": panel, "p5-osc-panel": osc}
for name, (how, why) in BROKEN.items():
    d = copy.deepcopy(osc)
    how(d)
    designs["broken: " + name] = d
    new = [e for e in refusals(d) if e not in osc_base]
    ok(any(re.search(why, e) for e in new), f"broken ({name}): refused with /{why}/ (new errors: {new})")


# ------------------------------------------------------------------------------- the generator's own rules ---
def scr(d, g, s=0):
    return next(x for x in d["groups"] if x["name"] == g)["screens"][s]


def el(d, g, i, s=0):
    return next(e for e in scr(d, g, s)["elements"] if e["id"] == i)


def classic_filt(d):
    next(x for x in d["groups"] if x["name"] == "FILT")["screens"] = copy.deepcopy(osc["groups"][1]["screens"])


def many(d):
    scr(d, "OSC")["elements"] += [{"id": f"x{k}", "type": "label", "x": 4 * k, "y": 224, "w": 8, "h": 12, "text": "x"}
                                  for k in range(14)]


def fx_swap(d):
    c = scr(d, "FX")["lanes"][0]["cells"]
    c[0], c[1] = c[1], c[0]


OWN = {
    "off the grid": (lambda d: el(d, "OSC", "a_freq").update(x=9), r"not on the 4 px grid"),
    "over the title line": (lambda d: el(d, "OSC", "a_freq").update(y=24), r"outside the design area"),
    "off the right edge": (lambda d: el(d, "OSC", "fa").update(w=240), r"outside the design area"),
    "a band element": (lambda d: scr(d, "ENV")["elements"].append(
        {"id": "b", "type": "band", "x": 0, "y": 140, "w": 240, "h": 96, "band": "env"}), r"a band element: not built"),
    "a classic screen in FILT": (classic_filt, r"a classic screen in FILT"),
    "the flat style": (set_(["style"], "flat"), r"the p5 style only"),
    "no style (flat)": (lambda d: d.pop("style"), r"the p5 style only"),
    "25 elements": (many, r"25 elements: at most 24"),
    "9 lanes": (lambda d: scr(d, "OSC")["lanes"].extend([[None] * 4] * 6), r"9 lanes: 1 \.\. 8"),
    "a lane of five": (lambda d: scr(d, "OSC")["lanes"][0].append(None), r"four: KNOB 1\.\.4"),
    "an element in two lanes": (lambda d: scr(d, "OSC")["lanes"][1].__setitem__(0, "a_freq"), r"a_freq is in two lanes"),
    "a bound element in no lane": (lambda d: scr(d, "OSC")["lanes"][2].__setitem__(0, None), r"m_a .* is in no lane"),
    "a frame in a lane": (lambda d: scr(d, "OSC")["lanes"][0].__setitem__(3, "fa"), r"fa binds no parameter"),
    "a knob bound to nothing": (lambda d: el(d, "OSC", "a_freq").pop("param"), r"a knob bound to nothing"),
    "a parameter twice": (lambda d: el(d, "OSC", "b_fine", 1).update(param="OSC A:FREQ"), r"OSC A:FREQ is bound twice"),
    "a pair apart": (lambda d: (el(d, "LFO", "l_saw").update(pair="left"), el(d, "LFO", "l_tri").update(pair="right")),
                     r"no right half touching"),
    "labelAt in the middle": (lambda d: el(d, "OSC", "a_sync").update(labelAt="middle"), r'"labelAt" is "above" or "below"'),
    "a frame of r 13": (lambda d: el(d, "OSC", "fa").update(r=13), r'corner radius "r" is 0 \.\. 12'),
    "three LEDs": (lambda d: el(d, "FILT", "f_rev").update(leds=3), r'"leds" is 1 or 2'),
    "two LEDs and a glyph": (lambda d: el(d, "FILT", "f_rev").update(glyph="saw"), r"two LEDs have their names above them"),
    "a dial of 48": (lambda d: el(d, "OSC", "a_freq").update(size=48), r"a knob's dial is 28 \.\. 44"),
    "a title not Latin-1": (lambda d: scr(d, "OSC").update(title="OSC Ā"), r"not Latin-1"),
    "FX changed": (fx_swap, r"today's platform screen"),
    "a free screen in MIX": (lambda d: next(x for x in d["groups"] if x["name"] == "MIX")["screens"].append(
        {"type": "free", "title": "MORE", "elements": [], "lanes": [[None] * 4]}), r"a free screen in MIX"),
    "no ENV": (lambda d: d["groups"].pop(2), r"ENV: no screens"),
}
for name, (how, why) in OWN.items():
    d = copy.deepcopy(panel)
    how(d)
    designs["own: " + name] = d
    got = refusals(d)
    ok(any(re.search(why, e) for e in got), f"the generator refuses {name}: /{why}/ (errors: {got})")
paired = copy.deepcopy(panel)
el(paired, "LFO", "l_saw").update(pair="left")
el(paired, "LFO", "l_tri").update(pair="right", x=148)
ok(not refusals(paired), "a pair whose halves touch: built")

# -------------------------------------------------------------------- the same rules as validateDesign (JS) ---
node = shutil.which("node")
if not node:
    print("gen_layout_test: skip: no node (the comparison with web/fm1layout.js validateDesign)")
else:
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "designs.json").write_text(json.dumps(designs))
        js = (f'import * as L from {json.dumps((ROOT / "web/fm1layout.js").as_uri())};\n'
              'import fs from "fs";\n'
              f'const cat = JSON.parse(fs.readFileSync({json.dumps(str(ROOT / "design/layouts/p5.json"))}, "utf8"));\n'
              f'const ds = JSON.parse(fs.readFileSync({json.dumps(str(Path(tmp) / "designs.json"))}, "utf8"));\n'
              'const out = {};\n'
              'for (const [k, d] of Object.entries(ds)) out[k] = [L.validateDesign(d, cat).errors, L.validateDesign(d, cat, { build: true }).errors];\n'
              'console.log(JSON.stringify(out));\n')
        (Path(tmp) / "v.mjs").write_text(js)
        r = subprocess.run([node, str(Path(tmp) / "v.mjs")], capture_output=True, text=True)
    ok(r.returncode == 0, f"node ran validateDesign ({r.stderr.strip()[:200]})")
    jsv = json.loads(r.stdout) if r.returncode == 0 else {}
    for built in (False, True):
        differ = [k for k, d in designs.items() if k in jsv and G.validate(copy.deepcopy(d), cat, built=built) != jsv[k][built]]
        ok(not differ, f"the generator's checks {'with' if built else 'without'} the build's = validateDesign's"
           f"{' {build: true}' if built else ''} errors, word for word, on all {len(designs)} designs" +
           "".join(f"\n    {k}: py {G.validate(copy.deepcopy(designs[k]), cat, built=built)} js {jsv[k][built]}" for k in differ[:3]))
    ok(not jsv.get("p5-panel", [["?"]])[0] and not jsv.get("p5-panel", [0, ["?"]])[1] and all(jsv.get("broken: " + k, [0])[0] for k in BROKEN),
       "validateDesign: p5-panel valid (and buildable), each broken design refused")

print(f"gen_layout_test: {checks} checks, {fails} failed")
sys.exit(1 if fails else 0)
