/* Parser CONFIG database + merge GUSUR (pnach selalu menang).
 * Port dari pnach2config.cpp (mergeDb + walkConfig + cmdSpec).
 * Murni logika word-array — tanpa file I/O. */
#ifndef WS_DBMERGE_H
#define WS_DBMERGE_H

#include <stddef.h>
#include <stdint.h>

#include "report.h"

#define WS_NETEMU_REV 17495
#define WS_MAX_PATCHES 32
#define WS_DB_SNAPSHOT "2026-11-02"

typedef struct {
    uint32_t addr;   /* word A: (mode << 28) | address — mode 0 dari pnach */
    uint32_t old;
    uint32_t value;
} ws_triple_t;

/* Satu command database: id + kata-katanya (menunjuk ke array words
 * milik pemanggil — jangan di-free per command). */
typedef struct {
    uint32_t id;
    const uint32_t *words;
    size_t nwords;
} ws_db_cmd_t;

/* Jalan di satu entry CONFIG database. Mengisi cmds (array malloc,
 * pemanggil yang free; words menunjuk ke `words_buf` yang juga malloc)
 * dan tail (menunjuk ke dalam data, dari word 0x00 sampai akhir).
 * Kembalikan 0 kalau sukses, -1 kalau tidak bisa di-parse aman
 * (pesan di err, maks err_size byte). */
int ws_db_walk(const unsigned char *data, size_t len,
               ws_db_cmd_t **cmds_out, int *n_cmds,
               uint32_t **words_buf,
               const unsigned char **tail_out, size_t *tail_len,
               char *err, size_t err_size);

/* Merge GUSUR. db_data = isi entry database (sudah ketemu lewat
 * configdb); db_sub = "NET"/"CUSTOM", db_display = nama file entry
 * (mis. "SLUS_203.14.CONFIG") untuk pesan.
 * Kembalikan 1 kalau merge jalan (out_words malloc + tail menunjuk ke
 * dalam db_data), 0 kalau jatuh ke patch-only (alasan di report).
 * out_words dimalloc; pemanggil yang free. */
int ws_db_merge(const ws_triple_t *pnach, int n_pnach,
                const unsigned char *db_data, size_t db_len,
                const char *db_sub, const char *db_display,
                ws_report_t *report,
                uint32_t **out_words, size_t *out_nwords,
                const unsigned char **tail, size_t *tail_len);

/* Baca balik file .CONFIG (hasil konverter atau webMAN): kembalikan
 * alamat-alamat triple 0x0a (masked 0x0FFFFFFF, tanpa duplikat) dalam
 * `addrs` (maks max_addrs). Dipakai editor patch untuk mengingat
 * pilihan user — .CONFIG adalah satu-satunya state. Kembalikan jumlah
 * alamat, atau -1 kalau file tidak bisa dibaca/di-parse (pemanggil
 * jatuh ke default widescreen-only). */
int ws_config_triple_addrs(const char *path, uint32_t *addrs, int max_addrs);

/* Watermark Fase 3c: ekor file bikinan app = [0x00][TitleID 10 byte]
 * [extra DB, kalau ada]["WSM1"]. Cari "WSM1" di ekor mulai offset 14
 * (sesudah 0x00 + title). Kembalikan 1 kalau ada, 0 kalau tidak,
 * -1 kalau file tidak bisa dibaca/di-parse (pemanggil anggap BUKAN
 * buatan app — arah aman). */
int ws_config_has_watermark(const char *path);

#endif
