#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure ChoralRoot In (the FM-1's USB recording) on a Mac: record it with ffmpeg while MIDI chords play the FM-1,
and report what the FM-1 itself sent during the capture (docs/USB-AUDIO.md, "Measuring the recording on a Mac").

usage: tools/uac_check.py OUT.wav [SECONDS] [--nonotes] [--debug] [--device NAME]

  SECONDS     the capture's wall time (default 14)
  --nonotes   no chords: the stream's behaviour in silence
  --debug     also print the counters as read, before and after (fm1_install.py --debug's names)
  --device    the capture device's name (default "ChoralRoot In")

The measure is the unit's own counters (SysEx 73 DEBUG, read before and after the capture through fm1_install.py's
MIDI side), not the WAV: ffmpeg's avfoundation capture is itself short (56.70 s of audio in a 60 s capture, from
ChoralRoot In and from an unrelated audio interface alike; 12 % short over an hour on a busy Mac), so the WAV's
length against the wall time measures ffmpeg. What is printed, from the difference of the two reads:

  opens       how often the host opened the stream during the capture (one per recording; each further one is a
              restart of the stream by the host: a gap in the recording)
  the open    the length of the last whole open against the wall time asked, and the frames the FM-1 sent in it
              against 44100 Hz x that length
  packets     against the USB frames (SOFs) the FM-1 saw while the stream was open: one a frame
  overruns    the host did not read and the ring ran over. One is counted at each close of the stream too (the
              host stops reading a moment before it closes it): said in the output, not subtracted in silence
  underruns, missed frames   the ring ran dry; USB frames that passed without a service of the endpoint
  nudges      a 45-frame packet in place of a 44 (or the reverse): the FM-1's audio clock against the computer's
              USB clock, in ppm

A healthy run: one open as long as the capture, frames within a fraction of a percent of 44100 Hz x its length (the
host's first read comes ~14 ms after the open, the FM-1's audio clock runs about +0.04 % over 44100), no underrun,
no missed frame, no overrun but the close's. The reference, an hour with the Mac busy (2026-10-10): 3 597 711
packets for 3 597 791 SOFs, 0 underruns, 12 overruns / 189 frames (nine in two minutes of heavy host load, three at
stream closes), 69 036 nudges up = about +435 ppm.

Then the WAV: its length as what ffmpeg wrote, the loud 50 ms windows, and every silent gap of 0.5 s or more while
chords were sent (that analysis is of the recording as a program gets it, so it stays).

