# Digitone envelope-time measurement

Measures the Elektron Digitone's (original, OS 1.4x) envelope time curves so the
FM TONE engine (`eng_quad.c`, `docs/QUAD.md`) can match them. Elektron publishes
no times.

- `dt_cc.py`: the Digitone's track MIDI CC/NRPN map (manual OS 1.44, Appendix B:
  <https://www.elektron.se/wp-content/uploads/2026/10/Digitone-User-Manual_ENG_OS1.44_260930.pdf>).
  14-bit parameters go out as NRPN, 7-bit ones as CC. `python3 tools/digitone/dt_cc.py`
  prints the table and example bytes.
- `dt_measure.py`: sets a plain-sine base patch on track 1 over MIDI, plays one C4
  (velocity 100) per measured value, records the Digitone's USB audio with
  `build/dt_rec` (device "Elektron Digitone", 48 kHz). It writes one WAV + JSON per
  capture. Each capture starts and ends with a short sync marker note; the analysis
  maps the logged host times onto the audio with the two markers, which removes
  recorder start latency, MIDI/audio latency and clock drift. The JSON also keeps
  dt_rec's host time of the first sample (`host_rec_start`) and its counts
  (`rec_stats`).
- `dt_rec.c`, `build_rec.sh`: `dt_rec DEVICE_NAME OUT.wav` records a CoreAudio input
  (AUHAL, native rate and channels, 32-bit float WAV) until `q`/EOF on stdin or
  SIGINT/SIGTERM. It prints `start <host s of the first sample>` (the
  `time.monotonic()` clock) and at the end `frames N overruns K ...`. K counts every
  discontinuity and must be 0. dt_measure builds it into `build/dt_rec` when it is
  missing. It exists because ffmpeg's avfoundation capture drops about 10 % of the
  samples on this Mac (a 2.000 s note recorded as 1.79 s), spread through the take,
  which makes every envelope time wrong. dt_rec delivers every sample: 10 s and
  60 s takes match the host span to under 1 ms. ffmpeg is only a fallback when the
  build fails, and dt_measure prints a loud warning when it uses it.
  `--plan` prints every step and the total time and sends nothing. `--run` measures;
  `--only amp_atk,filt_dec,...` picks groups (amp_atk amp_dec amp_rel filt_dec
  filt_atk filt_rel opA opB delay fade; amp_hold is accepted but skipped: the
  original Digitone has no AMP HOLD). `--out DIR` (default
  `/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/DigitoneRecordings/times/`).
  Ctrl-C sends all notes off, stops the recorder and writes the JSON for what was captured.
- `dt_analyse.py [DIR]`: per step the amplitude envelope (FIR Hilbert, 1 ms) or the
  spectral-centroid track; rise times, -20/-40/-60 dB times, centroid sweep times,
  LFO fade times; fits T(v) as a power law and as an exponential 10^(kv), keeps the
  better; writes `analysis.json` (raw points + fits) and PNG plots if matplotlib is
  installed.
- `test_dt.py`: no hardware: CC/NRPN bytes, `--plan`, and the analysis on synthetic
  captures with known times (simulated 0.37 s recorder latency and 25 ppm drift; one
  capture written as dt_rec's float32 WAV with `host_rec_start`).

## Before a run (owner)

1. Digitone SETTINGS > MIDI CONFIG > PORT CONFIG: RECEIVE NOTES and RECEIVE CC/NRPN
   on, INPUT FROM includes USB; CHANNELS: TRACK 1 CHANNEL = 1 (or pass `--channel`).
   Audio over USB on.
2. Track 1 holds the init sound. The script asks for the settings MIDI cannot make
   (ALGO, RATIOs, FILTER TYPE, LFO DEST/WAVE/MODE/MULT, tempo) before each capture
   and waits for Enter.
3. At the end it sends back init-sound values for every parameter it changed. The
   Digitone cannot report its values, so these are best-known init values: reload
   the sound/project without saving for an exact restore.
4. Permission: allow the script in `.claude/settings.local.json` like the existing
   `fm1_install.py` rule, e.g. `"Bash(python3 tools/digitone/dt_measure.py:*)"`.

Recordings stay outside the repository (the default `--out` is outside it); never
commit audio.
