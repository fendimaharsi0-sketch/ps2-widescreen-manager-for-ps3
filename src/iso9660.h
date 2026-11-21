/* Pembaca ISO9660 minimal (read-only, portabel).
 *
 * Hanya yang dibutuhkan aplikasi ini: buka image, baca PVD (sektor 16),
 * jalan dari root directory record, baca file lewat path ("SYSTEM.CNF",
 * "SLUS_211.59"). Tidak pakai path table; direktori dibaca record demi
 * record (record tidak pernah melewati batas blok data 2048 byte).
 *
 * Nama dicocokkan case-insensitive dan tanpa sufiks versi ";1".
 *
 * Format image: ISO polos (sektor 2048) MAUPUN mentah BIN (sektor 2352
 * / 2336 / 2448, data 2048 byte mulai +16 (Mode 1) atau +24 (Mode 2) di
 * tiap sektor) — deteksi ukuran sektor ala ManaGunZ get_SectorSize:
 * coba ukuran {2048, 2352, 2336, 2448} x offset data {0, 16, 24}, cari
 * tanda PVD di sektor 16. Pembacaan data selalu lewat read_data() yang
 * paham celah antar sektor, jadi file/direktori terbaca benar di kedua
 * format. Kalau PVD tetap tidak ketemu, scan.c punya fallback baca ELF
 * langsung dari byte mentah (tidak peduli format ISO).
 */
#ifndef WS_ISO9660_H
#define WS_ISO9660_H

#include <stddef.h>
#include <stdio.h>

#define WS_ISO_SECTOR 2048

typedef struct {
    FILE *f;
    unsigned root_extent;   /* LBA */
    unsigned root_size;     /* byte */
    unsigned sector_size;   /* 2048 (ISO) / 2352 / 2336 / 2448 (BIN mentah) */
    unsigned data_off;      /* 0 atau 24: offset data 2048 byte di tiap sektor */
} ws_iso_t;

int ws_iso_open(ws_iso_t *iso, const char *path);
void ws_iso_close(ws_iso_t *iso);

/* Buka untuk baca + tulis (ISO patch, Fase 2t). Sama seperti
 * ws_iso_open, tapi file dibuka "rb+" supaya write_data bisa
 * menulis. Kembalikan 0 kalau PVD ketemu, -1 kalau tidak. */
int ws_iso_open_rw(ws_iso_t *iso, const char *path);

/* Baca seluruh isi file di dalam ISO. *out dimalloc; pemanggil yang free.
   Kembalikan 0 kalau sukses, -1 kalau tidak ketemu / gagal baca. */
int ws_iso_read_file(ws_iso_t *iso, const char *path,
                     unsigned char **out, size_t *out_len);

/* Resolusi path ke extent (LBA) + ukuran file, tanpa baca isinya.
   Kembalikan 0 kalau ketemu, -1 kalau tidak. Dipakai ISO patch
   (Fase 2t) untuk posisi byte file di dalam image. */
int ws_iso_file_offset(ws_iso_t *iso, const char *path,
                       unsigned *extent_out, unsigned *size_out);

/* Tulis `len` byte DATA mulai dari offset data `off` di dalam file /
 * direktori yang bermula di `extent` — cerminan read_data (translasi
 * per-blok yang sama, jadi benar di ISO polos maupun BIN mentah;
 * jangan pernah fseek(flat extent*S + off) untuk tulis — itu bug
 * ManaGunZ di BIN mentah). Kembalikan 0 kalau semua byte tertulis,
 * -1 kalau tidak. */
int ws_iso_write_data(ws_iso_t *iso, unsigned extent, unsigned off,
                      const unsigned char *buf, size_t len);

/* List entri (nama, tanpa ";1") di direktori path ("", "/" = root).
   Nama ditulis ke names[i] (maks 64 byte). Kembalikan jumlah entri,
   atau -1 kalau gagal. */
int ws_iso_list_dir(ws_iso_t *iso, const char *path,
                    char names[][64], int max_names);

#endif
