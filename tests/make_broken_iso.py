#!/usr/bin/env python3
"""Buat ISO 'PVD rusak' dari ISO polos: corupsi tanda PVD di semua
lokasi yang dicek deteksi sektor (ukuran {2048,2352,2336,2448} x
offset {0,16,24} di sektor 16), supaya ws_iso_open gagal. Root dir,
SYSTEM.CNF, dan ELF dibiarkan utuh — simulasi ISO hasil konversi CD
/ wrapper aneh yang PVD-nya tidak kebaca. Fallback baca ELF langsung
(Fase 2g) harus tetap menemukan boot ELF + BOOT2 dari byte mentah.
"""
import sys

SIZES = (2048, 2352, 2336, 2448)
OFFS = (0, 16, 24)


def main(src, dst):
    with open(src, "rb") as f:
        data = bytearray(f.read())
    n = 0
    for s in SIZES:
        for o in OFFS:
            pos = s * 16 + o
            data[pos:pos + 8] = b"\x00" * 8
            n += 1
    with open(dst, "wb") as f:
        f.write(data)
    print("ISO PVD-rusak ditulis: %s (%d lokasi tanda PVD dikorupsi)"
          % (dst, n))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
