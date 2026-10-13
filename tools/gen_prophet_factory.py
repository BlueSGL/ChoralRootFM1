#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""PROPHET's factory programs: assets/prophet5-factory/prophet5-v1.03.syx (Sequential's 200 Prophet-5 / 10 factory
programs v1.03, unchanged native dumps; assets/prophet5-factory/README.md) -> a header for eng_prophet_test.c
(FELUCCA_P5_FACTORY: a build with 0 does not include it).

Melodee's tools/gen_prophet_factory.py (Kerem Kilic, GPL-3.0-only; its v1.2.1: the packed form of commit 1bc4555),
without its web editor's block. ChoralRoot's addition: P5_FACTORY_PRESETS(X), the PRESETS names (12 characters, upper
case as the other engines' are; the full 20-character name stays in the program's bytes).

  P5_FACTORY_N          200
  P5_FBASE / P5_FWIDTH  per raw byte k of the 133: the smallest value in the bank, the bits its range needs
  P5_FPACK              the programs, P5_FACTORY_BYTES each: raw byte k is P5_FBASE[k] + the next P5_FWIDTH[k] bits, LSB
                        first, a program starts on a byte boundary (eng_prophet_test.c p5_factory unpacks; checked here)
  P5_FACTORY_PRESETS(X) X(name) for each program, in the bank's order

  tools/gen_prophet_factory.py OUT.h
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/prophet5-factory/prophet5-v1.03.syx"
SHA256 = "1d6e9dec2753a08ac6d07b98f0f4e755e33b41de11dc3bed46fa23217619d232"
# the programs' names where they do not fit the 12 characters of a PRESETS name (upper case)
SHORT = {
    "IT'S A PROPHET 5": "ITS PROPHET5", "FORMANT SERAPHIM": "F. SERAPHIM", "FANTASTIC FIVE": "FANTASTIC 5",
    "DIGI-RECORDER": "DIGI-RECORDR", "REEDY STRING PAD": "REEDY STRPAD", "CANNED HAPPINESS": "CANNED HAPPY",
    "BORN IN THE 80'S": "BORN IN 80S", "STICKY PLUCKS": "STICKY PLUCK", "WATERY PLUCKS": "WATERY PLUCK",
    "TREMBLING ORGAN": "TREMBL ORGAN", "UNICORN DREAMS": "UNICORN DRMS", "COCKTAIL HOUR": "COCKTAIL HR",
    "NICE N SMOOTH": "NICE SMOOTH", "TUMESCENT BASS": "TUMESC BASS", "BLINKER BELLS": "BLINKR BELLS",
    "PICKLE PINCHER": "PICKLE PINCH", "UNDERWATER TRILL": "UWATER TRILL", "TANGERINE CHRCH THM": "TANGERINE CH",
    "PIPE ORGANISATION": "PIPE ORGANIS", "FAT POLY BASS": "FAT POLYBASS", "CHORAL VOICES": "CHORAL VOICE",
    "ANOTHER OCEAN": "ANOTHR OCEAN", "PEDALING SWEEP": "PEDAL SWEEP", "HUM-ALONG SOLO": "HUM-ALONG",
    "FAT BASS STACK": "FAT BASS STK", "AFTERBLOW HORN": "AFTRBLW HORN", "NO PARKING SWELL": "NOPARK SWELL",
    "INSTABILITY PAD": "INSTABIL PAD", "VINTAGE WURLY": "VINTAGE WRLY", "ROBOT HAMSTERS": "ROBOT HAMSTR",
    "PRESSURE POINTS": "PRESSURE PTS", "80S HORROR STRINGS": "80S HORROR", "SOFT MOVING BRASS": "MOVING BRASS",
    "MORSE CODE LEAD": "MORSE LEAD", "UNDER THE ICE": "UNDER ICE", "DISTANT FUTURE": "DISTANT FUTR",
    "HOLLOW DREAMS": "HOLLOW DRMS", "MOUSE BLASTER": "MOUSE BLASTR", "PINK NEON BRASS": "PINK NEON BR",
    "SKREAMER SOLO": "SKREAMR SOLO", "DREAM OF ORGONON": "DRM ORGONON", "VELO RES LEAD 2": "VELO RES LD2",
    "REGAL MOVEMENT": "REGAL MOVMNT", "LOOSE SQUARES": "LOOSE SQUARE", "RUBBER BUMPER": "RUBBR BUMPER",
    "GALVANIZED BUCKET": "GALV BUCKET", "REVERBERANT STRINGS": "REVERB STRGS", "PLUCKITY DUCK": "PLUCKTY DUCK",
    "TUNED PERCUSSION": "TUNED PERC", "FUNKY REV1 BELLS": "FUNKY BELLS", "NYLON FINGERS": "NYLON FINGRS",
    "SPARKLE PONIES": "SPARKLE PONY", "MARRAKESH RADIO C": "MARRAKESH C", "FAINT MEMORIES": "FAINT MEMORY",
    "OLD STRING MACHINE": "OLD STR MACH", "SYNC SWELLPAD": "SYNC SWELLPD", "FEATURE MY BASS": "FEATURE BASS",
    "AFTER THE DENOUEMENT": "DENOUEMENT", "MANEATER PING": "MANEATR PING", "BULL BY THE HORNS": "BULL BY HORN",
    "BUSTED AND DUSTED": "BUSTED DUSTD", "TIGHT BASS (556?)": "TIGHT BASS", "80S SCI FI DROP": "SCI FI DROP",
    "CYBERIAN WINDS": "CYBERIAN WND", "OMINOUS BELLFALL": "OMINOUS BELL", "METAL DRONESPACE": "METAL DRONE",
    "MUTED CLAVINET": "MUTED CLAV", "PERCUSSIVE E PIANO": "PERC E PIANO", "PERCUSSIVE ORGAN": "PERC ORGAN",
    "UNISON GLIDE W RES": "UNISON GLIDE", "ORGAN W RESONANCE": "ORGAN W RES", "TRUMPET FLUTE": "TRUMPT FLUTE",
    "BASS IN FIFTHS": "BASS IN 5THS", "PIPE ORGAN FLUTES": "PIPE FLUTES", "ELECTRIC PIANO I": "ELEC PIANO I",
    "OCTAVE SAWTEETH": "OCTAVE SAWS", "RELEASE REPEAT": "REL REPEAT", "DELAYED HARMONIC": "DELAYED HARM",
    "PULSE WIDTH MOD": "PULSE W MOD", "SLOW SYNC SWEEP": "SLOW SYNC SW", "FOURTHS W RESONANCE": "4THS W RES",
    "SWEEPING HARMONICS": "SWEEP HARMS", "RANDOM ARPEGGIATOR": "RANDOM ARP", "SAWTOOTH ARPEGGIATOR": "SAWTOOTH ARP",
    "CLANGOROUS BELLS": "CLANG BELLS", "DESCENDING BELLS": "DESC BELLS", "DESCENDING PWM": "DESC PWM",
    "RESONANCE BELLS": "RES BELLS",
}
# .. and by program number (1..200) where two programs' names differ in their case only (105 "High Strings")
SHORT_AT = {180: "HIGH STRGS 2"}


