#!/usr/bin/env python3
"""Measure Elektron Digitone envelope times: drive track 1 over MIDI, record USB audio.

  python3 tools/digitone/dt_measure.py --plan            # print the plan, send nothing
  python3 tools/digitone/dt_measure.py --run [--only amp_atk,amp_dec] [--out DIR]

Design: one WAV per capture (a group, or a group at one tempo) recorded by ffmpeg
(avfoundation, device found by name, 48 kHz). Every capture starts and ends with a
sync marker note (amp ATK 0, short gate). Note events are logged in host time
(time.monotonic); dt_analyse.py finds the two marker onsets in the audio and maps
host time -> audio time linearly, which removes ffmpeg start latency, MIDI and
audio latency and clock drift. The JSON next to the WAV holds every event.

Parameters the MIDI map cannot set (ALGO, RATIOs, FILTER TYPE, LFO DEST/WAVE/
MULT/MODE, tempo) are asked for by hand before the capture; press Enter.
"""
import argparse
import json
import math
import os
import re
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dt_cc  # noqa: E402

DEFAULT_OUT = "/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/DigitoneRecordings/times/"
DEVICE = "Elektron Digitone"
NOTE, VEL = 60, 100
SETTLE = 0.3          # after parameter changes, before the note
GAP = 0.7             # silence after each step's tail
MARK_GATE = 0.08
LEAD_IN = 1.5         # after ffmpeg start, before the first marker

# Plain sine: operator C only, filter open, no mod, no sends, LFOs at depth 0.
BASE = {
    "harm": 64, "dtun": 0, "fdbk": 0,
    "a_lev": 0, "b_lev": 0, "a_atk": 0, "a_dec": 0, "a_end": 0, "a_del": 0,
    "b_atk": 0, "b_dec": 0, "b_end": 0, "b_del": 0,
    "f_freq": 127, "f_reso": 0, "f_env": 64, "f_atk": 0, "f_dec": 0, "f_sus": 127,
    "f_rel": 0, "f_del": 0, "f_base": 0, "f_width": 127,
    "amp_atk": 0, "amp_dec": 0, "amp_sus": 127, "amp_rel": 0,
    "drive": 0, "pan": 64, "vol": 100, "chr_send": 0, "del_send": 0, "rev_send": 0,
    "lfo1_dep": 64, "lfo1_fade": 64, "lfo2_dep": 64, "lfo2_fade": 64,
}
MARKER = {"amp_atk": 0, "amp_dec": 0, "amp_sus": 127, "amp_rel": 0}

# Values of the owner's init sound we send back at the end. The Digitone cannot
# report its parameters over CC, so these are the documented init-sound values
# as best known; the owner should reload the sound/project without saving to be
# exact (README says so).
INIT_RESTORE = dict(BASE, **{
    "a_lev": 0, "b_lev": 0, "a_atk": 0, "a_dec": 64, "a_end": 0,
    "b_atk": 0, "b_dec": 64, "b_end": 0, "f_dec": 64, "f_sus": 0, "f_rel": 64,
    "amp_atk": 0, "amp_dec": 64, "amp_sus": 127, "amp_rel": 24,
    "chr_send": 0, "del_send": 0, "rev_send": 0,
})

HAND_SINE = ("Track 1: FILTER TYPE = OFF (or LP4 with FREQ 127); pick an ALGO where only "
             "operator C is heard with MIX fully to C's output; RATIO C = 1.00; check "
             "HARM reads 0.00; LFO DEST = none on both LFOs.")
HAND_FILT = ("Track 1: FILTER TYPE = LP4 (4-pole lowpass); ALGO/RATIO as for the sine "
             "(only C audible, RATIO C 1.00). FEEDBACK is set over MIDI to add harmonics.")
HAND_OPA = ("Track 1: ALGO where operator A modulates carrier C and C alone is heard "
            "(e.g. ALGO 1, MIX to C); RATIO C 1.00, RATIO A 1.00 (or 2.00); FILTER TYPE OFF.")
HAND_OPB = ("Track 1: ALGO where operator group B modulates carrier C (MIX to C); "
            "RATIO B 1.00/1.00; FILTER TYPE OFF.")
