#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# P5's host checks beside tests/cr_prophet_test.c's own (docs/PROPHET.md "Tests"), no make needed:
#   sh tests/run_prophet_test.sh            (tests/run_cr_tests.sh runs it)
# 1. The remix's three optimisations are bit-exact (eng_prophet_test.c P5_STAGE36: 1 a silent oscillator skipped,
#    2 the envelope's terms once a sample, 4 hard sync's 32-bit products): cr_prophet_test --pcm writes a digest of
#    every sample of a fixed render of every shipped program and of 683 stress programs (sync, Poly-Mod, the wheel on
#    every destination, both filters, noise); the file is built with P5_STAGE36 0 (Melodee 8a3074b's code), 1, 2, 4 and
#    7 (the default) and the five outputs must be the same bytes.
# 2. FELUCCA_P5_FACTORY=0: the engine with INIT alone (no factory data in the build) compiles, and
#    cr_prophet_test's checks pass on it (the bank's are replaced by "INIT alone").
# 3. THE SHIPPED LIST (assets/prophet5-factory/shipped.txt; docs/PROPHET.md "The shipped list"): the test is built
#    with three lists: the tree's (S), all 200 (A) and one of its own (B: seven programs in an order of their own,
#    two of them not in the tree's list). Each build passes its own checks; a program's render (--pcm) is the same
#    samples in every build that ships it; and the session's record of a part (--id-out: what cr_settings.c writes)
#    written by one build is read by the others (--id-in): the same program comes back, at the place THAT build has
#    it at, or from the record alone when it is not shipped there. So changing the list does not change what a saved
#    session plays.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host/p5_all build/host/p5_b
CC="sh tests/hostcc.sh cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests"
# list B: reversed against the bank, with programs the provisional list has (1, 3, 82, 135) and has not (200, 2, 7)
printf '# run_prophet_test.sh: a list of its own\n200  # CAT\n135\n82\n7    # NO WHINING (UNISON)\n3\n2\n1\n' > build/host/p5_b/shipped.txt
python3 tools/gen_prophet_factory.py build/host/p5_all/melodee_prophet_factory.h all > /dev/null
python3 tools/gen_prophet_factory.py build/host/p5_b/melodee_prophet_factory.h build/host/p5_b/shipped.txt > /dev/null
NS=$(python3 tools/gen_prophet_factory.py --list | wc -l | tr -d ' ')
for n in 0 1 2 4 7; do
    $CC -DP5_STAGE36=$n -o build/host/cr_prophet_s$n tests/cr_prophet_test.c -lm &
done
$CC -DFELUCCA_P5_FACTORY=0 -o build/host/cr_prophet_init tests/cr_prophet_test.c -lm &
# (the list's directory first: its header is the one the engine's #include finds)
sh tests/hostcc.sh cc -std=gnu11 -O2 -w -Ibuild/host/p5_all -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_prophet_all tests/cr_prophet_test.c -lm &
sh tests/hostcc.sh cc -std=gnu11 -O2 -w -Ibuild/host/p5_b -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_prophet_b tests/cr_prophet_test.c -lm &
wait
for n in 0 1 2 4 7; do
    [ -x build/host/cr_prophet_s$n ] || { echo "run_prophet_test.sh: build/host/cr_prophet_s$n did not build"; exit 1; }
    ./build/host/cr_prophet_s$n --pcm build/host/cr_prophet_s$n.pcm > /dev/null &
done
wait
lines=$(wc -l < build/host/cr_prophet_s0.pcm | tr -d ' ')
[ "$lines" -eq $((NS + 1 + 683)) ] || { echo "run_prophet_test.sh: $lines renders in build/host/cr_prophet_s0.pcm, want INIT + $NS + 683"; exit 1; }
for n in 1 2 4 7; do
    cmp -s build/host/cr_prophet_s0.pcm build/host/cr_prophet_s$n.pcm || {
        echo "run_prophet_test.sh: P5_STAGE36=$n renders other samples than 8a3074b's code:"
        diff build/host/cr_prophet_s0.pcm build/host/cr_prophet_s$n.pcm | head -5
        exit 1
    }
done
echo "prophet: P5_STAGE36 1, 2, 4 and 7 render 8a3074b's samples ($lines renders: INIT, the $NS shipped programs, the stress programs, bit for bit)"
[ -x build/host/cr_prophet_init ] || { echo "run_prophet_test.sh: FELUCCA_P5_FACTORY=0 did not build"; exit 1; }
./build/host/cr_prophet_init > build/host/cr_prophet_init.log || { grep -a "FAIL\|failed" build/host/cr_prophet_init.log; exit 1; }
echo "prophet: FELUCCA_P5_FACTORY=0 builds and plays ($(grep -ac '^ok ' build/host/cr_prophet_init.log) checks ok, INIT alone)"

# 3. the shipped list
for v in all b; do
    [ -x build/host/cr_prophet_$v ] || { echo "run_prophet_test.sh: the build with list '$v' did not build"; exit 1; }
    ./build/host/cr_prophet_$v > build/host/cr_prophet_$v.log || { grep -a "FAIL\|failed" build/host/cr_prophet_$v.log; exit 1; }
    ./build/host/cr_prophet_$v --pcm build/host/cr_prophet_$v.pcm > /dev/null
    ./build/host/cr_prophet_$v --id-out build/host/cr_prophet_$v.ids > /dev/null
done
./build/host/cr_prophet_s7 --id-out build/host/cr_prophet_s.ids > /dev/null
echo "prophet: the test passes built with all 200 ($(grep -ac '^ok ' build/host/cr_prophet_all.log) checks ok) and with a list of 7 ($(grep -ac '^ok ' build/host/cr_prophet_b.log) checks ok)"
# a program's samples do not depend on the list: every "program NNN" line of a list's renders is in the 200's
for v in s7 b; do
    grep '^program ' build/host/cr_prophet_$v.pcm > build/host/cr_prophet_$v.prog
    n=$(wc -l < build/host/cr_prophet_$v.prog | tr -d ' ')
    m=$(grep -c -x -F -f build/host/cr_prophet_$v.prog build/host/cr_prophet_all.pcm || true)
    [ "$n" -ge 2 ] && [ "$m" = "$n" ] || { echo "run_prophet_test.sh: list '$v': $m of $n programs render the samples they render among all 200"; exit 1; }
done
echo "prophet: a program renders the same samples wherever it stands (the tree's list and the list of 7 against all 200)"
# the session's record across lists: writer -> reader
id() {  # id READER WRITER-IDS WHAT
    ./build/host/cr_prophet_$1 --id-in build/host/cr_prophet_$2.ids > build/host/cr_prophet_id.log || {
        echo "run_prophet_test.sh: $3: a record does not bring its program back:"; cat build/host/cr_prophet_id.log; exit 1; }
    echo "prophet: $3: $(sed -n 's/^id-in: //p' build/host/cr_prophet_id.log)"
}
id b all "a session saved with all 200, read with the list of 7"
id all b "a session saved with the list of 7, read with all 200"
id s7 all "a session saved with all 200, read with the tree's list"
id b s "a session saved with the tree's list, read with the list of 7"
id s7 s "a session saved and read with the tree's list"
