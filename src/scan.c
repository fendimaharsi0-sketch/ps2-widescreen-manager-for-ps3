/* Scan direktori ISO PS2. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "crc32.h"
#include "elf.h"
#include "iso9660.h"
#include "iso_patch.h"
#include "scan.h"

void ws_title_id(const char *boot_name, char *out, size_t out_size)
{
    /* Bentuk masukan: "SLUS_211.59", "SLUS_21159", "SLUS-21159". */
    char tmp[32];
    size_t i, o = 0;
    strncpy(tmp, boot_name, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    /* Buang sufiks versi ";1" kalau ada. */
    {
        char *s = strchr(tmp, ';');
        if (s) *s = '\0';
    }
    /* "SLUS_211.59" -> "SLUS-21159": '_' jadi '-', '.' dibuang. */
    for (i = 0; tmp[i] && o + 1 < out_size; i++) {
        if (tmp[i] == '_') out[o++] = '-';
        else if (tmp[i] == '.') continue;
        else out[o++] = tmp[i];
    }
    out[o] = '\0';
}

/* Cari nilai BOOT2 di SYSTEM.CNF: "BOOT2 = cdrom0:\SLUS_211.59;1".
   Hasil: nama file boot ("SLUS_211.59"). */
static int parse_boot2(const unsigned char *cnf, size_t len,
                       char *boot, size_t boot_size)
{
    const char *p = (const char *)cnf, *e = p + len;
    while (p < e) {
        const char *nl = memchr(p, '\n', (size_t)(e - p));
        size_t linelen = nl ? (size_t)(nl - p) : (size_t)(e - p);
        if (linelen > 6 && strncasecmp(p, "BOOT2", 5) == 0) {
            const char *eq = memchr(p, '=', linelen);
            const char *v, *bs, *semi;
            size_t n;
            if (!eq) return -1;
            v = eq + 1;
            while (v < p + linelen && isspace((unsigned char)*v)) v++;
            /* "cdrom0:\SLUS_211.59;1" -> ambil setelah '\' terakhir. */
            bs = v;
            {
                const char *q;
                for (q = v; q < p + linelen; q++)
                    if (*q == '\\' || *q == ':') bs = q + 1;
            }
            semi = memchr(bs, ';', (size_t)((p + linelen) - bs));
            n = semi ? (size_t)(semi - bs) : (size_t)((p + linelen) - bs);
            while (n && isspace((unsigned char)bs[n - 1])) n--;
            if (n == 0 || n > boot_size - 1) return -1;
            memcpy(boot, bs, n);
            boot[n] = '\0';
            return 0;
        }
        p = nl ? nl + 1 : e;
    }
    return -1;
}

/* Nama file root yang bentuknya serial: "SLUS_211.59" (huruf+angka). */
static int looks_like_serial(const char *name)
{
    size_t n = strlen(name);
    int i;
    if (n != 11) return 0;
    for (i = 0; i < 4; i++)
        if (!isalpha((unsigned char)name[i])) return 0;
    if (name[4] != '_') return 0;
    for (i = 5; i < 8; i++)
        if (!isdigit((unsigned char)name[i])) return 0;
    if (name[8] != '.') return 0;
    return isdigit((unsigned char)name[9]) && isdigit((unsigned char)name[10]);
}

static int has_iso_suffix(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".iso") == 0;
}

static int cmp_game(const void *a, const void *b)
{
    return strcasecmp(((const ws_game_t *)a)->title,
                      ((const ws_game_t *)b)->title);
}

void ws_config_path(const char *dir, const char *iso_name,
                    char *out, size_t out_size)
{
    /* "MotoGP 4.iso" -> "<dir>/MotoGP 4.CONFIG". */
    char base[256];
    size_t n = strlen(iso_name);
    const char *dot = strrchr(iso_name, '.');
    if (dot && dot != iso_name) n = (size_t)(dot - iso_name);
    if (n > sizeof(base) - 1) n = sizeof(base) - 1;
    memcpy(base, iso_name, n);
    base[n] = '\0';
    snprintf(out, out_size, "%s/%s.CONFIG", dir, base);
}

