# SPDX-License-Identifier: GPL-3.0-only
"""A stand-in for python-rtmidi inside tools/fm1_install.py's MIDI worker (no hardware, no CoreMIDI), for
tests/install_test.py midi_side(), which puts this directory first on the worker's PYTHONPATH.

One port pair, "FM-1 fake": a ChoralRoot that answers the identity query, INFO, DEBUG and STUB, each reply with a note
right behind it (the firmware sends the notes and clock pulses that waited behind a SysEx in the reply's last USB
packet: usb.c ep1_tx; with python-rtmidi's input callback that is what stopped the old tool at its close).

FAKE_RTMIDI, a comma list:
  names, open, close   that call (get_ports, open_port, close_port) never returns, with the GIL held as python-rtmidi
                       holds it through every call (a C sleep through ctypes.PyDLL: no other thread of the worker runs)
  missing              no rtmidi to import
  vanish               as the firmware: 150 ms after its answer to STUB the FM-1 leaves the bus (its ports are no
                       longer listed), for half a second; it then is a unit that has just started (DEBUG's ms)
  late                 with vanish: the answer to STUB reaches rtmidi's queue only when this process has seen the
                       port gone (the answer and the leaving at once, in the worst order)
  mute                 with vanish: the answer to STUB never arrives
  no77                 STUB is not answered and nothing happens (a firmware without it)
  offline              with vanish: the FM-1 does not come back for CoreMIDI: devices() has it offline and on the USB
                       bus, until rescan()
  finishgone           an update's last "success" question reaches rtmidi's queue as the loader leaves the bus (it
                       does not wait for the answer), in the worst order as late; the new firmware is back 0.5 s later
FAKE_RTMIDI_NAME: another name for the port pair. FAKE_RTMIDI_PIDS: a file every worker appends its pid to.

FAKE_RTMIDI_STATE: a path. STATE holds the identity the FM-1 has now. With it the FM-1 also takes an update (the
upgrade command, its reads of STATE.image, the "success" questions: the fake device of tests/install_test.py) and
changes that identity as the real one restarts into the loader and into the new firmware: the next link's worker
reads it. STATE.bad appears when an answer of the tool was not the image's bytes. STATE.gone (from, to: the time the
FM-1 is off the bus), STATE.boot (when it last started, the stub request it started with), STATE.offline, and
STATE.log: a line for every open_port and close_port ("close PID in|out LISTED": was the port still listed?) and
every rescan."""
import ctypes
import os
import time

KNOBS = os.environ.get("FAKE_RTMIDI", "").split(",")
if "missing" in KNOBS:
    raise ImportError("no rtmidi (FAKE_RTMIDI=missing)")

import fm1_install as I  # noqa: E402  (tools/, the worker's own directory)

if os.environ.get("FAKE_RTMIDI_PIDS"):
    with open(os.environ["FAKE_RTMIDI_PIDS"], "a") as f:
        f.write(f"{os.getpid()}\n")

NAME = os.environ.get("FAKE_RTMIDI_NAME") or "FM-1 fake"
STATE = os.environ.get("FAKE_RTMIDI_STATE")
VERSION = b"ChoralRoot 0.17"
to_host = []                                      # what the FM-1 has sent: (ready(), message)
asks, waiting = [], None                          # an update: the reads to come, the one the tool has to answer
seen_gone = False                                 # this process has listed the ports and the FM-1 was not there


def never(what):
    while what in KNOBS:
        try:
            ctypes.PyDLL(None).sleep(3600)        # the GIL stays with this thread
        except (OSError, AttributeError, TypeError):   # (no libc sleep to call: the call still does not return)
            time.sleep(3600)


def state(ext, new=None):
    """STATE + ext: its text ('' when there is none), written first when new is given"""
    if not STATE:
        return ""
    if new is not None:
        with open(STATE + ext, "a" if ext == ".log" else "w") as f:
            f.write(new)
    try:
        with open(STATE + ext) as f:
            return f.read()
    except OSError:
        return ""


def gone():
    t = state(".gone").split()
    return bool(t) and float(t[0]) <= time.time() < float(t[1])


def identity(new=None):
    return (state("", new) or "FM-1_920").encode()


def debug_values():
    """--debug's numbers: the uptime first (ten minutes, or since the restart of STATE.boot); a stub (version 2)
    that has answered a test request (without STATE, or after a restart with one)"""
    boot = state(".boot").split()
    at = I.DEBUG_NAMES.index("stub_ms")
    v = [0] * (len(I.DEBUG_NAMES) + I.DEBUG_LOG) + [0, 0, 0, 0x100]
    v[0] = max(1, int((time.time() - float(boot[0])) * 1000)) if boot else 600000
    v[at:at + 5] = [61, 61000, 2, 1, (1 << 26) if not STATE or boot[1:] == ["1"] else 0]
    return v


