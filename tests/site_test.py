#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site (web/make_site.py) built from the device package, then every internal link walked.

  python3 tests/site_test.py          (from the repo root, after ./build.sh; skips without build/choralroot.fwsc)

Builds build/site from build/choralroot.fwsc as version "0.1-test" and checks: the landing page at the root with
every placeholder filled, the installer with the identity and the version in its metadata and the library
inlined, the packages and the licence files next to them, no webapp/editor written; then, for every .html in
build/site, every href / src that is not http(s):, mailto:, data: or a bare #fragment resolves to a file (a
folder needs index.html), every url() in a .css file too, and every #fragment points at an id on its page.

The versions (web/releases.json): the other versions' packages come from CR_PACKAGES_DIR, else
build/scratch/packages (python3 tools/fetch_packages.py fills it; the network is needed for that only). Each one
found must be published with the listed sha256 and offered by the installer; each one missing is reported as
"skipped" and must not be offered. make_site.py's refusals are run on made-up files (a wrong sha256, a missing
version with --require-all, a version that is not listed). When build/choralroot.fwsc is itself a listed release
(the build is reproducible) and every package is there, the site is also built as the Pages job builds it
(--require-all, into build/scratch/site-release) and checked the same way.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / "build" / "choralroot.fwsc"
OUT = ROOT / "build" / "site"
VERSION = "0.1-test"
RELEASES = json.loads((ROOT / "web" / "releases.json").read_text(encoding="utf-8"))
PACKAGES = Path(os.environ.get("CR_PACKAGES_DIR") or ROOT / "build" / "scratch" / "packages")
SCRATCH = ROOT / "build" / "scratch"
NAME = f"choralroot-{VERSION}.fwsc"
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
    failed += not cond
    return cond


class Page(HTMLParser):
    """the links (href / src) and the ids of one page"""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links, self.ids = [], set()

    def handle_starttag(self, tag, attrs):
        for k, v in attrs:
            if k == "id" and v:
                self.ids.add(v)
            elif k == "name" and tag == "a" and v:
                self.ids.add(v)
            elif k in ("href", "src") and v is not None:
                self.links.append((tag, k, v.strip()))

    handle_startendtag = handle_starttag


def parse(path):
    p = Page()
    p.feed(path.read_text(encoding="utf-8", errors="replace"))
    return p


def external(url):
    return re.match(r"(?i)(https?:|mailto:|data:|javascript:)", url) is not None or url.startswith("//")


def target(page, url):
    """the file a relative url on page names (a folder: its index.html), and its fragment"""
    parts = urlsplit(url)
    frag = unquote(parts.fragment)
    if not parts.path:
        return page, frag
    if parts.path.startswith("/"):
        return None, frag                           # site-absolute paths break on a project Pages URL
    t = (page.parent / unquote(parts.path)).resolve()
    if parts.path.endswith("/") or t.is_dir():
        t = t / "index.html"
    return t, frag


def walk(skip=(), OUT=OUT):
    pages = sorted(OUT.rglob("*.html"))
    ids = {}
    bad, frags, count = [], [], 0

    def page_ids(p):
        if p not in ids:
            ids[p] = parse(p).ids if p.suffix == ".html" and p.is_file() else set()
        return ids[p]

    for page in pages:
        for tag, attr, url in parse(page).links:
            if not url or external(url):
                continue
            count += 1
            t, frag = target(page, url)
            rel = page.relative_to(OUT).as_posix()
            if t is not None and any(d in t.parents for d in skip):
                continue
            if t is None or not t.is_file() or OUT.resolve() not in t.parents:
                bad.append(f"{rel}: <{tag} {attr}=\"{url}\">")
                continue
            if frag and t.suffix == ".html" and frag not in page_ids(t):
                frags.append(f"{rel}: <{tag} {attr}=\"{url}\"> (no id \"{frag}\")")
    for css in sorted(OUT.rglob("*.css")):
        for url in re.findall(r"url\(\s*['\"]?([^'\")]+)['\"]?\s*\)", css.read_text(encoding="utf-8")):
            if external(url):
                continue
            count += 1
            t, _ = target(css, url)
            if t is None or not t.is_file():
                bad.append(f"{css.relative_to(OUT).as_posix()}: url({url})")
    return pages, count, bad, frags


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_meta(page):
    """the installer's metadata: one line, `const meta = {...};`"""
    m = re.search(r"^const meta = (\{.*\});$", page, re.M)
    try:
        return json.loads(m.group(1).replace("<\\/", "</")) if m else None
    except ValueError:
        return None


