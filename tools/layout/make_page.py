#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ChoralRoot FM-1 layout tool: the files beside the preview module (docs/LAYOUT-TOOL.md).

  python3 tools/layout/make_page.py --loader "ChoralRoot 0.22"
      build/layout-web/crlayout.js from tools/layout/crlayout.tpl.js and build/layout-web/crlayout.wasm (the module as
      base64 in the loader: the page works from file://). tools/layout/build_layout.sh runs this.
  python3 tools/layout/make_page.py [--out DIR]
      DIR/index.html: web/layout.html with web/fm1layout.js inlined, as web/make_site.py writes it to the site's
      webapp/layout/ (the same function), and crlayout.js beside it, so the folder can be opened by itself. DIR:
      build/layout-web (the loader is there already):
      open build/layout-web/index.html, or python3 -m http.server -d build/layout-web
"""
import base64
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "layout-web"


def loader(version):
    wasm = (OUT / "crlayout.wasm").read_bytes()
    tpl = (Path(__file__).resolve().parent / "crlayout.tpl.js").read_text(encoding="utf-8")
    for mark in ("/*WASM_B64*/", "/*VERSION*/"):
        if tpl.count(mark) != 1:
            raise SystemExit(f"crlayout.tpl.js must contain {mark} once")
    js = tpl.replace("/*WASM_B64*/", base64.b64encode(wasm).decode("ascii")).replace("/*VERSION*/", version)
    (OUT / "crlayout.js").write_text(js, encoding="utf-8")


def page(out):
    sys.path.insert(0, str(ROOT / "web"))
    import make_site
    html = make_site.layout_page()
    out.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(html, encoding="utf-8")
    if out.resolve() != OUT.resolve():
        shutil.copy(OUT / "crlayout.js", out / "crlayout.js")
    print(f"layout-web: {out / 'index.html'} ({len(html)} B)")


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--loader":
        loader(a[1])
    elif len(a) == 2 and a[0] == "--out":
        page(Path(a[1]))
    elif not a:
        page(OUT)
    else:
        raise SystemExit(__doc__)
