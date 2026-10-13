#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless: settings persist across emulator runs through the file-backed flash (docs/SETTINGS.md).
#   run 1 (a fresh flash file): SELECT +17 -> 137 BPM, SAVE held: the global save (0.18: saving is manual), quit
#   run 2 (the same file): the dump says bpm 137; the BPM meter (SELECT +1 -1) is screenshotted:
#         build/emu/test/persist_bpm.ppm
#   run 1 also sets Play Style Advanced (Options); run 2: the engine plays Advanced (loaded by cr_ui_init)
#   run 3 (no --flash): 120 BPM (headless runs use a fresh flash unless --flash is given)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
EMU=build/host/emu
OUT=build/emu/test
S=tools/emu/scripts
FLASH=$OUT/persist_flash.bin
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }
if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
mkdir -p "$OUT"
rm -f "$FLASH" "$OUT"/persist_1.log "$OUT"/persist_2.log "$OUT"/persist_3.log "$OUT"/persist_bpm.ppm "$OUT"/persist_bpm.png
bpm() { sed -n 's/.* bpm \([0-9][0-9]*\) .*/\1/p' "$1" | tail -1; }

"$EMU" --headless --flash "$FLASH" --script "$S/persist_set.txt" >"$OUT/persist_1.log" 2>&1 || bad "run 1: exit status"
b=$(bpm "$OUT/persist_1.log")
[ "$b" = 137 ] && ok "run 1: tempo set to 137" || bad "run 1: bpm $b"
grep -q "settings saves 1 " "$OUT/persist_1.log" && grep -q '^save: global rc 0' "$OUT/persist_1.log" &&
    ok "run 1: SAVE held: the settings were saved once (the tempo, the looper's mode and keys, and the Option with them)" || bad "run 1: no save"
size=$(wc -c <"$FLASH" 2>/dev/null | tr -d ' ')
[ "$size" = 1048576 ] && ok "flash file: 1 MiB" || bad "flash file: ${size:-missing}"

"$EMU" --headless --flash "$FLASH" --script "$S/persist_check.txt" >"$OUT/persist_2.log" 2>&1 || bad "run 2: exit status"
b=$(bpm "$OUT/persist_2.log")
[ "$b" = 137 ] && ok "run 2: tempo 137 after a relaunch" || bad "run 2: bpm $b"
st() { sed -n 's/.* style \([0-9]*\) .*/\1/p' "$1" | tail -1; }
[ "$(st "$OUT/persist_1.log")" = 1 ] && ok "run 1: Play Style set to Advanced" || bad "run 1: style $(st "$OUT/persist_1.log")"
[ "$(st "$OUT/persist_2.log")" = 1 ] && ok "run 2: Play Style Advanced after a relaunch (cr_ui_init -> cr_settings_load: the engine has it)" \
    || bad "run 2: style $(st "$OUT/persist_2.log")"
lm() { sed -n 's/.* mode \([0-9]*\) keys \([0-9]*\) .*/\1 \2/p' "$1" | tail -1; }
[ "$(lm "$OUT/persist_1.log")" = "3 1" ] && [ "$(lm "$OUT/persist_2.log")" = "3 1" ] &&
    ok "the PLAY menu's record mode (Replace) and keys (Loops) after a relaunch (settings v8 loop_rec)" \
    || bad "record mode / keys: run 1 '$(lm "$OUT/persist_1.log")', run 2 '$(lm "$OUT/persist_2.log")'"
grep -q "record: current" "$OUT/persist_2.log" && ok "run 2: the record was read (current)" || bad "run 2: record not read"
[ -s "$OUT/persist_bpm.ppm" ] && ok "run 2: the BPM meter: $OUT/persist_bpm.ppm" || bad "run 2: no screenshot"

"$EMU" --headless --script "$S/persist_fresh.txt" >"$OUT/persist_3.log" 2>&1 || bad "run 3: exit status"
b=$(bpm "$OUT/persist_3.log")
[ "$b" = 120 ] && ok "run 3: no --flash: a fresh 120 BPM" || bad "run 3: bpm $b"
[ "$(st "$OUT/persist_3.log")" = 0 ] && ok "run 3: Play Style Simple" || bad "run 3: style $(st "$OUT/persist_3.log")"

# ---- saving is manual (0.18, the owner 2026-10-11; docs/SETTINGS.md "Manual and automatic"). gsave_none.txt on a fresh
# flash: the tempo, the KEY layer's tonic and an Option changed, 5 s, quit with NO SAVE held: the Option is in flash (it
# is automatic), the tempo and the key are not. gsave_save.txt on that flash: the tempo and the key again, SAVE held:
# "saved"; more changes, GLO + SAVE held: "reverted": the unit as saved; the relaunch: as saved
GF=$OUT/persist_gsave_flash.bin
G1=$OUT/persist_gsave_1.log; G2=$OUT/persist_gsave_2.log; G3=$OUT/persist_gsave_3.log; G4=$OUT/persist_gsave_4.log
rm -f "$GF" "$G1" "$G2" "$G3" "$G4" "$OUT"/gsave_saved.ppm "$OUT"/gsave_reverted.ppm
"$EMU" --headless --flash "$GF" --script "$S/gsave_none.txt" >"$G1" 2>&1 || bad "manual run 1: exit status"
"$EMU" --headless --flash "$GF" --script "$S/persist_check.txt" >"$G2" 2>&1 || bad "manual run 2: exit status"
"$EMU" --headless --flash "$GF" --script "$S/gsave_save.txt" >"$G3" 2>&1 || bad "manual run 3: exit status"
"$EMU" --headless --flash "$GF" --script "$S/persist_check.txt" >"$G4" 2>&1 || bad "manual run 4: exit status"
gk() { grep -a '^  cr: key' "$1" | sed -n "$2p" | sed -n 's/.*cr: key \([0-9]\) tonic \([0-9]*\) .* bass \([0-9]\) .* voicing \(-*[0-9]*\) .* bpm \([0-9]*\) view \([0-9]\) layer \([0-9]*\) options \([0-9]\).*/key \1 tonic \2 bass \3 voicing \4 bpm \5 layer \7 options \8/p'; }
gs() { grep -a '^  settings:' "$1" | sed -n "$2p" | sed -n 's/.* style \([0-9]\) .* leds \([0-9]\) .*/style \1 leds \2/p'; }
gw() { grep -a '^  flash:' "$1" | sed -n "$2p" | sed -n 's/.*, \([0-9]*\) writes, settings saves \([0-9]*\) .*/\1 \2/p'; }
gss() { grep -a '^  session: saves' "$1" | sed -n "$2p" | sed -n 's/.*saves \([0-9]*\) .* pending \([0-9]\) .*/\1 \2/p'; }
[ "$(gk "$G1" 1)" = "key 1 tonic 2 bass 0 voicing 0 bpm 137 layer 0 options 0" ] && [ "$(gs "$G1" 1)" = "style 1 leds 0" ] &&
    [ "$(gw "$G1" 1 | cut -d' ' -f2)" = 1 ] && [ "$(gss "$G1" 1 | cut -d' ' -f1)" = 0 ] && ! grep -aq '^save: global' "$G1" &&
    ok "no SAVE held: 137 BPM, the tonic D (the KEY layer) and Play Style Advanced set; 5 s later the settings object written once (the Option), the session never" \
    || bad "manual run 1: $(gk "$G1" 1) / $(gs "$G1" 1) / $(gw "$G1" 1) / $(gss "$G1" 1)"
[ "$(gk "$G2" 1)" = "key 0 tonic 0 bass 0 voicing 0 bpm 120 layer 0 options 0" ] && [ "$(gs "$G2" 1)" = "style 1 leds 0" ] &&
    grep -aq 'record: current' "$G2" &&
    ok "a power cycle without a global save: an Options entry (Play Style Advanced) was saved by itself; the tempo (120) and the KEY layer's tonic (Key Mode off) are as last saved: not kept" \
    || bad "manual run 2: $(gk "$G2" 1) / $(gs "$G2" 1)"
[ "$(gk "$G3" 1)" = "key 1 tonic 2 bass 0 voicing 0 bpm 137 layer 0 options 0" ] && grep -aq '^save: global rc 0' "$G3" &&
    [ "$(gss "$G3" 1)" = "1 0" ] && [ "$(gw "$G3" 1 | cut -d' ' -f2)" = 1 ] &&
    ok "SAVE held past the hold time: the global save: the settings and the session written (no layer opens, no dialog: SAVE still down, layer 0): $OUT/gsave_saved.ppm" \
    || bad "manual run 3, the save: $(gk "$G3" 1) / $(gss "$G3" 1) / $(gw "$G3" 1)"
