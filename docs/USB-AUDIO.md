# ChoralRoot FM-1: USB audio

ChoralRoot has Melodee's USB audio recording (Melodee 0.11.1: "Melodee Out plays the computer through the FM-1, Melodee
In records the four tracks as separate channels; each can be switched off"), renamed and re-cut for ChoralRoot's two
parts: **ChoralRoot In** records the master, the CHORD part and the BASS part as three stereo pairs. It is a
class-compliant USB Audio Class 1 device: no driver on macOS, Windows 10 / 11, Linux, iPadOS, at 44.1 kHz or, from
0.19, 48 kHz as the host chooses ("48 kHz" below). Melodee's playback
device (the computer's audio played through the FM-1; "ChoralRoot Out" in the 0.14 dev builds) is **not** in
ChoralRoot: it was removed in 0.14 (below, "Removed: the playback").

This is phase 3 of the Melodee platform work (phase 1: FM6, [FM6.md](FM6.md); phase 2: the CZ-1 engine,
[CZ1.md](CZ1.md)).

**Names, from the FM-1's point of view.** The Options entry is **USB Record**: the computer records the FM-1. The
device the computer lists keeps the computer's point of view, as DAWs label their inputs: **ChoralRoot In**, an
input of the computer.

## What the computer sees

One USB device, **ChoralRoot FM-1** (maker "ChoralRoot"; VID 0x1209, PID 0x0001 as before), with two functions:

| Function | Name | What | Format |
| --- | --- | --- | --- |
| MIDI | **ChoralRoot FM-1** (the MIDI port) | USB-MIDI in and out, as before | |
| Audio input | **ChoralRoot In** | six channels to the computer | 6 channels, 16 bit, 44.1 or 48 kHz (the host chooses; 0.19) |

In Audio MIDI Setup (macOS) or Sound settings (Windows) it appears as "ChoralRoot In"; a DAW sees a 6-channel input.
The FM-1 offers no audio output to the computer.

The MIDI port is now called **ChoralRoot FM-1** (it was "Felucca"). The installer (`tools/fm1_install.py`,
`web/fm1ota.js`) matches "FM-1" and the web editor (`web/editor.html`) matches "ChoralRoot" as well as "Felucca", so
both keep finding the instrument. The device's `bcdDevice` is 3.23 with the recording (3.22 to 0.18, when it had one
rate) and 3.21 with the console (below), so a host never reuses descriptors it cached for Felucca (3.11), Melodee
(3.06), the 0.14 dev builds' playback + recording (3.20), 0.18's one-rate recording or the other presentation.

### ChoralRoot In: the channels

