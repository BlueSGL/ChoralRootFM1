#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the ChoralRoot engine host tests (firmware/src/cr_engine.c), no make needed:
#   sh tests/run_cr_tests.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
# the boot guard's decision (firmware/src/cr_bootguard.h, main.c fm1_cstart)
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_bootguard_test tests/cr_bootguard_test.c
./build/host/cr_bootguard_test
# the boot stub (docs/BOOT-SAFETY.md "As built"): its decision (firmware/stub/stub_core.h: marker, rescue key, staged
# loader, rescue count; the sanitizers watch its reads), and the package builder's side (the marker, the compensation
# bytes, sector 0x4000 byte-identical for two bodies). The loader's interruption model: tests/ldr_test.c (run_tests.sh)
cc -std=c99 -Wall -Wextra -Wno-unused-function -Werror -pedantic -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o build/host/cr_stub_test tests/cr_stub_test.c
./build/host/cr_stub_test
python3 tests/stub_pkg_test.py
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_engine_test tests/cr_engine_test.c firmware/src/cr_engine.c
./build/host/cr_engine_test
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_loop_test tests/cr_loop_test.c firmware/src/cr_loop.c firmware/src/cr_engine.c
./build/host/cr_loop_test
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_settings_test tests/cr_settings_test.c firmware/src/cr_settings.c firmware/src/cr_engine.c
./build/host/cr_settings_test
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -Ibuild/gen -o build/host/cr_va_test tests/cr_va_test.c -lm
./build/host/cr_va_test
# QUAD (docs/QUAD.md): blob, pages, ratios, the routings, against tests/quad_ref.py's goldens, envelopes, filters, LFOs,
# presets; an int32 wrap aborts (the sanitizer). The CPU figure: tests/run_quad_test.sh
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -DFELUCCA_QUAD=1 -Ibuild/gen -o build/host/cr_quad_test tests/cr_quad_test.c -lm
./build/host/cr_quad_test
cc -std=c99 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_trans_test tests/cr_trans_test.c -lm
./build/host/cr_trans_test
# the bass part on the second core (firmware/src/cr_cpu1.h, docs/DUALCORE.md): the split bit for bit, the budgets, the fail-safes
cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_cpu1_test tests/cr_cpu1_test.c -lm
./build/host/cr_cpu1_test
# ChoralRoot's delay and reverb (firmware/src/cr_fx.c, docs/FX.md): the coefficients, tempo sync, the half rate's response
# and noise, every corner stable and ending in exact silence, smooth parameter changes, the level against Felucca's
# effects, mono, the cost; impulse responses for tools/fx_plots.py into build/fx-review
mkdir -p build/fx-review
cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_output_test tests/cr_output_test.c -lm
./build/host/cr_output_test > build/host/cr_output_test.log || { cat build/host/cr_output_test.log; exit 1; }
grep -c ' ok$' build/host/cr_output_test.log | sed 's/^/cr_output_test: /; s/$/ checks ok/'
tail -1 build/host/cr_output_test.log
cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fx_test tests/cr_fx_test.c -lm
./build/host/cr_fx_test build/fx-review > build/host/cr_fx_test.log || { cat build/host/cr_fx_test.log; exit 1; }
grep -c ' ok$' build/host/cr_fx_test.log | sed 's/^/cr_fx_test: /; s/$/ checks ok/'
tail -1 build/host/cr_fx_test.log
# 0.19: what does not pass the master lined up with it (the click, the USB recording's part pairs), the chorus's test
# switch, MASTER at power-on (docs/AUDIO-QUALITY.md "Round 3"), on the emulator's firmware build
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_align_test tests/cr_align_test.c -lm
./build/host/cr_align_test > build/host/cr_align_test.log 2>&1 || { grep -a 'cr_align' build/host/cr_align_test.log; exit 1; }
grep -ac ' ok$' build/host/cr_align_test.log | sed 's/^/cr_align_test: /; s/$/ checks ok/'
grep -a 'cr_align_test' build/host/cr_align_test.log | tail -1
# backup / restore SysEx (cr_backup.c) on the emulator's firmware build (tools/emu/emu_firmware.h; macOS: os_unfair_lock)
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_backup_test tests/cr_backup_test.c -lm
./build/host/cr_backup_test
# a restart asked over SysEx (72 RESTART, 77 STUB) leaves once the host has the reply: the device's ed_service and usb.c's
# EP1 against a model of the SIE, the host and TIMER5 (the same firmware build, FELUCCA_OTA 1)
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_restart_test tests/cr_restart_test.c -lm
./build/host/cr_restart_test
# FM6 (Melodee's engine, docs/FM6.md): patches, the blob, DX7 SysEx, the bank, the deep pages, the patch store, voices
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fm6_test tests/cr_fm6_test.c -lm
./build/host/cr_fm6_test > build/host/cr_fm6_test.log || { cat build/host/cr_fm6_test.log; exit 1; }
tail -1 build/host/cr_fm6_test.log
# CZ-1 (Melodee's engine, docs/CZ1.md): Casio's tones, the blob, Casio SysEx, the banks, the deep pages, the tone store
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_cz_test tests/cr_cz_test.c -lm
./build/host/cr_cz_test > build/host/cr_cz_test.log || { cat build/host/cr_cz_test.log; exit 1; }
tail -1 build/host/cr_cz_test.log
# Felucca #61 (1.0.3.1): DIGITAL -> FM6 conversion without a divide by zero (the JieLi compiler hoists a guarded
# divide; a 0.9 ORGAN preset trapped at boot -> UBOOT). Felucca's own test, built with the divide-by-zero sanitizer.
cc -O1 -w -fsanitize=integer-divide-by-zero -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/fm4_div0_test tests/fm4_div0_test.c -lm
./build/host/fm4_div0_test > build/host/fm4_div0_test.log || { cat build/host/fm4_div0_test.log; exit 1; }
grep -c ' ok$' build/host/fm4_div0_test.log | sed 's/^/fm4_div0_test: /; s/$/ checks ok/'
# USB audio recording (Melodee's, docs/USB-AUDIO.md): usb.c's descriptors in each presentation (USB Record on, the
# console), the capture routing, the PCM packing, the ring against clock drift and the host stopping (usb_audio_stream.c)
cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio_test tests/cr_usbaudio_test.c
./build/host/cr_usbaudio_test > build/host/cr_usbaudio_test.log || { cat build/host/cr_usbaudio_test.log; exit 1; }
tail -1 build/host/cr_usbaudio_test.log
# ChoralRoot In at 48 kHz (0.19, tests/cr_usbaudio48_test.c): the two-rate descriptors against SLOOP 2.5's, the
# sampling-frequency requests, the resampler (160 / 147 exactly, bit for bit against a reference, its spectrum), the
# 48 kHz stream and a rate switch mid-stream. (44.1 kHz bit for bit: the digest in cr_usbaudio_test above)
cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio48_test tests/cr_usbaudio48_test.c -lm
./build/host/cr_usbaudio48_test > build/host/cr_usbaudio48_test.log || { cat build/host/cr_usbaudio48_test.log; exit 1; }
tail -1 build/host/cr_usbaudio48_test.log
# Options > USB Rate (0.19, 2026-10-12: 48 kHz is opt-in; tests/cr_usbrate_test.c): at 44.1 kHz the descriptors are
# 0.18's byte for byte (tests/cr_usb018_desc.h) and SET_CUR 48000 changes nothing; the 44.1 kHz digest in both
# states; a change of the option leaves the bus once, and never under a SysEx transfer
cc -std=gnu11 -O2 -w -o build/host/cr_usbrate_test tests/cr_usbrate_test.c
./build/host/cr_usbrate_test > build/host/cr_usbrate_test.log || { cat build/host/cr_usbrate_test.log; exit 1; }
tail -1 build/host/cr_usbrate_test.log
# does the USB recording change what the instrument plays? (tests/cr_usbtap_test.c, docs/USB-AUDIO.md "Does USB
# recording affect the sound?"): the DAC's 24-bit words in five USB states (Record Off, closed, open and waiting,
# 44.1 kHz, 48 kHz) for Output Clean / Classic, USB Level, MASTER, one core and two: bit-identical
cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_usbtap_test tests/cr_usbtap_test.c -lm
./build/host/cr_usbtap_test > build/host/cr_usbtap_test.log || { cat build/host/cr_usbtap_test.log; exit 1; }
tail -1 build/host/cr_usbtap_test.log
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_midi_test tests/cr_midi_test.c
./build/host/cr_midi_test
# the sound templates the clients embed for .syx imports are the firmware's defaults (docs/SOUNDS.md)
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/sound_templates tests/sound_templates.c -lm
./build/host/sound_templates --check web/fm1sounds.js tools/fm1_install.py
