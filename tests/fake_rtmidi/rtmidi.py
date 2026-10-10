# SPDX-License-Identifier: GPL-3.0-only
"""A stand-in for python-rtmidi inside tools/fm1_install.py's MIDI worker (no hardware, no CoreMIDI), for
tests/install_test.py midi_side(), which puts this directory first on the worker's PYTHONPATH.

One port pair, "FM-1 fake": a ChoralRoot that answers the identity query, INFO, DEBUG and STUB, each reply with a note
right behind it (the firmware sends the notes and clock pulses that waited behind a SysEx in the reply's last USB
packet: usb.c ep1_tx; with python-rtmidi's input callback that is what stopped the old tool at its close).

FAKE_RTMIDI, a comma list, makes a call never return, with the GIL held as python-rtmidi holds it through every call
(a C sleep through ctypes.PyDLL: no other thread of the worker runs): names (get_ports), open (open_port), close
(close_port). missing: no rtmidi to import. FAKE_RTMIDI_NAME: another name for the port pair. FAKE_RTMIDI_PIDS: a
file every worker appends its pid to.

FAKE_RTMIDI_STATE: a file with the identity the FM-1 has now. With it the FM-1 also takes an update (the upgrade
command, its reads of STATE.image, the "success" questions: the fake device of tests/install_test.py) and changes
that identity as the real one restarts into the loader and into the new firmware: the next link's worker reads it.
STATE.bad appears when an answer of the tool was not the image's bytes."""
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
to_host = []                                      # what the FM-1 has sent
asks, waiting = [], None                          # an update: the reads to come, the one the tool has to answer


def identity(new=None):
    if not STATE:
        return b"FM-1_920"
    if new:
        with open(STATE, "wb") as f:
            f.write(new)
    with open(STATE, "rb") as f:
        return f.read()


def ask():
    global waiting
    if asks:
        waiting = addr, n = asks.pop(0)
        u = bytearray([0, 0x59, 0x30, 0, 0, 0, 0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        u.append(~sum(u[6:14]) & 0xFF)
        to_host.append(list(b"\xF0" + I.pack7(u) + b"\xF7"))


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
        identity(b"ota-FM-1_900")
    elif addr == I.FINISH_WRITE:
        identity(b"FM-1_900")
    else:
        ask()


def never(what):
    while what in KNOBS:
        try:
            ctypes.PyDLL(None).sleep(3600)        # the GIL stays with this thread
        except (OSError, AttributeError, TypeError):   # (no libc sleep to call: the call still does not return)
            time.sleep(3600)


def debug_values():
    """--debug's numbers of a unit whose stub (version 2) has just answered a test request"""
    at = I.DEBUG_NAMES.index("stub_ms")
    v = [0] * (len(I.DEBUG_NAMES) + I.DEBUG_LOG) + [0, 0, 0, 0x100]
    v[at:at + 5] = [61, 61000, 2, 1, 1 << 26]
    return v


def fm1(msg):
    msg, r = bytes(msg), None
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
        if b is not None:
            r = I.BK_HDR + bytes([msg[4], *b, 0xF7])
    if r:
        to_host.extend([list(r), [0x90, 60, 100]])


class Port:
    def __init__(self, *a, **k):
        self.opened = False

    def get_ports(self):
        never("names")
        return [NAME]

    def open_port(self, port=0, name=None):
        never("open")
        self.opened = True

    def close_port(self):
        never("close")
        self.opened = False

    def delete(self):
        pass


class MidiIn(Port):
    def ignore_types(self, sysex=True, timing=True, active_sense=True):
        pass

    def get_message(self):
        return (to_host.pop(0), 0.0) if self.opened and to_host else None


class MidiOut(Port):
    def send_message(self, msg):
        fm1(msg)
