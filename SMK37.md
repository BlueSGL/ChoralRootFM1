# ChoralRoot on the M-Vave SMK-37

ChoralRoot also builds for the M-Vave SMK-37, a 37-key controller with the same chip as the FM-1. The FM-1 build is
unchanged; everything for the SMK-37 is behind `BOARD_SMK37`. On the SMK-37 it is the same ChoralRoot: the same
sounds, screens, boot and [manual](docs/MANUAL.md), with velocity-sensitive keys and the SMK-37's extra controls.

Until this project's releases include SMK-37 packages, unofficial builds can be installed with the web installer: https://genkigenki.github.io/smk37/

## Supported models

- **SMK-37 Pro**: the default build (package identity `SMK-37 Pro_920`).
- **SMK-37 Elite**: `MODEL=elite` (identity `SMK-37 Elite_920`).
- **MKE-P37**: `MODEL=mke` (identity `MKE-P37_920`).
- **Starrykey 37 Play**: uses the SMK-37 Pro build.

All of them are the same hardware; each build carries its model's name, so M-Vave's updater still takes the
unit's factory package afterwards.

## Controls

The manual still applies: the middle 27 keys are the FM-1's keybed, and the SMK-37's buttons and knobs map onto the
FM-1's. The screen keeps the FM-1's names (SELECT, SEL, ENV, GLO, HOME, OCT-, ...); the table translates them.

| SMK-37 | ChoralRoot |
| --- | --- |
| Keys F3..C#4 | the chord block: 6, DIM, m7, MIN, M7, MAJ, LOCK (B3), 9, SUS |
| Keys D4..G5 | the roots, with velocity |
| Keys C3..E3 and G#5..C6 (the outer 10) | more roots, each at its own pitch: a chord with a chord key held or latched, a note on the Full keyboard; with the octave shift. Where the root keys pick (a layer's map, naming) they do nothing |
| Pads 1-4 (top row, left) / 9-12 (bottom row, left) | DIM MIN MAJ SUS / 6 m7 M7 9: the chord block as a grid of two rows. A pad does what its key does (it holds the chord type, it stacks, it picks the scale in the KEY layer, it plays a note on the Full keyboard) and lights as that key's light: types white, extensions orange |
| Pad 5 | LOCK (as B3: tap, or hold for the LOCK layer) |
| Pads 6, 7, 8, 13, 14, 15, 16 | the performance modes: Strum, Strum 2 Octaves, Slop, Arpeggiate, Arp 2 Octaves, Pattern, Harp. A pad turns its mode on; its pad again turns performance off. Blue: the mode playing bright, the mode picked while off dimmer |
| K1 | MASTER (volume) |
| K2 / K3 / K4 | SELECT / ALGORITHM / PRESETS |
| K5..K8 | KNOB 1..4 |
| SCALE / CHORD / NOTE REPEAT | KEY (printed SEL) / BASS (ENV) / LATCH (LFO) |
| PARA / GLOBE | EDIT / OPT (GLO) |
| PATCH / BT | HOME / SAVE |
| FX / ARP / SEQ | FX / PERF (ARP) / SEQ |
| SEQ PLAY / SEQ REC | LOOP (PLAY; its light red with a loop in the slot, green while it plays) / REC |
| OCTAVE- / OCTAVE+ | OCT- / OCT+ |
| KNOB BANK | the view menu (as HOME held); again: closed. Lit while it is open |
| < / > | where SELECT picks or pages (a layer, a menu, the Options, the sound editor, the save dialog): its step back / forward, never the tempo; on the view: the view before / after. Lit where they page |
| FADER BANK | what the faders set: A the chord part's sends, B the bass part's (lit on B) |
| Faders 1..4 | the sends into Drive, Chorus, Delay and Reverb (the order of the FX layer's Sends page), shown as they move; a fader takes over only once it moves, also after FADER BANK |
| Pitch wheel | bends the chord and the bass part (their channels' bend range: 2 semitones unless set over MIDI); also sent on the CHORD and BASS MIDI channels |
| Mod wheel | vibrato on both parts (5.5 Hz, up to half a semitone at the top) and the sounds' mod wheel source; also sent as CC 1 |
| Options > Velocity | `keys` (the SMK-37's default): as played; 1..127: every key and pad at that velocity |
| OCTAVE- held at power-on | update mode (OCTAVE-'s light flashes three times; install a package, or switch off and on) |
| OCTAVE- + OCTAVE+ held at power-on | HARDWARE CALIBRATION (the screen asks for the FM-1's names, the table above) |
| OCTAVE- + OCTAVE+ + PATCH held 5 s | update mode |

The SMK-37's TRS jack is a MIDI output, and this build does not use it; there is no TRS MIDI IN. MIDI works over USB.
The SMK-37's button lights are on or off: the FM-1's dim glow of idle buttons does not show. The pedal input is not
used.

## Build

See [BUILDING.md](BUILDING.md) for the toolchain.

```sh
BOARD=smk37 ./build.sh                 # build/choralroot-smk37.fwsc (SMK-37 Pro, Starrykey 37 Play)
BOARD=smk37 MODEL=elite ./build.sh     # build/choralroot-smk37-elite.fwsc
BOARD=smk37 MODEL=mke ./build.sh       # build/choralroot-smk37-mke.fwsc
./build.sh                             # the FM-1 build again before tests/run_tests.sh (the tests use its files)
```

## Install

Connect the keyboard by USB and run, with the package for your model:

```sh
python3 tools/fm1_install.py build/choralroot-smk37.fwsc
```

`fm1_install.py` needs `python-rtmidi` (`pip3 install python-rtmidi`). It checks that the package is for the
connected keyboard. As on the FM-1, an install that was interrupted resumes: switch the keyboard on (or hold OCTAVE-
while switching on) and run the install again.

## Going back to the stock firmware

Install M-Vave's firmware package for your model with M-Vave's updater, M-UPGRADE. It works directly, because
the installed package keeps the keyboard's model name.

## Safety

- Have M-Vave's stock package for your model and M-UPGRADE ready before you install.
- Keep the USB cable connected until the install has finished and the keyboard has restarted.
- ChoralRoot keeps its data apart from other firmwares' (its own data family): the first start clears the stock
  firmware's settings and any other firmware's data in the shared data area.
- The firmware never writes the keyboard's calibration, Bluetooth pairing or key areas; the flash map refuses
  them at compile time and again in the flash driver.
- Installing other firmware is at your own risk.
