#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Options > Output = Classic is 0.18's master, bit for bit (docs/AUDIO-QUALITY.md): every script of tools/emu/scripts
# through this tree's emulator with the Output held at Classic (EMU_OUTPUT=classic: the settings of a fresh flash say
# Clean) and through a reference emulator built from the 0.18 tree, each on a fresh RAM flash, and the two WAVs
# compared byte for byte. The reference runs ITS OWN copy of the script (an Options entry was added in 0.19: the two
# scripts that walk to an entry behind it, cr_version and cr_calib, count one step more here); a script the reference
# tree does not have is not compared.
#   mkdir -p build/scratch/ref18 && git archive 19b6453 | tar -x -C build/scratch/ref18
#   mkdir -p build/scratch/ref18/build && cp -R build/gen build/scratch/ref18/build/gen     (the generated headers)
#   (cd build/scratch/ref18 && sh tools/emu/build.sh)
#   sh tools/emu/classic_exact.sh build/scratch/ref18
# Headless: export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
# Not part of test_cr.sh (it needs the reference build); never run it beside test_cr.sh (both write build/emu/test).
cd "$(dirname "$0")/../.." || exit 1
RD=${1:?usage: classic_exact.sh REFERENCE_TREE}
REF=$RD/build/host/emu
EMU=build/host/emu
[ -x "$REF" ] || { echo "no $REF: build the reference first (the lines above)"; exit 1; }
O=build/scratch/classic_exact
mkdir -p "$O/new" "$O/ref" build/emu/test
n=0
same=0
diff=0
silent=0
new=0
for f in tools/emu/scripts/*.txt; do
    b=$(basename "$f" .txt)
    if [ ! -f "$RD/tools/emu/scripts/$b.txt" ]; then
        echo "NEW     $b (not in the reference tree: not compared)"
        new=$((new + 1))
        continue
    fi
    "$REF" --headless --script "$RD/tools/emu/scripts/$b.txt" --wav "$O/ref/$b.wav" >"$O/ref/$b.log" 2>&1
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
echo "classic_exact: $n scripts compared, $same WAVs identical to the reference's ($silent of them silent), $diff differ; $new new scripts not compared"
[ "$diff" -eq 0 ] && echo PASS || { echo FAIL; exit 1; }
