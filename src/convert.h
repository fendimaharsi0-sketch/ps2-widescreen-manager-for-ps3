/* Konverter pnach -> CONFIG (command 0x0a mode 0) + merge database.
 * Port dari pnach2config.cpp (parsePnach + convert), tanpa file I/O:
 * teks pnach + byte ELF + entry database masuk sebagai buffer, hasil
 * keluar sebagai byte CONFIG + laporan. Nama file output diputuskan
 * pemanggil (di PS3: <ISO>.CONFIG next-to-ISO). */
#ifndef WS_CONVERT_H
#define WS_CONVERT_H

#include <stddef.h>
#include <stdint.h>

#include "configfile.h"
#include "dbmerge.h"
#include "report.h"

typedef struct {
    uint32_t addr;      /* alamat EE sesudah & 0x0FFFFFFF */
    uint64_t raw_addr;  /* alamat mentah (untuk prefix extended / E) */
    char ptype[16];     /* "word"/"short"/"byte"/"extended"/lainnya */
    int size;           /* byte efektif, atau -1 kalau tipe tidak dikenal */
    uint32_t value;
    int has_old;        /* old dari komentar */
    uint32_t old;
    int lineno;
} ws_patch_t;

#define WS_MAX_PNACH_PATCHES 256

typedef struct {
    char title_id[16];  /* bentuk database "SLUS_211.59", atau "" */
    ws_patch_t patches[WS_MAX_PNACH_PATCHES];
    int n_patches;
} ws_pnach_t;

/* Parse teks pnach. Warning parse (hex rusak, value > 32-bit) masuk
 * report dengan prefix "warning: ". Kembalikan 0 kalau teks bisa dibaca
 * (patch bisa nol — itu ditangani convert), -1 kalau teks kosong. */
int ws_parse_pnach(const char *text, size_t len, ws_pnach_t *out,
                   ws_report_t *report);

/* Ukuran efektif tulis satu baris patch: word 4, short 2, byte 1,
 * extended dari prefix alamat (0x2 -> 4, 0x1 -> 2, selainnya 1).
 * -1 kalau tipe tidak dikenal. Dipakai konverter dan ISO patch
 * (Fase 2t). */
int ws_patch_effective_size(const char *ptype, uint64_t raw_addr);

/* Jalur lengkap: parse pnach, selesaikan old value (komentar dulu, lalu
 * ELF dari ISO), konversi ke triple, merge dengan entry database
 * (kecuali no_merge), rakit byte CONFIG.
 *
 * elf boleh NULL (tanpa file game — old yang tidak ada di komentar
 * akan di-skip). configdb_dir boleh NULL (tanpa merge); kalau diisi,
 * entry CONFIG untuk game itu dibaca per-file (satu file kecil) di
 * dalam convert. title_fallback = Title ID dari scan (bentuk dash
 * "SLUS-21159") kalau gametitle tidak memuat serial. elf_name = nama
 * file game untuk pesan (mis. nama ISO).
 *
 * Kembalikan 0 kalau ada output (*out malloc, *out_len byte),
 * -1 kalau tidak ada yang bisa ditulis (alasan di report).
 * *n_triples = jumlah patch pnach yang masuk output (untuk status UI),
 * *merged = 1 kalau database ikut ter-merge. */
/* Prioritas sumber old value (Fase 2s, toggle Select, session-only):
 * old value dari komentar // di pnach vs dari boot ELF. Patch-first
 * = perilaku lama (komentar dulu, ELF fallback); elf-first = ELF
 * dulu, komentar fallback. Tidak ada yang dikarang — kalau dua
 * sumber tidak ada, patch tetap di-skip + warning. */
typedef enum {
    WS_OLD_PATCH_FIRST = 0,
    WS_OLD_ELF_FIRST = 1
} ws_old_prio_t;

int ws_convert(const char *pnach_text, size_t pnach_len,
               const unsigned char *elf, size_t elf_len,
               const char *elf_name,
               const char *configdb_dir, int no_merge,
               ws_old_prio_t old_prio,
               const char *title_fallback,
               unsigned char **out, size_t *out_len,
               int *n_triples, int *merged,
               ws_report_t *report);

#endif
