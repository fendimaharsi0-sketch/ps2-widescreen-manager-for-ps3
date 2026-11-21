#!/usr/bin/env python3
"""Bangun ISO PS2 REALISTIS untuk tes reader app (Fase 2b debug).

Meniru disc PS2 asli — yang TIDAK dimiliki ISO sintetis Fase 1:
- root dir dengan ~120 file (span beberapa sektor, bukan 1 sektor)
- SYSTEM.CNF format asli (CRLF)
- boot ELF ~2 MB (bukan 1 KB), ditaruh sebagai entri root TERAKHIR
- subdirektori berisi file (kayak disc asli: IOP, dll.)
- PVD app ID "PLAYSTATION"

Juga mencetak CRC32 ELF yang benar (zlib) sebagai ground truth.
"""
import binascii
import os
import random
import struct

SECTOR = 2048
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "fixtures", "realistic.iso")


def dir_record(extent, size, flags, name: bytes):
    name_len = len(name)
    rec_len = 33 + name_len
    if rec_len % 2:
        rec_len += 1
    r = bytearray(rec_len)
    r[0] = rec_len
    struct.pack_into("<I", r, 2, extent)
    struct.pack_into(">I", r, 6, extent)
    struct.pack_into("<I", r, 10, size)
    struct.pack_into(">I", r, 14, size)
    r[25] = flags
    struct.pack_into("<H", r, 28, 1)
    struct.pack_into(">H", r, 30, 1)
    r[32] = name_len
    r[33:33 + name_len] = name
    return bytes(r)


def build_dir(entries):
    """entries: list (name_bytes, extent, size, flags). Kembalikan bytes
    direktori (dipad ke kelipatan sektor)."""
    data = bytearray()
    for name, extent, size, flags in entries:
        data += dir_record(extent, size, flags, name)
    pad = (-len(data)) % SECTOR
    data += b"\x00" * pad
    return bytes(data)


