#!/usr/bin/env bash
# Build PS2 Widescreen Manager (.pkg) dengan toolchain lokal.
# Pola pkg diambil dari fork (ps3/build.sh), disederhanakan: tanpa core,
# tanpa shader (tidak ada cgcomp di VM — app ini tidak pakai shader),
# tanpa SPU. Toolchain lokal = GCC 7.2, jadi TIDAK butuh patch clobber
# RSX yang dipakai fork di CI (patch itu untuk GCC 11+).
#
#   ./build.sh [nama-paket]   -> build/<nama-paket>.pkg
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
NAME=${1:-ps2-widescreen-manager}
export PS3DEV=${PS3DEV:-$HOME/ps3dev-image/rootfs/opt/ps3dev}
export PSL1GHT=${PSL1GHT:-$PS3DEV}
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH
# sfo/pkg: image lokal hanya punya sfo.py/pkg.py Python 2 (python2 tidak
# ada di VM). Pakai tool C yang dibuild dari sumber PSL1GHT
# (~/workspace/psl1ght-src/tools/sfo_pkg -> ~/workspace/ps3-tools/bin).
# Stock tool cukup: pkg ini cuma ~7 file, jauh di bawah batas MAX_FILES.
HOSTTOOLS=${HOSTTOOLS:-$HOME/workspace/ps3-tools/bin}
SFO=${SFO:-$HOSTTOOLS/sfo}
PKG=${PKG:-$HOSTTOOLS/pkg}
command -v ppu-gcc >/dev/null || { echo "ppu-gcc tidak ketemu di PATH"; exit 1; }
[ -x "$SFO" ] || { echo "sfo tidak ketemu di $SFO"; exit 1; }
[ -x "$PKG" ] || { echo "pkg tidak ketemu di $PKG"; exit 1; }

B=$ROOT/build
rm -rf "$B/pkg"
mkdir -p "$B/obj" "$B/pkg/USRDIR/data"

echo "== compile"
CF="-O2 -Wall -Wextra -Wno-unused-parameter -mcpu=cell -I$PSL1GHT/ppu/include -Isrc"
OBJS=""
for f in src/*.c; do
    o=$B/obj/$(basename "$f" .c).o
    ppu-gcc $CF -c "$f" -o "$o"
    OBJS="$OBJS $o"
done

echo "== link"
ppu-gcc -mcpu=cell -Wl,--gc-sections -L$PSL1GHT/ppu/lib $OBJS \
    -lrsx -lgcm_sys -lio -lsysutil -lsysmodule -lrt -llv2 -lm \
    -o "$B/$NAME.elf"
ppu-strip "$B/$NAME.elf" -o "$B/${NAME}_stripped.elf"
sprxlinker "$B/${NAME}_stripped.elf"

echo "== pkg"
CID=UP0001-PS2W00001_00-0000000000000001
make_self_npdrm "$B/${NAME}_stripped.elf" "$B/pkg/USRDIR/EBOOT.BIN" $CID >/dev/null
test -s "$B/pkg/USRDIR/EBOOT.BIN" || { echo "ERROR: EBOOT.BIN kosong"; exit 1; }
"$SFO" --title "PS2 Widescreen Manager" --appid PS2W00001 \
    -f "$HOSTTOOLS/sfo.xml" "$B/pkg/PARAM.SFO" >/dev/null
cp "$ROOT/data/ICON0.PNG" "$B/pkg/ICON0.PNG"
cp "$ROOT/data/PIC1.PNG" "$B/pkg/PIC1.PNG"
# Data per-file (pelajaran cheat fork): 4719 file .pnach + 1549 file
# .CONFIG disalin apa adanya, BUKAN digabung jadi blob. Tool `pkg` C
# sudah dinaikkan MAX_FILES-nya 1024 -> 8192 (sfo_pkg/pkg.c) supaya
# muat 6268 file data ini.
# Default = folder data/ di repo (data/patches ikut di repo; data/
# configdb BELUM ada di repo karena file .CONFIG itu biner — lihat
# README "Where the patch data comes from"). Override lewat env,
# mis. PATCHES_SRC=/jalan/ke/patches CONFIGDB_SRC=/jalan/ke/database.
PATCHES_SRC=${PATCHES_SRC:-$ROOT/data/patches}
CONFIGDB_SRC=${CONFIGDB_SRC:-$ROOT/data/configdb}
[ -d "$PATCHES_SRC" ] || { echo "ERROR: folder patches tidak ketemu: $PATCHES_SRC"; echo "Isi dulu data/patches/ dari snapshot PCSX2 (lihat README, 'Where the patch data comes from')."; exit 1; }
[ -d "$CONFIGDB_SRC" ] || { echo "ERROR: folder configdb tidak ketemu: $CONFIGDB_SRC"; echo "Isi dulu data/configdb/ dengan folder NET + CUSTOM dari database PS2CONFIG webMAN (lihat README)."; exit 1; }
# Folder data/ di repo sengaja kosong (.gitkeep saja) — user mengisinya
# dulu dari sumbernya (lihat README). Jangan gagal dengan pesan cp
# yang samar kalau foldernya masih kosong.
n_pnach=$(ls "$PATCHES_SRC"/*.pnach 2>/dev/null | wc -l)
[ "$n_pnach" -gt 0 ] || { echo "ERROR: data/patches/ kosong — download dulu snapshot PCSX2 (lihat README, 'Where the patch data comes from')."; exit 1; }
mkdir -p "$B/pkg/USRDIR/data/patches" "$B/pkg/USRDIR/data/configdb"
cp "$PATCHES_SRC"/*.pnach "$B/pkg/USRDIR/data/patches/"
cp -r "$CONFIGDB_SRC/NET" "$CONFIGDB_SRC/CUSTOM" "$B/pkg/USRDIR/data/configdb/"
echo "data: $(ls "$B/pkg/USRDIR/data/patches" | wc -l) pnach, $(find "$B/pkg/USRDIR/data/configdb" -type f | wc -l) CONFIG"
"$PKG" -c $CID "$B/pkg/" "$B/$NAME.pkg" >/dev/null
ls -la "$B/$NAME.pkg"
"$PKG" --list "$B/$NAME.pkg"