def records():
    data = SOURCE.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256 or len(data) != 200 * 159:
        raise ValueError("Sequential v1.03 source checksum or bank length changed")
    result = []
    for k in range(200):
        frame = data[k * 159:(k + 1) * 159]
        if frame[:4] != bytes([240, 1, 50, 2]) or frame[-1] != 247 or any(x > 127 for x in frame[1:-1]):
            raise ValueError(f"Invalid native frame {k + 1}")
        if frame[4] != k // 40 or frame[5] != k % 40:
            raise ValueError(f"Unexpected native address {k + 1}")
        packed = frame[6:-1]
        raw = bytes(packed[i + j + 1] | ((packed[i] >> j & 1) << 7)
                    for i in range(0, 152, 8) for j in range(7))
        result.append(raw + bytes([133, 50, 2, frame[4], frame[5]]))
    return result


def pack(patches):
    """The 133 raw bytes of every program, packed (eng_prophet_test.c p5_factory unpacks them): raw byte k is
    P5_FBASE[k] plus the next P5_FWIDTH[k] bits, LSB first; each program starts on a byte boundary. The
    trailer (size, model, command, group, program) follows from the program's number."""
    base = [min(p[k] for p in patches) for k in range(133)]
    width = [(max(p[k] for p in patches) - base[k]).bit_length() for k in range(133)]
    per = (sum(width) + 7) // 8
    data = bytearray()
    for n, p in enumerate(patches):
        if p[133:] != bytes([133, 50, 2, n // 40, n % 40]):
            raise ValueError(f"program {n + 1}: unexpected trailer")
        bits, pos = 0, 0
        for k in range(133):
            bits |= (p[k] - base[k]) << pos
            pos += width[k]
        data += bits.to_bytes(per, "little")
    for n, p in enumerate(patches):                      # the firmware's unpacking, checked here
        bits, pos = int.from_bytes(data[n * per:(n + 1) * per], "little"), 0
        for k in range(133):
            if base[k] + (bits >> pos & ((1 << width[k]) - 1)) != p[k]:
                raise ValueError(f"program {n + 1}: packing does not round-trip")
            pos += width[k]
    rows = lambda v: ["    " + ",".join(map(str, v[i:i + 32])) + "," for i in range(0, len(v), 32)]
    return ([f"#define P5_FACTORY_BYTES {per}u",
             "static const uint8_t P5_FBASE[133] = {"] + rows(base) + ["};",
             "static const uint8_t P5_FWIDTH[133] = {"] + rows(width) + ["};",
             f"static const uint8_t P5_FPACK[{len(data)}] = {{"] + rows(list(data)) + ["};"])


def names(patches):
    """the PRESETS names: the program's name in upper case, SHORT's where that is over 12 characters; all different"""
    out = []
    for n, p in enumerate(patches):
        full = bytes(p[65:85]).decode("ascii").rstrip().upper()
        name = SHORT_AT.get(n + 1, SHORT.get(full, full))
        if not name or len(name) > 12 or any(not 32 <= ord(c) <= 126 or c in '"\\' for c in name):
            raise ValueError(f"program {n + 1} {full!r}: no PRESETS name of 1..12 characters ({name!r})")
        out.append(name)
    if len(set(out)) != len(out):
        raise ValueError("two programs share a PRESETS name: " + ", ".join(sorted(x for x in set(out) if out.count(x) > 1)))
    unused = set(SHORT) - {bytes(p[65:85]).decode("ascii").rstrip().upper() for p in patches}
    if unused:
        raise ValueError("SHORT names no program: " + ", ".join(sorted(unused)))
    return out


def main(path):
    patches = records()
    lines = ["/* generated by tools/gen_prophet_factory.py from assets/prophet5-factory/prophet5-v1.03.syx:",
             " * Sequential's 200 Prophet-5 / 10 factory programs v1.03, unchanged native data (the rights stay with",
             " * Sequential: assets/prophet5-factory/README.md, LICENSING.md) */",
             "#pragma once",
             f"#define P5_FACTORY_N {len(patches)}u"]
    lines += pack(patches)
    lines.append("#define P5_FACTORY_PRESETS(X) \\")
    lines += [f'    X("{n}") \\' for n in names(patches)]
    lines.append("")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n")
    print(f"prophet factory: {len(patches)} programs -> {path}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