/* ---- Fallback Fase 2g: baca ELF langsung dari file mentah ----
 * Dipakai kalau ws_iso_open gagal (PVD tidak ketemu: ISO hasil konversi
 * CD, BIN multi-track, wrapper aneh, dsb.). Tidak peduli format ISO:
 * pindai byte mentah cari magic ELF, validasi header + program header,
 * pilih ELF valid TERBESAR (boot ELF jauh lebih besar dari modul IOP),
 * baca byte-nya secara berurutan dari posisi magic. Serial dicari dari
 * "BOOT2" di byte mentah (best effort). Catatan: pembacaan berurutan ini
 * benar kalau data ELF-nya berurutan di file (ISO 2048); kalau image-nya
 * mentah BIN dengan PVD yang juga rusak, CRC bisa salah (tapi tidak
 * crash — paling TIDAK ADA, sama seperti ERROR sebelumnya). */

/* Cari nama file boot satu ISO: SYSTEM.CNF BOOT2 dulu, lalu file root
 * berbentuk serial. Dipakai scan (baca ELF) dan ISO patch (Fase 2t,
 * butuh nama boot + extent). Kembalikan 0 kalau ketemu (nama di
 * boot_out), -1 kalau gagal (alasan di err). */
int ws_boot_name(const char *dir, const char *iso_name,
                 char *boot_out, size_t boot_size,
                 char *err, size_t err_size)
{
    char path[1024];
    ws_iso_t iso;
    unsigned char *cnf = NULL;
    size_t cnf_len = 0;
    char boot[64] = "";
    static char names[64][64];
    int n_names, i;
    snprintf(path, sizeof(path), "%s/%s", dir, iso_name);
    if (ws_iso_open(&iso, path) != 0) {
        snprintf(err, err_size, "not ISO9660");
        return -1;
    }
    if (ws_iso_read_file(&iso, "SYSTEM.CNF", &cnf, &cnf_len) == 0)
        parse_boot2(cnf, cnf_len, boot, sizeof(boot));
    if (!boot[0]) {
        n_names = ws_iso_list_dir(&iso, "/", names, 64);
        for (i = 0; i < n_names; i++) {
            if (looks_like_serial(names[i])) {
                strncpy(boot, names[i], sizeof(boot) - 1);
                boot[sizeof(boot) - 1] = '\0';
                break;
            }
        }
    }
    free(cnf);
    ws_iso_close(&iso);
    if (!boot[0]) {
        snprintf(err, err_size, "boot ELF not found");
        return -1;
    }
    strncpy(boot_out, boot, boot_size - 1);
    boot_out[boot_size - 1] = '\0';
    return 0;
}

/* Baca boot ELF satu ISO: buka ISO, cari boot (SYSTEM.CNF BOOT2 dulu,
 * lalu file root berbentuk serial), baca + validasi ELF. Nama boot
 * dikembalikan di boot_out (untuk Title ID). */
static int read_boot_elf(const char *dir, const char *iso_name,
                         unsigned char **elf, size_t *elf_len,
                         char *boot_out, size_t boot_size,
                         char *err, size_t err_size)
{
    char path[1024];
    ws_iso_t iso;
    char boot[64] = "";

    *elf = NULL;
    *elf_len = 0;
    if (ws_boot_name(dir, iso_name, boot, sizeof(boot), err, err_size)
        != 0)
        return -1;
    /* Nama boot dicatat dulu supaya pemanggil tahu file apa yang dicoba
       dibaca, walau bacanya gagal (diagnostik). */
    strncpy(boot_out, boot, boot_size - 1);
    boot_out[boot_size - 1] = '\0';
    snprintf(path, sizeof(path), "%s/%s", dir, iso_name);
    if (ws_iso_open(&iso, path) != 0) {
        snprintf(err, err_size, "not ISO9660");
        return -1;
    }
    if (ws_iso_read_file(&iso, boot, elf, elf_len) != 0) {
        ws_iso_close(&iso);
        snprintf(err, err_size, "ELF not readable");
        return -1;
    }
    ws_iso_close(&iso);
    if (!ws_elf_check(*elf, *elf_len)) {
        free(*elf);
        *elf = NULL;
        *elf_len = 0;
        snprintf(err, err_size, "not a 32-bit ELF");
        return -1;
    }
    return 0;
}

int ws_read_boot_elf(const char *dir, const char *iso_name,
                     unsigned char **buf, size_t *len,
                     char *err, size_t err_size)
{
    char boot[64];
    return read_boot_elf(dir, iso_name, buf, len, boot, sizeof(boot), err,
                         err_size);
}

