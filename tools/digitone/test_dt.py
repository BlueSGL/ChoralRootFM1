#!/usr/bin/env python3
"""Hardware-free tests for tools/digitone: CC/NRPN bytes, --plan, and the analysis on
synthetic captures with known times (with simulated ffmpeg start latency and drift).

  python3 tools/digitone/test_dt.py [WORKDIR]
"""
import json
import math
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dt_cc  # noqa: E402
import dt_analyse  # noqa: E402

SR = 48000
F0 = 261.63
LAT = 0.37      # simulated ffmpeg start latency (audio starts later than host_ff_start)
MIDI_LAT = 0.006
DRIFT = 25e-6
fails = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)


def test_cc():
    check(dt_cc.encode("amp_atk", 100) == [[0xB0, 104, 100]], "amp_atk -> B0 68 64")
    check(dt_cc.encode("amp_rel", 5, channel=2) == [[0xB1, 107, 5]], "channel 2 status B1")
    check(dt_cc.encode("f_freq", 20) == [[0xB0, 99, 1], [0xB0, 98, 20], [0xB0, 6, 20],
                                         [0xB0, 38, 0]], "f_freq hi-res as NRPN 1/20")
    check(dt_cc.encode("lfo1_dep", 64)[1] == [0xB0, 98, 55], "lfo1 depth NRPN LSB 55")
    check(dt_cc.encode("a_del", 16) == [[0xB0, 83, 16]], "A delay CC 83")
    try:
        dt_cc.encode("f_type", 1)
        check(False, "f_type refused")
    except ValueError:
        check(True, "f_type refused (hand-set)")
    check(dt_cc.note_on(60, 100) == [0x90, 60, 100], "note on bytes")
    check(dt_cc.all_notes_off()[0] == [0xB0, 123, 0], "all notes off")


def test_plan():
    p = subprocess.run([sys.executable, os.path.join(HERE, "dt_measure.py"), "--plan"],
                       capture_output=True, text=True)
    last = [l for l in p.stdout.splitlines() if "captures," in l]
    check(p.returncode == 0 and last, "--plan runs: " + (last[0] if last else p.stderr[-200:]))
    if last:
        mins = float(last[0].split("about ")[1].split(" min")[0])
        check(mins < 30, f"plan under 30 min ({mins} min)")


