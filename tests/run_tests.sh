#!/usr/bin/env bash
# Tes host core portabel (Fase 1 + 2). Tidak butuh toolchain PS3.
#   ./run_tests.sh
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
FIX=$ROOT/fixtures
OUT=$(python3 make_test_iso.py)
CRC=$(echo "$OUT" | grep "fake ELF CRC game (XOR-fold)" | awk '{print $NF}')
CRC_D=$(echo "$OUT" | grep "fake ELF CRC game D" | awk '{print $NF}')
echo "fake ELF CRC game (XOR-fold): $CRC"
echo "fake ELF CRC game D (XOR-fold): $CRC_D"

# Test patches dir (per-file, pelajaran cheat fork): satu file untuk CRC
# ELF palsu + dua file CRC ganda. Tidak ada bundler/blob lagi.
rm -rf "$FIX/patches" "$FIX/empty_db"
mkdir -p "$FIX/patches" "$FIX/empty_db/NET" "$FIX/empty_db/CUSTOM"
REAL_PATCHES="$HOME/workspace/pcsx2_patches_check/pcsx2_patches-main/patches"
cat > "$FIX/patches/SLUS-21159_${CRC}.pnach" <<EOF
gametitle=Moto GP 4 (U)(SLUS-21159)

[Widescreen 16:9]
gsaspectratio=16:9
patch=1,EE,005624a4,word,3f400000 //3f800000
patch=1,EE,0055c608,word,43000000 //43a00000
EOF
cat > "$FIX/patches/SLUS-20795_12345678.pnach" <<EOF
gametitle=Bloody Roar 4 (U)(SLUS-20795)

[Widescreen 16:9]
patch=1,EE,00115768,word,3c013fe3
EOF
cat > "$FIX/patches/SLPM-65499_12345678.pnach" <<EOF
gametitle=Bloody Roar 4 (J)(SLPM-65499)

[Widescreen 16:9]
patch=1,EE,00115768,word,3c013fe3
EOF
# Fase 3j: fixture fallback serial — satu file serial SLUS-99999
# (CRC AAAAAAAA), dan file kedua serial SLPM-65499 (CRC 11111111,
# jadi serial itu punya dua file beda CRC -> ambiguous).
cat > "$FIX/patches/SLUS-99999_AAAAAAAA.pnach" <<EOF
gametitle=Serial Fallback Test (U)(SLUS-99999)

[Widescreen 16:9]
patch=1,EE,00115768,word,3c013fe3
EOF
cat > "$FIX/patches/SLPM-65499_11111111.pnach" <<EOF
gametitle=Bloody Roar 4 (J)(SLPM-65499)

[Widescreen 16:9]
patch=1,EE,00115768,word,3c013fe3
EOF
# Fase 3l: fixture duplikat CRC — dua file CRC game_d dengan serial
# yang tidak cocok (game_d.iso, boot SLUS-99999, ELF beda) -> AMBIGUOUS.
cat > "$FIX/patches/SLUS-20795_${CRC_D}.pnach" <<EOF
gametitle=Bloody Roar 4 (U)(SLUS-20795)

[Widescreen 16:9]
patch=1,EE,00115768,word,3c013fe3
EOF
cat > "$FIX/patches/SLPM-65499_${CRC_D}.pnach" <<EOF
gametitle=Bloody Roar 4 (J)(SLPM-65499)

[Widescreen 16:9]
patch=1,EE,00115768,word,3c013fe3
EOF
# Fase 3k: pnach user — 4 nama untuk game yang sama (Atelier Iris 3,
# SLUS-21564, CRC 4CCC9212; isi disalin dari file asli PCSX2), 1 file
# shadow MotoGP (CRC ELF palsu — harus menang atas system di scan),
# 1 file tanpa identitas (di-skip dari indeks), 1 bukan .pnach (di-skip).
rm -rf "$FIX/user_pnach"
mkdir -p "$FIX/user_pnach"
for n in "Atelier Iris 3 - Grand Phantasm.pnach" "4CCC9212.pnach" "SLUS-21564.pnach" "SLUS_21564.pnach"; do
  cp "$REAL_PATCHES/SLUS-21564_4CCC9212.pnach" "$FIX/user_pnach/$n"