Needs ffmpeg (avfoundation) and python-rtmidi. The chords are C major, 0.6 s on and 0.4 s off, to the first MIDI
output whose name contains "ChoralRoot". Nothing here writes the FM-1 (no install, no settings)."""
import argparse
import math
import os
import struct
import subprocess
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fm1_install as fi                                # noqa: E402  (the MIDI side: every call of it has a bound)

RATE = 44100
PACKET_FRAMES = 44.1                                    # the packet pattern (usb_audio_stream.c): 44 and 45 frames
RAW_KEYS = ("ms", "ua_cap_alt", "ua_opens", "ua_open_ms", "ua_open_close_ms", "ua_frames_this_open",
            "ua_frames_last_open", "ua_tx_packets", "ua_sofs", "ua_overruns", "ua_underruns", "ua_missed_frames",
            "ua_nudges_up", "ua_nudges_down", "ua_holds", "ua_first_take_ms", "ua_pk528", "ua_pk540", "ua_pk_other",
            "usb_stalls", "usb_resets")
OPEN_SLACK_MS = 100                                     # an open's own: the first read ~14 ms in, the last before the close


def num(n):
    return f"{n:,}".replace(",", " ")


def counters(up):
    """the unit's counters now (73 DEBUG) -> ({name: value}, None), or (None, why not). The link is opened for the
    read and closed by its worker, as fm1_install.py --debug does"""
    try:
        dev = up.find()
        if not dev or dev.id.loader:
            if dev:
                dev.link.close()
            return None, ("the FM-1 is in update mode" if dev else "no FM-1 answers on USB-MIDI" +
                          (f" ({up.gave_up} did not return in time)" if up.gave_up else ""))
        try:
            vals = fi.debug_values(dev.link, 3.0)
        finally:
            dev.link.close()
    except fi.InstallError as e:
        return None, str(e)
    d = dict(zip(fi.DEBUG_NAMES, vals))
    return (d, None) if "ua_opens" in d else (None, "this firmware has no USB recording counters (73 DEBUG is shorter)")


def unit_delta(before, after):
    """what the unit did between two reads of its counters -> a dict of numbers (pure: the tests call it).
    restarted: its uptime went back, so the first read counts for nothing. closes: opens + was open - is open.
    whole: a close fell into the capture, so ua_open_close_ms / ua_frames_last_open are this capture's last open.
    overruns_open: the overruns beyond one per close (see the module's text). short_ms: the open's length less the
    frames sent in it at 44100 Hz (negative: more frames than that, the audio clock's +0.04 % over a long open)"""
    a = dict(after)
    restarted = a.get("ms", 0) < before.get("ms", 0)
    b = {} if restarted else dict(before)

    def d(k):
        return a.get(k, 0) - b.get(k, 0)
    opens, was_open, is_open = d("ua_opens"), bool(b.get("ua_cap_alt", 0)), bool(a.get("ua_cap_alt", 0))
    closes = opens + was_open - is_open
    whole = closes > 0 and a.get("ua_open_close_ms", 0) > 0
    last_ms = a.get("ua_open_close_ms", 0) if whole else 0
    frames = a.get("ua_frames_last_open", 0) if whole else 0
    packets, up_, down = d("ua_tx_packets"), d("ua_nudges_up"), d("ua_nudges_down")
    overruns = d("ua_overruns")
    return {"restarted": restarted, "opens": opens, "closes": closes, "was_open": was_open, "is_open": is_open,
            "whole": whole, "last_ms": last_ms, "open_ms": d("ua_open_ms"), "frames": frames,
            "frames_ratio": frames / (RATE * last_ms / 1000) if last_ms else None,
            "short_ms": last_ms - frames * 1000 / RATE if last_ms else None,   # audio at 44100 Hz not sent in the open
            "packets": packets, "sofs": d("ua_sofs"), "overruns": overruns,
            "overruns_open": max(0, overruns - max(0, closes)), "underruns": d("ua_underruns"),
            "missed": d("ua_missed_frames"), "nudges_up": up_, "nudges_down": down,
            "ppm": (up_ - down) / (packets * PACKET_FRAMES) * 1e6 if packets > 0 else None}


def unit_report(before, after, wall_s):
    """the verdict's lines: what the FM-1 did during a capture of wall_s seconds (pure: the tests call it)"""
    u = unit_delta(before, after)
    out = [f"the FM-1 during the capture (its own counters, read before and after; {wall_s:g} s asked of ffmpeg):"]
    odd = []
    if u["restarted"]:
        out.append("  the FM-1 restarted during the capture (its uptime went back): the counters below are since that start")
        odd.append("the FM-1 restarted")
    was = " (it was open before the capture: another program records too?)" if u["was_open"] else ""
    still = "; still open now (another program holds it)" if u["is_open"] else ""
    out.append(f"  opens of the stream: {u['opens']}, closes: {u['closes']}{was}{still}"
               "   (one open per recording; each further one is a restart of the stream by the host: a gap)")
    if u["opens"] > 1:
        odd.append(f"the host opened the stream {u['opens']} times")
    elif u["opens"] == 0 and not u["was_open"]:
        odd.append("the stream was never opened (did ffmpeg find the device?)")
    if u["whole"]:
        out.append(f"  the last whole open: {u['last_ms'] / 1000:.3f} s = {u['last_ms'] / 1000 / max(wall_s, 1e-9):.3f} x the "
                   f"{wall_s:g} s asked; open in all: {u['open_ms'] / 1000:.3f} s")
        out.append(f"  frames sent in it: {num(u['frames'])} = {u['frames'] / RATE:.3f} s at {RATE} Hz = "
                   f"{u['frames_ratio']:.5f} x its length ({abs(u['short_ms']):.0f} ms "
                   f"{'less' if u['short_ms'] >= 0 else 'more'} than {RATE} Hz x its length; the host's first read comes "
                   "~14 ms after the open, the FM-1's audio clock runs ~+0.04 %)")
        if u["short_ms"] > OPEN_SLACK_MS:
            odd.append(f"{u['short_ms']:.0f} ms of the open without audio sent")
        if u["opens"] == 1 and u["last_ms"] < 900 * wall_s:
            odd.append(f"the open lasted {u['last_ms'] / 1000:.1f} s of the {wall_s:g} s")
    else:
        out.append(f"  no whole open to measure (no close of the stream fell into the capture); open in all: "
                   f"{u['open_ms'] / 1000:.3f} s" +
                   (f", frames in this open so far: {num(after.get('ua_frames_this_open', 0))}" if u["is_open"] else ""))
    if u["sofs"] > 0:
        out.append(f"  packets: {num(u['packets'])} for {num(u['sofs'])} USB frames (SOFs) while open: "
                   f"{num(u['sofs'] - u['packets'])} frames without a packet")
    else:
        out.append(f"  packets: {num(u['packets'])}")
    out.append(f"  overruns: {u['overruns']}   (one is counted at each close of the stream, when the host stops reading "
               f"a moment before it closes it; {max(0, u['closes'])} close{'s' if u['closes'] != 1 else ''} here: "
               f"{u['overruns_open'] or 'none'} more than the closes account for)")
    out.append(f"  underruns: {u['underruns']}, missed frames: {u['missed']}")
    if u["overruns_open"]:
        odd.append(f"{u['overruns_open']} overrun{'s' if u['overruns_open'] != 1 else ''} beyond the closes' (the host "
                   "did not read for a moment while the stream ran)")
    if u["underruns"]:
        odd.append(f"{u['underruns']} underrun{'s' if u['underruns'] != 1 else ''} (the ring ran dry)")
    if u["missed"]:
        odd.append(f"{u['missed']} USB frame{'s' if u['missed'] != 1 else ''} missed")
    if u["ppm"] is not None:
        out.append(f"  nudges: {num(u['nudges_up'])} up, {num(u['nudges_down'])} down = the FM-1's audio clock "
                   f"{u['ppm']:+.0f} ppm against the computer's USB clock (absorbed: a 45-frame packet in place of a 44)")
    out.append("  verdict: " + ("the FM-1 sent the stream whole" if not odd else "; ".join(odd)))
    return out


def wav_report(path, secs, nonotes, sent):
    """the WAV as ffmpeg wrote it: its length (ffmpeg's, not the FM-1's), the loud windows, the silent gaps"""
    try:
        w = wave.open(path, "rb")
    except (OSError, EOFError, wave.Error) as e:
        return [f"{path}: ffmpeg wrote no WAV that can be read ({e}): is the device's name right (--device)?"]
    nch, sw, fr, nf = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    data = w.readframes(nf)
    w.close()
    win = fr // 20                                       # 50 ms windows
    fmt = "<%dh" % (nch * win)
    rms = []
    for i in range(0, nf - win, win):
        s = struct.unpack(fmt, data[i * nch * sw:(i + win) * nch * sw])
        rms.append(math.sqrt(sum(x * x for x in s) / len(s)))
    loud = [r > 200 for r in rms]
    first = next((i for i, v in enumerate(loud) if v), None)
    out = [f"{path}: what ffmpeg wrote: {nf / fr:.2f} s ({nch} ch, {fr} Hz) of the {secs:g} s asked = "
           f"{nf / fr / max(secs, 1e-9):.3f} (ffmpeg's avfoundation capture is itself short by 5 to 12 %: not a measure of the FM-1)",
           f"  loud 50 ms windows {sum(loud)} of {len(rms)}, first loud at "
           f"{first * 0.05 if first is not None else None} s   (chords sent: {sent})"]
    gaps, run, start = [], 0, None                       # silent runs >= 0.5 s while notes were being sent
    for i in range(first or 0, min(len(loud), int((secs - 3.5) * 20))):
        if not loud[i]:
            run += 1
            start = i if start is None else start
        else:
            if run >= 10:
                gaps.append((round(start * 0.05, 2), round(run * 0.05, 2)))
            run, start = 0, None
    if run >= 10:
        gaps.append((round(start * 0.05, 2), round(run * 0.05, 2)))
    out.append(f"  silent gaps >= 0.5 s while notes were sent (start s, length s): {gaps if not nonotes else '(no notes)'}")
    return out


def raw(label, d):
    print(f"--- the counters {label}")
    if d:
        for k in RAW_KEYS:
            if k in d:
                print(f"  {k} {d[k]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("seconds", nargs="?", type=float, default=14.0)
    ap.add_argument("--nonotes", action="store_true")
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("--device", default="ChoralRoot In")
    a = ap.parse_args()
    secs = a.seconds
    up = fi.Updater(fi.MidiBackend())                    # the port list and the open, each with its bound
    before, why = counters(up)
    if before is None:
        print(f"the FM-1's counters could not be read ({why}): no verdict on the FM-1 below, the WAV only")
    port_name = next((n for n in up.names(outs=True) if "ChoralRoot" in n), None)
    if port_name is None and not a.nonotes:
        sys.exit("no ChoralRoot MIDI output (use --nonotes to record without notes)" +
                 (f": {up.gave_up} did not return in time" if up.gave_up else ""))
    try:
        rec = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "avfoundation", "-i", f":{a.device}",
                                "-t", str(secs), "-ac", "2", "-ar", "44100", a.out])
    except OSError as e:
        sys.exit(f"ffmpeg did not start: {e}")
    time.sleep(2.0)                                      # the stream settles before the notes
    sent = 0
    if not a.nonotes:
        port = up.open(None, port_name)                  # an output only
        if not port:
            rec.kill()
            sys.exit(f"{port_name} did not open")
        t0 = time.time()
        while time.time() - t0 < secs - 3.0:
            for n in (60, 64, 67):
                port.send(bytes([0x90, n, 100]))         # note on, channel 1
            sent += 1
            time.sleep(0.6)
            for n in (60, 64, 67):
                port.send(bytes([0x80, n, 0]))           # note off
            time.sleep(0.4)
        port.close()
    try:
        rec.wait(timeout=secs + 30.0)
    except subprocess.TimeoutExpired:                    # (ffmpeg's -t did not end it: it is ended, the WAV is what it is)
        rec.kill()
        rec.wait()
        print(f"ffmpeg did not end {secs + 30:g} s after its start: killed")

    after = None
    if before is not None:
        for _ in range(3):                               # the host closes the stream a moment after ffmpeg has left
            time.sleep(1.0)
            after, why = counters(up)
            if after is None or not after.get("ua_cap_alt"):
                break
        if after is None:
            print(f"the FM-1's counters could not be read after the capture ({why}): no verdict on the FM-1")
        else:
            print("\n".join(unit_report(before, after, secs)))
    print("\n".join(wav_report(a.out, secs, a.nonotes, sent)))
    if a.debug:
        raw("before", before)
        raw("after", after)


if __name__ == "__main__":
    fi.leave(fi.exit_code(main))                         # (no MIDI worker is left, nothing holds the exit)
