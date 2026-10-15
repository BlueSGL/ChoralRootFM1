#!/usr/bin/env python3
"""Analyse dt_measure.py captures: envelope times per step, fitted T(value) curves.

  python3 tools/digitone/dt_analyse.py [DIR]     (default: dt_measure's --out)

For each <capture>.wav + .json: finds the two sync-marker onsets in the audio and
maps host time to audio time, then per step:
  attack          10/50/90 % rise times of the linear amplitude from note-on, the
                  98 % time, and the level at half the 98 % time (0.5 = linear)
  decay/release   times to -20/-40/-60 dB from note-on (decay) or note-off
                  (release), relative to the peak / the level at note-off
  filt_*, op_*    the spectral-centroid track (log Hz): 10/50/90 % of its sweep
  delay           time from note-on to 50 % of the centroid step
  fade            time for the LFO's modulation depth (dB swing) to reach 90 %
Then per parameter (and tempo) fits T(v) = a*v^p and T(v) = a*10^(k v), keeps the
better (rms residual in log10), writes analysis.json and prints a table. PNG plots
if matplotlib is installed.
"""
import glob
import json
import math
import os
import sys
import wave

import numpy as np

HOP = 0.001     # envelope hop, s
WIN = 0.001     # envelope smoothing window, s


def load_wav(path):
    with wave.open(path, "rb") as w:
        sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        x = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    elif sw == 2:
        x = np.frombuffer(raw, "<i2") / 32768.0
    elif sw == 4:
        x = np.frombuffer(raw, "<i4") / 2147483648.0
    else:
        raise ValueError(f"sample width {sw}")
    return x.reshape(-1, ch).mean(axis=1), sr


def hilbert_mag(x, sr):
    """|analytic signal| with a 10.7 ms Blackman-windowed FIR Hilbert transformer.

    A finite kernel keeps the envelope local (a whole-file FFT Hilbert smears an
    abrupt stop across the file at -60 dB). Accurate above about 150 Hz."""
    taps = 513
    k = np.arange(taps) - taps // 2
    h = np.where(k % 2 == 1, 2.0 / (np.pi * np.where(k == 0, 1, k)), 0.0) * np.blackman(taps)
    n = len(x) + taps - 1
    nf = 1 << (n - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(x, nf) * np.fft.rfft(h, nf), nf)[taps // 2: taps // 2 + len(x)]
    return np.sqrt(x * x + y * y)


def rms_env(x, sr):
    """Amplitude envelope: FIR-Hilbert magnitude, 1 ms moving average, 1 ms hop.

    (A plain 5 ms RMS ripples by a few % on a 262 Hz tone: 1.3 periods per window.)
    The result is scaled to RMS units (peak / sqrt 2) for a sine."""
    hop, win = int(sr * HOP), int(sr * 0.001)
    a = hilbert_mag(x, sr) / np.sqrt(2)
    c = np.concatenate([[0.0], np.cumsum(a)])
    starts = np.arange(0, len(x) - win, hop)
    e = (c[starts + win] - c[starts]) / win
    t = (starts + win / 2) / sr
    return t, e


def db(a):
    return 20 * np.log10(np.maximum(a, 1e-9))


def centroid_track(x, sr, n=512, hop=48):
    w = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / sr)
    idx = np.arange(0, len(x) - n, hop)
    out = np.empty(len(idx))
    lev = np.empty(len(idx))
    for i, s in enumerate(idx):
        m = np.abs(np.fft.rfft(x[s:s + n] * w))
        p = m * m
        tot = p.sum()
        lev[i] = tot
        out[i] = (p * f).sum() / tot if tot > 0 else 0.0
    return (idx + n / 2) / sr, np.log2(np.maximum(out, 1.0)), lev


def find_onset(t, edb, t0, t1):
    sel = (t >= t0) & (t <= t1)
    if not sel.any():
        return None
    seg, ts = edb[sel], t[sel]
    floor = np.percentile(seg, 5)
    peak = seg.max()
    thr = max(floor + 20, peak - 30)
    i = np.argmax(seg >= thr)
    return float(ts[i])


def host_map(meta, t, edb):
    """Return f(host) -> audio seconds from the marker onsets."""
    h0 = meta.get("host_ff_start", meta["markers"][0] - 1.5)
    found = []
    off = None   # audio - host offset, known after the first marker
    for m in meta["markers"]:
        exp = m - h0
        if off is None:
            on = find_onset(t, edb, max(0.0, exp - 1.4), exp + 0.6)
        else:   # later markers: narrow window so earlier notes' tails are not taken
            on = find_onset(t, edb, exp + off - 0.15, exp + off + 0.15)
        if on is not None:
            found.append((m, on))
            if off is None:
                off = on - exp
    if not found:
        return lambda h: h - h0, {"markers": 0}
    if len(found) == 1:
        (m, on), = found
        return lambda h: h - m + on, {"markers": 1, "offset": on - (m - h0)}
    (ma, oa), (mb, ob) = found[0], found[-1]
    a = (ob - oa) / (mb - ma)
    return (lambda h: oa + (h - ma) * a), {"markers": len(found), "rate": a,
                                         "offset": oa - (ma - h0)}