grn() { od -An -tu1 -v -j 15 "$1" | awk '{ for (k = 1; k <= NF; k++) v[n++] = $k }
     END { c = 0; for (o = 0; o + 2 < n; o += 3) if (v[o] < 100 && v[o+1] > 140 && v[o+2] > 90 && v[o+2] < 160) c++; print c }'; }
wht() { od -An -tu1 -v -j 15 "$1" | awk '{ for (k = 1; k <= NF; k++) v[n++] = $k }
     END { c = 0; for (o = 0; o + 2 < n; o += 3) if (v[o] > 230 && v[o+1] > 230 && v[o+2] > 215) c++; print c }'; }
g=$(grn "$OUT/gsave_saved.ppm"); w=$(wht "$OUT/gsave_reverted.ppm")
[ "$g" -gt 1000 ] && [ "$w" -gt 2000 ] && ok "the messages: \"saved\" in a green box ($g px), \"reverted\" in a white one ($w px): $OUT/gsave_reverted.ppm" \
    || bad "the messages: $g green px, $w white px"
[ "$(gk "$G3" 2)" = "key 1 tonic 2 bass 1 voicing 2 bpm 150 layer 0 options 0" ] && [ "$(gs "$G3" 2)" = "style 1 leds 1" ] &&
    grep -a '^  part 0:' "$G3" | sed -n 2p | grep -q 'voices [1-9]' &&
    ok "then changed: 150 BPM, voicing +2, Bass on, another chord sound, LEDs Stock (an Option), a chord latched and sounding" \
    || bad "manual run 3, the changes: $(gk "$G3" 2) / $(gs "$G3" 2)"
p2=$(grep -a '^  session chord:' "$G3" | sed -n 2p); p3=$(grep -a '^  session chord:' "$G3" | sed -n 3p); p1=$(grep -a '^  session chord:' "$G3" | sed -n 1p)
grep -aq '^save: reverted' "$G3" && [ "$(gk "$G3" 3)" = "key 1 tonic 2 bass 0 voicing 0 bpm 137 layer 0 options 0" ] &&
    [ "$(gs "$G3" 3)" = "style 1 leds 1" ] && [ "$p3" = "$p1" ] && [ "$p2" != "$p1" ] && [ -n "$p1" ] &&
    grep -a 'parts busy' "$G3" | sed -n 3p | grep -q 'parts busy 0 ' && [ "$(gss "$G3" 3)" = "1 0" ] &&
    grep -a '^  cr: voices' "$G3" | sed -n 3p | grep -q 'cr: voices 0 pending 0 ' &&
    ok "GLO + SAVE held 1 s: reverted: 137 BPM, voicing 0, the bass off, the saved chord sound, LATCH off, silence: the state is flash's again (nothing to save); the Option (LEDs Stock) is not the revert's" \
    || bad "the revert: $(gk "$G3" 3) / $(gs "$G3" 3) / $p3 / $(gss "$G3" 3)"
[ "$(grep -ac '^expect sound at .*: ok' "$G3")" = 2 ] && [ "$(gk "$G3" 4)" = "key 1 tonic 2 bass 0 voicing 0 bpm 142 layer 0 options 0" ] &&
    [ "$(grep -ac '^save: reverted' "$G3")" = 1 ] &&
    ok "after it the unit plays; GLO + SAVE let go after 0.6 s: nothing reverted (142 BPM), no Options, no dialog" \
    || bad "after the revert: $(gk "$G3" 4)"
gn() { grep -a '^  settings:' "$1" | sed -n "$2p" | sed -n 's/.*settings: now \([0-9A-F]*\) saved \([0-9A-F]*\) .* pending \([0-9]\) .*/\1 \2 \3/p'; }
set -- $(gn "$G3" 5)
[ "$(gw "$G3" 5 | cut -d' ' -f2)" = 2 ] && [ "$(gss "$G3" 5)" = "1 0" ] && [ "${1:-x}" != "${2:-x}" ] && [ "${3:-x}" = 0 ] &&
    ok "the Option changed meanwhile was written by itself (settings saves 2); the tempo turned after the revert is not (the block as it would be saved differs from flash's, nothing pending)" \
    || bad "manual run 3, the end: $(gw "$G3" 5) / $(gss "$G3" 5) / $(gn "$G3" 5)"
[ "$(gk "$G4" 1)" = "key 1 tonic 2 bass 0 voicing 0 bpm 137 layer 0 options 0" ] && [ "$(gs "$G4" 1)" = "style 1 leds 1" ] &&
    ok "the relaunch: as last SAVED (137 BPM, the tonic D), with the Options as they were left (Advanced, LEDs Stock)" \
    || bad "manual run 4: $(gk "$G4" 1) / $(gs "$G4" 1)"

# a fresh unit's Play Style (the owner's unit came up Advanced): run 1's flash (Advanced saved in a ChoralRoot record)
# made into what another firmware leaves: (a) a Felucca / Melodee PER4 settings object, the same fields without
# ChoralRoot's block (settings_persist.c: magic PER4, 192 bytes shorter), (b) the settings sectors erased (0xFF: no
# record; what SAFE MODE's Flash Data leaves, test_cr.sh). Both power on with the defaults: Simple, 120 BPM
P4=$OUT/persist_per4_flash.bin
ER=$OUT/persist_erased_flash.bin
rm -f "$P4" "$ER" "$OUT"/persist_per4.log "$OUT"/persist_erased.log
n4=$(python3 -c "
import struct, sys, zlib
b = bytearray(open(sys.argv[1], 'rb').read())
e = bytearray(b)
n = 0
for o in (0xFC000, 0xFD000):
    magic, typ, slot, seq, ln = struct.unpack_from('<IHHII', b, o)
    pay = o + 256
    if magic != 0x554C4546 or b[pay:pay + 4] != b'5REP' or b[pay + ln - 192:pay + ln - 188] != b'CRS1':
        continue
    b[pay:pay + 4] = b'4REP'
    b[pay + ln - 192:pay + ln] = b'\\xff' * 192
    ln -= 192
    struct.pack_into('<I', b, o + 12, ln)
    struct.pack_into('<I', b, o + 16, zlib.crc32(bytes(b[pay:pay + ln])) & 0xFFFFFFFF)
    struct.pack_into('<I', b, o + 28, zlib.crc32(bytes(b[o:o + 28])) & 0xFFFFFFFF)
    n += 1
e[0xFC000:0xFF000] = b'\\xff' * 0x3000
open(sys.argv[2], 'wb').write(b)
open(sys.argv[3], 'wb').write(e)
print(n)" "$FLASH" "$P4" "$ER")
"$EMU" --headless --flash "$P4" --script "$S/persist_check.txt" >"$OUT/persist_per4.log" 2>&1 || bad "PER4 run: exit status"
"$EMU" --headless --flash "$ER" --script "$S/persist_check.txt" >"$OUT/persist_erased.log" 2>&1 || bad "erased run: exit status"
[ "${n4:-0}" -ge 1 ] && [ "$(st "$OUT/persist_per4.log")" = 0 ] && [ "$(bpm "$OUT/persist_per4.log")" = 120 ] &&
    grep -q "record: defaults" "$OUT/persist_per4.log" &&
    ok "a Felucca PER4 settings record (no ChoralRoot block, $n4 copies): the defaults: Play Style Simple, 120 BPM" \
    || bad "PER4 record: $n4 copies, style $(st "$OUT/persist_per4.log") bpm $(bpm "$OUT/persist_per4.log")"
[ "$(st "$OUT/persist_erased.log")" = 0 ] && [ "$(bpm "$OUT/persist_erased.log")" = 120 ] && grep -q "record: defaults" "$OUT/persist_erased.log" &&
    ok "the settings sectors erased (no record): the defaults: Play Style Simple, 120 BPM" \
    || bad "erased settings: style $(st "$OUT/persist_erased.log") bpm $(bpm "$OUT/persist_erased.log")"

# Options > Reset Settings (the owner, 2026-10-12): the last Options entry, OCT+ twice. Run 1 (reset_set.txt, a fresh
# flash): a user sound saved (U01 ADG), settings changed and saved (SAVE held: Advanced, 137 BPM, Key Mode on, Motion Off, Hold
# Time, Dual Core On). Run 2 (reset_do.txt, a relaunch): the stored settings are those; the entry asks, cancels (another
# entry, OCT-, a closed menu), resets on the second OCT+: at once the record a new unit has (the dump's "settings: now"
# checksum = a fresh unit's, run 3 of above), saved. Run 3 (reset_check.txt, a relaunch): still that, U01 still there.
# The flash outside the settings object is byte for byte what it was (sounds, patches, loops, banks), and the panel
# calibration table inside it too
RF=$OUT/persist_reset_flash.bin
rm -f "$RF" "$OUT/persist_reset_before.bin" "$OUT"/persist_reset_1.log "$OUT"/persist_reset_2.log "$OUT"/persist_reset_3.log "$OUT"/persist_reset_fresh.log
"$EMU" --headless --flash "$RF" --script "$S/reset_set.txt" >"$OUT/persist_reset_1.log" 2>&1 || bad "reset run 1: exit status"
cp "$RF" "$OUT/persist_reset_before.bin"
"$EMU" --headless --flash "$RF" --script "$S/reset_do.txt" >"$OUT/persist_reset_2.log" 2>&1 || bad "reset run 2: exit status"
"$EMU" --headless --flash "$RF" --script "$S/reset_check.txt" >"$OUT/persist_reset_3.log" 2>&1 || bad "reset run 3: exit status"
"$EMU" --headless --script "$S/reset_check.txt" >"$OUT/persist_reset_fresh.log" 2>&1 || bad "reset, the fresh unit: exit status"
sn() { grep -a '^  settings:' "$1" | sed -n "$2p" | sed -n 's/.*settings: now \([0-9A-F]*\) saved \([0-9A-F]*\) .*/\1 \2/p'; }
sf() { grep -a '^  settings:' "$1" | sed -n "$2p" | sed -n 's/.* \(style .*\) chord-sound.*/\1/p'; }
ov() { grep -a '^  options:' "$1" | sed -n "$2p" | sed -n "s/.*options: open \([01]\) entry \([0-9]*\) of \([0-9]*\) '\([^']*\)' value '\(.*\)'$/\1 \2 \4: \5/p"; }
R1=$OUT/persist_reset_1.log; R2=$OUT/persist_reset_2.log; R3=$OUT/persist_reset_3.log
fresh=$(sn "$OUT/persist_reset_fresh.log" 1 | cut -d' ' -f1)
changed=$(sn "$R1" 1)
grep -aq '^save: part 0 slot U01 name ADG rc 0' "$R1" && [ "$(sf "$R1" 1)" = "style 1 extadd 0 key 1 motion 2 leds 0 hold 2 dualcore 1 usb 1/0 view 64" ] &&
    [ -n "$changed" ] && [ "$changed" = "$(sn "$R2" 1)" ] && [ "$(bpm "$R2")" != "" ] && grep -aq ' bpm 137 ' "$R2" &&
    ok "reset run 1 / 2: U01 saved; Advanced, 137 BPM, Key Mode on, Motion Off, Hold Time, Dual Core On stored and back after a relaunch" \
    || bad "reset: the changed settings: $(sf "$R1" 1) / $changed / $(sn "$R2" 1)"
[ "$(ov "$R2" 2)" = "1 24 Reset Settings: all options to default" ] && grep -aq "options: open 1 entry 24 of 25 " "$R2" &&
    ok "Reset Settings is the last Options entry (24 of 24): $OUT/reset_entry.ppm" || bad "the entry: $(ov "$R2" 2)"
[ "$(ov "$R2" 3)" = "1 24 Reset Settings: OCT+ again: reset" ] && [ "$(ov "$R2" 4)" = "1 24 Reset Settings: all options to default" ] &&
    [ "$(ov "$R2" 5)" = "0 24 Reset Settings: all options to default" ] && [ "$(ov "$R2" 6)" = "1 24 Reset Settings: all options to default" ] &&
    [ "$(sn "$R2" 3)" = "$changed" ] && [ "$(sn "$R2" 4)" = "$changed" ] && [ "$(sn "$R2" 5)" = "$changed" ] && [ "$(sn "$R2" 6)" = "$changed" ] &&
    ! grep -a '^options: settings reset' "$R2" | sed -n 2p | grep -q . &&
    ok "OCT+ asks (OCT+ again); another entry cancels; OCT- leaves and cancels; the menu reopened is not armed; nothing reset: $OUT/reset_ask.ppm" \
    || bad "the confirm: $(ov "$R2" 3) / $(ov "$R2" 4) / $(ov "$R2" 5) / $(ov "$R2" 6); $(sn "$R2" 6)"
grep -aq '^options: settings reset (Dual Core runs until the restart)' "$R2" && [ -n "$fresh" ] && [ "$(sn "$R2" 7 | cut -d' ' -f1)" = "$fresh" ] &&
    [ "$(sf "$R2" 7)" = "style 0 extadd 0 key 0 motion 0 leds 0 hold 1 dualcore 0 usb 1/0 view 0" ] && [ "$(ov "$R2" 7)" = "0 0 Play Style: Simple" ] &&
    [ "$(grep -a '^  lock: on' "$R2" | sed -n 7p | cut -c1-25)" = "  lock: on 0 latched 00 t" ] && [ "$(grep -a '^  cr: key' "$R2" | sed -n 7p | sed -n 's/.* bpm \([0-9]*\) view \([0-9]\) layer \([0-9]*\) options \([01]\).*/\1 \2 \3 \4/p')" = "120 0 0 0" ] &&
    ok "OCT+ twice: every setting at once what a new unit has (the record's checksum $fresh = a fresh unit's): Simple, Key off, 120 BPM, Motion Full, Hold 1, Dual Core Off; on the view, LOCK off: $OUT/reset_done.ppm" \
    || bad "the reset: now $(sn "$R2" 7) want $fresh; $(sf "$R2" 7); $(ov "$R2" 7)"
[ "$(sn "$R2" 8)" = "$fresh $fresh" ] && grep -aq '^expect sound .*: ok' "$R2" && grep -a '^  part 0:' "$R2" | sed -n 9p | grep -q 'FM6 / TINE EP, voices 3' &&
    ok "saved (the stored record = the defaults); the unit plays: MAJ + D4 on TINE EP" || bad "after the reset: $(sn "$R2" 8), $(grep -a '^  part 0:' "$R2" | sed -n 9p)"
b=$(grep -a 'parts busy' "$R2" | tail -1 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p'); [ "${b:-x}" = 0 ] || bad "reset run 2: $b voices still sounding at the end"
[ "$(sn "$R3" 1)" = "$fresh $fresh" ] && grep -aq 'record: current' "$R3" && [ "$(st "$R3")" = 0 ] && [ "$(bpm "$R3")" = 120 ] &&
    grep -aq '^sound: part 0 pos 26 ADG' "$R3" &&
    ok "a relaunch on the same flash: still the defaults (Play Style Simple, 120 BPM); the user sound saved before is still there (FM6 26 ADG)" \
    || bad "reset run 3: $(sn "$R3" 1) want $fresh; $(grep -a '^sound:' "$R3" | tail -1)"
kept=$(python3 -c "
import struct, sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
def newest(d):
    best = None
    for o in (0xFC000, 0xFD000):
        magic, typ, slot, seq, ln = struct.unpack_from('<IHHII', d, o)
        if magic == 0x554C4546 and d[o + 256:o + 260] == b'5REP' and (best is None or seq > best[0]):
            best = (seq, d[o + 256:o + 256 + ln])
    return best[1]
pa, pb = newest(a), newest(b)
cr, fav = len(pa) - 192, len(pa) - 192 - 520
outside = a[:0xB4000] == b[:0xB4000] and a[0xB6000:0xFC000] == b[0xB6000:0xFC000] and a[0xFF000:] == b[0xFF000:]   # (0xB4000 / 0xB5000: the session, reset with the record, checked below)
panel = pa[16:fav - 4] == pb[16:fav - 4] and len(pa) == len(pb)
favs = pa[fav:cr] == pb[fav:cr]
record = pa[cr:] != pb[cr:] and pb[cr:cr + 4] == b'CRS1'
print(int(outside), int(panel), int(favs), int(record), fav - 4 - 16)" "$OUT/persist_reset_before.bin" "$RF")
[ "$(echo "$kept" | cut -d' ' -f1-4)" = "1 1 1 1" ] &&
    ok "the flash: everything outside the settings object and the session byte for byte as before the reset (saved sounds, patches, banks, loops); in it the panel calibration table ($(echo "$kept" | cut -d' ' -f5) B) and the favourites unchanged, the ChoralRoot record rewritten" \
    || bad "the reset touched more than the record (outside, panel, favourites, record): $kept"

# the VA patch store (firmware/src/va_store.c, docs/VA.md): run 1 edits a VA sound's patch on its deep page and saves
# it to U01; run 2 (the same file) powers on with U01 (the record's chord sound): the patch's CRC as saved
VFLASH=$OUT/persist_va_flash.bin
rm -f "$VFLASH" "$OUT"/persist_va_1.log "$OUT"/persist_va_2.log
"$EMU" --headless --flash "$VFLASH" --script "$S/va_persist_set.txt" >"$OUT/persist_va_1.log" 2>&1 || bad "VA run 1: exit status"
"$EMU" --headless --flash "$VFLASH" --script "$S/va_persist_check.txt" >"$OUT/persist_va_2.log" 2>&1 || bad "VA run 2: exit status"
vs=$(sed -n 's/^va: save slot 1 .*patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_va_1.log" | tail -1)
vl=$(sed -n 's/^va: load slot 1 patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_va_2.log" | tail -1)
grep -q "^deep: part 0 .* edited" "$OUT/persist_va_1.log" && ok "VA run 1: the patch edited on a deep page" || bad "VA run 1: no deep edit"
[ -n "$vs" ] && [ "$vs" = "$vl" ] && ok "VA run 2: U01's patch after a relaunch (crc $vl)" || bad "VA: patch crc saved '${vs}' loaded '${vl}'"

# FM6 (docs/FM6.md): run 1 edits TINE EP (OP 2's LEVEL, the BEND range) and saves it in U01: the blob into the FM6
# patch store (fm6_ustore.c); run 2 (the same file) powers on with U01: the same blob back, OP 2's LEVEL 0
FFLASH=$OUT/persist_fm6_flash.bin
rm -f "$FFLASH" "$OUT"/persist_fm6_1.log "$OUT"/persist_fm6_2.log
"$EMU" --headless --flash "$FFLASH" --script "$S/fm6_persist_set.txt" >"$OUT/persist_fm6_1.log" 2>&1 || bad "FM6 run 1: exit status"
"$EMU" --headless --flash "$FFLASH" --script "$S/fm6_persist_check.txt" >"$OUT/persist_fm6_2.log" 2>&1 || bad "FM6 run 2: exit status"
fs=$(sed -n 's/^fm6: save slot 1 .*patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_fm6_1.log" | tail -1)
fl=$(sed -n 's/^fm6: load slot 1 patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_fm6_2.log" | tail -1)
grep -q "^deep: part 0 page 1 OP 2 col 0 LEVEL [0-9]* -> 0 " "$OUT/persist_fm6_1.log" &&
    grep -q "^deep: part 0 page 35 FUNC col 0 BEND 3 -> 7 " "$OUT/persist_fm6_1.log" &&
    ok "FM6 run 1: OP 2's LEVEL and the BEND range edited on the deep pages" || bad "FM6 run 1: the deep edits"
[ -n "$fs" ] && [ "$fs" = "$fl" ] && ok "FM6 run 2: U01's patch and functions after a relaunch (crc $fl)" || bad "FM6: patch crc saved '${fs}' loaded '${fl}'"
grep -q "^deep: part 0 page 1 OP 2 col 0 LEVEL 0 -> " "$OUT/persist_fm6_2.log" && grep -q '^expect sound .*: ok' "$OUT/persist_fm6_2.log" &&
    ok "FM6 run 2: OP 2's LEVEL is 0 as saved, the sound plays" || bad "FM6 run 2: $(grep -m1 '^deep:' "$OUT/persist_fm6_2.log")"
# the preset model (docs/PRESETS.md): run 1's SAVE on the edited TINE EP was an Overwrite, a record bound to FM6 01 in
# U01; run 2: the record's binding survived the relaunch (FM6 01 is U01, with the mark), the chord sound came back
grep -q '^save: part 0 overwrite slot U01 FM6 01 TINE EP rc 0' "$OUT/persist_fm6_1.log" &&
    grep -q '^popup: part 0 01 / TINE EP (mark) / FM6 · 01/25$' "$OUT/persist_fm6_2.log" &&
    ok "a bound preset after a relaunch: U01 overwrites FM6 01 TINE EP (the mark), the power-on sound" \
    || bad "bound preset: $(grep '^save: part 0' "$OUT/persist_fm6_1.log") / $(grep '^popup:' "$OUT/persist_fm6_2.log" | tail -1)"

# CZ-1 (docs/CZ1.md): run 1 edits CZ BRASS 1 (DCW 1's L1) and saves it in U01: the tone into the CZ-1 tone store
# (cz_ustore.c); run 2 (the same file) powers on with U01: the same tone back, DCW 1's L1 0
ZFLASH=$OUT/persist_cz_flash.bin
rm -f "$ZFLASH" "$OUT"/persist_cz_1.log "$OUT"/persist_cz_2.log
"$EMU" --headless --flash "$ZFLASH" --script "$S/cz_persist_set.txt" >"$OUT/persist_cz_1.log" 2>&1 || bad "CZ-1 run 1: exit status"
"$EMU" --headless --flash "$ZFLASH" --script "$S/cz_persist_check.txt" >"$OUT/persist_cz_2.log" 2>&1 || bad "CZ-1 run 2: exit status"
zs=$(sed -n 's/^cz: save slot 1 .*tone crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_cz_1.log" | tail -1)
zl=$(sed -n 's/^cz: load slot 1 tone crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_cz_2.log" | tail -1)
grep -q "^deep: part 0 page 11 DCW 1+ col 0 L1 [0-9]* -> 0 " "$OUT/persist_cz_1.log" &&
    ok "CZ-1 run 1: DCW 1's L1 edited on the deep pages" || bad "CZ-1 run 1: the deep edit"
[ -n "$zs" ] && [ "$zs" = "$zl" ] && ok "CZ-1 run 2: U01's tone after a relaunch (crc $zl)" || bad "CZ-1: tone crc saved '${zs}' loaded '${zl}'"
grep -q "^deep: part 0 page 11 DCW 1+ col 0 L1 0 -> " "$OUT/persist_cz_2.log" && grep -q '^expect sound .*: ok' "$OUT/persist_cz_2.log" &&
    ok "CZ-1 run 2: DCW 1's L1 is 0 as saved, the sound plays" || bad "CZ-1 run 2: $(grep -m1 '^deep:' "$OUT/persist_cz_2.log")"

# FM TONE (engine 15, code name QUAD; docs/QUAD.md): run 1 edits FM TONE's EP (Ratio A, KNOB 3 +5 on SYN 1) and saves it in U01: the patch into the QUAD
# patch store (quad_store.c, 0xB2000 / 0xB3000); run 2 (the same file) powers on with U01: the same patch back
QFLASH=$OUT/persist_quad_flash.bin
rm -f "$QFLASH" "$OUT"/persist_quad_1.log "$OUT"/persist_quad_2.log
"$EMU" --headless --flash "$QFLASH" --script "$S/quad_persist_set.txt" >"$OUT/persist_quad_1.log" 2>&1 || bad "FM TONE run 1: exit status"
"$EMU" --headless --flash "$QFLASH" --script "$S/quad_persist_check.txt" >"$OUT/persist_quad_2.log" 2>&1 || bad "FM TONE run 2: exit status"
qs=$(sed -n 's/^quad: save slot 1 .*patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_quad_1.log" | tail -1)
ql=$(sed -n 's/^quad: load slot 1 patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_quad_2.log" | tail -1)
grep -q "^deep: part 0 page 0 SYN 1 col 2 RATIO A 3 -> 8 .* edited" "$OUT/persist_quad_1.log" &&
    ok "FM TONE run 1: Ratio A edited on SYN 1 (3 -> 8, one step a detent)" || bad "FM TONE run 1: $(grep -m1 '^deep:' "$OUT/persist_quad_1.log")"
[ -n "$qs" ] && [ "$qs" = "$ql" ] && ok "FM TONE run 2: U01's patch after a relaunch (crc $ql)" || bad "FM TONE: patch crc saved '${qs}' loaded '${ql}'"
grep -q "^deep: part 0 page 0 SYN 1 col 2 RATIO A 8 -> 9 " "$OUT/persist_quad_2.log" && grep -q '^expect sound .*: ok' "$OUT/persist_quad_2.log" &&
    ok "FM TONE run 2: Ratio A is 8 as saved, the sound plays" || bad "FM TONE run 2: $(grep -m1 '^deep:' "$OUT/persist_quad_2.log")"

# the KEY layer's keys (Settings v13: bit 5 of loop_rec, docs/SETTINGS.md): run 1 switches them to play (KEY held, GLO);
# run 2 (the same file) opens the layer with them playing
KFLASH=$OUT/persist_kkeys_flash.bin
rm -f "$KFLASH" "$OUT"/persist_kkeys_1.log "$OUT"/persist_kkeys_2.log "$OUT"/persist_kkeys.ppm
"$EMU" --headless --flash "$KFLASH" --script "$S/persist_kkeys_set.txt" >"$OUT/persist_kkeys_1.log" 2>&1 || bad "key-layer keys run 1: exit status"
"$EMU" --headless --flash "$KFLASH" --script "$S/persist_kkeys_check.txt" >"$OUT/persist_kkeys_2.log" 2>&1 || bad "key-layer keys run 2: exit status"
kk() { sed -n 's/.* kkeys \([0-9]\)$/\1/p' "$1" | sed -n "$2p"; }
grep -q '^key: keys play' "$OUT/persist_kkeys_1.log" && grep -q "settings saves 1 " "$OUT/persist_kkeys_1.log" && [ "$(kk "$OUT/persist_kkeys_1.log" 1)" = 1 ] &&
    ok "key-layer keys run 1: GLO in the KEY layer: keys play; SAVE held: the settings saved once" || bad "key-layer keys run 1: $(grep '^key: keys' "$OUT/persist_kkeys_1.log")"
[ "$(kk "$OUT/persist_kkeys_2.log" 1)" = 1 ] && grep -q '(record: current)' "$OUT/persist_kkeys_2.log" && grep -q '^expect sound .*: ok' "$OUT/persist_kkeys_2.log" &&
    grep -q '^expect led GLO on .*: ok' "$OUT/persist_kkeys_2.log" && ! grep -q 'FAILED' "$OUT/persist_kkeys_2.log" &&
    ok "key-layer keys run 2: still play after a relaunch (the layer open: GLO lit, D4 sounded, the tonic unchanged): $OUT/persist_kkeys.ppm" \
    || bad "key-layer keys run 2: kkeys $(kk "$OUT/persist_kkeys_2.log" 1)"

# LOCK's two styles (Settings v13: bits 6 / 7 of loop_rec): run 1 sets Keeps and Pushed in the LOCK layer (B3 held);
# run 2 (the same file): they are back
OFLASH=$OUT/persist_lock_flash.bin
rm -f "$OFLASH" "$OUT"/persist_lock_1.log "$OUT"/persist_lock_2.log
"$EMU" --headless --flash "$OFLASH" --script "$S/persist_lock_set.txt" >"$OUT/persist_lock_1.log" 2>&1 || bad "LOCK styles run 1: exit status"
"$EMU" --headless --flash "$OFLASH" --script "$S/persist_lock_check.txt" >"$OUT/persist_lock_2.log" 2>&1 || bad "LOCK styles run 2: exit status"
grep -q 'type-key keeps ext-keys pushed' "$OUT/persist_lock_1.log" && grep -q "settings saves 1 " "$OUT/persist_lock_1.log" &&
    ok "LOCK styles run 1: Type key Keeps, Ext keys Pushed, the settings saved once" || bad "LOCK styles run 1: $(grep '  lock: on' "$OUT/persist_lock_1.log")"
grep -q 'lock: on 0 latched 00 type-key keeps ext-keys pushed' "$OUT/persist_lock_2.log" && grep -q '(record: current)' "$OUT/persist_lock_2.log" &&
    ok "LOCK styles run 2: Keeps and Pushed after a relaunch (LOCK itself off at power-on, as ever)" || bad "LOCK styles run 2: $(grep '  lock: on' "$OUT/persist_lock_2.log")"

# the KEY layer's page 2 and Split Bass (Settings v14: bits 1 / 2 of key_on, single = 2): run 1 sets Key plays Notes,
# Keyboard Full and Single Split Bass; run 2 (the same file): they are back, in the UI and in the engine
PFLASH=$OUT/persist_page2_flash.bin
rm -f "$PFLASH" "$OUT"/persist_page2_1.log "$OUT"/persist_page2_2.log
"$EMU" --headless --flash "$PFLASH" --script "$S/persist_page2_set.txt" >"$OUT/persist_page2_1.log" 2>&1 || bad "KEY page 2 run 1: exit status"
"$EMU" --headless --flash "$PFLASH" --script "$S/persist_page2_check.txt" >"$OUT/persist_page2_2.log" 2>&1 || bad "KEY page 2 run 2: exit status"
grep -q 'kbd: plays notes keyboard full single 2 ' "$OUT/persist_page2_1.log" && grep -q "settings saves 1 " "$OUT/persist_page2_1.log" &&
    ok "KEY page 2 run 1: Key plays Notes, Keyboard Full, Single Split Bass, the settings saved once" || bad "KEY page 2 run 1: $(grep '  kbd:' "$OUT/persist_page2_1.log")"
[ "$(grep -c 'kbd: plays notes keyboard full single 2 split-note 65 page 1 (engine: notes 1 full 1 single 2)' "$OUT/persist_page2_2.log")" = 3 ] &&
    grep -q '(record: current)' "$OUT/persist_page2_2.log" &&
    [ "$(grep '^  part 1:' "$OUT/persist_page2_2.log" | sed -n 2p | sed -n 's/.* voices \([0-9]*\),.*/\1/p')" = 1 ] &&
    [ "$(grep '^  part 0:' "$OUT/persist_page2_2.log" | sed -n 2p | sed -n 's/.* voices \([0-9]*\),.*/\1/p')" = 0 ] &&
    [ "$(grep '^  part 1:' "$OUT/persist_page2_2.log" | sed -n 3p | sed -n 's/.* voices \([0-9]*\),.*/\1/p')" = 0 ] &&
    ok "KEY page 2 run 2: the three back after a relaunch (UI and engine); B3 a bass note of the left side, F4 the chord part" \
    || bad "KEY page 2 run 2: $(grep '  kbd:' "$OUT/persist_page2_2.log" | head -1)"

# the engine picker's roots always play (0.18; until then Settings pick_roots, KNOB 4 in the picker: the byte is
# reserved now, written 0): run 1 turns KNOB 4 there (nothing) and plays D4; run 2 (the same file) the same
RFLASH=$OUT/persist_roots_flash.bin
rm -f "$RFLASH" "$OUT"/persist_roots_1.log "$OUT"/persist_roots_2.log "$OUT"/persist_roots.ppm
"$EMU" --headless --flash "$RFLASH" --script "$S/persist_roots_set.txt" >"$OUT/persist_roots_1.log" 2>&1 || bad "roots run 1: exit status"
"$EMU" --headless --flash "$RFLASH" --script "$S/persist_roots_check.txt" >"$OUT/persist_roots_2.log" 2>&1 || bad "roots run 2: exit status"
! grep -q '^picker: roots' "$OUT/persist_roots_1.log" && ! grep -q '^engine: ' "$OUT/persist_roots_1.log" &&
    grep -q '^expect sound .*: ok' "$OUT/persist_roots_1.log" &&
    ok "roots run 1: KNOB 4 in the picker does nothing, D4 plays (no engine switch)" || bad "roots run 1: the picker's roots"
grep -q '^picker: open part 0 ' "$OUT/persist_roots_2.log" && ! grep -q '^engine: ' "$OUT/persist_roots_2.log" &&
    grep -q '^expect sound .*: ok' "$OUT/persist_roots_2.log" &&
    ok "roots run 2: the picker's roots still play after a relaunch (D4 sounded, no engine switch): $OUT/persist_roots.ppm" \
    || bad "roots run 2: $(grep '^picker: open' "$OUT/persist_roots_2.log")"

# Options > Hold Time alone (Felucca's HOLD, the record's `bold`: outside ChoralRoot's block, watched by the poll as
# cr_settings.c crs_hold_seen): run 1 sets 600 ms and nothing else, saved once; run 2 (the same file) opens Options on
# Hold Time: 600 ms on every frame (the UI log's item / value), nothing saved again
HFLASH=$OUT/persist_hold_flash.bin
rm -f "$HFLASH" "$OUT"/persist_hold_1.log "$OUT"/persist_hold_2.log
EMU_UI_LOG=0 "$EMU" --headless --flash "$HFLASH" --script "$S/persist_hold_set.txt" >"$OUT/persist_hold_1.log" 2>&1 || bad "Hold Time run 1: exit status"
EMU_UI_LOG=0 "$EMU" --headless --flash "$HFLASH" --script "$S/persist_hold_check.txt" >"$OUT/persist_hold_2.log" 2>&1 || bad "Hold Time run 2: exit status"
ht() { LC_ALL=C sed -n "s/.*item 'Hold Time' value '\([0-9]* ms\)'.*/\1/p" "$1" | sort -u | tr '\n' ' '; }
hs() { LC_ALL=C grep -ao 'settings saves [0-9]* (record: [a-z]*)' "$1"; }
[ "$(ht "$OUT/persist_hold_1.log")" = "400 ms 600 ms " ] && [ "$(hs "$OUT/persist_hold_1.log")" = "settings saves 1 (record: defaults)" ] &&
    ok "Hold Time run 1: KNOB 1 +2 on Hold Time, 400 -> 600 ms, and nothing else: saved once" \
    || bad "Hold Time run 1: '$(ht "$OUT/persist_hold_1.log")', $(hs "$OUT/persist_hold_1.log")"
[ "$(ht "$OUT/persist_hold_2.log")" = "600 ms " ] && [ "$(hs "$OUT/persist_hold_2.log")" = "settings saves 0 (record: current)" ] &&
    ok "Hold Time run 2: 600 ms after a relaunch, nothing to save" \
    || bad "Hold Time run 2: '$(ht "$OUT/persist_hold_2.log")', $(hs "$OUT/persist_hold_2.log")"

# the loop slots (docs/LOOPER.md "Slots and the record"): run 1 (tools/emu/scripts/cr_loop_slots.txt) records into
# slots 1 and 2 and only switches between them (no SAVE held): each loop is saved to its slot as it is left (or at the
# stop that cancelled a switch); run 2 (the same file) finds both: slot 1 with its three overdubs (5 events, 4 layers),
# slot 2's take
LFLASH=$OUT/persist_loop_flash.bin
rm -f "$LFLASH" "$OUT"/persist_loop_1.log "$OUT"/persist_loop_2.log
"$EMU" --headless --flash "$LFLASH" --script "$S/cr_loop_slots.txt" >"$OUT/persist_loop_1.log" 2>&1 || bad "loop run 1: exit status"
"$EMU" --headless --flash "$LFLASH" --script "$S/loop_persist_check.txt" >"$OUT/persist_loop_2.log" 2>&1 || bad "loop run 2: exit status"
lp() { grep '^  loop:' "$OUT/persist_loop_2.log" | sed -n "$1p" | sed -n "s/.* $2 \([0-9A-F]*\) .*/\1/p"; }
grep -q '^loop: auto-save slot 1 .* rc 0' "$OUT/persist_loop_1.log" && ! grep -q '^loop: save slot' "$OUT/persist_loop_1.log" &&
    ok "loop run 1: slots 1 and 2 recorded, saved only by leaving them (no SAVE held)" || bad "loop run 1: no auto-save"
[ "$(lp 1 used)" = 003 ] && [ "$(lp 2 slot)" = 2 ] && [ "$(lp 2 events)" = 1 ] && [ "$(lp 3 slot)" = 1 ] &&
    [ "$(lp 3 events)" = 5 ] && [ "$(lp 3 layers)" = 4 ] && [ "$(lp 4 state)" = 2 ] && grep -q '^expect sound .*: ok' "$OUT/persist_loop_2.log" &&
    ok "loop run 2: both slots after a relaunch (used 003): slot 2's take, slot 1 with its overdubs (5 events, 4 layers), playing" \
    || bad "loop run 2: $(grep '^  loop:' "$OUT/persist_loop_2.log" | head -3 | tr '\n' ' ')"
# loops are saved by hand (0.18, the owner 2026-10-11; docs/LOOPER.md "Slots and the record"): the loop in RAM is
# written to its slot by the global save (SAVE held) and when its slot is left, never at a stop, after a take or when
# idle. Four runs on one flash file, loop_persist_back.txt after each (what a power cycle brings):
#   stop:   a take saved (SAVE held), then recorded over and NOT saved     -> the old loop is back (2 events)
#   save:   recorded over, SAVE held                                        -> the new one (1 event)
#   switch: recorded over (3 events), slot 2 and back, no SAVE              -> kept (3 events: leaving a slot saves it)
#   revert: recorded over, GLO + SAVE                                       -> the saved one (3 events), at once
lv() { echo "$1" | sed -n "s/.* $2 \([0-9A-F]*\) .*/\1/p"; }
PF=$OUT/persist_loop_hand.bin
rm -f "$PF" "$OUT"/persist_loop_stop.log "$OUT"/persist_loop_save.log "$OUT"/persist_loop_switch.log "$OUT"/persist_loop_revert.log \
    "$OUT"/persist_loop_*_back.log
lrun() { "$EMU" --headless --flash "$PF" --script "$S/loop_persist_$1.txt" >"$OUT/persist_loop_$1.log" 2>&1 || bad "loop_persist_$1: exit status"
         "$EMU" --headless --flash "$PF" --script "$S/loop_persist_back.txt" >"$OUT/persist_loop_$1_back.log" 2>&1 || bad "loop_persist_$1 back: exit status"; }
ll() { grep -a '^  loop:' "$OUT/persist_loop_$1.log" | sed -n "$2p"; }
l2() { grep -a '^  loop2:' "$OUT/persist_loop_$1.log" | sed -n "$2p" | sed -n 's/.* dirty \([01]\) .*/\1/p'; }
lw() { grep -a '^  flash:' "$OUT/persist_loop_$1.log" | sed -n "$2p" | sed -n 's/.*, \([0-9]*\) writes, .*/\1/p'; }
lrun stop
[ "$(lv "$(ll stop 1)" used)" = 000 ] && [ "$(lv "$(ll stop 1)" events)" = 2 ] && [ "$(l2 stop 1)" = 1 ] && [ "$(lw stop 1)" = 0 ] &&
    [ "$(lv "$(ll stop 2)" used)" = 000 ] && [ "$(lw stop 2)" = 0 ] && ! grep -aq '^loop: auto-save' "$OUT/persist_loop_stop.log" &&
    ok "a take, stopped, 3 s idle: nothing written (0 flash writes: no save at the stop, none when idle), the loop marked not saved" \
    || bad "loop_persist_stop, before the save: $(ll stop 1) / dirty $(l2 stop 1) / writes $(lw stop 1) $(lw stop 2)"
[ "$(lv "$(ll stop 3)" used)" = 001 ] && [ "$(l2 stop 3)" = 0 ] && grep -aq '^loop: save slot 1 [0-9]* bytes rc 0' "$OUT/persist_loop_stop.log" &&
    ok "SAVE held: the loop written to slot 1 with the settings (used 001): $OUT/loop_persist_stop.ppm" || bad "loop_persist_stop, the save: $(ll stop 3)"
[ "$(lv "$(ll stop 4)" events)" = 1 ] && [ "$(l2 stop 4)" = 1 ] && [ "$(lw stop 4)" = "$(lw stop 3)" ] &&
    [ "$(grep -ac '^loop: save slot' "$OUT/persist_loop_stop.log")" = 1 ] &&
    ok "recorded over (1 event), stopped, 3.5 s: not saved, no flash write" || bad "loop_persist_stop, recorded over: $(ll stop 4) / writes $(lw stop 3) -> $(lw stop 4)"
bk() { grep -a '^  loop:' "$OUT/persist_loop_$1_back.log" | sed -n "$2p"; }
[ "$(lv "$(bk stop 1)" events)" = 2 ] && [ "$(lv "$(bk stop 1)" state)" = 1 ] && [ "$(lv "$(bk stop 2)" state)" = 2 ] &&
    grep -q '^expect sound .*: ok' "$OUT/persist_loop_stop_back.log" &&
    ok "a power cycle without saving: the OLD loop is back (2 events: the one recorded over it is gone) and plays" || bad "loop_persist_stop back: $(bk stop 1)"
lrun save
[ "$(lv "$(ll save 1)" events)" = 1 ] && [ "$(l2 save 1)" = 0 ] && [ "$(lv "$(bk save 1)" events)" = 1 ] && [ "$(lv "$(bk save 2)" state)" = 2 ] &&
    ok "recorded over, SAVE held, a power cycle: the NEW loop (1 event)" || bad "loop_persist_save: $(ll save 1) / $(bk save 1)"
lrun switch
[ "$(lv "$(ll switch 1)" events)" = 3 ] && [ "$(l2 switch 1)" = 1 ] && grep -aq '^loop: auto-save slot 1 [0-9]* bytes rc 0 (leaving it)' "$OUT/persist_loop_switch.log" &&
    ! grep -aq '^loop: save slot' "$OUT/persist_loop_switch.log" && [ "$(lv "$(ll switch 2)" events)" = 3 ] && [ "$(l2 switch 2)" = 0 ] &&
    [ "$(lv "$(ll switch 2)" slot)" = 1 ] && [ "$(lv "$(bk switch 1)" events)" = 3 ] &&
    ok "recorded over (3 events), the slot left and come back to, no SAVE held: written when it was left; kept over a power cycle" \
    || bad "loop_persist_switch: $(ll switch 1) / $(ll switch 2) / $(bk switch 1)"
cmp -s "$OUT/loop_persist_dirty.ppm" "$OUT/loop_persist_clean.ppm" && bad "the PLAY menu's slot strip: no mark on a loop not saved" ||
    ok "the PLAY menu's slot strip: a mark on the slot whose loop is not saved, gone once it is: $OUT/loop_persist_dirty.ppm / loop_persist_clean.ppm"
"$EMU" --headless --flash "$PF" --script "$S/loop_persist_revert.txt" >"$OUT/persist_loop_revert.log" 2>&1 || bad "loop_persist_revert: exit status"
[ "$(lv "$(ll revert 1)" events)" = 1 ] && [ "$(lv "$(ll revert 1)" state)" = 2 ] && [ "$(l2 revert 1)" = 1 ] &&
    grep -aq '^save: reverted' "$OUT/persist_loop_revert.log" && [ "$(lv "$(ll revert 2)" events)" = 3 ] && [ "$(lv "$(ll revert 2)" state)" = 1 ] &&
    [ "$(l2 revert 2)" = 0 ] && grep -a 'parts busy' "$OUT/persist_loop_revert.log" | sed -n 2p | grep -q 'parts busy 0 ' && [ "$(lv "$(ll revert 3)" state)" = 2 ] &&
    grep -aq '^expect sound at .*: ok' "$OUT/persist_loop_revert.log" && [ "$(lw revert 3)" = 0 ] &&
    ok "recorded over a saved loop while it plays, GLO + SAVE: the loop stops, the SAVED loop is back (3 events) and plays; no flash write" \
    || bad "loop_persist_revert: $(ll revert 1) / $(ll revert 2) / $(ll revert 3) / writes $(lw revert 3)"
# a loop's bass-side notes (Split Bass, 0.18) after a relaunch: cr_loop_bass.txt leaves slot 1 with G major and a
# bass-side D (written by its last line: SAVE held); loop_bass_persist_check.txt on the same file: the event is still a bass-side one
# and plays on the bass part (part 1) with Single = Full
BF=$OUT/persist_loop_bass.bin
rm -f "$BF" "$OUT"/persist_loop_bass_1.log "$OUT"/persist_loop_bass_2.log
"$EMU" --headless --flash "$BF" --script "$S/cr_loop_bass.txt" >"$OUT/persist_loop_bass_1.log" 2>&1 || bad "loop bass run 1: exit status"
"$EMU" --headless --flash "$BF" --script "$S/loop_bass_persist_check.txt" >"$OUT/persist_loop_bass_2.log" 2>&1 || bad "loop bass run 2: exit status"
bl() { grep '^  loop:' "$OUT/persist_loop_bass_2.log" | sed -n "$1p"; }
bv() { grep -a '^  loopv:' "$OUT/persist_loop_bass_2.log" | sed -n "$1p" | sed -n 's/.*chord \([0-9]*\) bass \([0-9]*\) bass-side-events \([0-9]*\) stream-notes main \([0-9]*\) bass \([0-9]*\)$/\1 \2 \3 \4 \5/p'; }
bp() { grep -a "^  part 1:" "$OUT/persist_loop_bass_2.log" | sed -n "$1p" | sed -n 's/.* voices \([0-9]*\),.*/\1/p'; }
bk() { grep -a '^  kbd:' "$OUT/persist_loop_bass_2.log" | sed -n "$1p" | sed -n 's/.*keyboard \([a-z]*\) single \([0-9]\) .*/\1 \2/p'; }
grep -q '^loop: save slot 1 32 bytes rc 0' "$OUT/persist_loop_bass_1.log" &&
    [ "$(lv "$(bl 1)" used)" = 001 ] && [ "$(lv "$(bl 1)" events)" = 2 ] && [ "$(bv 1)" = "0 0 1 0 0" ] && [ "$(bk 1)" = "chords 2" ] &&
    ok "loop bass relaunched: slot 1 back with its 2 events, 1 of them bass-side (the record's flag and bit survive the flash)" \
    || bad "loop bass relaunched: $(bl 1) / $(bv 1) / $(bk 1)"
[ "$(bk 2)" = "chords 0" ] && [ "$(bv 2)" = "1 0 1 3 0" ] && [ "$(bv 3)" = "0 1 1 0 1" ] && [ "$(bp 3)" -ge 1 ] &&
    [ "$(grep 'parts busy' "$OUT/persist_loop_bass_2.log" | tail -1 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p')" = 0 ] &&
    ok "loop bass relaunched, Single = Full: G major on the chord part, the recorded D on the bass part alone; stopped: silent" \
    || bad "loop bass relaunched, playing: $(bk 2) / $(bv 2) / $(bv 3) part 1 $(bp 3)"

# ---- the session (0.18, docs/SETTINGS.md "The session"): since 2026-10-11 written by the global save alone: the unit
# comes back as last SAVED.
# Run 1 (session_set.txt, a fresh flash): a loop recorded in slot 3 and stopped, both parts' sounds edited and not
# saved, Bass on with its Level down, the delay picked and set (both its pages), Perform on with Arpeggiate, voicing +2, octave -1, LOCK
# with MAJ + M7 latched; 4 s: nothing written; SAVE held: written. Run 2 (session_check.txt, a relaunch): all of it back at power-on, the loop loaded and
# stopped, silence; D4 plays the latched Dmaj7 arpeggiated over the bass; nothing is written (the state is the stored)
SF=$OUT/persist_session_flash.bin
S1=$OUT/persist_session_1.log; S2=$OUT/persist_session_2.log
rm -f "$SF" "$S1" "$S2" "$OUT"/persist_session_*.log "$OUT"/persist_session_*.bin
"$EMU" --headless --flash "$SF" --script "$S/session_set.txt" >"$S1" 2>&1 || bad "session run 1: exit status"
"$EMU" --headless --flash "$SF" --script "$S/session_check.txt" >"$S2" 2>&1 || bad "session run 2: exit status"
# the n-th dump's line of a kind, and a field of the "cr:" line
dl() { grep -a "^  $2" "$1" | sed -n "$3p"; }
sv() { dl "$1" 'session: saves' "$2" | sed -n 's/.*saves \([0-9]*\) (at power-on: \([^)]*\)) now \([0-9A-F]*\) flash \([0-9A-F]*\) pending \([0-9]\)  \(.*\)$/\1|\2|\3|\4|\5|\6/p'; }
sx() { sv "$1" "$2" | cut -d'|' -f"$3"; }
crl() { dl "$1" 'cr: key' "$2" | sed -n 's/.* perform \([0-9]\) mode \([0-9]\) bass \([0-9]\) (sound \([0-9]*\)) voicing \(-*[0-9]*\) octave \(-*[0-9]*\) .* view \([0-9]\) layer \([0-9]*\) options \([0-9]\).*/perform \1 mode \2 bass \3 sound \4 voicing \5 octave \6 view \7 layer \8 options \9/p'; }
want_state="voicing 2 octave -1 bass 1 lock 1 latched 44"
want_cr="perform 1 mode 2 bass 1 sound 18 voicing 2 octave -1 view 0 layer 0 options 0"
# (the FX block by tag, cr_settings.h CSS_FXP_*: 4, 5 the chorus; 26..29 the bass part's sends (reverb 10); 25, 10..17
# the delay: 10 its division 6 -> 8 (1/8.), 12 its spread 0 -> 1 (3:4), 13 its feedback 58 -> 52, 15 its mode 1 -> 0
# (Stereo), 16 its tone 70 -> 74; 18..24 the reverb; amt: the chord part's sends, the delay's turned 25 -> 43,
# three detents of 6)
want_fx="  session fx: on 1 sel 2 (Delay 2/2) amt 35 45 43 0 sends 35 45 43 0 par 4=40 5=60 26=10 27=0 28=0 29=0 25=91 10=8 11=0 12=1 13=52 14=1 15=0 16=74 17=24 18=88 19=56 20=52 21=16 22=56 23=60 24=127"
c1=$(dl "$S1" 'session chord:' 2); b1=$(dl "$S1" 'session bass:' 2)
[ "$(sx "$S1" 2 6)" = "$want_state" ] && [ "$(crl "$S1" 2)" = "$want_cr" ] && [ "$(dl "$S1" 'session fx:' 2)" = "$want_fx" ] &&
    echo "$c1" | grep -q 'FM6 / TINE EP edited 1 kind 1 crc [0-9A-F]* level 92 pan 0' &&
    echo "$b1" | grep -q 'VA / DEEP SUB edited 1 kind 1 crc [0-9A-F]* level 84 pan 0' &&
    dl "$S1" 'loop: state' 2 | grep -q 'state 1 cap 0 layers 1 events 2 .* slot 3 .* used 004 ' &&
    ok "session run 1: the state set: both sounds edited (not saved), Bass on at level 84, Delay picked and set, Arpeggiate on, voicing +2, octave -1, LOCK with MAJ + M7, a loop in slot 3" \
    || bad "session run 1: $(sx "$S1" 2 6) / $(crl "$S1" 2) / $(dl "$S1" 'session fx:' 2) / $c1 / $b1"
[ "$(sx "$S1" 1 1)" = 0 ] && [ "$(sx "$S1" 1 5)" = 1 ] && dl "$S1" 'flash:' 1 | grep -q ', 0 writes, settings saves 0 ' &&
    dl "$S1" 'loop: state' 1 | grep -q ' used 000 ' &&
    ok "session run 1: 4 s after all of it: nothing written (0 flash writes: no settings, no session, no loop)" \
    || bad "session run 1: written without a save: $(dl "$S1" 'flash:' 1) / $(sv "$S1" 1)"
[ "$(sx "$S1" 2 1)" = 1 ] && [ "$(sx "$S1" 2 5)" = 0 ] && [ "$(sx "$S1" 2 3)" = "$(sx "$S1" 2 4)" ] &&
    dl "$S1" 'settings: now' 2 | grep -q 'pending 0' && dl "$S1" 'flash:' 2 | grep -q 'settings saves 1 ' &&
    grep -aq '^loop: save slot 3 [0-9]* bytes rc 0' "$S1" &&
    ok "session run 1: SAVE held: the settings, the session and the loop each written once (flash = now $(sx "$S1" 2 3))" \
    || bad "session run 1: not saved: $(sv "$S1" 2)"
grep -aq '^session: applied (parts 1 1, voicing 2, octave -1, bass 1, lock 1 latched 44, fx 2)' "$S2" &&
    [ "$(sx "$S2" 1 2)" = current ] && [ "$(sx "$S2" 1 6)" = "$want_state" ] && [ "$(crl "$S2" 1)" = "$want_cr" ] &&
    ok "session run 2 (a relaunch): at power-on voicing +2, octave -1, Bass on (its sound, ALGORITHM's place 18), Perform on with Arpeggiate, LOCK on with MAJ + M7 latched; on the view, nothing open" \
    || bad "session run 2: $(sv "$S2" 1) / $(crl "$S2" 1)"
[ "$(dl "$S2" 'session chord:' 1)" = "$c1" ] && [ "$(dl "$S2" 'session bass:' 1)" = "$b1" ] && [ -n "$c1" ] &&
    ok "session run 2: both parts' unsaved edits are back, the same patch checksums, 'edited', Level and Pan ($(echo "$c1" | sed 's/.*crc //') / $(echo "$b1" | sed 's/.*crc //'))" \
    || bad "session run 2: the parts: $(dl "$S2" 'session chord:' 1) / $(dl "$S2" 'session bass:' 1) want $c1 / $b1"
[ "$(dl "$S2" 'session fx:' 1)" = "$want_fx" ] &&
    ok "session run 2: the FX layer in full: Delay's second page picked, its division (1/8.), spread (3:4), feedback (52), mode (Stereo) and tone (74) as turned, the reverb's as they were, both parts' sends as left (the chord part's 35 45 37 0, the bass part's reverb 10)" \
    || bad "session run 2: fx: $(dl "$S2" 'session fx:' 1)"
dl "$S2" 'loop: state' 1 | grep -q 'state 1 cap 0 layers 1 events 2 .* slot 3 played 0 .* used 004 ' &&
    grep -aq '^expect silence at 800 ms: ok' "$S2" && dl "$S2" 'mods: down' 1 | grep -q 'down 44 active 44 voices free' &&
    [ "$(grep -a 'parts busy' "$S2" | sed -n 1p | sed -n 's/.*parts busy \([0-9]*\).*/\1/p')" = 0 ] &&
    ok "session run 2: slot 3 is loaded and STOPPED (it never starts by itself), nothing sounds, the latched keys are down in the engine" \
    || bad "session run 2: the loop / silence: $(dl "$S2" 'loop: state' 1)"
grep -aq '^expect sound at .*: ok' "$S2" && dl "$S2" 'part 0:' 2 | grep -q 'FM6 / TINE EP, voices [1-9]' &&
    dl "$S2" 'part 1:' 2 | grep -q 'VA / DEEP SUB, voices 1' && dl "$S2" 'mods: down' 2 | grep -q 'voices 50:q3/x4' &&
    ok "session run 2: D4 alone plays the latched chord (a major 7th) on the edited TINE EP, the bass under it" \
    || bad "session run 2: playing: $(dl "$S2" 'part 0:' 2) / $(dl "$S2" 'part 1:' 2) / $(dl "$S2" 'mods: down' 2)"
[ "$(sx "$S2" 3 1)" = 0 ] && [ "$(sx "$S2" 3 5)" = 0 ] && [ "$(sx "$S2" 3 3)" = "$(sx "$S1" 2 3)" ] &&
    dl "$S2" 'flash:' 3 | grep -q ', 0 writes, settings saves 0 (record: current)' &&
    ok "session run 2: nothing written (0 flash writes): what the unit came back as is what was stored ($(sx "$S2" 3 3))" \
    || bad "session run 2: wrote: $(dl "$S2" 'flash:' 3) / $(sv "$S2" 3)"

# a session that does not belong to the settings is not applied: the flash of the runs above with the settings object
# as 0.17 would have rewritten it (a v14 block, another tempo: a downgrade and back). Power-on: the settings (99 BPM),
# none of the session: voicing 0, the bass off, LOCK off, the pool's TINE EP
SO=$OUT/persist_session_old_flash.bin
rm -f "$SO"
python3 -c "
import struct, sys, zlib
b = bytearray(open(sys.argv[1], 'rb').read())
best = None
for o in (0xFC000, 0xFD000):
    magic, typ, slot, seq, ln = struct.unpack_from('<IHHII', b, o)
    if magic == 0x554C4546 and b[o + 256 + ln - 192:o + 256 + ln - 188] == b'CRS1' and (best is None or seq > best[1]):
        best = (o, seq, ln)
o, seq, ln = best
c = o + 256 + ln - 192
assert struct.unpack_from('<H', b, c + 4)[0] == 15
struct.pack_into('<H', b, c + 4, 14)             # the block's version
struct.pack_into('<H', b, c + 30, 99)            # bpm
h = 2166136261
for x in b[c + 12:c + 192]:
    h = ((h ^ x) * 16777619) & 0xFFFFFFFF
struct.pack_into('<I', b, c + 8, h)
struct.pack_into('<I', b, o + 16, zlib.crc32(bytes(b[o + 256:o + 256 + ln])) & 0xFFFFFFFF)
struct.pack_into('<I', b, o + 28, zlib.crc32(bytes(b[o:o + 28])) & 0xFFFFFFFF)
open(sys.argv[2], 'wb').write(b)" "$SF" "$SO" || bad "session, an older settings object: the flash could not be made"
SL=$OUT/persist_session_old.log
"$EMU" --headless --flash "$SO" --script "$S/persist_check.txt" >"$SL" 2>&1 || bad "session, older settings: exit status"
[ "$(bpm "$SL")" = 99 ] && grep -aq 'record: migrated' "$SL" && [ "$(sx "$SL" 1 2)" = "not these settings'" ] &&
    [ "$(sx "$SL" 1 6)" = "voicing 0 octave 0 bass 0 lock 0 latched 00" ] && ! grep -aq '^session: applied' "$SL" &&
    dl "$SL" 'session chord:' 1 | grep -q 'FM6 / TINE EP edited 0 kind 1 crc [0-9A-F]* level 92 pan 0' &&
    ok "a settings object rewritten by an older firmware (v14, 99 BPM): its settings load (migrated), the session left beside it is NOT applied (voicing 0, the bass off, LOCK off, the pool's TINE EP)" \
    || bad "session, older settings: bpm $(bpm "$SL") $(sv "$SL" 1) / $(dl "$SL" 'session chord:' 1)"

# nothing is written by itself, however long (session_storm.txt); what a save costs and what it sounds like under a
# held chord, and the automatic part still waiting for silence (session_hold.txt)
ST=$OUT/persist_session_storm.log; SH=$OUT/persist_session_hold.log
"$EMU" --headless --script "$S/session_storm.txt" >"$ST" 2>&1 || bad "session storm: exit status"
"$EMU" --headless --script "$S/session_hold.txt" >"$SH" 2>&1 || bad "session hold: exit status"
fw() { dl "$1" 'flash:' "$2" | sed -n 's/.*, \([0-9]*\) writes, settings saves \([0-9]*\) .*/\1 \2/p'; }
[ "$(sx "$ST" 1 1) $(sx "$ST" 1 5)" = "0 1" ] && [ "$(fw "$ST" 1)" = "0 0" ] && echo "$(sx "$ST" 1 6)" | grep -q '^voicing 4 ' &&
    ok "KNOB 1 turned for 3 s (20 detents), then 3 s of nothing: no flash write (the state differs from flash until a save)" \
    || bad "session storm, the voicing: $(sv "$ST" 1) / $(fw "$ST" 1)"
[ "$(sx "$ST" 2 1) $(sx "$ST" 2 5)" = "1 0" ] && [ "$(fw "$ST" 2)" = "11 1" ] && [ "$(sx "$ST" 3 1)" = 1 ] && [ "$(fw "$ST" 3)" = "11 1" ] &&
    ok "SAVE held, a new unit's first save: both objects written once (the settings: 1 erase + 4 programs; the session: 1 erase + 5); none after" \
    || bad "session storm, the save: $(sv "$ST" 2) / $(fw "$ST" 2) / $(fw "$ST" 3)"
[ "$(sx "$ST" 4 1) $(sx "$ST" 4 5)" = "1 1" ] && [ "$(sx "$ST" 5 1) $(sx "$ST" 5 5)" = "2 0" ] && [ "$(fw "$ST" 5)" = "17 1" ] &&
    dl "$ST" 'session chord:' 5 | grep -q 'edited 1' && [ "$(fw "$ST" 6)" = "17 1" ] && [ "$(sx "$ST" 6 1)" = 2 ] &&
    ok "an editor knob turned for 1.2 s, 3 s: not written; SAVE held: ONE object written (the session: the settings block did not change); SAVE held again with nothing changed: no erase at all" \
    || bad "session storm, the edit: $(sv "$ST" 4) / $(sv "$ST" 5) / $(fw "$ST" 5) / $(fw "$ST" 6)"
dl "$ST" 'flash erases' 6 | grep -q 'audio stalled 3 (48 blocks' &&
    ok "each object a save writes: one sector erase (45 ms: 16 audio blocks of silence)" \
    || bad "session storm: erases: $(dl "$ST" 'flash erases' 6)"
[ "$(grep -ac '^expect sound at .*: ok' "$SH")" = 3 ] && [ "$(sx "$SH" 1 1) $(sx "$SH" 1 5)" = "0 1" ] && [ "$(fw "$SH" 1)" = "0 0" ] &&
    ok "a change under a held chord: nothing written (3 s)" || bad "session hold, held: $(sv "$SH" 1) / $(fw "$SH" 1)"
[ "$(sx "$SH" 2 1) $(sx "$SH" 2 5)" = "1 0" ] && dl "$SH" 'flash erases' 2 | grep -q 'audio stalled 2 (32 blocks' &&
    ok "SAVE held under the sounding chord: saved at once (the player's own press): an erase an object written (here two, a new unit's first save: 2 x 45 ms of silence in the chord), the chord sounds on" \
    || bad "session hold, the save: $(sv "$SH" 2) / $(dl "$SH" 'flash erases' 2)"
[ "$(fw "$SH" 3 | cut -d' ' -f2)" = 1 ] && dl "$SH" 'settings: now' 3 | grep -q 'style 1 .* pending 1 ' &&
    [ "$(fw "$SH" 4 | cut -d' ' -f2)" = 2 ] && dl "$SH" 'settings: now' 4 | grep -q 'pending 0 ' &&
    dl "$SH" 'flash erases' 4 | grep -q 'audio stalled 3 (48 blocks' &&
    ok "an Option changed under the same chord: the automatic write waits (3 s: nothing), written once the chord and its tail are over" \
    || bad "session hold, the Option: $(fw "$SH" 3) / $(fw "$SH" 4) / $(dl "$SH" 'settings: now' 3)"

# Options > Reset Settings clears the session too (the reset runs above): after the reset the session as it would be
# saved is a new unit's (the fresh run's), it is in flash, and the relaunch comes up on it
fs=$(sx "$OUT/persist_reset_fresh.log" 1 3)
[ -n "$fs" ] && [ "$(sx "$R2" 8 3)" = "$fs" ] && [ "$(sx "$R2" 8 4)" = "$fs" ] && [ "$(sx "$R2" 8 5)" = 0 ] &&
    [ "$(sx "$R3" 1 3)" = "$fs" ] && [ "$(sx "$R3" 1 2)" = current ] &&
    ok "Reset Settings: the session too is a new unit's ($fs), in flash, and what the relaunch comes up on" \
    || bad "reset, the session: fresh $fs, after the reset $(sv "$R2" 8), relaunched $(sv "$R3" 1)"

# ---- FM TONE's Drive and the session (docs/FX.md "FM TONE's Drive"). Run 1 (quad_drive_set.txt, a fresh flash): the
# chord part's drive send 12, then FM TONE WOBBLE with its AMP Drive turned to 28 (saved to no preset); the bass part's
# send 18, then FM TONE EP with its Drive turned to 24; FX off; SAVE held. Run 2 (quad_drive_check.txt, a relaunch): as
# saved; a Drive turned and GLO + SAVE: as saved again; both parts to the VA, FX on: the parts' sends; Reset Settings
QF=$OUT/persist_qdrive_flash.bin
Q1=$OUT/persist_qdrive_1.log; Q2=$OUT/persist_qdrive_2.log
rm -f "$QF" "$Q1" "$Q2"
"$EMU" --headless --flash "$QF" --script "$S/quad_drive_set.txt" >"$Q1" 2>&1 || bad "FM TONE drive run 1: exit status"
"$EMU" --headless --flash "$QF" --script "$S/quad_drive_check.txt" >"$Q2" 2>&1 || bad "FM TONE drive run 2: exit status"
qd() { dl "$1" 'drive:' "$2" | sed 's/^  drive: //'; }
qf() { dl "$1" 'session fx:' "$2" | sed -n 's/^  session fx: on \([01]\) .* amt \([0-9 ]*\) sends \([0-9 ]*\) par.* 29=\([0-9]*\) .*/on \1 amt \2 sends \3 bass-send \4/p'; }
want_d="chord FM TONE track 28 send 12 edited 1 | bass FM TONE track 24 send 18 edited 1"
want_f="on 0 amt 35 45 25 12 sends 0 0 0 28 bass-send 18"
[ "$(qd "$Q1" 1)" = "$want_d" ] && [ "$(qf "$Q1" 1)" = "$want_f" ] && [ "$(sx "$Q1" 1 1)" = 1 ] && [ "$(sx "$Q1" 1 5)" = 0 ] &&
    ok "FM TONE drive run 1: both parts on FM TONE, their Drives edited (28, 24) and saved to no preset, FX off, the parts' own sends 12 / 18 behind; SAVE held: the session written" \
    || bad "FM TONE drive run 1: $(qd "$Q1" 1) / $(qf "$Q1" 1) / $(sv "$Q1" 1)"
[ "$(qd "$Q2" 1)" = "$want_d" ] && [ "$(qf "$Q2" 1)" = "$want_f" ] && [ "$(sx "$Q2" 1 2)" = current ] &&
    [ "$(sx "$Q2" 1 3)" = "$(sx "$Q1" 1 3)" ] && [ "$(sx "$Q2" 1 5)" = 0 ] &&
    [ "$(dl "$Q2" 'session chord:' 1)" = "$(dl "$Q1" 'session chord:' 1)" ] && [ "$(dl "$Q2" 'session bass:' 1)" = "$(dl "$Q1" 'session bass:' 1)" ] &&
    ok "FM TONE drive run 2 (a relaunch): as it sounded when saved: the Drives 28 / 24 with FX off, both sounds still edited, the session's checksum the stored one ($(sx "$Q2" 1 3))" \
    || bad "FM TONE drive run 2: $(qd "$Q2" 1) / $(qf "$Q2" 1) / $(sv "$Q2" 1)"
[ "$(qd "$Q2" 2)" = "chord FM TONE track 40 send 12 edited 1 | bass FM TONE track 24 send 18 edited 1" ] && [ "$(sx "$Q2" 2 5)" = 1 ] &&
    grep -aq '^save: reverted' "$Q2" && [ "$(qd "$Q2" 3)" = "$want_d" ] && [ "$(qf "$Q2" 3)" = "$want_f" ] && [ "$(sx "$Q2" 3 5)" = 0 ] &&
    ok "a Drive turned (28 -> 40), GLO + SAVE held: the revert: 28 / 24 with FX off again, nothing pending" \
    || bad "FM TONE drive, the revert: $(qd "$Q2" 2) -> $(qd "$Q2" 3) / $(qf "$Q2" 3)"
[ "$(qd "$Q2" 4)" = "chord track 12 send 12 edited 0 | bass track 18 send 18 edited 0" ] &&
    ok "both parts switched to the VA, FX on: the parts' own drive sends as before FM TONE (12, 18), kept by the session beside the sounds' Drives" \
    || bad "FM TONE drive, the parts' sends after the relaunch: $(qd "$Q2" 4)"
[ "$(qd "$Q2" 5)" = "chord track 0 send 0 edited 0 | bass track 0 send 0 edited 0" ] && dl "$Q2" 'session chord:' 5 | grep -q 'FM6 / TINE EP edited 0' && dl "$Q2" 'session bass:' 5 | grep -q 'VA / DEEP SUB edited 0' &&
    ok "Options > Reset Settings from FM TONE on both parts: a new unit's sounds, no drive anywhere" \
    || bad "FM TONE drive, the reset: $(qd "$Q2" 5) / $(dl "$Q2" 'session chord:' 5)"
# Felucca's SPEAKER setting left behind (0.19, docs/AUDIO-QUALITY.md "Left behind"): a unit that ran Felucca with
# MENU > SPEAKER at LOWCUT or BASS+ kept that master stage under ChoralRoot, which has no control for it. A ChoralRoot
# settings object with the field at 2 (BASS+), and a Felucca PER4 object with it at 1 (LOWCUT): both power on with the
# stage off, and the next write of the object stores 0
LCF=$OUT/persist_lowcut_flash.bin
LC4=$OUT/persist_lowcut4_flash.bin
rm -f "$LCF" "$LC4" "$OUT"/persist_lowcut*.log
"$EMU" --headless --flash "$LCF" --script "$S/persist_set.txt" >"$OUT/persist_lowcut_0.log" 2>&1 || bad "lowcut run 0: exit status"
cp "$LCF" "$LC4"
n5=$(python3 tools/emu/seed_lowcut.py "$LCF" 2)
n4=$(python3 tools/emu/seed_lowcut.py "$LC4" 1 --per4)
lc() { LC_ALL=C grep -a '^  master:' "$1" | tail -1 | sed 's/.*lowcut //'; }
"$EMU" --headless --flash "$LCF" --script "$S/persist_check.txt" >"$OUT/persist_lowcut_1.log" 2>&1 || bad "lowcut run 1: exit status"
[ "${n5:-0}" -ge 1 ] && [ "$(python3 tools/emu/seed_lowcut.py "$LCF")" = 2 ] && [ "$(lc "$OUT/persist_lowcut_1.log")" = "0/0 found 2" ] &&
    [ "$(bpm "$OUT/persist_lowcut_1.log")" = 137 ] &&
    ok "a settings object with Felucca's SPEAKER at BASS+ ($n5 copies): found, held off (settings.lowcut 0, fx_lowcut 0); the settings kept (137 BPM)" \
    || bad "lowcut 2 in a PER5 object: $n5 copies, $(lc "$OUT/persist_lowcut_1.log")"
"$EMU" --headless --flash "$LCF" --script "$S/cr_output_classic.txt" >"$OUT/persist_lowcut_2.log" 2>&1 || bad "lowcut run 2: exit status"
"$EMU" --headless --flash "$LCF" --script "$S/persist_check.txt" >"$OUT/persist_lowcut_3.log" 2>&1 || bad "lowcut run 3: exit status"
[ "$(python3 tools/emu/seed_lowcut.py "$LCF")" = 0 ] && [ "$(lc "$OUT/persist_lowcut_3.log")" = "0/0 found 0" ] &&
    ok "the next write of the object (an Option changed) stores 0: the flash is clean for whatever firmware comes next" \
    || bad "lowcut after a save: flash $(python3 tools/emu/seed_lowcut.py "$LCF"), $(lc "$OUT/persist_lowcut_3.log")"
"$EMU" --headless --flash "$LC4" --script "$S/persist_check.txt" >"$OUT/persist_lowcut_4.log" 2>&1 || bad "lowcut run 4: exit status"
[ "${n4:-0}" -ge 1 ] && [ "$(lc "$OUT/persist_lowcut_4.log")" = "0/0 found 1" ] && grep -q "record: defaults" "$OUT/persist_lowcut_4.log" &&
    ok "a Felucca PER4 object with SPEAKER at LOWCUT ($n4 copies): found, held off; ChoralRoot's defaults" \
    || bad "lowcut 1 in a PER4 object: $n4 copies, $(lc "$OUT/persist_lowcut_4.log")"
# ---- the session and a removed trim row (0.19, docs/CZ1.md "Level"): a part's trim is its sound's in THIS firmware's
# table (cr_bank.c CB_TRIM), not what the session held. Run 1 (trim_set.txt, a fresh flash): CZ-1 BRASS 1 on the chord
# part, CZ-1 PIPE ORGAN 1 on the bass, SAVE held. The session's two trims are then set to what 0.18 stored for them
# (-10, -12: seed_trim.py). Run 2 (trim_check.txt, a relaunch): trim 0 on both at power-on, and after GLO + SAVE
TF=$OUT/persist_trim_flash.bin
T1=$OUT/persist_trim_1.log; T2=$OUT/persist_trim_2.log
rm -f "$TF" "$T1" "$T2"
"$EMU" --headless --flash "$TF" --script "$S/trim_set.txt" >"$T1" 2>&1 || bad "trim run 1: exit status"
tr_() { dl "$1" 'trim:' "$2" | sed 's/^  trim: //'; }
want_t="chord 0 kind 1 fk 1 slot 255 | bass 0 kind 1 fk 35 slot 255"
[ "$(tr_ "$T1" 1)" = "$want_t" ] && dl "$T1" 'session chord:' 1 | grep -q 'CZ-1 / BRASS 1 edited 0' &&
    dl "$T1" 'session bass:' 1 | grep -q 'CZ-1 / PIPE ORGAN 1 edited 0' && [ "$(sx "$T1" 1 1)" = 1 ] &&
    [ "$(python3 tools/emu/seed_trim.py "$TF")" = "0 0" ] &&
    ok "trim run 1: CZ-1 BRASS 1 / PIPE ORGAN 1 on the parts, no trim (their rows went with 0.19's -6 dB); SAVE held: the session written" \
    || bad "trim run 1: $(tr_ "$T1" 1) / $(dl "$T1" 'session bass:' 1) / $(sv "$T1" 1)"
nt=$(python3 tools/emu/seed_trim.py "$TF" -10 -12)
"$EMU" --headless --flash "$TF" --script "$S/trim_check.txt" >"$T2" 2>&1 || bad "trim run 2: exit status"
[ "${nt:-0}" -ge 1 ] && [ "$(python3 tools/emu/seed_trim.py "$TF")" = "-10 -12" ] && [ "$(sx "$T2" 1 2)" = current ] &&
    [ "$(tr_ "$T2" 1)" = "$want_t" ] && dl "$T2" 'session chord:' 1 | grep -q 'CZ-1 / BRASS 1 edited 0' &&
    [ "$(dl "$T2" 'session chord:' 1)" = "$(dl "$T1" 'session chord:' 1)" ] && [ "$(dl "$T2" 'session bass:' 1)" = "$(dl "$T1" 'session bass:' 1)" ] &&
    ok "trim run 2 (a relaunch on a session that holds 0.18's trims -10 / -12, $nt copies): the session is read, both sounds as saved, their trims 0: the table's, not the record's" \
    || bad "trim run 2: seeded '$nt', flash '$(python3 tools/emu/seed_trim.py "$TF")', $(tr_ "$T2" 1) / $(sv "$T2" 1)"
grep -aq '^save: reverted' "$T2" && [ "$(tr_ "$T2" 2)" = "$want_t" ] && grep -q '^expect sound .*: ok' "$T2" &&
    ok "GLO + SAVE held: the revert reads that session again: trim 0 on both parts; a chord sounds" \
    || bad "trim, the revert: $(tr_ "$T2" 2)"
[ $fail = 0 ] && echo PASS || echo FAIL
exit $fail
