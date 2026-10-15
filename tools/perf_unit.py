#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The audio ISR's worst cases ON THE UNIT, scenario by scenario, over USB alone (docs/DUALCORE.md "Geek Out 2",
docs/FX.md "What the unit says"): MIDI notes into "ChoralRoot FM-1" and SysEx (76 SELECT for the sounds, 73 DEBUG and
78 PEAK for the figures; tools/fm1_install.py's protocol).

  python3 tools/perf_unit.py [--port NAME] [--seconds 20] [--record] [--no-restore] [SCENARIO ...]

A scenario: the chord part's sound and the bass's (76 SELECT: engine, pool position), then an 8-note chord on the
Perform channel (1) and a bass note on the Bass channel (2), held `--seconds` s and moved up a semitone every 2 s so
voices are stolen as they are when playing; the counters are read before (78 PEAK's worst cases are "since the last
read": the read clears them) and after. With --record, ffmpeg records "ChoralRoot In" (the USB recording at the
computer's rate, 44.1 kHz unless Options > USB Rate offers 48 and the computer takes it) through the scenario.
The sounds the parts held are the session's, in RAM: the unit comes back to its saved session at the next power-on,
or at once with `fm1_install.py --restore` of a backup (--no-restore leaves the last scenario's sounds in place; the
default is to put the power-on sounds back, FM6 TINE EP and VA DEEP SUB, with SELECT).

Scenarios: idle (nothing played), fm6 (TINE EP + DEEP SUB), va (MORPH PAD + PUNCH BASS: the VA's heaviest), cz (JET
ROAR + SYNTH.BASS: a CZ-1 chord, its most expensive case), quad (WOBBLE + BASS: FM TONE's), all (the five, in that
order). Engines: ChoralRoot's numbers (engines.c ENGINES[]): FM6 12, VA 13, CZ-1 14, FM TONE 15; the position: 0
INIT, 1.. the factory presets in the pool's order (docs/PRESETS.md).

What the table says (us of the 2902 us half; the worst since the read before the scenario): block = the largest
whole half (TIMER5 left out); tick part0 part1 fx master wait = each stage's largest; t5 = the most TIMER5 took of
one half; wall = the longest half by the clock, TIMER5 in it; late / stale = halves that ended after their boundary /
in which the DAC played something old (the audible ones: 0 is the rule); gap = TIMER5's longest absence (the
interrupts masked for about that, less 100). The emulator's estimates are not these: this is the unit.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm1_install as fi  # noqa: E402

ENG = {'fm6': 12, 'va': 13, 'cz': 14, 'quad': 15}
CHORD = [48, 52, 55, 59, 62, 66, 69, 72]        # C3 E3 G3 B3 D4 F#4 A4 C5: an 8-note voicing (a maj13); --notes N
BASS = 36                                       # takes the first N
SCENARIOS = {                                   # name: (chord engine, pos, bass engine, pos)
    'idle': None,
    'fm6': (ENG['fm6'], 1, ENG['va'], 17),      # TINE EP + DEEP SUB (the power-on sounds)
    'va': (ENG['va'], 21, ENG['va'], 18),       # MORPH PAD + PUNCH BASS (docs/VA.md: the heaviest)
    'cz': (ENG['cz'], 62, ENG['cz'], 13),       # JET ROAR + SYNTH.BASS (docs/CZ1.md: the most expensive)
    'quad': (ENG['quad'], 10, ENG['quad'], 3),  # WOBBLE + BASS
}
ORDER = ['idle', 'fm6', 'va', 'cz', 'quad']
COLS = ['pk_block_us', 'pk_tick_us', 'pk_part0_us', 'pk_part1_us', 'pk_fx_us', 'pk_master_us', 'pk_wait_us',
        'pk_t5_us', 'pk_wall_us', 'pk_late', 'stale_halves', 'irq_gap_us', 'voices_shed', 'cpu_avg_us',
        'st_part0_us', 'st_fx_us']
HEAD = ['block', 'tick', 'part0', 'part1', 'fx', 'master', 'wait', 't5', 'wall', 'late', 'stale', 'gap', 'shed',
        'avg', 'p0avg', 'fxavg']               # (avg, p0avg, fxavg: 73 DEBUG's last second, the chord held; shed:
                                               # the voices the overload shed dropped since power-on, 78 PEAK)


def connect(port):
    up = fi.Updater(fi.MidiBackend(), port)
    dev = up.find()
    if not dev or dev.id.loader:
        if dev:
            dev.link.close()
        raise SystemExit('no running FM-1 found')
    return dev


def select(port, part, eng, pos):
    dev = connect(port)
    try:
        r = fi.BackupLink(dev.link).request(fi.BK_SELECT, [part, eng, pos & 127, pos >> 7], 1.5)
    finally:
        dev.link.close()
    if len(r) != 1 or r[0]:
        raise SystemExit(f'SELECT part {part} engine {eng} pos {pos}: {fi.SELECT_RC.get(r[0] if r else -1, r)}')


def read(port):
    """73 DEBUG and 78 PEAK as one dict (the PEAK read clears the worst cases)"""
    dev = connect(port)
    try:
        r = fi.BackupLink(dev.link).request(fi.BK_DEBUG, [], 1.5)
        pk = fi.BackupLink(dev.link).request(fi.BK_PEAK, [], 1.0)
    finally:
        dev.link.close()
    d = dict(zip(fi.DEBUG_NAMES, [fi.bk_r32(r, 2 + 5 * k) for k in range(r[1])]))
    d.update(zip(fi.DEBUG_PEAK, [fi.bk_r32(pk, 2 + 5 * k) for k in range(pk[1])]))
    return d


class Notes:
    def __init__(self, n):
        import rtmidi
        self.n = n
        self.m = rtmidi.MidiOut()
        ports = self.m.get_ports()
        names = [p for p in ports if 'ChoralRoot' in p]
        if not names:
            raise SystemExit(f'no "ChoralRoot FM-1" MIDI port (ports: {ports})')
        self.m.open_port(ports.index(names[0]))

    def chord(self, on, shift):
        st = 0x90 if on else 0x80
        for n in CHORD[:self.n]:
            self.m.send_message([st, n + shift, 100 if on else 0])
        self.m.send_message([st | 1, BASS + shift, 100 if on else 0])

    def close(self):
        self.m.close_port()


def play(notes, seconds):
    t0 = time.monotonic()
    shift = 0
    while time.monotonic() - t0 < seconds:
        notes.chord(True, shift)
        time.sleep(2.0)
        notes.chord(False, shift)
        shift = (shift + 1) % 6
        time.sleep(0.05)


def row(name, d):
    return f"{name:<10}" + ''.join(f"{d.get(k, 0):>7}" for k in COLS)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('scenarios', nargs='*', help='idle fm6 va cz quad, or all (default: all)')
    p.add_argument('--port', help='MIDI port name (default: found as fm1_install.py finds it)')
    p.add_argument('--seconds', type=float, default=20, help='how long each scenario plays (default 20)')
    p.add_argument('--record', action='store_true', help='record "ChoralRoot In" with ffmpeg through each scenario')
    p.add_argument('--no-restore', action='store_true', help='leave the last scenario\'s sounds on the parts')
    p.add_argument('--notes', type=int, default=8, help='notes in the chord, 1..8 (default 8)')
    a = p.parse_args()
    if not 1 <= a.notes <= len(CHORD):
        p.error('--notes is 1..8')
    names = a.scenarios or ['all']
    if 'all' in names:
        names = ORDER
    for n in names:
        if n not in SCENARIOS:
            p.error(f'unknown scenario {n!r} (idle fm6 va cz quad all)')
    notes = Notes(a.notes)
    print(f"{'scenario':<10}" + ''.join(f"{h:>7}" for h in HEAD) + f"  ({a.notes} notes + bass"
          + (', recording)' if a.record else ')'))
    for n in names:
        s = SCENARIOS[n]
        if s:
            select(a.port, 0, s[0], s[1])
            select(a.port, 1, s[2], s[3])
            time.sleep(0.5)
        rec = None
        if a.record:
            rec = subprocess.Popen(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'avfoundation',
                                    '-i', ':ChoralRoot In', '-t', str(a.seconds + 3), '-f', 'null', '-'])
            time.sleep(1.5)                     # (the stream open and the first packets)
        read(a.port)                            # (clears PEAK)
        if s:
            play(notes, a.seconds)
        else:
            time.sleep(a.seconds)
        d = read(a.port)
        if rec:
            rec.wait()
        print(row(n, d), flush=True)
    notes.close()
    if not a.no_restore:
        select(a.port, 0, ENG['fm6'], 1)
        select(a.port, 1, ENG['va'], 17)
    return 0


if __name__ == '__main__':
    sys.exit(main())
