/* Patch widescreen per-file (pelajaran cheat fork pcsx_rearmed):
 * SATU file .pnach per game (nama <SERIAL>_<CRC>.pnach atau
 * <CRC>.pnach, persis seperti di repo PCSX2) — BUKAN satu blob
 * 3,4 MB yang dimuat utuh ke memori. Yang dimuat saat startup
 * hanyalah INDEKS nama file (CRC + serial, ~140 KB); isi file
 * dibaca hanya untuk game yang sedang diproses (satu file kecil,
 * langsung di-free sesudah dipakai), persis seperti fork memuat
 * satu file .cht per game dari ps3/data/cheats/ (4497 file, 30 MB
 * total, tapi yang dimuat cuma file game yang sedang jalan).
 * Portabel: hanya stdio + dirent + memori. */
#ifndef WS_PATCHFILE_H
#define WS_PATCHFILE_H

#include <stddef.h>
#include <stdint.h>

#define WS_PATCH_MAX_FILES 8192

typedef struct {
    uint32_t crc;
    char serial_norm[12]; /* alnum saja, uppercase: "SLUS21159" ("" kalau CRC-only) */
    char filename[64];    /* nama file, mis. "SLUS-21159_AF7BEB14.pnach" */
} ws_patch_entry_t;

typedef struct {
    ws_patch_entry_t *entries;
    size_t count;
} ws_patch_index_t;

/* Indeks pnach USER (Fase 3k, Fendi): file .pnach di
 * /dev_hdd0/PS2ISO/pnach/ — suplai user sendiri, prioritas di atas
 * database system. BEDA dengan indeks system: nama file user bebas
 * apa saja ("Atelier Iris 3 - Grand Phantasm.pnach", "4CCC9212.pnach",
 * "SLUS-21564.pnach", "SLUS_21564.pnach" semuanya sah), jadi indeks
 * ini MEMBACA ISI file saat load — serial dari gametitle di dalam
 * file ("(SLUS-21564)"), CRC dari token hex di nama file. Folder user
 * kecil (punya user sendiri), jadi baca semua di startup masih murah;
 * indeks system (4719 file) tetap nama-saja. File tanpa serial DAN
 * tanpa CRC tidak bisa dicocokkan ke game mana pun — di-skip. */
#define WS_USER_PATCH_MAX_FILES 512

typedef struct {
    uint32_t crc;
    int has_crc; /* 1 kalau nama file mengandung token hex 7-8 digit */
    char serial_norm[12]; /* "" kalau tidak ketemu */
    char filename[128]; /* nama file apa adanya */
} ws_user_patch_entry_t;

typedef struct {
    ws_user_patch_entry_t *entries;
    size_t count;
} ws_user_index_t;

/* Bangun indeks user dari `dir` (baca isi tiap *.pnach).
 * Kembalikan 0 kalau direktori bisa dibuka, -1 kalau tidak ada
 * (folder user tidak ada = tidak ada pnach user, bukan error). */
int ws_user_index_load(ws_user_index_t *index, const char *dir);
void ws_user_index_free(ws_user_index_t *index);

/* Bangun indeks dari nama-nama file *.pnach di `dir` (hanya nama,
 * isi file TIDAK dibaca). Kembalikan 0 kalau direktori bisa dibuka. */
int ws_patch_index_load(ws_patch_index_t *index, const char *dir);
void ws_patch_index_free(ws_patch_index_t *index);

/* Normalisasi serial: "SLUS-21159" / "SLUS_211.59" -> "SLUS21159". */
void ws_serial_norm(const char *serial, char *out, size_t out_size);

typedef enum {
    WS_LOOKUP_NOT_FOUND = 0,  /* tidak ada file untuk CRC ini */
    WS_LOOKUP_FOUND,          /* satu file cocok */
    WS_LOOKUP_AMBIGUOUS,      /* 2+ file, tidak ada yang cocok serialnya */
    WS_LOOKUP_SERIAL_ONLY,    /* fallback terakhir: tidak ada file untuk
                                 * CRC ini, tapi tepat satu file serialnya
                                 * cocok (CRC beda — revisi lain). */
    WS_LOOKUP_USER            /* file user menang (Fase 3k): cocok serial
                                 * ATAU CRC di indeks user — prioritas di
                                 * atas SEMUA hasil system, walau system
                                 * punya CRC yang persis cocok. */
} ws_lookup_status_t;

/* Cari NAMA FILE pnach untuk (crc, serial_norm game). Serial game
 * boleh "". Aturan (sama seperti blob dulu): (serial, CRC) persis
 * dulu, lalu file CRC-only (tanpa serial), lalu satu-satunya file
 * untuk CRC ini, lalu ambiguous. Fallback TERAKHIR (Fase 3j, Fendi):
 * kalau TIDAK ADA file untuk CRC ini sama sekali, cari file yang
 * serialnya cocok (CRC beda — revisi lain): tepat satu -> pakai
 * (WS_LOOKUP_SERIAL_ONLY); 2+ -> ambiguous; 0 -> not found.
 * Fallback ini TIDAK jalan di jalur ambiguous CRC: kalau ada kandidat
 * CRC tapi tak bisa dipilih, hasilnya tetap ambiguous.
 * *filename_out diisi nama file (tanpa path). */
ws_lookup_status_t ws_patch_lookup(ws_patch_index_t *index, uint32_t crc,
                                   const char *serial_norm,
                                   char *filename_out, size_t filename_size);

/* Cari file pnach USER untuk (crc, serial_norm game). Aturan LONGGAR
 * (Fase 3k, Fendi): serial cocok ATAU CRC cocok sudah cukup — keduanya
 * tidak wajib. Peringkat: keduanya cocok (3) > CRC saja (2) > serial
 * saja (1). Seri di peringkat yang sama dimenangkan nama file yang
 * alfabetis pertama (deterministik, tidak tergantung urutan readdir).
 * Serial game "" tetap bisa cocok lewat CRC. *filename_out diisi nama
 * file (tanpa path). */
