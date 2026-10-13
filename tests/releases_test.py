#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""web/releases.json: the versions the web installer offers, checked against the repository.

  python3 tests/releases_test.py          (from the repo root; needs nothing built, no network)

Every version has its notes in docs/releases/, a date, a tag, 4 to 8 bullets and a well-formed sha256; the list is
newest first without doubles; nothing older than the minimum (0.16: the boot stub) is offered; every version but
the newest says what going back to it means; the firmware's own version (firmware/src/choralroot.c) is in the list
once it is tagged. A package found in build/scratch/packages (or CR_PACKAGES_DIR) must have the listed sha256.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<72} {'ok' if cond else 'FAIL'}")
    failed += not cond
    return cond


def num(v):
    return tuple(int(x) for x in v.split("."))


def main():
    data = json.loads((ROOT / "web" / "releases.json").read_text(encoding="utf-8"))
    rel = data["releases"]
    vers = [r["version"] for r in rel]
    ok(len(rel) >= 1 and all(re.fullmatch(r"\d+\.\d+(\.\d+)?", v) for v in vers), f"versions: {', '.join(vers)}")
    ok(len(set(vers)) == len(vers) and vers == sorted(vers, key=num, reverse=True), "newest first, none twice")
    minimum = data["minimum"]["version"]
    ok(minimum == "0.16" and all(num(v) >= num(minimum) for v in vers) and "0.14" not in vers and "0.15" not in vers,
       "nothing older than 0.16 (no boot stub; the idle fault of 0.14 / 0.15)")
    ok(all(len(data["minimum"][k]) > 40 for k in ("note", "boot_safe")) and re.match(r"https://github\.com/[\w-]+/[\w-]+$", data["repo"]) is not None,
       "the minimum's note, the boot-safety note, the repository")
    for i, r in enumerate(rel):
        v = r["version"]
        notes = ROOT / "docs" / "releases" / f"{v}.md"
        ok(notes.is_file() and notes.stat().st_size > 200, f"{v}: docs/releases/{v}.md")
        try:
            d = date.fromisoformat(r["date"])
        except (KeyError, ValueError):
            d = None
        ok(d is not None and date(2026, 10, 1) <= d and (i == 0 or d <= date.fromisoformat(rel[i - 1]["date"])),
           f"{v}: a date ({r.get('date')}), not after the newer version's")
        ok(r.get("tag") == "v" + v, f"{v}: tag v{v}")
        ok(re.fullmatch(r"[0-9a-f]{64}", r.get("sha256", "")) is not None and [x["sha256"] for x in rel].count(r["sha256"]) == 1,
           f"{v}: a sha256 of its own")
        b = r.get("bullets", [])
        ok(4 <= len(b) <= 8 and all(isinstance(x, str) and 20 <= len(x) <= 240 for x in b) and bool(r.get("title")),
           f"{v}: a title and {len(b)} bullets (4 to 8, each one or two sentences)")
        ok(all(isinstance(x, str) and x for x in r.get("notes", [])), f"{v}: its notes are text")
        if i == 0:
            ok("going_back" not in r, f"{v}: the newest has no going-back note")
        else:
            g = r.get("going_back", {})
            ok(40 <= len(g.get("short", "")) <= 260 and len(g.get("points", [])) >= 2 and all(isinstance(x, str) and x for x in g.get("points", [])),
               f"{v}: what going back means (one sentence, and the points)")
    src = (ROOT / "firmware" / "src" / "choralroot.c").read_text(encoding="utf-8", errors="replace")
    m = re.search(r'FELUCCA_VERSION\s+"ChoralRoot ([0-9.]+)"', src)
    cur = m.group(1) if m else None
    tags = subprocess.run(["git", "-C", str(ROOT), "tag", "--list", "v*"], capture_output=True, text=True).stdout.split()
    if cur and "v" + cur in tags:
        ok(cur in vers, f"the firmware's version {cur} is tagged: it is in the list")
    else:
        print(f"note: the firmware reads {cur}, not tagged here: it need not be in the list yet (docs/HANDOFF.md, 'Release steps')")
    for t in [t for t in tags if num(t[1:]) >= num(minimum)]:
        ok(t[1:] in vers, f"the tag {t} is in the list")
    pk = Path(os.environ.get("CR_PACKAGES_DIR") or ROOT / "build" / "scratch" / "packages")
    for r in rel:
        f = pk / f"choralroot-{r['version']}.fwsc"
        if f.is_file():
            ok(hashlib.sha256(f.read_bytes()).hexdigest() == r["sha256"], f"{r['version']}: {f.relative_to(ROOT) if ROOT in f.parents else f} has the listed sha256")
        else:
            print(f"skipped: {f.name} not in {pk}: its sha256 is not compared (python3 tools/fetch_packages.py)")
    print("releases: ALL OK" if not failed else f"releases: {failed} FAILED")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
