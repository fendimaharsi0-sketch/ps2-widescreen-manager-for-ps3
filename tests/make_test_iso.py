#!/usr/bin/env python3
"""Membangun ISO PS2 sintetis untuk tes host core (Fase 1).

ISO9660 minimal tapi nyata: PVD di sektor 16, root directory record,
file SYSTEM.CNF + satu ELF palsu. Empat varian:
  game_a.iso — normal: SYSTEM.CNF menunjuk BOOT2 ke SLUS_211.59
  game_b.iso — tanpa SYSTEM.CNF: boot ELF ketemu lewat nama serial
  game_c.iso — boot file bukan ELF (jalur error)
  game_d.iso — boot SLUS_999.99, ELF varian (CRC beda dari game_a/b,
    jadi file shadow user tidak menutupinya): serialnya tidak cocok
    dengan file patch mana pun yang CRC-nya sama (Fase 3l) — lookup
    ambiguous
Juga menghitung CRC32 ELF palsu (binascii) untuk dipakai ekspektasi tes.
"""
import binascii
import os
import struct
import sys

SECTOR = 2048
FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")


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


def build_iso(path, files, system_cnf):
    """files: list (nama_iso, bytes). system_cnf: bytes atau None."""
    # Alokasi sektor: 0-15 kosong, 16 PVD, 17 root dir, 18+ data.
    sector = 18
    extents = {}
    data = bytearray()
    items = []
    if system_cnf is not None:
        items.append(("SYSTEM.CNF;1", system_cnf))
    items.extend(files)
    for name, content in items:
        extents[name] = (sector, len(content))
        data += content
        data += b"\x00" * ((-len(content)) % SECTOR)
        sector += (len(content) + SECTOR - 1) // SECTOR
    root_extent = 17
    root = bytearray(SECTOR)
    off = 0
    for name, size in ((b"\x00", SECTOR), (b"\x01", SECTOR)):
        rec = dir_record(root_extent, SECTOR, 2, name)
        root[off:off + len(rec)] = rec
        off += len(rec)
    for name, content in items:
        extent, size = extents[name]
        rec = dir_record(extent, size, 0, name.encode())
        root[off:off + len(rec)] = rec
        off += len(rec)
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    rec = dir_record(root_extent, SECTOR, 2, b"\x00")
    pvd[156:156 + len(rec)] = rec
    iso = bytearray(16 * SECTOR) + pvd + root + data
    with open(path, "wb") as f:
        f.write(iso)
    return extents


def fake_elf():
    e = bytearray(1024)
    e[0:4] = b"\x7fELF"
    e[4] = 1  # ELFCLASS32
    e[5] = 1  # little-endian
    e[6] = 1  # version
    for i in range(52, 1024):
        e[i] = i & 0xFF
    return bytes(e)


def fake_elf_d():
    """Varian ELF buat game_d: isi beda -> CRC (XOR-fold) beda, jadi
    file shadow user (CRC game_a) tidak menutupi game_d."""
    e = bytearray(1024)
    e[0:4] = b"\x7fELF"
    e[4] = 1  # ELFCLASS32
    e[5] = 1  # little-endian
    e[6] = 1  # version
    for i in range(52, 1024):
        e[i] = (i * 3 + 7) & 0xFF
    return bytes(e)


def xor_fold(elf):
    x = 0
    for i in range(0, len(elf) - 3, 4):
        x ^= struct.unpack_from("<I", elf, i)[0]
    return x


def main():
    os.makedirs(os.path.join(FIX, "ps2iso"), exist_ok=True)
    elf = fake_elf()
    crc = binascii.crc32(elf) & 0xFFFFFFFF
    print(f"fake ELF CRC32 IEEE: {crc:08X} (bukan CRC game — hanya referensi)")
    # CRC game PS2 = XOR-fold word LE (ManaGunZ/crc.py): ini yang
    # dipakai app + ekspektasi tes.
    x = xor_fold(elf)
    print(f"fake ELF CRC game (XOR-fold): {x:08X}")
    with open(os.path.join(FIX, "fake_elf_crc.txt"), "w") as f:
        f.write(f"{x:08X}\n")
    elf_d = fake_elf_d()
    x_d = xor_fold(elf_d)
    print(f"fake ELF CRC game D (XOR-fold): {x_d:08X}")
    with open(os.path.join(FIX, "fake_elf_crc_d.txt"), "w") as f:
        f.write(f"{x_d:08X}\n")
    cnf = b"BOOT2 = cdrom0:\\SLUS_211.59;1\nVER = 1.00\n"
    build_iso(os.path.join(FIX, "ps2iso", "game_a.iso"),
              [("SLUS_211.59;1", elf)], cnf)
    build_iso(os.path.join(FIX, "ps2iso", "game_b.iso"),
              [("SLUS_211.59;1", elf)], None)
    build_iso(os.path.join(FIX, "ps2iso", "game_c.iso"),
              [("SLUS_211.59;1", b"bukan ELF sama sekali")], cnf)
    cnf_d = b"BOOT2 = cdrom0:\\SLUS_999.99;1\nVER = 1.00\n"
    build_iso(os.path.join(FIX, "ps2iso", "game_d.iso"),
              [("SLUS_999.99;1", elf_d)], cnf_d)
    with open(os.path.join(FIX, "ps2iso", "bukan_iso.txt"), "w") as f:
        f.write("ini bukan ISO\n")
    print("fixtures written to", FIX)


if __name__ == "__main__":
    main()