int ws_game_load_patch(ws_game_t *g, const char *patches_dir,
                       const char *patch_file)
{
    char *text = NULL;
    size_t text_len = 0;
    static ws_patch_item_t items[WS_MAX_PATCH_ITEMS];
    int i, n_items, n_ws = 0, n_conv = 0;
    char gametitle[256] = "";
    /* Isi file dibaca hanya untuk game ini (satu file kecil), lalu
       di-free — bukan seluruh database. Hitung: total baris patch,
       widescreen 16:9 (lapis 1, yang menentukan avail), dan baris
       patch=1 yang convertible. */
    if (ws_patch_read_file(patches_dir, patch_file, &text,
                           &text_len) != 0) {
        g->avail = WS_AVAIL_ERROR;
        snprintf(g->err, sizeof(g->err), "patch %s not readable",
                 patch_file);
        return -1;
    }
    n_items = ws_patch_list(text, text_len, items,
                            WS_MAX_PATCH_ITEMS);
    for (i = 0; i < n_items; i++) {
        if (items[i].enabled && items[i].ws_kind == 2) n_ws++;
        if (items[i].enabled && items[i].convertible) n_conv++;
    }
    ws_gametitle(text, text_len, gametitle, sizeof(gametitle));
    free(text);
    strncpy(g->patch_file, patch_file, sizeof(g->patch_file) - 1);
    g->patch_file[sizeof(g->patch_file) - 1] = '\0';
    g->n_lines = n_items;
    g->ws_lines = n_ws;
    g->n_conv = n_conv;
    g->avail = n_ws > 0 ? WS_AVAIL_YES : WS_AVAIL_NO;
    if (gametitle[0]) {
        strncpy(g->title, gametitle, sizeof(g->title) - 1);
        g->title[sizeof(g->title) - 1] = '\0';
    }
    return 0;
}

static void scan_one(const char *dir, const char *iso_name,
                     ws_patch_index_t *pindex, const char *patches_dir,
                     ws_user_index_t *uindex,
                     const char *user_patches_dir,
                     ws_game_t *g)
{
    char cfg_path[1024];
    FILE *cfgf;
    unsigned char *elf = NULL;
    size_t elf_len = 0;
    char boot[64] = "";
    char patch_file[128] = "";
    const char *patch_dir = patches_dir;
    ws_lookup_status_t st;
    char err[128];

    memset(g, 0, sizeof(*g));
    strncpy(g->iso_name, iso_name, sizeof(g->iso_name) - 1);
    /* Nama tampil default: nama ISO tanpa .iso. */
    {
        size_t n = strlen(iso_name);
        if (n > 4) n -= 4;
        if (n > sizeof(g->title) - 1) n = sizeof(g->title) - 1;
        memcpy(g->title, iso_name, n);
        g->title[n] = '\0';
    }
    /* Ada .CONFIG di sebelah ISO? (status toggle, dicek untuk semua game). */
    ws_config_path(dir, iso_name, cfg_path, sizeof(cfg_path));
    cfgf = fopen(cfg_path, "rb");
    g->has_config = cfgf != NULL;
    if (cfgf) fclose(cfgf);

    if (read_boot_elf(dir, iso_name, &elf, &elf_len, boot, sizeof(boot),
                      err, sizeof(err)) != 0) {
        g->avail = WS_AVAIL_ERROR;
        snprintf(g->err, sizeof(g->err), "%s", err);
        /* Nama boot tetap dicatat (diagnostik) walau baca gagal. */
        if (boot[0]) {
            strncpy(g->boot, boot, sizeof(g->boot) - 1);
            g->boot[sizeof(g->boot) - 1] = '\0';
            ws_title_id(boot, g->title_id, sizeof(g->title_id));
            ws_serial_norm(g->title_id, g->serial_norm,
                           sizeof(g->serial_norm));
        }
        return;
    }
    if (boot[0]) {
        ws_title_id(boot, g->title_id, sizeof(g->title_id));
        ws_serial_norm(g->title_id, g->serial_norm,
                       sizeof(g->serial_norm));
    }
    g->crc = ws_ps2crc(elf, elf_len);  /* CRC game = XOR-fold, bukan CRC32 IEEE */
    g->elf_len = elf_len;
    /* Fase 2t: game yang sudah di-ISO-patch — .wsrest ada di sebelah
     * ISO. CRC di atas adalah CRC dari ELF yang SUDAH ke-patch; untuk
     * lookup pnach butuh CRC ASLI. Rekonstruksi: tulis old bytes dari
     * backup balik ke buffer ELF (di memori saja, file tidak
     * disentuh), lalu hitung ulang. Restore digerakkan oleh ada
     * tidaknya .wsrest, bukan oleh lookup. */
    {
        char wsrest_path[1024];
        static ws_wsrest_rec_t recs[WS_WSREST_MAX_RECS];
        int n_recs;
        ws_wsrest_path(dir, iso_name, wsrest_path, sizeof(wsrest_path));
        n_recs = ws_wsrest_read(wsrest_path, recs, WS_WSREST_MAX_RECS);
        if (n_recs > 0) {
            int k;
            for (k = 0; k < n_recs; k++) {
                if (recs[k].size >= 1 && recs[k].size <= 4 &&
                    (size_t)recs[k].offset + recs[k].size <= elf_len)
                    memcpy(elf + recs[k].offset, recs[k].old_bytes,
                           recs[k].size);
            }
            g->crc = ws_ps2crc(elf, elf_len);
            g->iso_patched = 1;
        }
        /* n_recs -1 = tidak ada file (normal, bukan patched);
           -2 = corrupt (diperlakukan tidak ada — game tampil dengan
           CRC hasil patch, lookup gagal jadi "no patch"). */
    }
    /* e_entry ELF (offset 24, LE) — diagnostik: kalau ini sampah,
       extent yang dibaca salah. */
    if (elf_len >= 28)
        g->elf_entry = (uint32_t)elf[24] | ((uint32_t)elf[25] << 8) |
                       ((uint32_t)elf[26] << 16) | ((uint32_t)elf[27] << 24);
    if (boot[0]) {
        strncpy(g->boot, boot, sizeof(g->boot) - 1);
        g->boot[sizeof(g->boot) - 1] = '\0';
    }
    free(elf);

    /* Fase 3k: pnach user DULU — file user menang atas database
     * system, walau system punya CRC yang persis cocok. */
    if (uindex && user_patches_dir) {
        st = ws_user_lookup(uindex, g->crc, g->serial_norm, patch_file,
                            sizeof(patch_file));
        if (st == WS_LOOKUP_USER) {
            g->user_patch = 1;
            patch_dir = user_patches_dir;
        }
    }
    if (!g->user_patch) {
        st = ws_patch_lookup(pindex, g->crc, g->serial_norm, patch_file,
                             sizeof(patch_file));
        if (st == WS_LOOKUP_NOT_FOUND) {
            g->avail = WS_AVAIL_NO;
            return;
        }
        if (st == WS_LOOKUP_AMBIGUOUS) {
            /* Fase 3l: jangan tolak — catat jumlah kandidatnya,
             * user yang pilih di picker (X/Segitiga/Start). */
            g->avail = WS_AVAIL_AMBIGUOUS;
            g->dup_count = ws_patch_candidates(pindex, g->crc,
                                               g->serial_norm, NULL, 0);
            return;
        }
        /* Fase 3j: patch dari fallback serial — tandai, biar log tahu
         * patch ini tebakan (serial cocok, CRC beda). */
        if (st == WS_LOOKUP_SERIAL_ONLY) g->serial_fallback = 1;
    }
    ws_game_load_patch(g, patch_dir, patch_file);
}

