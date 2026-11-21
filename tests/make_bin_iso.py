#!/usr/bin/env python3
"""Ubah ISO polos (sektor 2048) jadi BIN mentah (sektor 2352, Mode 2).

Tiap sektor data 2048 byte dibungkus jadi sektor mentah 2352 byte:
  [0:12]  sync (00 FF*10 00)
  [12:16] header MSF (diisi 0 — reader tidak memakainya)
  [16:24] subheader (0 — reader tidak memakainya)
  [24:2072] data 2048 byte
  [2072:2352] EDC + reserved + ECC (0 — reader tidak memakainya)

Dipakai untuk menguji reader BIN di app (Fase 2f): 17 game Fendi yang
ERROR "bukan ISO9660" diduga format mentah seperti ini.
"""
import os
import sys

SECTOR = 2048
RAW = 2352
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


def to_bin(src, dst):
    with open(src, "rb") as f:
        data = f.read()
    assert len(data) % SECTOR == 0, "ISO polos harus kelipatan 2048"
    out = bytearray()
    for i in range(0, len(data), SECTOR):
        out += SYNC
        out += b"\x00" * 4      # header MSF
        out += b"\x00" * 8      # subheader
        out += data[i:i + SECTOR]
        out += b"\x00" * (RAW - 24 - SECTOR)  # EDC/reserved/ECC
    with open(dst, "wb") as f:
        f.write(out)
    print("BIN ditulis: %s (%d byte, %d sektor mentah)"
          % (dst, len(out), len(data) // SECTOR))


if __name__ == "__main__":
    to_bin(sys.argv[1], sys.argv[2])