def main():
    random.seed(42)
    # --- boot ELF ~2 MB: header ELF32 LE MIPS yang BENAR (bukan cuma
    # magic) + 1 PT_LOAD menutupi seluruh file + payload acak. Header
    # yang benar penting: fallback baca ELF langsung (Fase 2g) memvalidasi
    # program header sebelum menerima ELF sebagai boot ELF. ---
    ELF_SIZE = 2 * 1024 * 1024
    elf = bytearray(ELF_SIZE)
    elf[0:4] = b"\x7fELF"
    elf[4] = 1  # 32-bit
    elf[5] = 1  # little-endian
    elf[6] = 1  # ELF version 1
    # e_type=2 EXEC, e_machine=8 MIPS, e_version=1, e_entry=0x00100008,
    # e_phoff=52, e_shoff=0, e_flags=0, e_ehsize=52, e_phentsize=32,
    # e_phnum=1, e_shentsize=0, e_shnum=0, e_shstrndx=0
    struct.pack_into("<HHIIIIIHHHHHH", elf, 16,
                     2, 8, 1, 0x00100008, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    # PT_LOAD di offset 52: p_type=1, p_offset=0, p_vaddr/p_paddr=
    # 0x00100000, p_filesz/p_memsz=ukuran file, p_flags=5 (R+X),
    # p_align=0x1000.
    struct.pack_into("<IIIIIIII", elf, 52,
                     1, 0, 0x00100000, 0x00100000,
                     ELF_SIZE, ELF_SIZE, 5, 0x1000)
    elf[84:] = random.randbytes(ELF_SIZE - 84)
    elf = bytes(elf)
    elf_crc = binascii.crc32(elf) & 0xFFFFFFFF
    # CRC game PS2 = XOR-fold word LE (ManaGunZ/crc.py) — ini ground
    # truth untuk app; CRC32 IEEE hanya referensi.
    x = 0
    for i in range(0, len(elf) - 3, 4):
        x ^= struct.unpack_from("<I", elf, i)[0]
    print("ELF size: %d  CRC32 IEEE (referensi): %08X  CRC game XOR-fold: %08X"
          % (len(elf), elf_crc, x))

    cnf = b"BOOT2 = cdrom0:\\SLUS_211.59;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"

    # --- isi file: 120 file dummy di root + 2 subdir + CNF + ELF terakhir ---
    files = []  # (nama_iso, bytes, is_dir_content?)
    for i in range(120):
        files.append(("FILE%04d.DAT;1" % i, os.urandom(100 + i)))
    sub1 = [(("IOPMOD%02d.IRX;1" % i), os.urandom(500)) for i in range(8)]
    sub2 = [(("DATA%02d.BIN;1" % i), os.urandom(300)) for i in range(5)]

    # --- alokasi sektor: 0-15 kosong, 16 PVD, lalu root (n sektor),
    # subdir, lalu data file. Root dir dihitung dulu dari nama (panjang
    # record hanya tergantung nama, bukan extent) supaya tidak tabrakan
    # dengan subdir/data (bug builder v1: subdir menimpa sektor root). ---
    root_names = ([b"\x00", b"\x01", b"SYSTEM.CNF;1"]
                  + [n.encode() for n, _ in files]
                  + [b"MODULES", b"DATA", b"SLUS_211.59;1"])
    root_size = sum(33 + len(n) + ((33 + len(n)) % 2)
                    for n in root_names)
    root_size = (root_size + SECTOR - 1) // SECTOR * SECTOR
    root_sectors = root_size // SECTOR
    root_extent = 17
    sub1_extent = root_extent + root_sectors
    sub2_extent = sub1_extent + 1
    sector = sub2_extent + 1
    extents = {}

    def alloc(name, content):
        nonlocal sector
        extents[name] = (sector, len(content))
        nsec = (len(content) + SECTOR - 1) // SECTOR
        sector += nsec
        return nsec

    alloc("SYSTEM.CNF;1", cnf)
    for name, content in files:
        alloc(name, content)
    for name, content in sub1:
        alloc(name, content)
    for name, content in sub2:
        alloc(name, content)
    alloc("SLUS_211.59;1", elf)  # boot ELF = entri root TERAKHIR

    # --- direktori ---
    root_entries = [(b"\x00", root_extent, root_size, 2),
                    (b"\x01", root_extent, root_size, 2)]
    root_entries.append((b"SYSTEM.CNF;1", extents["SYSTEM.CNF;1"][0],
                         extents["SYSTEM.CNF;1"][1], 0))
    for name, _ in files:
        e, s = extents[name]
        root_entries.append((name.encode(), e, s, 0))
    root_entries.append((b"MODULES", sub1_extent, SECTOR, 2))
    root_entries.append((b"DATA", sub2_extent, SECTOR, 2))
    e, s = extents["SLUS_211.59;1"]
    root_entries.append((b"SLUS_211.59;1", e, s, 0))
    root_data = build_dir(root_entries)
    assert len(root_data) == root_size, (len(root_data), root_size)
    print("root dir: %d entri, %d byte (%d sektor)"
          % (len(root_entries), root_size, root_sectors))

    sub1_entries = [(b"\x00", sub1_extent, SECTOR, 2),
                    (b"\x01", root_extent, root_size, 2)]
    for name, _ in sub1:
        e, s = extents[name]
        sub1_entries.append((name.encode(), e, s, 0))
    sub2_entries = [(b"\x00", sub2_extent, SECTOR, 2),
                    (b"\x01", root_extent, root_size, 2)]
    for name, _ in sub2:
        e, s = extents[name]
        sub2_entries.append((name.encode(), e, s, 0))

    # --- PVD ---
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    struct.pack_into("<H", pvd, 120, SECTOR)  # logical block size LE
    struct.pack_into(">H", pvd, 122, SECTOR)
    pvd[574:574 + 11] = b"PLAYSTATION"
    root_rec = dir_record(root_extent, root_size, 2, b"\x00")
    pvd[156:156 + len(root_rec)] = root_rec

    # --- rakit file ISO ---
    total_sectors = sector
    iso = bytearray(total_sectors * SECTOR)
    iso[16 * SECTOR:17 * SECTOR] = pvd
    iso[root_extent * SECTOR:root_extent * SECTOR + root_size] = root_data
    iso[sub1_extent * SECTOR:sub1_extent * SECTOR + SECTOR] = build_dir(sub1_entries)
    iso[sub2_extent * SECTOR:sub2_extent * SECTOR + SECTOR] = build_dir(sub2_entries)
    for name, content in ([("SYSTEM.CNF;1", cnf)] + files + sub1 + sub2
                          + [("SLUS_211.59;1", elf)]):
        e, _s = extents[name]
        iso[e * SECTOR:e * SECTOR + len(content)] = content
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(iso)
    print("ISO ditulis: %s (%d byte, %d sektor)" % (OUT, len(iso), total_sectors))


if __name__ == "__main__":
    main()