class Cap:
    """Builds one synthetic capture: host timeline -> audio buffer."""

    def __init__(self, name, total):
        self.name = name
        self.h0 = 1000.0
        self.x = np.random.default_rng(1).normal(0, 3e-5, int(total * SR))
        self.meta = {"capture": name, "group": name, "wav": name + ".wav",
                     "sample_rate": SR, "host_ff_start": self.h0, "markers": [],
                     "steps": []}
        self.h = self.h0 + 1.5

    def a(self, host):
        return (host - self.h0) * (1 + DRIFT) - LAT + MIDI_LAT

    def put(self, host, sig):
        i = int(round(self.a(host) * SR))
        n = min(len(sig), len(self.x) - i)
        self.x[i:i + n] += sig[:n]

    def marker(self):
        n = int(0.08 * SR)
        self.put(self.h, 0.5 * np.sin(2 * np.pi * F0 * np.arange(n) / SR))
        self.meta["markers"].append(self.h)
        self.h += 1.3

    def step(self, ev, sig_fn, gate, tail):
        self.h += 0.3
        on, off = self.h, self.h + gate
        sig = sig_fn(gate, tail)
        self.put(on, sig)
        ev.update(on=on, off=off, end=off + tail, gate=gate, tail=tail)
        self.meta["steps"].append(ev)
        self.h = off + tail + 0.7

    def write(self, d):
        self.marker()
        y = np.clip(self.x, -1, 1)
        q = np.round(y * (2 ** 23 - 1)).astype(np.int32)
        b = np.stack([q & 255, (q >> 8) & 255, (q >> 16) & 255], 1).astype(np.uint8)
        with wave.open(os.path.join(d, self.name + ".wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(3)
            w.setframerate(SR)
            w.writeframes(b.tobytes())
        json.dump(self.meta, open(os.path.join(d, self.name + ".json"), "w"))


def sine(n):
    return 0.5 * np.sin(2 * np.pi * F0 * np.arange(n) / SR)


SIX = [0, 32, 64, 96, 112, 126]
T_ATK = lambda v: 0.005 * 10 ** (v / 35)     # noqa: E731  linear ramp length
T_DEC = lambda v: 0.02 * 10 ** (v / 40)      # noqa: E731  T60 of exponential decay
TAU_F = lambda v: 0.01 * 10 ** (v / 45)      # noqa: E731  log-cutoff time constant


def gen(d):
    c = Cap("amp_atk", 80)
    c.marker()
    for v in SIX:
        T = T_ATK(v)
        def f(g, tl, T=T):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR
            env = np.minimum(t / T, 1.0) * (t < g)
            return sine(n) * env
        c.step({"param": "amp_atk", "value": v, "kind": "attack"}, f, max(1.0, T * 1.3), 0.3)
    c.write(d)

    c = Cap("amp_dec", 90)
    c.marker()
    for v in SIX:
        T = T_DEC(v)
        def f(g, tl, T=T):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR
            return sine(n) * 10 ** (-3 * t / T) * (t < g)
        c.step({"param": "amp_dec", "value": v, "kind": "decay"}, f, min(max(1.0, T * 1.2), 30), 0.3)
    c.write(d)

    c = Cap("filt_dec", 80)
    c.marker()
    fmin, fmax = 200.0, 12000.0
    for v in SIX:
        tau = TAU_F(v)
        def f(g, tl, tau=tau):
            n = int((g + tl) * SR)
            t = np.arange(n) / SR
            saw = 0.4 * (2 * ((t * F0) % 1.0) - 1)
            fc = fmin * (fmax / fmin) ** np.exp(-t / tau)
            a = 1 - np.exp(-2 * np.pi * fc / SR)
            y = np.empty(n)
            s1 = s2 = 0.0
            for i in range(n):
                s1 += a[i] * (saw[i] - s1)
                s2 += a[i] * (s1 - s2)
                y[i] = s2
            return y * (t < g)
        c.step({"param": "f_dec", "value": v, "kind": "filt_decay"}, f,
               min(max(1.0, tau * 5), 30), 0.3)
    c.write(d)


def test_analysis(d):
    gen(d)
    out = dt_analyse.analyse_dir(d, plots=False)
    st = {(r["param"], r["value"]): r for r in out["steps"]}
    sync = out["steps"][0]["sync"]
    check(sync["markers"] == 2 and abs(sync["rate"] - (1 + DRIFT)) < 2e-5,
          f"two markers found, drift rate {sync.get('rate'):.6f}")
    for v in SIX:
        r = st[("amp_atk", v)]
        exp = 0.9 * T_ATK(v)
        tol = max(0.002, 0.03 * exp)
        check(r["t90"] is not None and abs(r["t90"] - exp) < tol,
              f"atk v{v}: t90 {r['t90']:.4f} vs {exp:.4f}")
        if T_ATK(v) > 0.05:
            check(abs(r["level_at_half_t98"] - 0.5) < 0.05,
                  f"atk v{v}: linear, level at t98/2 = {r['level_at_half_t98']:.3f}")
    for v in SIX:
        r = st[("amp_dec", v)]
        exp = T_DEC(v)
        check(r["T"] is not None and abs(r["T"] - exp) < max(0.006, 0.03 * exp),
              f"dec v{v}: T60 {r['T']:.4f} vs {exp:.4f} ({r.get('T_from')})")
    ca = out["curves"]["amp_atk"]["fit"]
    check(ca["best"] == "exp" and abs(ca["exp"]["k"] - 1 / 35) < 0.002,
          f"atk fit exp k={ca['exp']['k']:.5f} vs {1/35:.5f}")
    cd = out["curves"]["amp_dec"]["fit"]
    check(cd["best"] == "exp" and abs(cd["exp"]["k"] - 1 / 40) < 0.001,
          f"dec fit exp k={cd['exp']['k']:.5f} vs {1/40:.5f}")
    cf = out["curves"]["f_dec"]["fit"]
    ratios = [st[("f_dec", v)]["T"] / TAU_F(v) for v in SIX if v > 0]
    print("     filt t90/tau ratios:", " ".join(f"{q:.2f}" for q in ratios))
    check(cf["best"] == "exp" and abs(cf["exp"]["k"] - 1 / 45) < 0.0015,
          f"filt fit exp k={cf['exp']['k']:.5f} vs {1/45:.5f}")


if __name__ == "__main__":
    test_cc()
    test_plan()
    d = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="dt_test_")
    os.makedirs(d, exist_ok=True)
    test_analysis(d)
    print(f"\n{len(fails)} failure(s); synthetic files in {d}")
    sys.exit(1 if fails else 0)
