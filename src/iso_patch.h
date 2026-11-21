/* ISO patch (Fase 2t): tulis patch pnach LANGSUNG ke file ISO
 * (burn-to-ISO, pola ManaGunZ apply_pnach, minus dua hazard-nya).
 * Beda dari .CONFIG: direct write — tidak ada gate old value, tidak
 * ada batasan word-only (byte/short ikut di-apply, Fendi 2026-11-12
 * 05:40 WIB); backup = file .wsrest next-to-ISO, restore = tulis
 * old bytes balik. Portabel: hanya stdio + string.
 *
 * Format .wsrest: record 12 byte, urutan file (endian-aman —
 * offset/size ditulis little-endian eksplisit, old_bytes byte
 * mentah):
 *   [0..3]  offset    (u32 LE, offset file di dalam boot ELF)
 *   [4..7]  size      (u32 LE, 1/2/4)
 *   [8..11] old_bytes (byte mentah file, hanya `size` yang valid)
 * Ukuran file harus kelipatan 12; kalau tidak = corrupt.
 * Offset disimpan relatif terhadap file ELF boot (bukan posisi host
 * di ISO) — jadi rekonstruksi CRC di scan dan restore sama-sama
 * tidak peduli format sektor image. */
#ifndef WS_ISO_PATCH_H
#define WS_ISO_PATCH_H

#include <stddef.h>
#include <stdint.h>

#include "report.h"

#define WS_WSREST_REC_SIZE 12
#define WS_WSREST_MAX_RECS 256

typedef struct {
    uint32_t offset;   /* offset file di dalam boot ELF */
    uint32_t size;     /* 1, 2, atau 4 */
    unsigned char old_bytes[4]; /* byte mentah, hanya size yang valid */
} ws_wsrest_rec_t;

/* Path file backup di sebelah ISO: "<dir>/<nama-iso-tanpa-ekstensi>.wsrest". */
void ws_wsrest_path(const char *dir, const char *iso_name,
                    char *out, size_t out_size);

/* Baca file .wsrest ke recs (maks max_recs). Kembalikan jumlah record
 * (>= 0), -1 kalau file tidak ada / tidak bisa dibuka, -2 kalau
 * corrupt (ukuran bukan kelipatan 12, record size bukan 1/2/4,
 * atau record melebihi max_recs). */
int ws_wsrest_read(const char *path, ws_wsrest_rec_t *recs, int max_recs);

/* Tulis record backup ke file .wsrest (overwrite). Kembalikan 0
 * kalau sukses, -1 kalau gagal. */
int ws_wsrest_write(const char *path, const ws_wsrest_rec_t *recs,
                    int n_recs);

/* Hitung berapa patch yang akan ditulis / di-skip, tanpa menulis
 * apa-apa (untuk layar konfirmasi). Kembalikan 0 kalau ada yang akan
 * ditulis, 1 kalau sudah ke-patch (semua byte sudah sama), -1 kalau
 * tidak ada yang bisa di-apply / gagal (alasan di report).
 * *n_write = yang akan ditulis, *n_skip = yang di-skip (termasuk
 * yang sudah sama nilainya). */
int ws_iso_patch_preview(const char *dir, const char *iso_name,
                         const char *pnach_text, size_t pnach_len,
                         ws_report_t *report, int *n_write, int *n_skip);

/* Terapkan teks pnach ke ISO (buka rb+, tulis langsung).
 * Hanya baris patch=1 yang aktif; per baris: size dari
 * ws_patch_effective_size; skip kondisional 0xE, tipe tidak dikenal,
 * alamat >= 0x2000000, dan alamat BSS / di luar byte file ELF.
 * Tulis tepat `size` byte (low byte nilai, little-endian) — tidak
 * pernah flat 4 byte (itu bug ManaGunZ).
 * Kalau semua byte sudah sama dengan nilai baru (sudah ke-patch),
 * kembalikan 1 (already patched, tidak ada yang ditulis).
 * Gagal di tengah jalan -> semua yang sudah tertulis di-restore dari
 * backup di memori, file .wsrest TIDAK ditulis, ISO tidak ditinggal
 * setengah ke-patch. Sukses -> .wsrest ditulis next-to-ISO.
 * *n_applied = jumlah patch yang ditulis, *n_skipped = yang di-skip
 * (kondisional / tipe / alamat / BSS / sudah sama nilainya).
 * Kembalikan 0 sukses, 1 sudah ke-patch, -1 gagal (alasan di report). */
int ws_iso_patch_apply(const char *dir, const char *iso_name,
                       const char *pnach_text, size_t pnach_len,
                       ws_report_t *report, int *n_applied, int *n_skipped);

/* Restore ISO dari .wsrest: tulis old bytes balik, lalu hapus file
 * .wsrest. Gagal tulis di tengah -> .wsrest TIDAK dihapus (bisa
 * coba lagi). Kembalikan 0 sukses, -1 gagal (alasan di report). */
int ws_iso_patch_restore(const char *dir, const char *iso_name,
                         ws_report_t *report);

#endif