def ask():
    global waiting
    if asks:
        waiting = addr, n = asks.pop(0)
        u = bytearray([0, 0x59, 0x30, 0, 0, 0, 0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        u.append(~sum(u[6:14]) & 0xFF)
        ready = None
        if addr == I.FINISH_WRITE and "finishgone" in KNOBS:
            state(".gone", f"{time.time()} {time.time() + 0.5}")
            identity("FM-1_900")
            waiting, ready = None, (lambda: seen_gone)
        to_host.append((ready, list(b"\xF0" + I.pack7(u) + b"\xF7")))


def answered(msg):
    """the tool's answer to the read (or the "success" question) that waits: checked; the last one restarts"""
    global waiting
    (addr, n), waiting = waiting, None
    u = I.unpack7(msg[1:-1])
    with open(STATE + ".image", "rb") as f:
        want = b"success\0" if addr >= I.FINISH_CHECK else f.read()[addr:addr + n]
    if u[14:14 + len(want)] != want or len(u) != 15 + len(want):
        open(STATE + ".bad", "w").close()
    if addr == I.FINISH_CHECK:
        identity("ota-FM-1_900")
    elif addr == I.FINISH_WRITE:
        identity("FM-1_900")
    else:
        ask()


def fm1(msg):
    msg, r, ready = bytes(msg), None, None
    if gone():
        return                                    # (nobody there)
    if msg == I.HS_QUERY:
        t = identity()
        r = b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7"
    elif msg == I.UPGRADE and STATE:
        asks[:] = ([(k * 512, 512) for k in range(6)] + [(I.FINISH_WRITE, 8)] if identity().startswith(b"ota-") else
                   [(0, 64), (0x40, 160), (0x1000, 512), (I.FINISH_CHECK, 8)])
        ask()
    elif waiting:
        answered(msg)
    elif msg[:4] == I.BK_HDR and msg[-1] == 0xF7 and not identity().startswith(b"ota-"):
        d = debug_values()
        b = {I.BK_INFO: [*VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3], I.BK_STUB: [0],
             I.BK_DEBUG: [1, len(d)] + [x for v in d for x in I.bk_u32(v)]}.get(msg[4])
        if msg[4] == I.BK_STUB and "no77" in KNOBS:
            b = None
        elif msg[4] == I.BK_STUB and "vanish" in KNOBS:   # the answer, and 150 ms later the bus is left
            t = time.time() + 0.15
            state(".gone", f"{t} {1e18 if 'offline' in KNOBS else t + 0.5}")
            state(".boot", f"{t + 0.5} {msg[5]}")
            if "offline" in KNOBS:
                state(".offline", "1")
            if "mute" in KNOBS:
                b = None
            elif "late" in KNOBS:
                ready = lambda: seen_gone         # noqa: E731
        if b is not None:
            r = I.BK_HDR + bytes([msg[4], *b, 0xF7])
    if r:
        to_host.extend([(ready, list(r)), (ready, [0x90, 60, 100])])


def devices():
    """the worker's midi_devices(): CoreMIDI's devices with their offline mark, the names on the USB bus"""
    off = bool(state(".offline"))
    return {"midi": [["Some Interface", False], [NAME, off or gone()], ["Felucca Update", True]],
            "usb": ["Some Hub"] + [NAME] * (off or not gone())}


def rescan():
    """the worker's midi_rescan(): the FM-1 that CoreMIDI kept offline is there again"""
    state(".log", "rescan\n")
    if state(".offline"):
        os.remove(STATE + ".offline")
        state(".gone", "0 0")
        state(".boot", f"{time.time()} 1")
    return 0


class Port:
    kind = "?"

    def __init__(self, *a, **k):
        self.opened = False

    def get_ports(self):
        global seen_gone
        never("names")
        seen_gone = seen_gone or gone()
        return [] if gone() else [NAME]

    def open_port(self, port=0, name=None):
        never("open")
        self.opened = True
        state(".log", f"open {os.getpid()} {self.kind}\n")

    def close_port(self):
        never("close")
        self.opened = False
        state(".log", f"close {os.getpid()} {self.kind} {0 if gone() else 1}\n")

    def delete(self):
        pass


class MidiIn(Port):
    kind = "in"

    def ignore_types(self, sysex=True, timing=True, active_sense=True):
        pass

    def get_message(self):
        if to_host and (to_host[0][0] is None or to_host[0][0]()):
            return to_host.pop(0)[1], 0.0
        return None


class MidiOut(Port):
    kind = "out"

    def send_message(self, msg):
        fm1(msg)