def make(args):
    return subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), *map(str, args)],
                          capture_output=True, text=True)


def versions(out, meta, want, stdout):
    """the multi-version layout: exactly the wanted packages under firmware/, each with its sha256, each offered by
    the installer with a link that resolves, SHA256SUMS and firmware/index.html agreeing"""
    fw = out / "firmware"
    names = [f"choralroot-{w['version']}.fwsc" for w in want]
    ok(sorted(p.name for p in fw.glob("*.fwsc")) == sorted(names), f"firmware/: {len(names)} package(s): {', '.join(w['version'] for w in want)}")
    ok(all((fw / n).is_file() and sha(fw / n) == w["sha256"] for n, w in zip(names, want)), "firmware/: every package has its listed sha256")
    rel = (meta or {}).get("releases", [])
    ok([r.get("version") for r in rel] == [w["version"] for w in want] and all(r.get("sha256") == w["sha256"] for r, w in zip(rel, want)),
       "installer: offers exactly these versions, in this order, with these sha256")
    inst = out / "webapp" / "installer"
    ok(all((inst / r["pkg"]).resolve() == (fw / n).resolve() and (inst / r["pkg"]).is_file() and r.get("size") == (fw / n).stat().st_size
           for r, n in zip(rel, names)), "installer: every version's package link resolves to its file")
    ok(all(r.get("bullets") and r.get("date") and r.get("tag") for r in rel if r["version"] != VERSION)
       and all("going_back" in r for r in rel if r.get("tag") and r["version"] != RELEASES["releases"][0]["version"]),
       "installer: every listed version carries its bullets, date and tag; the older ones their going-back note")
    sums = (fw / "SHA256SUMS").read_text(encoding="utf-8").splitlines() if (fw / "SHA256SUMS").is_file() else []
    ok(sums == [f"{w['sha256']}  {n}" for n, w in zip(names, want)], "firmware/SHA256SUMS lists them")
    idx = (fw / "index.html").read_text(encoding="utf-8") if (fw / "index.html").is_file() else ""
    ok(all(f'href="{n}"' in idx and w["sha256"] in idx for n, w in zip(names, want))
       and all(f'/tree/{r["tag"]}"' in idx for r in rel if r.get("tag")) and all(f'href="{x}"' in idx for x in ("LICENSE", "LICENSING.md", "LICENSES/")),
       "firmware/index.html: every package, its sha256, its source at its tag, the licences")
    ok(not any(f"choralroot-{v}.fwsc" in idx or (fw / f"choralroot-{v}.fwsc").exists() for v in ("0.14", "0.15")), "firmware/: 0.14 and 0.15 are not published")
    ok(stdout.count("skipped: ") == len(RELEASES["releases"]) - len([w for w in want if w["version"] != VERSION]),
       "make_site.py: one \"skipped:\" line for each listed version without a package")


