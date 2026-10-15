#!/usr/bin/env python3
"""Elektron Digitone (original, OS 1.4x) MIDI CC / NRPN map for one synth track.

Source: Digitone User Manual OS 1.44 (260930), "APPENDIX B: MIDI IMPLEMENTATION",
tables B.1-B.6:
  https://www.elektron.se/wp-content/uploads/2026/10/Digitone-User-Manual_ENG_OS1.44_260930.pdf
(linked from https://www.elektron.se/en/digitone ; the tables are unchanged across 1.4x.)

Channels (manual 14.5.3): synth tracks listen on TRACK 1-4 CHANNEL (factory 1-4;
the manual does not state the default, check SETTINGS > MIDI CONFIG > CHANNELS);
the AUTO channel (default 10) reaches the active track. SETTINGS > MIDI CONFIG >
PORT CONFIG > RECEIVE CC/NRPN and RECEIVE NOTES must be on, and INPUT FROM must
include USB.

Parameters with a CC LSB column are 14-bit; they are sent as NRPN (CC99 MSB,
CC98 LSB, CC6 value MSB, CC38 value LSB) so the fine part is set to 0 too.
7-bit parameters are sent as plain CCs. Bipolar parameters take 64 = 0.

NOT settable here (enumerated or missing from Appendix B, set by hand):
  ALGO (CC 90 exists but maps 0..127 onto 8 algorithms: hand-check it),
  RATIO C/A/B (CC exists but the ratio list is non-linear: set by hand),
  FILTER TYPE (CC 74, enumerated: set by hand), LFO MULT / DEST / WAVE / MODE
  (enumerated: set by hand), tempo. The original Digitone has no AMP HOLD.
"""

# name: (cc_msb, cc_lsb or None, nrpn_msb, nrpn_lsb)
PARAMS = {
    # B.1 track / B.2 trig
    "track_level": (95, None, 1, 100),
    "velocity": (4, None, 3, 1),
    # B.3 SYN1
    "algo": (90, None, 1, 72),
    "ratio_c": (91, None, 1, 73),
    "ratio_a": (92, None, 1, 74),
    "ratio_b": (16, 48, 1, 75),
    "harm": (17, 49, 1, 76),
    "dtun": (18, 50, 1, 77),
    "fdbk": (19, 51, 1, 78),
    "mix": (20, 52, 1, 79),
    # B.3 SYN2
    "a_atk": (75, None, 1, 80),
    "a_dec": (76, None, 1, 81),
    "a_end": (77, None, 1, 82),
    "a_lev": (78, None, 1, 83),
    "b_atk": (79, None, 1, 84),
    "b_dec": (80, None, 1, 85),
    "b_end": (81, None, 1, 86),
    "b_lev": (82, None, 1, 87),
    "a_del": (83, None, 1, 88),
    "a_trig": (84, None, 1, 89),
    "b_del": (86, None, 1, 91),
    "b_trig": (87, None, 1, 92),
    # B.4 FILTER
    "f_freq": (23, 55, 1, 20),
    "f_reso": (24, 56, 1, 21),
    "f_type": (74, None, 1, 22),
    "f_atk": (70, None, 1, 16),
    "f_dec": (71, None, 1, 17),
    "f_sus": (72, None, 1, 18),
    "f_rel": (73, None, 1, 19),
    "f_env": (25, 57, 1, 23),
    "f_del": (43, None, 1, 66),
    "f_base": (26, 58, 1, 24),
    "f_width": (27, 59, 1, 25),
    # B.5 AMP
    "amp_atk": (104, None, 1, 32),
    "amp_dec": (105, None, 1, 33),
    "amp_sus": (106, None, 1, 34),
    "amp_rel": (107, None, 1, 35),
    "drive": (9, 41, 1, 36),
    "pan": (10, 42, 1, 37),
    "vol": (7, 39, 1, 38),
    "chr_send": (12, 44, 1, 41),
    "del_send": (13, 45, 1, 40),
    "rev_send": (14, 46, 1, 39),
    # B.6 LFO
    "lfo1_spd": (28, 60, 1, 48),
    "lfo1_mult": (108, None, 1, 49),
    "lfo1_fade": (109, None, 1, 50),
    "lfo1_dest": (110, None, 1, 51),
    "lfo1_wave": (111, None, 1, 52),
    "lfo1_mode": (113, None, 1, 54),
    "lfo1_dep": (29, 61, 1, 55),
    "lfo2_spd": (30, 62, 1, 57),
    "lfo2_mult": (114, None, 1, 58),
    "lfo2_fade": (115, None, 1, 59),
    "lfo2_dest": (116, None, 1, 60),
    "lfo2_wave": (117, None, 1, 61),
    "lfo2_mode": (119, None, 1, 63),
    "lfo2_dep": (31, 63, 1, 64),
}

# Parameters this tool never sends: the owner sets them by hand.
HAND_ONLY = {"algo", "ratio_c", "ratio_a", "ratio_b", "f_type",
             "lfo1_mult", "lfo1_dest", "lfo1_wave", "lfo1_mode",
             "lfo2_mult", "lfo2_dest", "lfo2_wave", "lfo2_mode"}


def encode(name, value, channel=1):
    """Return a list of 3-byte MIDI messages setting `name` to integer 0..127.

    channel is 1-based. 14-bit parameters go out as NRPN with the fine byte 0.
    """
    if name in HAND_ONLY:
        raise ValueError(f"{name} must be set by hand")
    msb, lsb, nm, nl = PARAMS[name]
    v = int(value)
    if not 0 <= v <= 127:
        raise ValueError(f"{name}={value} out of 0..127")
    st = 0xB0 | ((channel - 1) & 0x0F)
    if lsb is None:
        return [[st, msb, v]]
    return [[st, 99, nm], [st, 98, nl], [st, 6, v], [st, 38, 0]]


def note_on(note, vel, channel=1):
    return [0x90 | (channel - 1), note, vel]


def note_off(note, channel=1):
    return [0x80 | (channel - 1), note, 0]


def all_notes_off(channel=1):
    st = 0xB0 | (channel - 1)
    return [[st, 123, 0], [st, 120, 0]]


if __name__ == "__main__":
    for n in sorted(PARAMS):
        tag = "HAND" if n in HAND_ONLY else ("NRPN" if PARAMS[n][1] is not None else "CC")
        msgs = "" if n in HAND_ONLY else " ".join(
            "".join(f"{b:02X}" for b in m) for m in encode(n, 100))
        print(f"{n:11s} {tag:4s} {PARAMS[n]}  {msgs}")
