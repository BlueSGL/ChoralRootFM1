#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""Fetch the released packages the web installer offers (web/releases.json) for a local build of the site.

  python3 tools/fetch_packages.py [DIR]        (default: build/scratch/packages)

Each choralroot-X.Y.fwsc is downloaded from its GitHub release (the tag in releases.json) unless a file with the
right sha256 is already there, and checked against the sha256 in releases.json: a file that does not match is
removed and the script fails. web/make_site.py and tests/site_test.py take the folder by default (or
CR_PACKAGES_DIR / --packages); without it a local site offers only the version it was built for.
Needs the network; nothing else in the build does. The Pages job does the same with `gh release download`.
"""
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main(out):
    data = json.loads((ROOT / "web" / "releases.json").read_text(encoding="utf-8"))
    out.mkdir(parents=True, exist_ok=True)
    failed = 0
    for r in data["releases"]:
        name = f"choralroot-{r['version']}.fwsc"
        dst = out / name
        if dst.is_file() and hashlib.sha256(dst.read_bytes()).hexdigest() == r["sha256"]:
            print(f"{name}: there, sha256 ok")
            continue
        url = f"{data['repo']}/releases/download/{r['tag']}/{name}"
        try:
            with urllib.request.urlopen(url, timeout=60) as f:
                raw = f.read()
        except OSError as e:
            print(f"{name}: FAILED to download {url}: {e}")
            failed += 1
            continue
        got = hashlib.sha256(raw).hexdigest()
        if got != r["sha256"]:
            print(f"{name}: FAILED: sha256 {got}, web/releases.json says {r['sha256']} (not kept)")
            if dst.is_file():
                dst.unlink()
            failed += 1
            continue
        dst.write_bytes(raw)
        print(f"{name}: downloaded, {len(raw)} B, sha256 ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "scratch" / "packages"))
