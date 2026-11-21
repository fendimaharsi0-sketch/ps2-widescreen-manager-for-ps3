#!/usr/bin/env python3
"""Fixture tes konverter Fase 2: ELF sintetis + pnach tes (tanpa //old,
supaya old value HARUS dibaca dari ELF) + output ekspektasi dari tool
Python (pnach2config.py) yang dijalankan di sini.

Kasus:
  motogp / br4 / downhill — patch-only (tanpa entry DB)
  slus_203.14 — merge: 0x09 digusur, 0x20 + 0x46 dipertahankan
  slus_213.26 — merge: 2 triple 0x0a DB dipertahankan, overlay 0x42 dipertahankan
  slus_211.52 — merge: overlay + triple 0x0a + 0x4b + ekor Title ID
  bss          — alamat BSS -> old 0
  over32       — 33 patch -> error keras (tidak ada file)
  noold        — 1 patch tanpa old + tanpa ELF -> error (tidak ada file)
"""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "convert")
PATCHES_DIR = "/home/hatch/workspace/pcsx2_patches_check/pcsx2_patches-main/patches"
PYTOOL = "/home/hatch/workspace/pnach2config/pnach2config.py"

# CRC pnach -> nama kasus
CASES = {
    0xAF7BEB14: "motogp",
    0xC9F6F222: "br4",
    0x5AE01D98: "downhill",
}
# Title ID (bentuk database) -> nama kasus merge
MERGE_CASES = {
    "SLUS_203.14": "slus_203.14",
    "SLUS_213.26": "slus_213.26",
    "SLUS_211.52": "slus_211.52",
    "SCUS_971.11": "scus_971.11",
}


def load_pnach_texts():
    # Per-file (pelajaran cheat fork): baca langsung file .pnach dari
    # snapshot PCSX2, CRC diambil dari 8 hex terakhir nama file.
    texts = {}
    for name in os.listdir(PATCHES_DIR):
        if not name.lower().endswith(".pnach"):
            continue
        try:
            crc = int(name[-14:-6], 16)
        except ValueError:
            continue
        with open(os.path.join(PATCHES_DIR, name), "r",
                  encoding="utf-8", errors="replace") as f:
            texts[crc] = f.read()
    return texts


def strip_patch_comments(text):
    """Buang //komentar dari baris patch saja (gametitle/group tetap)."""
    out = []
    for line in text.splitlines():
        if line.strip().startswith("patch=") and "//" in line:
            line = line[: line.index("//")].rstrip()
        out.append(line)
    return "\n".join(out) + "\n"


def patch_addrs(text):
    addrs = set()
    for line in text.splitlines():
        s = line.strip()
        if not s.startswith("patch="):
            continue
        parts = s.split(",")
        if len(parts) < 4:
            continue
        try:
            addrs.add(int(parts[2].strip(), 16) & 0x0FFFFFFF)
        except ValueError:
            pass
    return addrs


def build_elf(path, addrs):
    """ELF32 LE: seg 0 PT_LOAD vaddr 0..0x1000000 (file-backed),
    seg 1 PT_LOAD vaddr 0x01F00000 filesz 0x1000 memsz 0x20000 (BSS).
    Seg 0 sengaja mentok di 16 MB supaya alamat BSS 0x01F05000 hanya
    tercakup seg 1 (old harus 0, bukan kata file).
    Tiap alamat tes diisi old sintetis 0x11000000 + i*0x101."""
    ehsize = 52
    phnum = 2
    phoff = ehsize
    seg0_off = 0x1000
    seg0_size = 0x01000000
    seg1_off = seg0_off + seg0_size
    seg1_filesz = 0x1000
    total = seg1_off + seg1_filesz
    buf = bytearray(total)
    # e_ident
    buf[0:4] = b"\x7fELF"
    buf[4] = 1  # 32-bit
    buf[5] = 1  # LE
    buf[6] = 1  # version
    struct.pack_into("<HHIIIIIHHHHHH", buf, 16,
                     2, 8, 1, 0x100000, phoff, 0, 0,
                     ehsize, 32, phnum, 0, 0, 0)
    # PH0: PT_LOAD vaddr 0
    struct.pack_into("<IIIIIIII", buf, phoff,
                     1, seg0_off, 0, 0, seg0_size, seg0_size, 7, 0x1000)
    # PH1: PT_LOAD vaddr 0x01F00000, BSS 0x01F01000..0x01F20000
    struct.pack_into("<IIIIIIII", buf, phoff + 32,
                     1, seg1_off, 0x01F00000, 0x01F00000,
                     seg1_filesz, 0x20000, 7, 0x1000)
    for i, a in enumerate(sorted(addrs)):
        if a < seg0_size:
            struct.pack_into("<I", buf, seg0_off + a, 0x11000000 + i * 0x101)
    with open(path, "wb") as f:
        f.write(buf)
    print(f"synthetic ELF: {total} bytes, {len(addrs)} old words")


