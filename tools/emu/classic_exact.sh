#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Options > Output = Classic is 0.18's master, bit for bit (docs/AUDIO-QUALITY.md): every script of tools/emu/scripts
# through this tree's emulator with the Output held at Classic (EMU_OUTPUT=classic: the settings of a fresh flash say
# Clean) and through a reference emulator built from the 0.18 tree, each on a fresh RAM flash, and the two WAVs
# compared byte for byte.
#   mkdir -p build/scratch/ref18 && git archive 19b6453 | tar -x -C build/scratch/ref18
#   cp -R build/gen build/scratch/ref18/build/gen      (the generated headers: the same sources make the same)
#   (cd build/scratch/ref18 && sh tools/emu/build.sh)
#   sh tools/emu/classic_exact.sh build/scratch/ref18/build/host/emu
# Headless: export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
# Not part of test_cr.sh (it needs the reference build); never run it beside test_cr.sh (both write build/emu/test).
cd "$(dirname "$0")/../.." || exit 1
REF=${1:?usage: classic_exact.sh REFERENCE_EMU}
EMU=build/host/emu
O=build/scratch/classic_exact
mkdir -p "$O/new" "$O/ref" build/emu/test
n=0
same=0
diff=0
silent=0
for f in tools/emu/scripts/*.txt; do
    b=$(basename "$f" .txt)
    "$REF" --headless --script "$f" --wav "$O/ref/$b.wav" >"$O/ref/$b.log" 2>&1
    EMU_OUTPUT=classic "$EMU" --headless --script "$f" --wav "$O/new/$b.wav" >"$O/new/$b.log" 2>&1
    n=$((n + 1))
    if [ ! -s "$O/ref/$b.wav" ] || [ ! -s "$O/new/$b.wav" ]; then
        echo "NO WAV  $b"
        diff=$((diff + 1))
    elif cmp -s "$O/ref/$b.wav" "$O/new/$b.wav"; then
        same=$((same + 1))
        # (a WAV that is all zeros proves nothing: counted apart)
        if [ "$(tail -c +45 "$O/new/$b.wav" | tr -d '\000' | wc -c)" -eq 0 ]; then
            silent=$((silent + 1))
        fi
    else
        echo "DIFFERS $b"
        diff=$((diff + 1))
    fi
done
echo "classic_exact: $n scripts, $same WAVs identical to the reference ($silent of them silent), $diff differ"
[ "$diff" -eq 0 ] && echo PASS || { echo FAIL; exit 1; }
