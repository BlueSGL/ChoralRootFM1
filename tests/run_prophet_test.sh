#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# PROPHET's host checks beside tests/cr_prophet_test.c's own (docs/PROPHET.md "Tests"), no make needed:
#   sh tests/run_prophet_test.sh            (tests/run_cr_tests.sh runs it)
# 1. The remix's three optimisations are bit-exact (eng_prophet_test.c P5_STAGE36: 1 a silent oscillator skipped,
#    2 the envelope's terms once a sample, 4 hard sync's 32-bit products): cr_prophet_test --pcm writes a digest of
#    every sample of a fixed render of every factory program and of 683 stress programs (sync, Poly-Mod, the wheel on
#    every destination, both filters, noise); the file is built with P5_STAGE36 0 (Melodee 8a3074b's code), 1, 2, 4 and
#    7 (the default) and the five outputs must be the same bytes.
# 2. FELUCCA_P5_FACTORY=0: the engine with INIT PROPHET alone (no factory data in the build) compiles, and
#    cr_prophet_test's checks pass on it (the bank's are replaced by "INIT alone").
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
CC="sh tests/hostcc.sh cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests"
for n in 0 1 2 4 7; do
    $CC -DP5_STAGE36=$n -o build/host/cr_prophet_s$n tests/cr_prophet_test.c -lm &
done
$CC -DFELUCCA_P5_FACTORY=0 -o build/host/cr_prophet_init tests/cr_prophet_test.c -lm &
wait
for n in 0 1 2 4 7; do
    [ -x build/host/cr_prophet_s$n ] || { echo "run_prophet_test.sh: build/host/cr_prophet_s$n did not build"; exit 1; }
    ./build/host/cr_prophet_s$n --pcm build/host/cr_prophet_s$n.pcm > /dev/null &
done
wait
lines=$(wc -l < build/host/cr_prophet_s0.pcm | tr -d ' ')
[ "$lines" -ge 884 ] || { echo "run_prophet_test.sh: $lines renders in build/host/cr_prophet_s0.pcm, want 201 + 683"; exit 1; }
for n in 1 2 4 7; do
    cmp -s build/host/cr_prophet_s0.pcm build/host/cr_prophet_s$n.pcm || {
        echo "run_prophet_test.sh: P5_STAGE36=$n renders other samples than 8a3074b's code:"
        diff build/host/cr_prophet_s0.pcm build/host/cr_prophet_s$n.pcm | head -5
        exit 1
    }
done
echo "prophet: P5_STAGE36 1, 2, 4 and 7 render 8a3074b's samples ($lines renders: 201 programs, the stress programs, bit for bit)"
[ -x build/host/cr_prophet_init ] || { echo "run_prophet_test.sh: FELUCCA_P5_FACTORY=0 did not build"; exit 1; }
./build/host/cr_prophet_init > build/host/cr_prophet_init.log || { grep -a "FAIL\|failed" build/host/cr_prophet_init.log; exit 1; }
echo "prophet: FELUCCA_P5_FACTORY=0 builds and plays ($(grep -ac '^ok ' build/host/cr_prophet_init.log) checks ok, INIT PROPHET alone)"