def refusals():
    """make_site.py must stop before it writes anything: a package that is not the listed one, a missing version
    or an unlisted current version when everything is required"""
    listed = RELEASES["releases"]
    neg, out = SCRATCH / "site-neg-packages", SCRATCH / "site-neg"
    for d in (neg, out):                            # (made by this test, in build/scratch)
        if d.exists():
            shutil.rmtree(d)
    neg.mkdir(parents=True)
    victim = listed[-1]["version"]
    bad = bytearray(PKG.read_bytes())
    bad[-1] ^= 0xFF
    (neg / f"choralroot-{victim}.fwsc").write_bytes(bad)
    r = make([PKG, VERSION, out, "--packages", neg])
    ok(r.returncode != 0 and f"version {victim}" in r.stderr and "refused" in r.stderr and not out.exists(),
       f"make_site.py refuses a choralroot-{victim}.fwsc whose sha256 is not the listed one")
    (neg / f"choralroot-{victim}.fwsc").unlink()
    r = make([PKG, VERSION, out, "--packages", neg, "--require-all"])
    ok(r.returncode != 0 and "not in web/releases.json" in r.stderr and not out.exists(),
       "make_site.py --require-all refuses a version that is not in releases.json")
    if len(listed) > 1 and sha(PKG) == listed[0]["sha256"]:
        r = make([PKG, listed[0]["version"], out, "--packages", neg, "--require-all"])
        ok(r.returncode != 0 and f"version {listed[1]['version']}: choralroot-{listed[1]['version']}.fwsc is missing" in r.stderr and not out.exists(),
           f"make_site.py --require-all refuses a build without choralroot-{listed[1]['version']}.fwsc, naming it")
        r = make([PKG, listed[1]["version"], out, "--packages", neg])
        ok(r.returncode != 0 and "is not the released" in r.stderr and not out.exists(),
           "make_site.py refuses a current package whose sha256 is not its version's")
    else:
        print("skipped: the --require-all refusal of a missing older package (build/choralroot.fwsc is not the newest listed release)")
    foreign = bytearray(PKG.read_bytes())
    foreign[0x2F] = (ord("X") + 1) & 0xFF            # the identity's first marker byte: XM-1_920
    (neg / "foreign.fwsc").write_bytes(foreign)
    r = make([neg / "foreign.fwsc", VERSION, out, "--packages", neg])
    ok(r.returncode != 0 and "FM-1_9xx" in r.stderr and not out.exists(), "make_site.py refuses a package whose identity is not FM-1_9xx")


