#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless: settings persist across emulator runs through the file-backed flash (docs/SETTINGS.md).
#   run 1 (a fresh flash file): SELECT +17 -> 137 BPM, saved 1.5 s later, quit
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
grep -q "settings saves 1 " "$OUT/persist_1.log" && ok "run 1: the settings were saved once" || bad "run 1: no save"
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
# flash): a user sound saved (U01 ADG), settings changed and saved (Advanced, 137 BPM, Key Mode on, Motion Off, Hold
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
[ "$(ov "$R2" 2)" = "1 21 Reset Settings: all options to default" ] && grep -aq "options: open 1 entry 21 of 22 " "$R2" &&
    ok "Reset Settings is the last Options entry (22 of 22): $OUT/reset_entry.ppm" || bad "the entry: $(ov "$R2" 2)"
[ "$(ov "$R2" 3)" = "1 21 Reset Settings: OCT+ again: reset" ] && [ "$(ov "$R2" 4)" = "1 21 Reset Settings: all options to default" ] &&
    [ "$(ov "$R2" 5)" = "0 21 Reset Settings: all options to default" ] && [ "$(ov "$R2" 6)" = "1 21 Reset Settings: all options to default" ] &&
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
outside = a[:0xFC000] == b[:0xFC000] and a[0xFF000:] == b[0xFF000:]
panel = pa[16:fav - 4] == pb[16:fav - 4] and len(pa) == len(pb)
favs = pa[fav:cr] == pb[fav:cr]
record = pa[cr:] != pb[cr:] and pb[cr:cr + 4] == b'CRS1'
print(int(outside), int(panel), int(favs), int(record), fav - 4 - 16)" "$OUT/persist_reset_before.bin" "$RF")
[ "$(echo "$kept" | cut -d' ' -f1-4)" = "1 1 1 1" ] &&
    ok "the flash: everything outside the settings object byte for byte as before the reset (saved sounds, patches, banks, loops); in it the panel calibration table ($(echo "$kept" | cut -d' ' -f5) B) and the favourites unchanged, the ChoralRoot record rewritten" \
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
    ok "key-layer keys run 1: GLO in the KEY layer: keys play, the settings saved once" || bad "key-layer keys run 1: $(grep '^key: keys' "$OUT/persist_kkeys_1.log")"
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

# the engine picker's roots (Settings pick_roots, docs/SETTINGS.md): run 1 turns them to "play" in the picker (KNOB 4);
# run 2 (the same file) opens the picker with them playing
RFLASH=$OUT/persist_roots_flash.bin
rm -f "$RFLASH" "$OUT"/persist_roots_1.log "$OUT"/persist_roots_2.log "$OUT"/persist_roots.ppm
"$EMU" --headless --flash "$RFLASH" --script "$S/persist_roots_set.txt" >"$OUT/persist_roots_1.log" 2>&1 || bad "roots run 1: exit status"
"$EMU" --headless --flash "$RFLASH" --script "$S/persist_roots_check.txt" >"$OUT/persist_roots_2.log" 2>&1 || bad "roots run 2: exit status"
grep -q '^picker: roots play' "$OUT/persist_roots_1.log" && grep -q "settings saves 1 " "$OUT/persist_roots_1.log" &&
    ok "roots run 1: KNOB 4 in the picker: the roots play, saved" || bad "roots run 1: not set / not saved"
