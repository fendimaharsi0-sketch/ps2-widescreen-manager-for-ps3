/* Checksum ELF PS2. PERHATIAN: ws_crc32 (IEEE) BUKAN cara PCSX2
 * menghitung CRC game — komentar lama di file ini salah, dan itu
 * yang bikin semua game TIDAK ADA (Fase 2b). CRC game PCSX2 /
 * ManaGunZ / webMAN adalah ws_ps2crc (XOR-fold, lihat crc32.c).
 * Portabel: tidak ada dependensi platform. */
#ifndef WS_CRC32_H
#define WS_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t ws_crc32(const void *data, size_t len);
uint32_t ws_crc32_update(uint32_t crc, const void *data, size_t len);

/* "CRC" ELF PS2 untuk lookup pnach: XOR semua word 32-bit
 * little-endian file ELF (sisa byte < 4 di akhir diabaikan). */
uint32_t ws_ps2crc(const void *data, size_t len);

#endif