HAND_FADE = ("Track 1: LFO1 DEST = AMP VOL (volume), WAVE = SQR, MODE = TRG, MULT = 1 "
             "(BPM); FILTER TYPE OFF; only C audible.")


def tempo_note(t):
    return f" Set the project TEMPO to {t} BPM."


def _hold_attack(v):
    return 2.0 if v <= 64 else 3.0 if v <= 96 else 6.0 if v <= 112 else 40.0


def _hold_decay(v):
    return 2.0 if v <= 64 else 4.0 if v <= 96 else 10.0 if v <= 112 else 30.0


def _hold_filter(v):
    return 2.5 if v <= 64 else 6.0 if v <= 96 else 14.0 if v <= 112 else 30.0


SIX = [0, 32, 64, 96, 112, 126]
THREE = [0, 64, 112]


def build_plan():
    caps = []

    def cap(name, group, hand, base, steps):
        caps.append({"name": name, "group": group, "hand": hand, "base": base,
                     "steps": steps})

    cap("amp_atk", "amp_atk", HAND_SINE, {}, [
        {"param": "amp_atk", "value": v, "set": {"amp_atk": v}, "gate": _hold_attack(v),
         "tail": 0.3, "kind": "attack"} for v in SIX + [127]])
    cap("amp_dec", "amp_dec", HAND_SINE, {"amp_sus": 0}, [
        {"param": "amp_dec", "value": v, "set": {"amp_dec": v}, "gate": _hold_decay(v),
         "tail": 0.3, "kind": "decay"} for v in SIX])
    cap("amp_rel", "amp_rel", HAND_SINE, {}, [
        {"param": "amp_rel", "value": v, "set": {"amp_rel": v}, "gate": 1.0,
         "tail": _hold_decay(v), "kind": "release"} for v in SIX])
    filt = {"f_freq": 20, "f_reso": 40, "f_env": 127, "fdbk": 70, "f_sus": 0}
    cap("filt_dec", "filt_dec", HAND_FILT, filt, [
        {"param": "f_dec", "value": v, "set": {"f_dec": v}, "gate": _hold_filter(v),
         "tail": 0.3, "kind": "filt_decay"} for v in SIX])
    cap("filt_atk", "filt_atk", HAND_FILT, dict(filt, f_sus=127), [
        {"param": "f_atk", "value": v, "set": {"f_atk": v}, "gate": _hold_filter(v),
         "tail": 0.3, "kind": "filt_attack"} for v in THREE])
    cap("filt_rel", "filt_rel", HAND_FILT, dict(filt, f_sus=127, amp_rel=127), [
        {"param": "f_rel", "value": v, "set": {"f_rel": v}, "gate": 1.0,
         "tail": min(_hold_filter(v), 14.0), "kind": "filt_release"} for v in THREE])
    for op, hand in (("a", HAND_OPA), ("b", HAND_OPB)):
        g = "op" + op.upper()
        base = {f"{op}_lev": 110}
        steps = [{"param": f"{op}_atk", "value": v,
                  "set": {f"{op}_atk": v, f"{op}_dec": 127, f"{op}_end": 127},
                  "gate": _hold_filter(v), "tail": 0.3, "kind": "op_attack"} for v in THREE]
        steps += [{"param": f"{op}_dec", "value": v,
                   "set": {f"{op}_atk": 0, f"{op}_dec": v, f"{op}_end": 0},
                   "gate": _hold_filter(v), "tail": 0.3, "kind": "op_decay"} for v in THREE]
        cap(g, g, hand, base, steps)
    for tempo in (120, 80):
        cap(f"delay_{tempo}", "delay", HAND_OPA + tempo_note(tempo),
            {"a_lev": 110, "a_atk": 0, "a_dec": 127, "a_end": 127}, [
                {"param": "a_del", "value": v, "tempo": tempo, "set": {"a_del": v},
                 "gate": 8.0, "tail": 0.3, "kind": "delay"} for v in (16, 64, 112)])
    # LFO fade-in: bipolar, CC 64 = no fade, below 64 = fade in. Display -8, -32, -64.
    for tempo in (120, 80):
        cap(f"fade_{tempo}", "fade", HAND_FADE + tempo_note(tempo),
            {"lfo1_dep": 127, "lfo1_spd": 96}, [
                {"param": "lfo1_fade", "value": v, "display": v - 64, "tempo": tempo,
                 "set": {"lfo1_fade": v}, "gate": 12.0, "tail": 0.3, "kind": "fade"}
                for v in (56, 32, 0)])
    return caps


