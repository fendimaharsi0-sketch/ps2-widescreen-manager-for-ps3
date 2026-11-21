/* Scan /dev_hdd0/PS2ISO/ (atau direktori apa pun di host saat tes):
 * tiap *.iso dibaca ISO9660-nya, dicari boot ELF-nya, dihitung CRC32-nya,
 * lalu dicocokkan ke blob patch. Portabel. */
#ifndef WS_SCAN_H
#define WS_SCAN_H

#include <stddef.h>
#include <stdint.h>

#include "patchfile.h"

#define WS_MAX_GAMES 512

typedef enum {
    WS_AVAIL_YES = 0,   /* ada patch widescreen aktif */
    WS_AVAIL_NO,        /* CRC tidak ada di blob */
    WS_AVAIL_AMBIGUOUS, /* 2+ file untuk CRC ini, serial tidak cocok */
    WS_AVAIL_ERROR      /* ISO/ELF tidak bisa dibaca (lihat err) */
} ws_avail_t;

typedef struct {
    char iso_name[256];   /* nama file ISO */
    char title[256];      /* nama tampil: gametitle pnach, atau nama ISO */
    char title_id[16];    /* "SLUS-21159" atau "" */
    char serial_norm[12]; /* "SLUS21159" atau "" */
    uint32_t crc;         /* CRC32 file ELF boot */
    size_t elf_len;       /* ukuran byte ELF boot yang dibaca (0 kalau gagal) */
    uint32_t elf_entry;   /* e_entry dari header ELF (diagnostik) */
    char boot[64];        /* nama file boot ("SLUS_211.59") atau "" */
    char patch_file[128]; /* nama file .pnach game ini ("" kalau tidak ada);
                             * 128 karena nama file user bisa panjang
                             * ("Atelier Iris 3 - Grand Phantasm.pnach") */
    int serial_fallback;  /* 1 kalau patch_file dari fallback serial
                             * (Fase 3j): serial cocok, CRC beda */
    int user_patch;       /* 1 kalau patch_file dari folder user
                             * (Fase 3k): menang atas database system */
    int dup_count;        /* jumlah file kandidat kalau avail ==
                             * WS_AVAIL_AMBIGUOUS (Fase 3l): 2+ file
                             * untuk game ini, app tidak bisa memilih
                             * sendiri — user yang pilih (picker) */
    int ws_lines;         /* baris patch widescreen 16:9 aktif (lapis 1) */
    int n_lines;          /* semua baris patch di file (0 kalau tidak ada) */
    int n_conv;           /* baris patch=1 yang convertible (word /
                             * extended 0x2, bukan kondisional/sub-word) */
    int has_config;       /* file <ISO>.CONFIG ada di sebelah ISO */
    int iso_patched;      /* file <ISO>.wsrest ada: ISO sudah di-patch
                             * langsung (Fase 2t); crc di atas adalah CRC
                             * ASLI (direkonstruksi dari backup) */
    ws_avail_t avail;
    char err[128];        /* pesan kalau avail == WS_AVAIL_ERROR */
} ws_game_t;

typedef struct {
    ws_game_t games[WS_MAX_GAMES];
    int count;
} ws_scan_result_t;

/* Isi satu game dari satu file pnach (Fase 3l): baca file, hitung
 * baris patch, gametitle -> title, avail YES/NO. Dipakai scan_one
 * (sesudah lookup) dan picker duplikat (sesudah user memilih file).
 * Gagal baca -> avail ERROR + err, patch_file tidak diisi.
 * Kembalikan 0 kalau sukses, -1 kalau gagal. */
int ws_game_load_patch(ws_game_t *g, const char *patches_dir,
                       const char *patch_file);

/* Dipanggil sesudah tiap ISO selesai dibaca (done = jumlah ISO yang
   sudah diproses). Kembalikan bukan-0 untuk membatalkan scan
   (mis. tombol PS ditekan — app harus bisa keluar walau scan belum
   selesai). Boleh NULL. */
typedef int (*ws_scan_progress_t)(int done, void *ctx);

/* Scan direktori. Kembalikan 0 kalau direktori bisa dibuka (game yang
   gagal dibaca tetap masuk list dengan avail == WS_AVAIL_ERROR),
   1 kalau dibatalkan lewat progress, -1 kalau direktori gagal dibuka.
   pindex = indeks nama file patch (dibangun sekali dari folder patches);
   patches_dir = folder file .pnach (isi file dibaca satu-per-game).
   uindex = indeks pnach user (Fase 3k, boleh NULL = tidak ada pnach
   user); user_patches_dir = folder pnach user (boleh NULL). Lookup
   user jalan DULU — file user menang atas database system. */
int ws_scan_dir(const char *dir, ws_patch_index_t *pindex,
                const char *patches_dir, ws_user_index_t *uindex,
                const char *user_patches_dir, ws_scan_result_t *out,
                ws_scan_progress_t progress, void *ctx);

/* Hitung file ISO di direktori (Fase 2x): pass readdir saja, tanpa
 * baca isi file — total untuk progress bar scan. Kriteria sama
 * dengan ws_scan_dir (sufiks .iso, cap WS_MAX_GAMES). Kembalikan
 * jumlah, atau -1 kalau direktori gagal dibuka. */
int ws_count_iso_files(const char *dir);

/* "SLUS_211.59" -> "SLUS-21159". "SLUS_21159" -> "SLUS-21159". */
void ws_title_id(const char *boot_name, char *out, size_t out_size);

/* Cari nama file boot satu ISO: SYSTEM.CNF BOOT2 dulu, lalu file root
 * berbentuk serial. Kembalikan 0 kalau ketemu (nama di boot_out),
 * -1 kalau gagal (alasan di err). Dipakai scan dan ISO patch. */
int ws_boot_name(const char *dir, const char *iso_name,
                 char *boot_out, size_t boot_size,
                 char *err, size_t err_size);

/* Path file .CONFIG di sebelah ISO: "<dir>/<nama-iso-tanpa-ekstensi>.CONFIG". */
void ws_config_path(const char *dir, const char *iso_name,
                    char *out, size_t out_size);

/* Baca byte boot ELF dari satu ISO (dipakai toggle ON untuk old value).
 * *buf dimalloc; pemanggil yang free. Kembalikan 0 kalau sukses,
 * -1 kalau gagal (alasan di err). */
int ws_read_boot_elf(const char *dir, const char *iso_name,
                     unsigned char **buf, size_t *len,
                     char *err, size_t err_size);

#endif
