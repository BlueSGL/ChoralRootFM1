#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Why tools/fm1_install.py keeps its MIDI ports in worker processes and polls them ("MIDI" in its source): the hang
of python-rtmidi's close_port, shown without an FM-1 (macOS, python-rtmidi; by hand, not part of the automated tests).

  python3 tests/midi_close_hang.py          an input with a callback (what mido's rtmidi backend always sets)
  python3 tests/midi_close_hang.py --poll   an input that is polled (get_message), as the tool's worker does

A second process sends notes on a virtual port of its own (no other port is opened; its name matches nothing
fm1_install.py looks for). This one opens and closes an input on it 200 times. With the callback it stops within a
few closes, for good: close_port (MIDIPortDispose) holds the GIL and waits for the delivery in progress, the delivery
(CoreMIDI's thread, python-rtmidi's callback) waits for the GIL. faulthandler, which needs no GIL, prints the stacks
after 5 s and ends the process (exit 1): the main thread at its close_port line, a thread without a Python frame
beside it. Polled, all 200 closes return (exit 0)."""
import faulthandler
import os
import subprocess
import sys
import time

import rtmidi

if sys.argv[1:2] == ["--send"]:                   # the other process: notes, 2000 a second, while its parent lives
    o, parent = rtmidi.MidiOut(), os.getppid()
    o.open_virtual_port(sys.argv[2])
    print("up", flush=True)
    while os.getppid() == parent:
        o.send_message([0x90, 60, 100])
        time.sleep(0.0005)
    os._exit(0)

name = f"close-hang-{os.getpid()}"
sender = subprocess.Popen([sys.executable, os.path.abspath(__file__), "--send", name], stdout=subprocess.PIPE)
sender.stdout.readline()
time.sleep(0.5)
poll, got = "--poll" in sys.argv, [0]
try:
    for k in range(200):
        m = rtmidi.MidiIn()
        if not poll:
            m.set_callback(lambda msg, data: got.__setitem__(0, got[0] + 1))
        m.open_port(m.get_ports().index(name))
        time.sleep(0.02)
        while poll and m.get_message():
            got[0] += 1
        faulthandler.dump_traceback_later(5, exit=True)     # (on a hang nothing of Python runs again, not `finally`
        m.close_port()                                      # either: the sender ends when this process has)
        faulthandler.cancel_dump_traceback_later()
        m.delete()
        print(f"\rclose {k + 1} returned ({got[0]} notes in)", end="", flush=True)
    print("\nall 200 closes returned")
finally:
    sender.kill()