ws_lookup_status_t ws_user_lookup(ws_user_index_t *index, uint32_t crc,
                                  const char *serial_norm,
                                  char *filename_out, size_t filename_size);

/* Header pnach (Fase 3l): gametitle= + comment= + baris // SEBELUM
 * grup pertama — di sanalah penjelasan file berada (mis. "(English
 * patch CRC)" di gametitle Front Mission 5, atau comment= yang
 * menjelaskan patch). Baris patch / isi grup TIDAK ikut. Isi lines
 * (maks max_lines baris, tiap baris maks 127 karakter + NUL);
 * kembalikan jumlah baris (0 kalau tidak ada). */
#define WS_PATCH_HEADER_MAX_LINES 6
#define WS_PATCH_HEADER_LINE_LEN 128
int ws_patch_header(const char *text, size_t len,
                    char lines[][WS_PATCH_HEADER_LINE_LEN], int max_lines);

/* Semua file kandidat untuk satu game (Fase 3l): yang CRC-nya cocok
 * dulu — kalau ada, itulah kandidatnya; kalau tidak ada sama sekali,
 * yang serialnya cocok. Dipakai picker duplikat + hitung dup_count
 * saat scan. files diisi (maks max_files) dan diurutkan alfabetis;
 * files == NULL -> cuma hitung. Kembalikan JUMLAH total kandidat
 * (bisa > max_files); 0 = tidak ada. */
int ws_patch_candidates(ws_patch_index_t *index, uint32_t crc,
                        const char *serial_norm,
                        char files[][128], int max_files);

/* Baca isi SATU file pnach (dir + filename). *out dimalloc; pemanggil
 * yang free. Kembalikan 0 kalau sukses, -1 kalau gagal. */
int ws_patch_read_file(const char *dir, const char *filename,
                       char **out, size_t *out_len);

/* Satu baris teks (bukan NUL-terminated). */
typedef struct { const char *s; size_t n; } ws_text_line_t;

/* Klasifikasi grup patch — filter dua lapis (Fase 2p, Fendi):
 * lapis 1 (16:9) menang atas lapis 2 (wide generik). Lapis 1 =
 * nama grup mengandung bentuk rasio 16:9 ("16:9", "16/9", "16_9",
 * "16 9"). Lapis 2 = mengandung "wide" (mencakup "widescreen",
 * "wide screen", "wide view"). Kembalikan 2 = widescreen 16:9,
 * 1 = widescreen rasio lain/tidak jelas, 0 = bukan widescreen.
 * shortcut: "16 9" (spasi) dan "wide" substring bisa false-positive
 * di nama grup aneh ("Widen headlight beam" ikut lapis 2) — tidak
 * berpengaruh: cuma lapis 1 yang auto-centang. */
int ws_group_kind(const char *group);

/* Baris patch=1 aktif di bawah header widescreen 16:9 (lapis 1,
   tanpa baris komentar, tanpa gsaspectratio). Mengembalikan jumlah
   baris (maks max_lines); lines[i] menunjuk ke dalam text (jangan
   di-free). */
int ws_ws_lines(const char *text, size_t len,
                ws_text_line_t lines[], int max_lines);

/* Baris gametitle= aktif pertama ("" kalau tidak ada). */
void ws_gametitle(const char *text, size_t len, char *out, size_t out_size);

/* Satu baris patch (patch=1 atau patch=0) beserta grupnya — untuk
 * editor patch (Triangle). `line` = baris penuh sesudah di-strip
 * (termasuk komentar `//`, karena konverter baca old value dari
 * situ), NUL-terminated, maks 255 karakter (baris terpanjang di
 * database 206 karakter). `enabled` = patch=1 di file. `ws_kind` =
 * klasifikasi grup (ws_group_kind): 2 = widescreen 16:9, 1 =
 * widescreen rasio lain/tidak jelas, 0 = bukan widescreen. */
#define WS_MAX_PATCH_ITEMS 1024
typedef struct {
    char group[48];   /* nama grup, mis. "Widescreen 16:9" */
    char line[256];   /* baris patch penuh, sudah di-strip */
    int enabled;      /* 1 = patch=1, 0 = patch=0 */
    int ws_kind;      /* 2 = widescreen 16:9, 1 = wide lain, 0 = bukan wide */
    uint32_t addr;    /* alamat EE (raw & 0x0FFFFFFF), untuk cocokkan .CONFIG */
    int convertible;  /* 1 kalau konverter bisa menulisnya (word, atau
                         * extended prefix 0x2; bukan kondisional 0xE,
                         * bukan sub-word, alamat < 0x2000000) */
} ws_patch_item_t;

/* Semua baris patch=1/patch=0 di text (semua grup, urutan file).
 * Mengembalikan jumlah (maks max_items); baris komentar, baris
 * kosong, dan baris non-patch di-skip. */
int ws_patch_list(const char *text, size_t len,
                  ws_patch_item_t items[], int max_items);

/* Bangun teks pnach baru: baris gametitle= dari text + baris-baris
 * patch yang checked[i] != 0 (urutan file). Teks ini yang diberikan
 * ke ws_convert — konverter tidak peduli grup, jadi header grup
 * tidak perlu ikut. *out dimalloc; pemanggil yang free. Kembalikan
 * 0 kalau sukses. */
int ws_patch_filtered_text(const char *text, size_t len,
                           const ws_patch_item_t *items,
                           const int *checked, int n,
                           char **out, size_t *out_len);

#endif