grep -q '^picker: open part 0 .* roots play' "$OUT/persist_roots_2.log" && ! grep -q '^engine: ' "$OUT/persist_roots_2.log" &&
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
# the loop written at its stop (no SAVE, no switch; docs/LOOPER.md "Slots and the record"): a take, stopped at once
# (loop_persist_stop) / left playing 6 s then stopped (loop_persist_play); a relaunch on the same file: the loop is back
lv() { echo "$1" | sed -n "s/.* $2 \([0-9A-F]*\) .*/\1/p"; }
for c in stop play; do
    PF=$OUT/persist_loop_$c.bin
    rm -f "$PF" "$OUT"/persist_loop_$c.log "$OUT"/persist_loop_${c}_back.log
    "$EMU" --headless --flash "$PF" --script "$S/loop_persist_$c.txt" >"$OUT/persist_loop_$c.log" 2>&1 || bad "loop_persist_$c: exit status"
    "$EMU" --headless --flash "$PF" --script "$S/loop_persist_back.txt" >"$OUT/persist_loop_${c}_back.log" 2>&1 || bad "loop_persist_$c back: exit status"
    l1=$(grep '^  loop:' "$OUT/persist_loop_$c.log" | sed -n 1p); l2=$(grep '^  loop:' "$OUT/persist_loop_$c.log" | sed -n 2p)
    b1=$(grep '^  loop:' "$OUT/persist_loop_${c}_back.log" | sed -n 1p); b2=$(grep '^  loop:' "$OUT/persist_loop_${c}_back.log" | sed -n 2p)
    [ "$(lv "$l1" used)" = 000 ] && [ "$(lv "$l1" state)" = 2 ] && [ "$(lv "$l2" used)" = 001 ] &&
        grep -q '^loop: auto-save slot 1 [0-9]* bytes rc 0 (stopped)' "$OUT/persist_loop_$c.log" &&
        ! grep -q '^loop: save slot' "$OUT/persist_loop_$c.log" &&
        ok "loop_persist_$c: the take written to slot 1 at its stop, no SAVE (used 000 -> 001): $OUT/loop_persist_$c.ppm" \
        || bad "loop_persist_$c: $l1 / $l2"
    [ "$(lv "$b1" used)" = 001 ] && [ "$(lv "$b1" slot)" = 1 ] && [ "$(lv "$b1" events)" = 2 ] && [ "$(lv "$b2" state)" = 2 ] &&
        grep -q '^expect sound .*: ok' "$OUT/persist_loop_${c}_back.log" &&
        ok "loop_persist_$c relaunched: slot 1 back (2 events) and plays" || bad "loop_persist_$c back: $b1 / $b2"
done
# a loop's bass-side notes (Split Bass, 0.18) after a relaunch: cr_loop_bass.txt leaves slot 1 with G major and a
# bass-side D (written at its stop); loop_bass_persist_check.txt on the same file: the event is still a bass-side one
# and plays on the bass part (part 1) with Single = Full
BF=$OUT/persist_loop_bass.bin
rm -f "$BF" "$OUT"/persist_loop_bass_1.log "$OUT"/persist_loop_bass_2.log
"$EMU" --headless --flash "$BF" --script "$S/cr_loop_bass.txt" >"$OUT/persist_loop_bass_1.log" 2>&1 || bad "loop bass run 1: exit status"
"$EMU" --headless --flash "$BF" --script "$S/loop_bass_persist_check.txt" >"$OUT/persist_loop_bass_2.log" 2>&1 || bad "loop bass run 2: exit status"
bl() { grep '^  loop:' "$OUT/persist_loop_bass_2.log" | sed -n "$1p"; }
bv() { grep -a '^  loopv:' "$OUT/persist_loop_bass_2.log" | sed -n "$1p" | sed -n 's/.*chord \([0-9]*\) bass \([0-9]*\) bass-side-events \([0-9]*\) stream-notes main \([0-9]*\) bass \([0-9]*\)$/\1 \2 \3 \4 \5/p'; }
bp() { grep -a "^  part 1:" "$OUT/persist_loop_bass_2.log" | sed -n "$1p" | sed -n 's/.* voices \([0-9]*\),.*/\1/p'; }
bk() { grep -a '^  kbd:' "$OUT/persist_loop_bass_2.log" | sed -n "$1p" | sed -n 's/.*keyboard \([a-z]*\) single \([0-9]\) .*/\1 \2/p'; }
grep -q '^loop: auto-save slot 1 32 bytes rc 0 (stopped)' "$OUT/persist_loop_bass_1.log" &&
    [ "$(lv "$(bl 1)" used)" = 001 ] && [ "$(lv "$(bl 1)" events)" = 2 ] && [ "$(bv 1)" = "0 0 1 0 0" ] && [ "$(bk 1)" = "chords 2" ] &&
    ok "loop bass relaunched: slot 1 back with its 2 events, 1 of them bass-side (the record's flag and bit survive the flash)" \
    || bad "loop bass relaunched: $(bl 1) / $(bv 1) / $(bk 1)"
[ "$(bk 2)" = "chords 0" ] && [ "$(bv 2)" = "1 0 1 3 0" ] && [ "$(bv 3)" = "0 1 1 0 1" ] && [ "$(bp 3)" -ge 1 ] &&
    [ "$(grep 'parts busy' "$OUT/persist_loop_bass_2.log" | tail -1 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p')" = 0 ] &&
    ok "loop bass relaunched, Single = Full: G major on the chord part, the recorded D on the bass part alone; stopped: silent" \
    || bad "loop bass relaunched, playing: $(bk 2) / $(bv 2) / $(bv 3) part 1 $(bp 3)"
[ $fail = 0 ] && echo PASS || echo FAIL
exit $fail