| Channels | Name | The signal |
| --- | --- | --- |
| 1-2 | Master L / R | the master output after the limiter and the speaker EQ (Felucca's LOWCUT / BASS+ when set), as the DAC gets it, **without** the metronome click. Its level follows MASTER, or not (USB Level, below) |
| 3-4 | Chord L / R | the CHORD part (part 0: the chord sound, the RAW chord's when it plays part 0) as it goes into the mix: after its DIST, LEVEL, pan and the stereo spread (VA's SPREAD), **before** the sends, the FX buses (chorus, delay, reverb), the FX layer and MASTER; at **half level (−6 dB)** |
| 5-6 | Bass L / R | the BASS part (part 1), the same way |

The part pairs are dry: the shared FX buses cannot be split by part. They are taken at −6 dB because a part alone may
be louder than the master's full scale before the limiter (a six-note chord at a high LEVEL reaches about twice the
limiter's threshold); all channels saturate at 16-bit full scale. Capture is 16 bit only: the mix is 16-bit, and a
24-bit format would only pad a zero byte (and need 810-byte packets instead of 540; 882 for 588 at 48 kHz).

Hosts that read UAC1 channel names (the input terminal's `iChannelNames`, strings 4..9) show "Master L" .. "Bass R";
Windows numbers them 1..6.

### Removed: the playback

The 0.14 dev builds also had Melodee's playback device, "ChoralRoot Out" (the computer's stereo added after the master
limiter, x MASTER, with Options > USB Audio Out). It is gone: its interfaces, its OUT endpoint (EP2 OUT) and explicit
feedback endpoint (EP3 IN), its 4 KiB ring in the POOL, its packet buffers and the mix-in after the limiter. Its
settings byte is reserved (docs/SETTINGS.md, version 5). USB MIDI is unchanged.

## Options

Two entries in Options (GLO tap), after Hold Time, one setting per screen; KNOB 1 sets them. The line under the value
says what each does:

| Option | Values | Default | What it does |
| --- | --- | --- | --- |
| **USB Record** | On / Off | On | the computer can record the FM-1: presents ChoralRoot In (the line: "records master, chord, bass") |
| **USB Level** | Master / Fixed | Master | Master ("record level: MASTER knob"): ChoralRoot In's master pair follows the MASTER knob. Fixed ("record full, MASTER: speaker"; Felucca 1.0.5's MENU > USB LEVEL FIXED, #42): the mix goes to the limiter at the full MASTER level, ChoralRoot In records that, and only then does MASTER scale what the DAC (speaker, headphones) gets, so you can record at full level with the speaker turned down |

(The value texts stay "Master" / "Fixed": "Fixed (record at full level)" is wider than the picker's value line.)

USB Level changes at once. USB Record changes **what the computer is given** (Melodee's mechanism: the configuration
is rebuilt): once the setting has rested for 0.6 s (so stepping through Off and back costs nothing), the FM-1 leaves
the bus for about a second and connects again; the screen says "USB RECONNECTING", and the computer reads the new
configuration. The MIDI port disappears for that second too. The settings are saved with the others
(docs/SETTINGS.md, record version 5) and the device presents itself that way from power-on.

**The serial console** is available when **USB Record is Off**, or in **SAFE MODE**. The recording and the USB serial
console (`console.c`, the `cpu`, `boot`, `status` commands; docs/INTEGRATION.md "Performance") both need EP2 IN (the
capture's isochronous stream, the console's notification endpoint), so the firmware presents one or the other: with
USB Record Off, MIDI and the **console** (as earlier builds always did); with it On, MIDI and the recording. **SAFE
MODE** always presents the console (no audio function: `UAC_BLOCKED`, `core.h`), so a crashing boot can be read over
the console. Removing the playback freed EP2 OUT and EP3, which the console's data could take, but the two still do
not go together: the console's notification endpoint would need an IN endpoint other than EP2 (only EP4 is left,
whose DMA registers differ and which only Felucca's isochronous stream ever used), and, decisively, Felucca's #67
(fixed in Felucca 1.0.5 by its MENU > USB SERIAL): macOS 13 to 15 attach Apple's CDC composite driver to a device that
has a CDC function, and their audio driver then never takes the audio interfaces. ChoralRoot's recording presentation
has no CDC function at all (Melodee's layout), so the switch stays.

## Limits

- **44.1 kHz is the FM-1's own rate; 48 kHz is converted** (0.19, "48 kHz" below): the host chooses. At 44.1 kHz
  the computer gets the samples as the FM-1 made them; at 48 kHz the FM-1 resamples them (-1 dB at 16 kHz, -2.7 dB
  at 18 kHz, and about 185 us more of each 2.9 ms of processor time).
- **Clock:** the FM-1's I2S clock (~44,117.6 Hz) is not locked to USB. The endpoint is asynchronous: capture packets
  follow a fixed 44.1-frame pattern (44 frames, a 45 every 10th packet); when the ring's fill leaves 128..384 frames
  a packet takes one frame more or fewer (a 44 made 45 or a 45 made 44, at most one in 10 packets). Nothing is
  resampled for the clock's sake; the ring absorbs the difference. (At 48 kHz: 48 frames, a 49 or a 47.)
- **Latency** (on top of the computer's buffers): ~5.8 ms (the ring holds 256 frames; 5.3 ms at 48 kHz). A ring that runs over (the
  host stops reading) or dry (a 45 ms flash erase when a setting or a sound is saved) is counted and re-primed: a
  short gap, never stale or repeated audio.
- **Bus bandwidth:** the packets carry up to 540 bytes (588 at 48 kHz); the endpoint reserves 688 a frame (640 to
  0.18; bit stuffing: "What went wrong and why" below) of a full-speed frame's 1500, inside a USB 2 hub's
  split-transaction budget (~1157). (Six channels at 24 bit would have been 810.)
- **The metronome click** is not in ChoralRoot In.
- **No playback:** the computer cannot play through the FM-1 (removed, above).
- **The emulator has no USB:** the Options entries are there and saved, nothing streams (`FELUCCA_UAC` is 0 in the
  emulator's build, `tools/emu/emu_firmware.h` through `tests/hostsim.c`).
- **Felucca's single stereo input** (the master on EP4, FELUCCA_UAC 1.0) is gone: ChoralRoot In's channels 1-2 are
  the same signal.

## 48 kHz (0.19)

Many phones, tablets and apps only take 48 kHz, and a DAW project at 48 kHz had to convert or refuse. From 0.19
ChoralRoot In runs at **44.1 or 48 kHz: the host chooses**. There is no option on the FM-1. The synth, the effects and
the headphone output stay at 44.1 kHz; at 48 kHz the FM-1 converts what it sends to the computer. This is SLOOP
2.5's feature ("USB audio at 48 kHz too ... The FM-1 resamples its 44.1 kHz output itself (after Felucca 1.1.5, with
a smaller filter); at 44.1 kHz nothing changed", <https://github.com/isod89/sloop-fm1>), which came from Felucca
1.1.5 (Leo Kuroshita), ported to ChoralRoot's six channels.

**At 44.1 kHz nothing changed**: the packets are bit for bit what 0.18 sent (`tests/cr_usbaudio_test.c` "golden": a
digest of every byte and length of 45 950 packets over three simulated runs, taken on the tree before this work and
compared in every test run). The audio ISR tests the rate once a block and the packet code once a packet.

### What the host sees

- **One alternate setting, two discrete rates** (SLOOP's and Felucca's form, not a second alternate setting): the
  type I format descriptor is 14 bytes and lists 44100 and 48000 Hz; the endpoint's class descriptor has the
  sampling-frequency control as before. The streaming descriptors are SLOOP 2.5's byte for byte but the channel
  count, the terminal's id, the endpoint's number and its size (the host test compares them).
- **The requests** (endpoint EP2 IN, `usb_audio.c`): `SET_CUR` takes the nearer of the two rates (UAC1 rounds);
  `GET_CUR` answers the rate in force, `GET_MIN` 44100, `GET_MAX` 48000, `GET_RES` 0. The rate is 44.1 kHz after a
  bus reset or a new configuration and otherwise stays what the host last set, across closes and reopens of the
  stream and across suspend and resume.
- **A change of rate** empties the ring: the host gets silence packets (of the new rate's size) until the ring is
  primed again, about 5 ms, and the resampler starts from silence. `SET_CUR` with the rate already in force does
  nothing to a running stream. macOS and Windows send `SET_CUR` after `SET_INTERFACE` at every open (inference from
  the UAC1 drivers' behaviour and Melodee's note that hosts "set it while starting a stream"; not observed on this
  device at 48 kHz yet).
- **Packets at 48 kHz** are 48 frames (576 bytes) a millisecond, with no pattern. The FM-1's audio clock is not the
  host's (about +435 ppm against the owner's Mac), so about one packet in 48 is 49 frames (588 bytes); a slow clock
  would give 47s. The same fill band and pace as the 44.1 kHz nudge.
- **`wMaxPacketSize` is 688** (was 640): the one endpoint serves both rates and a 49-frame packet bit-stuffed is up
  to 588 x 7 / 6 = 686 bytes ("What went wrong and why", below). This is the one thing a 44.1 kHz host sees
  differently on the bus: 48 more bytes of reserved time a frame, 688 of a full-speed frame's 1500.
- **`bcdDevice` is 3.23** (was 3.22). macOS and Windows cache a device's descriptors by VID, PID, serial and
  `bcdDevice`; without the bump a computer that knew 0.18 could go on showing one rate. SLOOP 2.5 and Felucca did the
  same for their second rate (SLOOP: 3.10 to 3.20). Nothing else of the identity changed: VID 1209, PID 0001, the
  strings, the MIDI function, the console's presentation (3.21, byte for byte). `tools/fm1_install.py`, the web
  installer and `tests/install_test.py` do not read `bcdDevice` (they match the MIDI port's name and the SysEx
  identity). After the update the computer sees a new device version: on Windows it installs the audio device again
  (a few seconds, once); a DAW that remembered "ChoralRoot In" by name finds it again. No replug beyond the
  restart the update does anyway.
- **Which rate a computer picks first** is the host's choice and is not known for this device: `GET_CUR` says 44100
  after every reset and 44100 is listed first, so 44.1 kHz is the likely default on macOS (inference); the owner's
  test plan below reads it off Audio MIDI Setup.

### The resampler

48000 / 44100 = 160 / 147. A polyphase FIR: 160 phases of 12 taps in Q15, a Kaiser-windowed sinc (beta 7.5, cut-off
20 kHz) of 1920 taps at 7.056 MHz; the table holds 80 phases (the other 80 are the same read backwards), 1920 bytes
of flash (`firmware/src/uac_fir.h`, made by `tools/gen_uac_fir.py`). It is SLOOP 2.5's table value for value.
Felucca 1.1.5's is 24 taps in Q30 (7680 bytes); both of theirs filter two channels, ChoralRoot's six, which is why
the smaller one was taken: the taps are the cost.

Every 147 frames in give exactly 160 out, whatever the block sizes (a phase counter, no accumulated fraction: no
drift); each phase's taps sum to exactly 1.0, so DC and levels are exact; the sums cannot overflow and the output
clips at 16 bits. The filter's delay is 5.5 frames of 44.1 kHz (0.125 ms), the same in all six channels.

Measured on the C code (`build/host/cr_usbaudio48_test --table`; a tone at -1 dBFS in, 48 000 frames, the spectrum
with a Blackman-Harris window in 10 Hz bins):

| Tone in | Gain | SNR (all that is not the tone) | Largest other product | at |
| --- | --- | --- | --- | --- |
| 100 Hz | -0.00 dB | 82.2 dB | -108.0 dB | 3800 Hz |
| 1000 Hz | -0.00 dB | 80.0 dB | -85.3 dB | 2900 Hz |
| 5000 Hz | -0.00 dB | 75.8 dB | -79.9 dB | 8900 Hz |
| 10000 Hz | -0.00 dB | 73.7 dB | -76.5 dB | 13900 Hz |
| 12000 Hz | -0.02 dB | 78.1 dB | -82.7 dB | 19800 Hz |
| 14000 Hz | -0.21 dB | 77.5 dB | -79.7 dB | 17900 Hz |
| 16000 Hz | -0.95 dB | 55.6 dB | -55.6 dB | 19900 Hz |
| 18000 Hz | -2.72 dB | 30.4 dB | -30.4 dB | 21900 Hz |
| 20000 Hz | -6.02 dB | 14.2 dB | -14.2 dB | 23900 Hz |

Read it so: the passband is flat to 12 kHz, -0.2 dB at 14 kHz, -1 dB at 16 kHz and **-2.7 dB at 18 kHz**. The largest
product is the tone's first image (at 44 100 - f before the rate change), which lands at **f + 3900 Hz**: 77 dB or
more below the tone for tones up to 14 kHz, 56 dB for a 16 kHz tone (heard at 19.9 kHz), and above that it lands
over 20 kHz, where it is not heard but is in the file: a 20 kHz tone leaves 23.9 kHz at -14 dB. With 12 taps for six
channels this is the balance SLOOP chose "for recording into a phone's sampler". **Recording at 44.1 kHz stays the
exact one**: the samples as the FM-1 made them.

Other filters from the same generator (`tools/gen_uac_fir.py --report N FC BETA`; not built):

| Taps a phase | Flash | 16 kHz | 18 kHz | Image of a 16 kHz tone | Cost on the unit (the count below, scaled) |
| --- | --- | --- | --- | --- | --- |
| 12 (SLOOP 2.5; built) | 1920 B | -0.95 dB | -2.72 dB | -55.6 dB | 185 us a half |
| 16, cut-off 20.5 kHz, beta 8 | 2560 B | -0.26 dB | -1.48 dB | -79.7 dB | about 225 us |
| 24 in Q30 (Felucca 1.1.5; its response was not measured here) | 7680 B | | | | about 310 us |

### Where it runs, and what it costs on the unit

`audio.c` `audio_block`: after the mix has staged the block's six channels (`fx.c` `ua_stage`, both cores' work
already in it: Dual Core On and Off take the same path), and only when the host runs the stream at 48 kHz,
`ua_block48` resamples the 32 frames to 34 or 35 into `ua.rs_out` **with the interrupts on**, and the copy into the
ring is the same short interrupts-off section as at 44.1 kHz (35 frames for 32). The ring holds 48 kHz frames; the
packet code only differs in the packet's length. A block resampled for a rate the host just left is dropped at the
ring (`ua_feed`), and nothing is resampled while the host does not read yet (`cap_hold`).

Counted from `build/choralroot.dis` at 205 instructions a microsecond (docs/FX.md "The cost on the unit"), not from
the emulator:

| Per | Instructions |
| --- | --- |
| output frame, six channels: `ua_fir6_fwd` 224 / `ua_fir6_bwd` 230 (straight code, 144 multiply-adds in one instruction each: `r7_r6 += r10 * r4`), its call and the loop around it 12 | 236 / 242 |
| input frame (the window's pointer, the phase) | 13 |
| block of 32 frames: 34.83 output frames, 32 input frames, the history's two copies of 11 frames (376), `ua_block48` and its two clock reads (36), the rest (20) | about 9 150 |
| **half of 128 frames: four blocks** | **36 600 = 179 us** |
| the ring copy of 139 frames for 128, and packets of 48 frames for 44.1 (2.9 a half) | + 1 200 = 6 us |
| **48 kHz in all, on top of the 44.1 kHz stream** | **about 185 us a half, 6.4 % of 2902** |

The work does not depend on the signal: the worst half (140 output frames, all through the longer function) is 187
us. At **44.1 kHz the addition is about 12 instructions a block and 5 a packet (0.3 us a half)**: the test of the
rate.

**This is not small.** The heaviest case measured on the unit, CZ-1 chords with both effects, was `max_us 2580` of
2902 (docs/FX.md); with a 48 kHz recording running that would be about 2765 (95 %), and the firmware gives up a voice
after two halves over its limit in a row (`audio.c` `shed_check`). So in the heaviest patches a 48 kHz recording can
cost a voice that a 44.1 kHz recording does not. The owner reads the real figure off the unit: `fm1_install.py
--debug` prints `ua_rs_avg_us` (one block; x 4 = a half) and `ua_rs_max_us`, and PEAK's `pk_block_us` with and
without the 48 kHz stream. The count assumes the 64-bit multiply-add costs what an average instruction does: not
measured (the 205 a microsecond came from loops of 32-bit multiplies).

How the count got there (each step measured on the unit's compiler): a loop over the taps with six 32-bit sums was
326 instructions a frame (250 us a half); written out for three channels at a time with 64-bit sums, 277 inlined
(the taps went to the stack); as functions of their own, 236. The 64-bit sums are for the instruction, not for their
size.

### Memory

Against the build before (f402608): **XIP +4896 B** (83.0 -> 83.8 %: the table 1920 B, the two filter functions
1700 B, the rest the requests, the counters and SysEx 79), **POOL +724 B** (91.5 -> 91.7 %: the resampler's history
264 B and its block of 36 frames 432 B, behind the ring), **RAM +128 B** (82.1 -> 82.2 %: the two packet buffers are
588 bytes each for 540, the counters).

### Counters

73 DEBUG is full, so the rate has a command of its own, **SysEx 79 USB** (read-only, clears nothing;
`cr_backup.c crb_usb`, web/EDITOR_PROTOCOL.md). `fm1_install.py --debug` prints it after the others, and
`tools/uac_check.py` reads it with them:

| field | what |
| --- | --- |
| `ua_rate` | the rate in force, 44100 or 48000 |
| `ua_rate_last_open` | the rate of the last whole open (0: none yet): what `ua_frames_last_open` counts frames of |
| `ua_rate_sets`, `ua_rate_changes` | the host's `SET_CUR`s; those that changed the rate |
| `ua_pk576`, `ua_pk564`, `ua_pk588` | 48 kHz packets of 48, 47 and 49 frames (`ua_pk528` / `ua_pk540` stay the 44.1 kHz ones; `ua_pk_other` stays "a bug") |
| `ua_rs_blocks`, `ua_rs_avg_us`, `ua_rs_max_us` | blocks resampled; one block's time, the recent average and the longest (TIMER5 nested in it): x 4 = a half |
| `ua_packet_max` | 588 |

The counters of 73 keep their meaning at both rates: `ua_frames_last_open` and `ua_frames_this_open` count frames
as sent (48 kHz frames in a 48 kHz open); a change of rate while the stream is open starts the open's count and its
clock again, so the frames of an open are all of one rate; `ua_nudges_up` / `_down` are one frame each at either
rate; `ua_overruns` / `ua_underruns` count ring resets as before (a rate change is not one).

### What was tested, and what only hosts can

`tests/cr_usbaudio48_test.c` (57 checks, in `tests/run_cr_tests.sh` and `tests/run_tests.sh`): the descriptors as
above; the requests; 14 700 frames of noise in six channels against a plain reference of the same table, bit for
bit, in blocks of 32, of 7 and of random sizes; 160 out for 147 in; DC; full-scale squares (clipping, no wrap); no
leak between channels; the table above; 11 s of stream at 48 kHz with the I2S fast (+400 ppm), slow (-1000 ppm) and
nominal: packets of 47 / 48 / 49 frames only, the mean the clock's, no under- or overrun, and what the host gets is
one unbroken 997 Hz sine in all six channels (SNR 81 dB: phase-continuous across every packet); 44.1 -> 48 -> 44.1
while streaming. Also run under the address and undefined-behaviour sanitizers.

**Not tested: anything on a real host.** Enumeration with two rates on macOS, Windows, iOS / iPadOS, Android and
Linux; which rate each picks; whether each sends `SET_CUR` as assumed; the 49-frame packets and the 688-byte
reservation on the JieLi controller and through a hub; a rate change while a program records; the unit's real cost.

### The owner's test plan

Flash the build, then each step: `python3 tools/fm1_install.py --debug > before.txt`, do the step, `--debug >
after.txt`, compare the named counters. (`tools/uac_check.py OUT.wav 60 --rate 48000` does the two reads and the
arithmetic for a Mac recording; `--rate` only says what to expect, the Mac's rate is set in Audio MIDI Setup.)

1. **macOS, first look.** Audio MIDI Setup > ChoralRoot In: the Format menu must offer **44 100 Hz and 48 000 Hz**,
   6 ch 16-bit. Note which one is selected before you touch it (the default: report it). `--debug`: `ua_rate` is what
   the menu says.
2. **macOS at 44.1 kHz** (the regression check). Format 44 100; `tools/uac_check.py a441.wav 60 --rate 44100`. Pass:
   "the FM-1 sent the stream whole", one open, rate 44100, `ua_pk528` / `ua_pk540` about 90 / 10 %, `ua_pk576` and
   `ua_pk_other` unchanged, frames 44.10 to 44.13 a ms, no underrun, no overrun beyond the close's.
3. **macOS at 48 kHz.** Format 48 000; `tools/uac_check.py a48.wav 60 --rate 48000` while playing (or with its
   chords). Pass: one open, "the rate of that open ... 48000 Hz", frames 48.00 to 48.04 a ms, `ua_pk576` about 98 %
   of the packets and `ua_pk588` about 2 % (`ua_pk564` 0), `ua_pk_other` unchanged, no underrun, no overrun beyond
   the close's; `ua_rs_avg_us` near 45 (x 4 = about 180 us a half). Listen to the WAV: no clicks, pitch right.
4. **A DAW at 48 kHz** (Live or Logic, project at 48 kHz, six inputs): record the three pairs for a minute with a
   busy patch. Pass: no gaps, the stems line up with the master, `ua_opens` + 1.
5. **Switch the rate while recording** (QuickTime or the DAW recording, then change Format in Audio MIDI Setup, both
   ways). Pass: a short gap at the switch and sound after it, `ua_rate_changes` + 1 each time, no `usb_stalls`, the
   MIDI port stays.
6. **Reopen at the other rate; sleep and wake the Mac** with ChoralRoot In at 48 kHz. Pass: after the wake a new
   recording works at 48 kHz (`ua_rate` 48000, or 44100 with the menu saying so: report which), `usb_resets` may
   rise.
7. **The heavy case.** CZ-1 chords, bass playing, chorus/delay and reverb up, Dual Core Off, then On: `--debug`
   (clears PEAK), play 30 s while recording at 44.1 kHz, `--debug`; the same at 48 kHz. Compare `pk_block_us`,
   `pk_late`, `stale_halves` (must stay 0) and whether voices drop. This is the number that decides whether six
   channels at 48 kHz are affordable in every patch.
8. **iPhone / iPad** (USB-C or the camera adapter; GarageBand, AUM or Voice Memos) and **Android** if at hand: the
   input appears and records at the app's 48 kHz; afterwards `--debug` on the Mac: `ua_rate_last_open` 48000. Most
   phone apps take channels 1-2 (the master).
9. **Windows 10 / 11.** Sound settings > ChoralRoot In > Properties > Advanced: both rates offered; record 30 s at
   each in Audacity (WASAPI). Pass as 2 and 3. (Windows installs the device anew once: `bcdDevice` changed.)
10. **Through a USB hub** at 48 kHz, 60 s: one open (the 0.14 fault was reopens through a hub), `ua_opens` + 1 only.

If 3 fails with reopens (`ua_opens` rising) only at 48 kHz: suspect the 49-frame packets or the 688-byte reservation
on a hub; if `ua_pk_other` moves: a packet length bug; if `ua_rate` stays 44100 with the Mac at 48 000: the host did
not send `SET_CUR` (the event log in `--debug` shows `SET_CUR(ep rate)` or not).

## Cost

(At 44.1 kHz; what 48 kHz adds is counted in "48 kHz" above.)

The emulator cannot stream, so the cost is measured on the host and scaled (`sh tools/emu/perf.sh`, scenario (u):
`tests/cr_usbaudio_test.c --bench`, emu.c's ratio of 259 host instructions per device µs): while the computer
records, the audio ISR's share (the stage cleared, the part captures, the master tap, the ring copy) is about 21 µs a
2.9 ms half and TIMER5's packets (the packet out) about 10 µs: **~31 µs, 1.1 % of the half**, plus the SIE
register accesses of the endpoint service (up to 4 kHz, nested in the render; not in the host figure). Without the
stream the ISR does none of it (a flag test a block, no IRQ-off section). On the device: record, then switch USB
Record Off and read `cpu` and `status` on the console: `audio_max_all_us`, `ua_poll_max_us`, `ua_service_max_us`
(the longest endpoint service), and the glitch counters.

## Memory

| Buffer | Where | Size |
| --- | --- | --- |
| capture ring: 512 frames x 6 ch x 16 bit | POOL (`usb_audio_stream.c` `ua`) | 6144 B |
| the stream state and counters | POOL | 72 B |
| 48 kHz: the resampler's history (22 frames) and its block (36 frames), x 6 ch x 16 bit | POOL (`ua.rs_in`, `ua.rs_out`) | 696 B |
| 48 kHz: the filter's table, 80 phases x 12 taps | flash (`uac_fir.h`) | 1920 B |
| capture packets, double-buffered: 2 x 588 B (2 x 540 to 0.18) | RAM (`usb_audio.c` `ua_tx`) | 1176 B |
| the configuration as sent (`ua_cfg`) | RAM | 211 B |
| one block's capture frames, 32 x 6 x 16 bit | RAM (`fx.c` `ua_stage`) | 384 B |

Felucca's master-only input (a 2 KiB ring and its packet in RAM) is removed. Removing the playback, measured by
`./build.sh` against the 0.14 dev build with it: POOL −4,124 B (321,612 → 317,488 B, 92.3 %), RAM −432 B (79,516 →
79,084 B, 80.4 %), XIP −1,272 B (349,200 → 347,928 B).

## Verified, and what only hardware can verify

Host tests (`tests/cr_usbaudio_test.c`, in `tests/run_cr_tests.sh` and `tests/run_tests.sh`): the descriptors of
both presentations (USB Record on, the console) parsed as a host parses them, no playback (no OUT or feedback
endpoint, no stereo format); the routing (the six staged channels into the ring, none once the host closed the
stream); the PCM16 packing; 20 s of a fast (44,117.6 Hz) and a slow (44,070 Hz) I2S clock against the host's frames
with renders at random times: no glitch, every frame once and in order, the rate matched, the ring's fill well inside
it; the host stopping for 50 ms: counted, re-primed, in order again; the 44.1 kHz stream's digest ("48 kHz" above).
48 kHz: `tests/cr_usbaudio48_test.c`. The settings: `tests/cr_settings_test.c`.

Only the device can show: **enumeration on macOS and Windows** (the recording device and the MIDI port appear, the
names, 44.1 kHz accepted and 48 kHz offered; the console with USB Record Off, the replug), long-run **clock drift** against a real host
(no `ua_cap_underruns` / `_overruns` over an hour), the 540-byte capture packets on the JieLi controller (Melodee's
largest was the same 540), the stream with a full chord (`audio_max_all_us`), and iPadOS.

## What went wrong and why (0.14, 2026-10-10)

The first device recordings (QuickTime, ffmpeg, Live) gave one slice of sound and then seconds of silence: a 14 s
capture held 6.5 s of audio. GEEK OUT's `usb` line showed the ring over-running (`o` rising, `u` 0) only while notes
sounded. Measured with `tools/uac_check.py` and the device's counters (`fm1_install.py --debug`, below), one step
at a time:

- **The device was not starved.** The endpoint is armed again as soon as the host takes a packet: at every call of
  the service, from TIMER5 (every tick, nested in the render too) and from the audio ISR before each of its blocks,
  with the IRQs off (`main.c ua_service_paced`, `audio.c ua_isr_service`). With that, ~992 packets a second went out
  while the stream was open, every gap between two packets under 1.2 ms.
- **The host closed and reopened the stream.** The event log showed `SET_INTERFACE` alt 0 about 250 ms after each
  alt 1, then alt 1 again: ~25 times in 14 s with chords, ~6 in silence, nothing on the device's side between. Each
  reopen cost the recording a restart. The packet rate (a servo, an exact 44.1 pattern, the I2S's own 44.1176), the
  sync type (async, adaptive, synchronous), the CPU load and the MIDI traffic made no difference.
- **The sample bytes did.** Builds that replaced the audio in the packets: all zeros with chords playing, 1 reopen
  (0.938 of the wall time recorded); constant 0x1000, 4; noise, 6; the master pair only (the part channels zero), 19;
  the real chords, ~25; every sample 0xFFFF (silent to the ear), 29, the worst. USB inserts a 0 bit after six 1 bits
  (bit stuffing), so a packet of 0xFF bytes is up to 7/6 longer on the wire; small negative samples (0xFFxx), all over
  quiet and decaying audio, do the same. The endpoint reserved exactly 540 bytes a frame, and the FM-1 was on a
  USB 2 hub (split transactions, a tight schedule): a stuffed packet overran its slot, the host counted an error and
  CoreAudio restarted the stream.
- **The fix:** `wMaxPacketSize` 640 (`usb_audio_stream.c UA_EP_MAXP`; 540 x 7 / 6 = 630 covers the worst case; 688
  since 0.19, for 48 kHz's 588-byte packets), the packets unchanged. On the device: 12.7 and 13.2 s of 14 recorded with chords, 27.9 s of 30, the stream opened once
  and held. A port straight on the computer may have held with 540; 640 costs nothing (a full-speed frame holds 1500
  bytes).
- **Kept from the hunt:** the ring is not fed until the host takes its first packet (`ua.cap_hold`: macOS reads ~14
  ms after `SET_INTERFACE`, which over-ran the 11.6 ms ring at every open) and is held again if the host stops
  reading for 20 ms; the fixed packet pattern (Melodee's fill servo wandered around 44.07 frames a packet after every
  ring reset); the endpoint armed from the audio ISR too, under `usb_sie_owner` (`usb_audio.c`: no two contexts in
  one SIE register sequence, `INDEX` then the indexed registers); the counters and the event log below.

## Reading the counters during a recording

`python3 tools/fm1_install.py --debug` (USB-MIDI SysEx 73, read-only, also while recording; web/EDITOR_PROTOCOL.md)
prints the device's counters by name, the mean packet length of the last whole open, and the last 16 events (each
`SETUP` the host sent, by name; a stalled one; each ring over-run) with the device's time in ms. Take one before and
one after a recording; the differences say:

| field | healthy |
| --- | --- |
| `ua_opens`, `ua_open_close_ms` | one open per recording, held for its length (each reopen is a gap in the recording) |
| `ua_tx_packets` / `ua_open_ms` | ~1 packet a ms while open |
| `ua_overruns`, `ua_underruns` | no under-run (the ring ran dry); an over-run (the host stopped reading) only at a close of the stream: one is counted there, the host stops reading a moment before it closes it |
| `ua_arm_gap_*` | the gaps between two armed packets: all `< 1.2 ms` but one per open |
| `ua_pk528`, `ua_pk540`, `ua_pk_other` | ~90 % / ~10 % / 0 (the 44.1 pattern; any other length is a bug). At 48 kHz these two stand still and `ua_pk576` / `ua_pk588` count (SysEx 79, "48 kHz" above); `ua_pk_other` stays 0 |
| `ua_first_take_ms` | ~14: the host's first read after an open |
| `ua_holds` | 0 (the host stopped reading without closing the stream) |
| `ua_isr_sie_busy`, `ua_t5_sie_busy` | 0 (an SIE sequence was preempted and its service skipped) |
| `nested`, `late`, `cpu_*`, `st_*` | the audio ISR: TIMER5 nested in it, late halves, the render's time and stages |

## Measuring the recording on a Mac

`tools/uac_check.py OUT.wav [SECONDS] [--nonotes] [--debug] [--rate 44100|48000]` records ChoralRoot In with ffmpeg
(avfoundation) while it plays C-major chords on the FM-1 over USB-MIDI. Its verdict is what the FM-1 itself did
during the capture: it reads the counters above (SysEx 73 DEBUG, and 0.19's 79 USB for the rate) before and after,
and prints their difference. The rate is the one the FM-1 reports for the open (`ua_rate_last_open`), not an
assumption: the same frame count is 8.8 % apart between the two. `--rate` says which rate was meant (the Mac's is
set in Audio MIDI Setup; the tool does not change it), writes the WAV at it, and makes the other rate a fault:

- the opens of the stream: one per recording; each further one is a restart of the stream by the host, a gap;
- the last whole open's length against the wall time, and the frames sent in it against its rate x that length
  (`ua_open_close_ms`, `ua_frames_last_open`, `ua_rate_last_open`): within a fraction of a percent (the host's
  first read comes ~14 ms after the open; the FM-1's audio clock runs about +0.04 % over 44100);
- at 48 kHz the packets by size, the host's `SET_CUR`s and the resampler's time on the unit;
- the packets against the USB frames seen while the stream was open (`ua_tx_packets`, `ua_sofs`): one a frame;
- `ua_overruns`, `ua_underruns`, `ua_missed_frames`. One overrun is counted at each close of the stream, when the
  host stops reading a moment before it closes it: the tool says so and names what is beyond the closes;
- the nudges, as the FM-1's audio clock against the computer's USB clock in ppm.

**The WAV's length is not the measure.** ffmpeg's avfoundation capture is itself short: 56.70 s of audio in a 60 s
capture, from ChoralRoot In and from an unrelated audio interface alike, and 12 % short over an hour on a busy Mac.
The earlier "drain ratio" (WAV length / wall time) measured ffmpeg, not the unit. The tool now prints the WAV's
length as what ffmpeg wrote, and keeps the WAV for what it does show: the loud 50 ms windows and every silent gap
of 0.5 s or more while chords were sent.

The reference, an hour with `--nonotes` on a Mac busy with builds (2026-10-10): 3 597 711 packets for 3 597 791
SOFs, 0 underruns, 12 overruns / 189 frames (nine in two minutes of heavy host load, three at stream closes),
`ua_nudges_up` 69 036 = the FM-1's audio clock about +435 ppm against the Mac's USB clock, absorbed by the nudges.

`--debug` adds the counters as read. Needs `ffmpeg` and `python-rtmidi`; it never writes the FM-1.

## Where it lives

| File | What |
| --- | --- |
| `firmware/src/usb.c` | the descriptors (`CFG_DESC` with `usb_audio_desc.h`, `CFG_DESC_CDC`), the strings, EP0 (SET_INTERFACE, the UAC1 sampling-frequency requests), `usb_cdc_on`, `usb_replug`, `ua_off_set` / `ua_off_apply`, `ua_service` |
| `firmware/src/usb_audio.c` | Melodee's endpoint service: `ua_cfg_build`, SET_INTERFACE, the isochronous endpoint in TIMER5 |
| `firmware/src/usb_audio_stream.c` | the capture ring, the packet pattern and its nudge, `UA_EP_MAXP`, the packet format, `ua_feed` / `ua_audio`; 48 kHz: `ua_rate_set`, `ua_resample` and its kernels (no hardware: the host tests build it) |
| `firmware/src/uac_fir.h`, `tools/gen_uac_fir.py` | the 44.1 -> 48 kHz filter's table (SLOOP 2.5's) and its generator (`--report`: a filter's figures) |
| `firmware/src/usb_audio_desc.h` | the recording function's descriptors |
| `firmware/src/fx.c` | `ua_stage` and the part captures (`mix_part`), `fx_usb_fixed` / `usb_fixed_dac` (USB Level) |
| `firmware/src/choralroot.c` | the master tap (the `mix_block` shim, before the click) |
| `firmware/src/audio.c` | USB Level Fixed's DAC scaling, then at 48 kHz `ua_block48` (the resampler, IRQs on), then `ua_feed` with the IRQs off |
| `firmware/src/cr_backup.c` | SysEx 73 DEBUG's `ua_*` counters; 79 USB (the rate, the 48 kHz counters) |
| `firmware/src/main.c` | `ua_service` in TIMER5 (every 250 µs at most, nested in the render too); `ua_off_apply` in the main loop |
| `firmware/src/cr_settings.[ch]`, `cr_ui.c` | the two settings and their Options entries |
| `firmware/src/console.c` | `status` / `dbg`: the `ua_*` counters (since the boot) |

Build flags: `FELUCCA_UAC` (1: the USB recording) and `FELUCCA_CDC` (1: the console, presented as above). `FELUCCA_UAC=0`
gives the previous MIDI + console device (no audio). The update loader is unchanged (byte-identical).
