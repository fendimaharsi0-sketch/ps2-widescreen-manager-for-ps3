/* CRC32 (IEEE, polinomial 0xEDB88320) — BUKAN CRC ELF PS2.
 * CRC ELF PS2 (yang dipakai database pnach PCSX2 / ManaGunZ /
 * webMAN untuk lookup) adalah XOR-fold, lihat ws_ps2crc di bawah.
 * Jangan pakai ws_crc32 untuk CRC game! */
#include "crc32.h"

static uint32_t table[256];
static int table_ready;

static void make_table(void)
{
    uint32_t i, j, c;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    table_ready = 1;
}

uint32_t ws_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    if (!table_ready) make_table();
    crc = ~crc;
    for (i = 0; i < len; i++)
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

uint32_t ws_crc32(const void *data, size_t len)
{
    return ws_crc32_update(0, data, len);
}

/* "CRC" ELF PS2 (ManaGunZ Get_PS2CRC + crc.py asahui, dipakai pnach
 * PCSX2 / webMAN untuk lookup): XOR semua word 32-bit little-endian
 * file ELF. BUKAN CRC32 IEEE — itu yang bikin semua game TIDAK ADA:
 * app menghitung CRC32 IEEE (MotoGP 4 FEB4A894) padahal database
 * memakai XOR-fold (MotoGP 4 AF7BEB14). Sisa byte < 4 di akhir
 * diabaikan (sama seperti ManaGunZ max=size/4 dan crc.py array).
 * Dibaca per byte (LE eksplisit) supaya hasilnya sama di host
 * little-endian maupun PPU big-endian (ManaGunZ mencapai hal yang
 * sama dengan XOR di BE + reverse32/byteswap di akhir). */
uint32_t ws_ps2crc(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    uint32_t crc = 0;
    size_t i;
    for (i = 0; i + 4 <= len; i += 4)
        crc ^= (uint32_t)p[i] | ((uint32_t)p[i + 1] << 8) |
               ((uint32_t)p[i + 2] << 16) | ((uint32_t)p[i + 3] << 24);
    return crc;
}