done
cat > "$FIX/user_pnach/${CRC}.pnach" <<EOF
gametitle=Moto GP 4 (U)(SLUS-21159)

[Widescreen 16:9]
patch=1,EE,005624a4,word,3f400000 //3f800000
patch=1,EE,0055c608,word,43000000 //43a00000
EOF
cat > "$FIX/user_pnach/Random Name.pnach" <<EOF
gametitle=Some Random Game

[Widescreen 16:9]
patch=1,EE,00115768,word,3c013fe3
EOF
echo "bukan pnach" > "$FIX/user_pnach/notes.txt"
gcc -O1 -Wall -Wextra -I../src -o "$FIX/test_core" test_core.c \
    ../src/crc32.c ../src/iso9660.c ../src/elf.c ../src/patchfile.c \
    ../src/scan.c ../src/iso_patch.c ../src/convert.c ../src/report.c \
    ../src/configfile.c ../src/dbmerge.c
"$FIX/test_core" "$FIX" "$FIX/patches" "$REAL_PATCHES"

# Fase 2f: reader BIN mentah (sektor 2352) — ground truth XOR-fold
# dari make_realistic_iso.py.
REAL_OUT=$(python3 make_realistic_iso.py)
BIN_CRC=$(echo "$REAL_OUT" | grep "CRC game XOR-fold" | awk '{print $NF}')
python3 make_bin_iso.py "$FIX/realistic.iso" "$FIX/realistic.bin" >/dev/null
gcc -O1 -Wall -Wextra -I../src -o "$FIX/test_bin" test_bin.c \
    ../src/crc32.c ../src/iso9660.c ../src/elf.c
"$FIX/test_bin" "$FIX/realistic.bin" "$BIN_CRC"

# Fase 2j: scan ELF langsung DIHAPUS (brute force bikin PS3 hang) —
# tes DIRECT (ISO PVD-rusak) ikut dihapus; pembaca ISO9660 yang
# diperbaiki (tanpa ftell) yang menangani game-nya.

# Fase 2q: primitif gambar + simbol tombol (draw.c).
gcc -O1 -Wall -Wextra -I../src -o "$FIX/test_draw" test_draw.c \
    ../src/draw.c
"$FIX/test_draw"

# Fase 2: konverter + merge + operasi file toggle.
python3 make_convert_fixtures.py >/dev/null
gcc -O1 -Wall -Wextra -I../src -o "$FIX/test_convert" test_convert.c \
    ../src/patchfile.c ../src/configfile.c ../src/convert.c ../src/crc32.c \
    ../src/dbmerge.c ../src/elf.c ../src/iso9660.c ../src/iso_patch.c \
    ../src/report.c ../src/scan.c ../src/state.c ../src/toggle.c
"$FIX/test_convert"

# Fase 2t: ISO patch (burn-to-ISO) + backup .wsrest + restore +
# translasi per-blok BIN mentah. Bekerja di SALINAN realistic.iso/
# realistic.bin (file asli tidak pernah ditulis).
rm -rf "$FIX/isopatch"
mkdir -p "$FIX/isopatch/patches"
cp "$FIX/realistic.iso" "$FIX/isopatch/test.iso"
python3 make_bin_iso.py "$FIX/realistic.iso" "$FIX/isopatch/test.bin" >/dev/null
cat > "$FIX/isopatch/patches/SLUS-21159_${BIN_CRC}.pnach" <<EOF
gametitle=Test Game (U)(SLUS-21159)

[Widescreen 16:9]
patch=1,EE,00100100,word,3f400000
EOF
gcc -O1 -Wall -Wextra -I../src -o "$FIX/test_iso_patch" test_iso_patch.c \
    ../src/patchfile.c ../src/configfile.c ../src/convert.c ../src/crc32.c \
    ../src/dbmerge.c ../src/elf.c ../src/iso9660.c ../src/iso_patch.c \
    ../src/report.c ../src/scan.c ../src/state.c ../src/toggle.c
"$FIX/test_iso_patch" "$FIX/isopatch"