GROUPS = ["amp_atk", "amp_dec", "amp_rel", "amp_hold", "filt_dec", "filt_atk", "filt_rel",
          "opA", "opB", "delay", "fade"]


def capture_seconds(c):
    t = LEAD_IN + 2 * (MARK_GATE + 1.0 + SETTLE)
    for s in c["steps"]:
        t += SETTLE + s["gate"] + s["tail"] + GAP
    return t


def print_plan(caps):
    total = 0.0
    nsteps = 0
    for c in caps:
        d = capture_seconds(c)
        total += d
        print(f"\n== capture {c['name']}  ({d:.0f} s)")
        print(f"   by hand: {c['hand']}")
        print(f"   base over MIDI: {dict(BASE, **c['base'])}")
        for s in c["steps"]:
            nsteps += 1
            extra = f" tempo {s['tempo']}" if "tempo" in s else ""
            print(f"   {s['param']:9s}={s['value']:3d}{extra}  set {s['set']}  note C4 v{VEL} "
                  f"gate {s['gate']:.1f} s, tail {s['tail']:.1f} s ({s['kind']})")
    print(f"\n{len(caps)} captures, {nsteps} steps, about {total/60:.1f} min of recording "
          f"(plus the hand-set prompts).")
    print("amp_hold: the original Digitone has no AMP HOLD parameter; skipped.")
    print("At the end the tool sends the init-sound values back for every parameter it "
          "touched (best known values; reload the sound without saving to be exact).")
    return total


def find_audio_index(name=DEVICE):
    p = subprocess.run(["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices",
                        "true", "-i", ""], capture_output=True, text=True)
    audio = False
    for line in p.stderr.splitlines():
        if "audio devices" in line:
            audio = True
        elif audio:
            m = re.search(r"\[(\d+)\] (.+)$", line)
            if m and m.group(2).strip() == name:
                return int(m.group(1))
    raise SystemExit(f"audio input '{name}' not found in ffmpeg avfoundation list")


class Rig:
    def __init__(self, channel, out_dir):
        import rtmidi
        self.ch = channel
        self.out_dir = out_dir
        self.mo = rtmidi.MidiOut()
        ports = self.mo.get_ports()
        idx = [i for i, p in enumerate(ports) if DEVICE in p]
        if not idx:
            raise SystemExit(f"MIDI out '{DEVICE}' not found: {ports}")
        self.mo.open_port(idx[0])
        self.aidx = find_audio_index()
        self.ff = None
        self.touched = set()

    def send(self, msg):
        self.mo.send_message(msg)

    def setp(self, params):
        for k, v in params.items():
            for m in dt_cc.encode(k, v, self.ch):
                self.send(m)
                time.sleep(0.002)
            self.touched.add(k)

    def panic(self):
        for m in dt_cc.all_notes_off(self.ch):
            self.send(m)
        self.send(dt_cc.note_off(NOTE, self.ch))

    def start_ff(self, wav):
        self.ff = subprocess.Popen(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation",
             "-i", f":{self.aidx}", "-ar", "48000", "-c:a", "pcm_s24le", wav],
            stdin=subprocess.PIPE)
        return time.monotonic()

    def stop_ff(self):
        if self.ff and self.ff.poll() is None:
            try:
                self.ff.communicate(b"q", timeout=10)
            except Exception:
                self.ff.kill()
        self.ff = None

    def note(self, gate, log):
        t_on = time.monotonic()
        self.send(dt_cc.note_on(NOTE, VEL, self.ch))
        time.sleep(gate)
        t_off = time.monotonic()
        self.send(dt_cc.note_off(NOTE, self.ch))
        log.update(on=t_on, off=t_off)

    def marker(self, base, log_list):
        self.setp(MARKER)
        time.sleep(SETTLE)
        ev = {}
        self.note(MARK_GATE, ev)
        log_list.append(ev["on"])
        time.sleep(1.0)
        self.setp({k: base[k] for k in MARKER})