def main():
    os.makedirs(OUT, exist_ok=True)
    texts = load_pnach_texts()
    # Cari teks merge dari serial di baris gametitle SAJA
    by_title = {}
    for crc, text in texts.items():
        gt = ""
        for line in text.splitlines():
            if line.strip().startswith("gametitle="):
                gt = line.strip()[10:]
                break
        m = re.search(r"\(?([A-Za-z]{4})[-_](\d{3})\.?(\d{2})\)?", gt)
        if m:
            tid = f"{m.group(1).upper()}_{m.group(2)}.{m.group(3)}"
            by_title[tid] = text
    jobs = {}
    for crc, name in CASES.items():
        jobs[name] = strip_patch_comments(texts[crc])
    for tid, name in MERGE_CASES.items():
        jobs[name] = strip_patch_comments(by_title[tid])
    # bss: satu patch di wilayah BSS seg 1 (serial fiktif, tanpa entry DB)
    jobs["bss"] = ("gametitle=BSS Test (SLUS-99999)\n\n"
                   "[Widescreen 16:9]\n"
                   "patch=1,EE,01F05000,word,12345678\n")
    # over32: 33 patch word dengan komentar old (cap keras, tanpa ELF)
    lines = ["gametitle=Over32 Test (SLUS-21159)", "", "[Widescreen 16:9]"]
    for i in range(33):
        lines.append(f"patch=1,EE,001000{i:02X},word,00000001 //00000000")
    jobs["over32"] = "\n".join(lines) + "\n"
    # noold: 1 patch tanpa old, tanpa ELF
    jobs["noold"] = ("gametitle=NoOld Test (SLUS-21159)\n\n"
                     "[Widescreen 16:9]\n"
                     "patch=1,EE,00115768,word,3c013fe3\n")
    # Kumpulkan semua alamat yang butuh old dari ELF
    addrs = set()
    for name, text in jobs.items():
        if name in ("over32", "noold"):
            continue
        addrs |= patch_addrs(text)
    addrs.add(0x01F05000)  # BSS (tidak ditulis — wilayah BSS)
    elf_path = os.path.join(OUT, "synthetic.elf")
    build_elf(elf_path, addrs)
    # Tulis pnach + jalankan tool Python sebagai ekspektasi
    for name, text in jobs.items():
        p = os.path.join(OUT, name + ".pnach")
        with open(p, "w") as f:
            f.write(text)
    exp_dir = os.path.join(OUT, "expected")
    os.makedirs(exp_dir, exist_ok=True)
    for name in ["motogp", "br4", "downhill", "slus_203.14",
                 "slus_213.26", "slus_211.52", "scus_971.11", "bss"]:
        p = os.path.join(OUT, name + ".pnach")
        r = subprocess.run([sys.executable, PYTOOL, p, elf_path,
                            "--out", exp_dir],
                           capture_output=True, text=True,
                           cwd="/home/hatch/workspace/pnach2config")
        print(f"--- {name}: rc={r.returncode}")
        print(r.stdout.strip())
        if r.stderr.strip():
            print("STDERR:", r.stderr.strip())
    # KOREKSI downhill: tool Python TIDAK punya perbaikan E-prefix (itu
    # perbaikan port C++): baris kondisional E0030002 ikut terkonversi
    # jadi triple di alamat 0x00030002. Kode C (port C++) men-skip-nya
    # dengan benar — kondisional PCSX2 bukan tulis memori, menulisnya
    # sebagai poke bisa merusak kode game. Jadi ekspektasi = output
    # Python minus triple itu + count diperbaiki.
    dpath = os.path.join(exp_dir, "downhill.CONFIG")
    d = bytearray(open(dpath, "rb").read())
    words = struct.unpack(f"<{len(d)//4}I", d)
    assert words[0] == 0x3D and words[2] == 0x0A
    count = words[3]
    triples = [words[4 + i * 3:7 + i * 3] for i in range(count)]
    kept = [t for t in triples if t[0] != 0x00030002]
    assert len(kept) == count - 1, "triple E0030002 tidak ketemu"
    out = struct.pack("<IIII", 0x3D, 17495, 0x0A, len(kept))
    for t in kept:
        out += struct.pack("<III", *t)
    open(dpath, "wb").write(out)
    print(f"--- downhill: ekspektasi dikoreksi ({count} -> {len(kept)} "
          "triple, baris kondisional E dihapus)")
    for name in ["over32", "noold"]:
        p = os.path.join(OUT, name + ".pnach")
        r = subprocess.run([sys.executable, PYTOOL, p,
                            "--out", exp_dir],
                           capture_output=True, text=True,
                           cwd="/home/hatch/workspace/pnach2config")
        print(f"--- {name}: rc={r.returncode} (diharapkan gagal)")
        print(r.stdout.strip())


if __name__ == "__main__":
    main()
