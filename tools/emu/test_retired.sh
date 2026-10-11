#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless: a unit that comes to 0.18 with sounds on the engines retired there (PHASE, LOFI, VOICE, TRIO, NOISE:
# docs/INTEGRATION.md "0.18: PHASE, LOFI, VOICE, TRIO, NOISE retired").
#   run 1 (the pre-cut build, a fresh flash file): the chord part to PHASE (EDIT held + A4), saved as U01 "ADG", then
#         parked on NOISE (OPT + PRESETS); the settings saved. The pre-cut build is this tree with the five switched
#         back on (-DFELUCCA_PHASE=1 .. -DFELUCCA_NOISE=1: the engine set 0.17 had), build/host/emu_precut
#   run 2 (today's build, the same file): the unit starts and plays FM6's default (TINE EP), as a fresh unit; the PHASE
#         sound is an added preset of the VA's pool under its name, the VA's init sound when picked, with the message
#         "PHASE retired: stand-in": build/emu/test/retired_standin.ppm
#   run 3 (the pre-cut build, the same file): U01 is still the PHASE sound (nothing rewritten, nothing deleted)
#   sh tools/emu/test_retired.sh       (test_cr.sh runs it)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
EMU=build/host/emu
PRE=build/host/emu_precut
OBJ=build/host/emu_obj
OUT=build/emu/test
S=tools/emu/scripts
FLASH=$OUT/retired_flash.bin
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }
has() { LC_ALL=C grep -aq "$1" "$2"; }
if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
# the pre-cut build: the firmware unit again with the five engines on, the emulator's own objects as build.sh left them
if [ ! -x "$PRE" ] || [ "$EMU" -nt "$PRE" ]; then
    SDL_LIBS=$(/opt/homebrew/bin/sdl2-config --libs 2>/dev/null || echo "-L/opt/homebrew/lib -lSDL2")
    ${CC:-clang} -O2 -w -Ibuild/gen -Ifirmware/src '-DFELUCCA_VERSION="ChoralRoot 0.17"' -DFELUCCA_PHASE=1 -DFELUCCA_LOFI=1 \
        -DFELUCCA_VOICE=1 -DFELUCCA_TRIO=1 -DFELUCCA_NOISE=1 -c tools/emu/emu_fw.c -o "$OBJ/emu_fw_precut.o" &&
    ${CC:-clang} -o "$PRE" "$OBJ/emu.o" "$OBJ/keymap.o" "$OBJ/emu_img.o" "$OBJ/emu_midi.o" "$OBJ/emu_fw_precut.o" \
        $SDL_LIBS -lz -lm -framework CoreMIDI -framework CoreFoundation || { echo "pre-cut build failed"; exit 1; }
fi
mkdir -p "$OUT"
rm -f "$FLASH" "$OUT"/retired_1.log "$OUT"/retired_2.log "$OUT"/retired_3.log "$OUT"/retired_standin.ppm "$OUT"/retired_2.wav
part0() { LC_ALL=C sed -n 's/^.*part 0: \([^,]*\).*$/\1/p' "$1" | tail -1; }

echo "retired engines: a 0.17 unit's flash on 0.18"
EMU_UI_LOG=1 "$PRE" --headless --flash "$FLASH" --script "$S/retired_set.txt" >"$OUT/retired_1.log" 2>&1 || bad "run 1: exit status"
has '^engine: part 0 -> PHASE' "$OUT/retired_1.log" && has '^save: part 0 slot U01 name ADG' "$OUT/retired_1.log" &&
    ok "run 1 (pre-cut): the chord part on PHASE, saved as U01 ADG" || bad "run 1: no PHASE sound saved"
has 'part 0: NOISE /' "$OUT/retired_1.log" && has 'settings saves [1-9]' "$OUT/retired_1.log" &&
    ok "run 1 (pre-cut): the chord part parked on NOISE ($(part0 "$OUT/retired_1.log")), the settings saved" ||
    bad "run 1: not parked on NOISE: $(part0 "$OUT/retired_1.log")"

EMU_UI_LOG=1 "$EMU" --headless --flash "$FLASH" --script "$S/retired_check.txt" --wav "$OUT/retired_2.wav" >"$OUT/retired_2.log" 2>&1 ||
    bad "run 2: exit status"
has 'record: current' "$OUT/retired_2.log" && ok "run 2: the unit starts, its settings record read" || bad "run 2: record not read"
LC_ALL=C grep -a 'part 0: ' "$OUT/retired_2.log" | head -1 | grep -q 'part 0: FM6 / TINE EP' &&
    ok "run 2: the chord part parked on NOISE comes up on FM6 TINE EP, as a fresh unit" ||
    bad "run 2: the chord part: $(LC_ALL=C grep -a 'part 0: ' "$OUT/retired_2.log" | head -1)"
has '^load: slot 1 engine 2 (PHASE) retired -> INIT on VA, its sends, ATK, REL' "$OUT/retired_2.log" &&
    has '^sound: part 0 pos 26 ADG' "$OUT/retired_2.log" &&
    ok "run 2: VA 26 is the PHASE sound ADG: the VA's init sound, a stand-in (its sends, ATK, REL)" ||
    bad "run 2: no stand-in: $(LC_ALL=C grep -a '^load:\|^sound: part 0' "$OUT/retired_2.log" | tail -2 | tr '\n' ' ')"
has '^popup: part 0 26 / ADG / VA ' "$OUT/retired_2.log" && LC_ALL=C grep -a 'part 0: ' "$OUT/retired_2.log" | tail -1 | grep -q 'part 0: VA /' &&
    [ -s "$OUT/retired_standin.ppm" ] &&
    ok "run 2: it plays on the VA under its own name (the popup: ADG, VA 26/26): $OUT/retired_standin.ppm" || bad "run 2: $(part0 "$OUT/retired_2.log")"
n=$(LC_ALL=C sed -n 's/.*non-zero samples \([0-9]*\).*/\1/p' "$OUT/retired_2.log" | tail -1)
[ "${n:-0}" -gt 1000 ] && ok "run 2: both chords sounded ($n non-zero samples)" || bad "run 2: silent (${n:-?} non-zero samples)"
b=$(LC_ALL=C grep -a 'parts busy' "$OUT/retired_2.log" | tail -1 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p')
[ "${b:-x}" = 0 ] && ok "run 2: silent 2 s after the release" || bad "run 2: ${b:-?} voices still sounding"

EMU_UI_LOG=1 "$PRE" --headless --flash "$FLASH" --script "$S/retired_back.txt" >"$OUT/retired_3.log" 2>&1 || bad "run 3: exit status"
has '^sound: part 0 pos 7 ADG' "$OUT/retired_3.log" && has '^popup: part 0 07 / ADG / PHASE ' "$OUT/retired_3.log" && has 'part 0: PHASE /' "$OUT/retired_3.log" &&
    ok "run 3 (pre-cut): U01 is still the PHASE sound (PHASE 07 ADG): nothing rewritten, nothing deleted" ||
    bad "run 3: $(part0 "$OUT/retired_3.log")"

[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