def sound_check(rig, out_dir):
    """one 0.5 s note recorded before the first capture: stop at once when the Digitone does not answer over
    USB MIDI or its USB audio is silent (a whole capture of silence otherwise)"""
    from dt_analyse import load_wav
    wav = os.path.join(out_dir, "_sound_check.wav")
    ff = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation",
                           "-i", f":{rig.aidx}", "-t", "2.5", "-ar", "48000", "-c:a", "pcm_s24le", wav])
    time.sleep(1.0)
    rig.send(dt_cc.note_on(NOTE, VEL, rig.ch))
    time.sleep(0.5)
    rig.send(dt_cc.note_off(NOTE, rig.ch))
    ff.wait()
    x, _ = load_wav(wav)
    peak = 20 * math.log10(max(float(abs(x).max()), 1e-12))
    if peak < -60:
        raise SystemExit(f"sound check: peak {peak:.0f} dBFS from '{DEVICE}' after a note on channel {rig.ch}. "
                         "Check MIDI CONFIG > PORT CONFIG (INPUT FROM USB or MIDI+USB, RECEIVE NOTES and "
                         "RECEIVE CC/NRPN on), the track's channel (--channel), and that the track is not muted.")
    print(f"sound check: peak {peak:.0f} dBFS, ok")


def run(caps, args):
    os.makedirs(args.out, exist_ok=True)
    rig = Rig(args.channel, args.out)
    stop = {"flag": False}

    def on_sig(*_):
        stop["flag"] = True
        raise KeyboardInterrupt
    signal.signal(signal.SIGINT, on_sig)
    rig.panic()
    checked = False
    try:
        for c in caps:
            base = dict(BASE, **c["base"])
            print(f"\n== {c['name']}\nBY HAND: {c['hand']}")
            input("Press Enter when set (Ctrl-C to stop) ... ")
            rig.setp(base)
            if not checked:
                sound_check(rig, args.out)
                checked = True
            wav = os.path.join(args.out, f"{c['name']}.wav")
            meta = {"capture": c["name"], "group": c["group"], "wav": os.path.basename(wav),
                    "sample_rate": 48000, "note": NOTE, "velocity": VEL, "channel": args.channel,
                    "hand": c["hand"], "base": base, "markers": [], "steps": []}
            jpath = wav[:-4] + ".json"
            try:
                meta["host_ff_start"] = rig.start_ff(wav)
                time.sleep(LEAD_IN)
                rig.marker(base, meta["markers"])
                for s in c["steps"]:
                    rig.setp(s["set"])
                    time.sleep(SETTLE)
                    ev = {k: v for k, v in s.items() if k != "set"}
                    ev["set"] = s["set"]
                    rig.note(s["gate"], ev)
                    time.sleep(s["tail"])
                    ev["end"] = time.monotonic()
                    meta["steps"].append(ev)
                    print(f"  {s['param']}={s['value']} done")
                    time.sleep(GAP)
                    rig.setp({k: base[k] for k in s["set"]})
                rig.marker(base, meta["markers"])
            finally:
                rig.panic()
                rig.stop_ff()
                with open(jpath, "w") as f:
                    json.dump(meta, f, indent=1)
                print(f"  wrote {wav} + {os.path.basename(jpath)}")
    except KeyboardInterrupt:
        print("\nstopped: all notes off, ffmpeg stopped, JSON written for the partial capture")
    finally:
        rig.panic()
        rig.stop_ff()
        restore = {k: INIT_RESTORE[k] for k in rig.touched if k in INIT_RESTORE}
        rig.setp(restore)
        print(f"sent init values back for {len(restore)} parameters: {sorted(restore)}")
        print("For an exact restore reload the track's sound/project without saving.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--plan", action="store_true")
    g.add_argument("--run", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--channel", type=int, default=1, help="track 1 MIDI channel (1-16)")
    a = ap.parse_args()
    caps = build_plan()
    if a.only:
        want = a.only.split(",")
        bad = [w for w in want if w not in GROUPS]
        if bad:
            ap.error(f"unknown group(s) {bad}; choose from {GROUPS}")
        if "amp_hold" in want:
            print("amp_hold: the original Digitone has no AMP HOLD; skipped.")
        caps = [c for c in caps if c["group"] in want]
    if a.plan:
        print_plan(caps)
    else:
        run(caps, a)


if __name__ == "__main__":
    main()