def release_site(cur):
    """the site as the Pages job builds it: the newest release as the current version, every listed version required"""
    out = SCRATCH / "site-release"
    r = make([PKG, cur["version"], out, "--packages", PACKAGES, "--require-all"])
    if not ok(r.returncode == 0, f"make_site.py --require-all builds the site for {cur['version']} (build/scratch/site-release)"):
        print(r.stdout + r.stderr)
        return
    page = (out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")
    versions(out, read_meta(page), RELEASES["releases"], r.stdout)
    html = (out / "index.html").read_text(encoding="utf-8")
    ok(f'New in {cur["version"]}' in html and all(escape_html(b) in html for b in cur["bullets"]) and 'href="webapp/installer/"' in html,
       f"index.html: \"New in {cur['version']}\" with its bullets, and the button to the installer")
    ok(all(f'href="firmware/choralroot-{x["version"]}.fwsc"' in html for x in RELEASES["releases"]), "index.html: a download link for every version")
    emu = (ROOT / "build" / "emu-web" / "index.html").is_file()
    pages, count, bad, frags = walk(() if emu else ((out / "emu").resolve(),), out)
    for b in bad + frags:
        print(f"  missing: {b}")
    ok(not bad and not frags, f"links (the release site): {count} internal href / src / url() in {len(pages)} pages resolve")


def escape_html(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def main():
    if not PKG.is_file():
        print(f"skip: {PKG.relative_to(ROOT)} missing (run ./build.sh first)")
        sys.exit(0)
    editor = OUT / "webapp" / "editor"
    had_editor = editor.exists()
    t0 = time.time() - 1
    r = make([PKG, VERSION, OUT, "--packages", PACKAGES])
    if not ok(r.returncode == 0, "make_site.py builds build/site"):
        print(r.stdout + r.stderr)
        return 1

    index = OUT / "index.html"
    html = index.read_text(encoding="utf-8") if index.is_file() else ""
    ok(index.is_file(), "index.html (the landing page) at the root")
    ok("{{" not in html and "}}" not in html, "index.html: every placeholder filled")
    ok(NAME in html and f'href="firmware/{NAME}"' in html, f"index.html: links the package {NAME}")
    ok('href="webapp/installer/"' in html, "index.html: links webapp/installer/")
    ok("http-equiv=\"refresh\"" not in html, "index.html: no redirect")
    ok((OUT / "site.css").is_file() and (OUT / "InterTight.woff2").is_file() and (OUT / "OFL.txt").is_file(),
       "site.css, InterTight.woff2, OFL.txt at the root")

    inst = OUT / "webapp" / "installer" / "index.html"
    ih = inst.read_text(encoding="utf-8") if inst.is_file() else ""
    ok(inst.is_file(), "webapp/installer/index.html written")
    ok("FM-1_920" in ih, "installer: identity FM-1_920")
    ok("/*LIB*/" not in ih and "/*META*/" not in ih, "installer: /*LIB*/ and /*META*/ replaced")
    meta = read_meta(ih)
    ok(meta is not None and meta.get("version") == VERSION and meta.get("product") == "FM-1_920"
       and meta.get("pkg") == f"../../firmware/{NAME}", "installer: META JSON (version, product, pkg)")

    fw = OUT / "firmware" / NAME
    ok(fw.is_file() and fw.stat().st_size == PKG.stat().st_size, f"firmware/{NAME} ({PKG.stat().st_size} B)")
    ok(all((OUT / "firmware" / f).is_file() for f in ("LICENSE", "LICENSING.md", "LICENSES/index.html")),
       "firmware/: LICENSE, LICENSING.md, LICENSES/index.html")
    ok('id="new"' in html and "a local build" in html, "index.html: the version block (a local build: no notes)")
    here = [r for r in RELEASES["releases"] if (PACKAGES / f"choralroot-{r['version']}.fwsc").is_file()]
    for x in RELEASES["releases"]:
        if x not in here:
            print(f"skipped: choralroot-{x['version']}.fwsc not in {PACKAGES}: version {x['version']} is not "
                  "published by this local build (python3 tools/fetch_packages.py; the Pages job has them all)")
    versions(OUT, meta, [{"version": VERSION, "sha256": sha(PKG)}] + here, r.stdout)
    refusals()
    cur = next((r for r in RELEASES["releases"] if r["sha256"] == sha(PKG)), None)
    if cur is not None and cur is RELEASES["releases"][0] and len(here) == len(RELEASES["releases"]):
        release_site(cur)
    else:
        print("skipped: the site as the Pages job builds it (--require-all): build/choralroot.fwsc is not the newest "
              "listed release, or a listed package is missing")
    if had_editor:
        new = [p for p in editor.rglob("*") if p.is_file() and p.stat().st_mtime >= t0]
        ok(not new, "webapp/editor: an old one left as it was, nothing written")
    else:
        ok(not editor.exists(), "webapp/editor: not written")

    emu_src = ROOT / "build" / "emu-web"
    if (emu_src / "index.html").is_file():
        ok((OUT / "emu" / "index.html").is_file() and (OUT / "emu" / "choralroot.wasm").is_file(),
           "emu/: index.html and choralroot.wasm copied")
        ok('href="emu/"' in html, "index.html: links emu/ (Try it in the browser)")
    else:
        print(f"skip: {emu_src.relative_to(ROOT)} missing: emu/ and the links to it not checked "
              "(sh tools/emu/web/build_web.sh)")
    skip = () if (emu_src / "index.html").is_file() else ((OUT / "emu").resolve(),)
    pages, count, bad, frags = walk(skip)
    for b in bad:
        print(f"  missing: {b}")
    ok(not bad, f"links: {count} internal href / src / url() in {len(pages)} pages resolve")
    for f in frags:
        print(f"  fragment: {f}")
    ok(not frags, "links: every #fragment names an id on its page")
    landing = parse(index) if index.is_file() else Page()
    ok("playing" in landing.ids and any(u == "#playing" for _, _, u in landing.links),
       "index.html: Manual -> #playing")
    return 1 if failed else 0


if __name__ == "__main__":
    rc = main()
    print("site: ALL OK" if rc == 0 and not failed else f"site: {failed} FAILED")
    sys.exit(rc)