def crossing(ts, ys, level, rising=True):
    """First time ys crosses level (linear interpolation)."""
    ok = ys >= level if rising else ys <= level
    if not ok.any():
        return None
    i = int(np.argmax(ok))
    if i == 0:
        return float(ts[0])
    y0, y1 = ys[i - 1], ys[i]
    f = 0.0 if y1 == y0 else (level - y0) / (y1 - y0)
    return float(ts[i - 1] + f * (ts[i] - ts[i - 1]))


def analyse_step(s, f, t, e, edb, ct, cc, x, sr, cl=None):
    on, off, end = f(s["on"]), f(s["off"]), f(s["end"])
    k = s["kind"]
    r = {"param": s["param"], "value": s["value"], "kind": k}
    for key in ("tempo", "display"):
        if key in s:
            r[key] = s[key]
    if k == "attack":
        sel = (t >= on) & (t <= off)
        ts, ys = t[sel] - on, e[sel]
        plat = np.median(ys[-max(5, len(ys) // 20):])
        for p in (10, 50, 90, 98):
            r[f"t{p}"] = crossing(ts, ys, plat * p / 100)
        if r["t98"]:
            r["level_at_half_t98"] = float(np.interp(r["t98"] / 2, ts, ys) / plat)
        r["onset_audio"] = find_onset(t, edb, on - 0.05, off)
        r["onset_lag"] = None if r["onset_audio"] is None else r["onset_audio"] - on
        r["T"] = r["t90"]
    elif k in ("decay", "release"):
        ref_t = on if k == "decay" else off
        stop = off if k == "decay" else end
        sel = (t >= ref_t) & (t <= stop)
        ts, ys = t[sel] - ref_t, edb[sel]
        if k == "decay":
            ip = int(np.argmax(ys[: max(1, int(0.2 / HOP))]))
            ref = ys[ip]
        else:
            ref = float(np.median(edb[(t >= off - 0.02) & (t < off)]))
            ip = 0
        floor = float(np.percentile(edb[(t >= end) & (t <= end + 0.5)], 50)) \
            if ((t >= end) & (t <= end + 0.5)).any() else -200
        r["ref_db"], r["floor_db"] = float(ref), floor
        for d in (20, 40, 60):
            c = crossing(ts[ip:], ys[ip:], ref - d, rising=False) if ref - d > floor + 3 else None
            r[f"t_{d}db"] = c
        # T60 estimate: measured, else extrapolated from the deepest point (exp. decay)
        for d in (60, 40, 20):
            if r[f"t_{d}db"]:
                r["T"] = r[f"t_{d}db"] * 60 / d
                r["T_from"] = f"-{d} dB"
                break
        else:
            r["T"] = None
    elif k in ("filt_decay", "filt_attack", "filt_release", "op_attack", "op_decay", "delay"):
        ref_t = off if k == "filt_release" else on
        stop = end if k == "filt_release" else off
        sel = (ct >= ref_t) & (ct <= stop)
        if sel.any():  # drop frames more than 40 dB below the step's loudest (noise centroid)
            sel &= cl >= cl[sel].max() * 1e-4
        ts, ys = ct[sel] - ref_t, cc[sel]
        if len(ys) < 4:
            r["T"] = None
            return r
        r["centroid_hz"] = [float(2 ** ys.max()), float(2 ** ys.min())]
        nfin = max(3, len(ys) // 20)
        first = ys[2] if k in ("filt_attack", "op_attack", "delay") else ys[: max(3, int(0.02 * sr / 48))].max()
        if k == "filt_release":
            first = ys[0]
        last = np.median(ys[-nfin:])
        span = last - first
        r["span_oct"] = float(span)
        rising = span > 0
        for p in (10, 50, 90):
            r[f"t{p}"] = crossing(ts, ys, first + span * p / 100, rising=rising)
        r["T"] = r["t50"] if k == "delay" else r["t90"]
    elif k == "fade":
        sel = (t >= on) & (t <= off)
        ts, ys = t[sel] - on, edb[sel]
        w = int(0.3 / HOP)
        depth = np.array([ys[i:i + w].max() - ys[i:i + w].min() for i in range(0, len(ys) - w, 10)])
        dts = ts[0:len(ys) - w:10][:len(depth)] + 0.15
        fin = np.median(depth[-max(3, len(depth) // 10):])
        r["final_depth_db"] = float(fin)
        r["t90"] = crossing(dts, depth, 0.9 * fin)
        r["T"] = r["t90"]
    return r


def fit(points):
    pts = [(v, T) for v, T in points if T and T > 0 and v > 0]
    if len(pts) < 2:
        return None
    v = np.array([p[0] for p in pts], float)
    y = np.log10([p[1] for p in pts])
    out = {}
    A = np.vstack([np.ones_like(v), np.log10(v)]).T
    c, *_ = np.linalg.lstsq(A, y, rcond=None)
    res = float(np.sqrt(np.mean((A @ c - y) ** 2)))
    out["power"] = {"a": float(10 ** c[0]), "p": float(c[1]), "rms_log10": res,
                    "form": "T = a * v**p"}
    A = np.vstack([np.ones_like(v), v]).T
    c, *_ = np.linalg.lstsq(A, y, rcond=None)
    res = float(np.sqrt(np.mean((A @ c - y) ** 2)))
    out["exp"] = {"a": float(10 ** c[0]), "k": float(c[1]), "rms_log10": res,
                  "form": "T = a * 10**(k*v)"}
    out["best"] = min(("power", "exp"), key=lambda n: out[n]["rms_log10"])
    return out


def analyse_dir(d, plots=True):
    results, curves = [], {}
    for jp in sorted(glob.glob(os.path.join(d, "*.json"))):
        if os.path.basename(jp) == "analysis.json":
            continue
        meta = json.load(open(jp))
        wav = os.path.join(d, meta["wav"])
        if not os.path.exists(wav):
            continue
        x, sr = load_wav(wav)
        t, e = rms_env(x, sr)
        edb = db(e)
        f, minfo = host_map(meta, t, edb)
        need_c = any(s["kind"].startswith(("filt", "op_")) or s["kind"] == "delay"
                     for s in meta["steps"])
        ct, cc, cl = centroid_track(x, sr) if need_c else (None, None, None)
        for s in meta["steps"]:
            r = analyse_step(s, f, t, e, edb, ct, cc, x, sr, cl)
            r["capture"] = meta["capture"]
            r["sync"] = minfo
            results.append(r)
    groups = {}
    for r in results:
        key = r["param"] + (f"@{r['tempo']}" if "tempo" in r else "")
        groups.setdefault(key, []).append(r)
    for key, rs in groups.items():
        pts = [(r["value"] if "display" not in r else abs(r["display"]), r.get("T")) for r in rs]
        curves[key] = {"kind": rs[0]["kind"], "points": pts, "fit": fit(pts)}
    out = {"steps": results, "curves": curves}
    with open(os.path.join(d, "analysis.json"), "w") as fh:
        json.dump(out, fh, indent=1)
    print_table(results, curves)
    if plots:
        make_plots(d, curves)
    return out


def _f(v):
    return "   -   " if v is None else f"{v:7.3f}"


def print_table(results, curves):
    print(f"{'capture':10s} {'param':10s} {'val':>4s} {'kind':12s} {'T (s)':>8s}  details")
    for r in results:
        det = {k: r[k] for k in ("t10", "t50", "t90", "t_20db", "t_40db", "t_60db",
                                 "level_at_half_t98", "span_oct") if k in r}
        det = " ".join(f"{k}={_f(v).strip()}" for k, v in det.items())
        print(f"{r['capture']:10s} {r['param']:10s} {r['value']:4d} {r['kind']:12s} "
              f"{_f(r.get('T'))}  {det}")
    print("\nfitted curves (rms residual in log10 units):")
    for k, c in curves.items():
        ft = c["fit"]
        if not ft:
            print(f"  {k:14s} not enough points")
            continue
        p, x = ft["power"], ft["exp"]
        print(f"  {k:14s} power a={p['a']:.4g} p={p['p']:.3f} (res {p['rms_log10']:.3f}) | "
              f"exp a={x['a']:.4g} k={x['k']:.4f} (res {x['rms_log10']:.3f}) -> {ft['best']}")


def make_plots(d, curves):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed: plots skipped")
        return
    for k, c in curves.items():
        pts = [(v, T) for v, T in c["points"] if T]
        if not pts:
            continue
        fig, ax = plt.subplots(figsize=(6, 4))
        ax.semilogy([p[0] for p in pts], [p[1] for p in pts], "o", label="measured")
        ft = c["fit"]
        if ft:
            vv = np.linspace(1, 127, 200)
            ax.semilogy(vv, ft["power"]["a"] * vv ** ft["power"]["p"], label="power")
            ax.semilogy(vv, ft["exp"]["a"] * 10 ** (ft["exp"]["k"] * vv), label="exp")
        ax.set_xlabel("value")
        ax.set_ylabel("time (s)")
        ax.set_title(k)
        ax.legend()
        fig.tight_layout()
        fig.savefig(os.path.join(d, f"curve_{k.replace('@', '_')}.png"), dpi=100)
        plt.close(fig)


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from dt_measure import DEFAULT_OUT
    analyse_dir(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT)
