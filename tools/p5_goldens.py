#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""P5's golden renders and CPU entries alone, after assets/prophet5-factory/shipped.txt changed (docs/PROPHET.md "The
shipped list"):

    python3 tools/p5_goldens.py            tests/golden.txt and tests/cpu_baseline.txt: P5's lines made again
    python3 tools/p5_goldens.py --check    nothing written: what would change

P5's lines (preset/P5/NNN_NAME, cpu/P5/NNN_NAME: NNN is the program's Sequential number, 000 INIT; tests/regress.c)
are replaced by this build's, in the pool's order, where P5's block stands in each file. Every other line stays as it
is, byte for byte: GOLDEN_UPDATE=1 / BUDGET_UPDATE=1 on tests/run_tests.sh would write every engine's CPU count again
(they move about 1 % from run to run). If a render of ANOTHER engine differs from its committed line the tool stops and
writes nothing: that is not a change of the list.

It generates the header (tools/gen_prophet_factory.py), builds tests/regress.c as tests/run_tests.sh does and runs it
with GOLDEN_UPDATE=1 BUDGET_UPDATE=1 on copies of the two files in build/scratch/p5_goldens."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
W = ROOT / "build/scratch/p5_goldens"
MINE = ("preset/P5/", "cpu/P5/", "preset/PROPHET/", "cpu/PROPHET/")       # (PROPHET: the engine's name before 0.22's P5)


def lines(path):
    return path.read_text().splitlines()


def merge(old, new, what):
    """old's lines with its P5 block replaced by new's; the other entries must be new's too (goldens only)"""
    mine = [x for x in new if x.startswith(MINE)]
    out, placed = [], False
    for x in old:
        if x.startswith(MINE):
            if not placed:
                out += mine
                placed = True
            continue
        out.append(x)
    if not placed:
        out += mine
    if what == "golden":
        a = [x for x in old if not x.startswith(MINE) and not x.startswith("#")]
        b = [x for x in new if not x.startswith(MINE) and not x.startswith("#")]
        if a != b:
            diff = sorted(set(a) ^ set(b))
            sys.exit("p5_goldens.py: renders of OTHER engines differ from tests/golden.txt (nothing written):\n  " +
                     "\n  ".join(diff[:12]))
    return out


def main():
    check = sys.argv[1:] == ["--check"]
    if sys.argv[1:] and not check:
        sys.exit(__doc__)
    os.chdir(ROOT)
    W.mkdir(parents=True, exist_ok=True)
    run = lambda *c, **k: subprocess.run(c, check=True, **k)
    run(sys.executable, "tools/gen_prophet_factory.py", "build/gen/melodee_prophet_factory.h")
    run("sh", "tests/hostcc.sh", "cc", "-O1", "-Wall", "-Wno-unused-function", "-O2", "-w", "-Ibuild/gen", "-Ifirmware/src",
        "-o", "build/host/regress", "tests/regress.c", "-lm")
    for f in ("golden.txt", "cpu_baseline.txt"):
        shutil.copyfile(ROOT / "tests" / f, W / f)
    env = dict(os.environ, GOLDEN_UPDATE="1", BUDGET_UPDATE="1")
    r = subprocess.run(["build/host/regress", str(W / "golden.txt"), str(W / "cpu_baseline.txt")], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (W / "regress.log").write_text(r.stdout)
    if r.returncode:
        sys.stdout.write(r.stdout[-3000:])
        sys.exit(f"p5_goldens.py: the regression failed for another reason than the goldens ({W / 'regress.log'})")
    for f, what in (("golden.txt", "golden"), ("cpu_baseline.txt", "cpu")):
        old, new = lines(ROOT / "tests" / f), lines(W / f)
        out = merge(old, new, what)
        was = {x.split()[0]: x for x in old if x.startswith(MINE)}
        now = {x.split()[0]: x for x in out if x.startswith(MINE)}
        added = [k for k in now if k not in was]
        gone = [k for k in was if k not in now]
        changed = [k for k in now if k in was and was[k] != now[k]]
        print(f"tests/{f}: {len(now)} P5 entries ({len(added)} new, {len(gone)} gone, {len(changed)} changed), "
              f"{len(out) - len(now)} other lines untouched" + (" [--check: not written]" if check else ""))
        if what == "golden":
            for k in changed:
                print(f"  changed: {k}")
        if not check and out != old:
            (ROOT / "tests" / f).write_text("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