int ws_count_iso_files(const char *dir)
{
    DIR *d;
    struct dirent *de;
    int n = 0;
    d = opendir(dir);
    if (!d) return -1;
    while ((de = readdir(d)) != NULL) {
        if (!has_iso_suffix(de->d_name)) continue;
        if (n >= WS_MAX_GAMES) break;
        n++;
    }
    closedir(d);
    return n;
}

int ws_scan_dir(const char *dir, ws_patch_index_t *pindex,
                const char *patches_dir, ws_user_index_t *uindex,
                const char *user_patches_dir, ws_scan_result_t *out,
                ws_scan_progress_t progress, void *ctx)
{
    DIR *d;
    struct dirent *de;
    memset(out, 0, sizeof(*out));
    d = opendir(dir);
    if (!d) return -1;
    while ((de = readdir(d)) != NULL) {
        if (!has_iso_suffix(de->d_name)) continue;
        if (out->count >= WS_MAX_GAMES) break;
        scan_one(dir, de->d_name, pindex, patches_dir, uindex,
                 user_patches_dir,
                 &out->games[out->count]);
        out->count++;
        /* Beri kesempatan keluar (tombol PS) di tengah scan — scan 39
           ISO baca ELF penuh tiap game, tanpa ini app tidak responsif
           sampai scan selesai. */
        if (progress && progress(out->count, ctx)) {
            closedir(d);
            qsort(out->games, (size_t)out->count, sizeof(ws_game_t),
                  cmp_game);
            return 1;
        }
    }
    closedir(d);
    qsort(out->games, (size_t)out->count, sizeof(ws_game_t), cmp_game);
    return 0;
}
