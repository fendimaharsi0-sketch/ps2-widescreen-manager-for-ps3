/* Tes host ISO patch Fase 2t: tulis pnach langsung ke ISO (burn-to-ISO)
 * + backup .wsrest + restore + translasi per-blok BIN mentah.
 *
 * Kompilasi: gcc -O1 -Wall -Wextra -I../src test_iso_patch.c
 *   ../src/patchfile.c ../src/configfile.c ../src/convert.c ../src/crc32.c
 *   ../src/dbmerge.c ../src/elf.c ../src/iso9660.c ../src/iso_patch.c
 *   ../src/report.c ../src/scan.c ../src/state.c ../src/toggle.c
 *   -o test_iso_patch
 * (tanpa main.c/video.c/draw.c/pad.c — PS3-only).
 *
 * Dijalankan run_tests.sh dengan fixtures/isopatch/ berisi:
 *   test.iso   (salinan realistic.iso — yang di-patch)
 *   test.bin   (salinan realistic.bin 2352 — yang di-patch)
 *   patches/   (pnach untuk CRC ELF realistic)
 * File asli realistic.iso TIDAK pernah ditulis — tes selalu bekerja di
 * salinan, dan restore mengembalikan salinan byte-identik.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "elf.h"
#include "iso9660.h"
#include "iso_patch.h"
#include "patchfile.h"
#include "report.h"
#include "scan.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Baca byte mentah file host di offset tertentu (untuk verifikasi
 * posisi tulis di BIN mentah). */
static int read_host_at(const char *path, long off, unsigned char *buf,
                        size_t len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, off, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* pnach tes: 6 yang harus ke-apply (3 word + short + byte + extended
 * 1-byte) + 2 yang harus di-skip (kondisional 0xE + alamat di luar
 * range). Alamat EE di dalam ELF realistic (vaddr 0x00100000, 2 MB):
 * file offset = addr - 0x00100000. */
static const char TEST_PNACH[] =
    "gametitle=Test Game (U)(SLUS-21159)\n"
    "\n"
    "[Widescreen 16:9]\n"
    "patch=1,EE,00100100,word,3f400000\n"
    "patch=1,EE,00100200,word,43000000\n"
    "patch=1,EE,00108000,word,3c013f40\n"
    "patch=1,EE,00100300,short,1234\n"
    "patch=1,EE,00100400,byte,AB\n"
    "patch=1,EE,00100500,extended,20000001\n"
    "patch=1,EE,E0010000,extended,00000000\n"
    "patch=1,EE,02000000,word,00000001\n";

/* Offset file ELF untuk tiap patch yang ke-apply (addr - 0x100000). */
static const uint32_t APPLY_OFF[6] = { 0x100, 0x200, 0x8000, 0x300, 0x400, 0x500 };
static const uint32_t APPLY_SIZE[6] = { 4, 4, 4, 2, 1, 1 };
/* Nilai baru, little-endian (low byte dulu). */
static const unsigned char APPLY_NEW[6][4] = {
    { 0x00, 0x00, 0x40, 0x3f },
    { 0x00, 0x00, 0x00, 0x43 },
    { 0x40, 0x3f, 0x01, 0x3c },
    { 0x34, 0x12, 0x00, 0x00 },
    { 0xAB, 0x00, 0x00, 0x00 },
    { 0x01, 0x00, 0x00, 0x00 },
};

int main(int argc, char **argv)
{
    const char *fix;
    char iso_dir[1024];
    char iso_path[1024];
    char bin_path[1024];
    char wsrest_path[1024];
    unsigned char *orig_elf = NULL, *elf = NULL;
    size_t orig_len = 0, elf_len = 0;
    uint32_t orig_crc;
    static ws_report_t report;
    int n_applied, n_skipped, rc, i;

    if (argc < 2) {
        printf("pakai: %s <fixtures/isopatch>\n", argv[0]);
        return 1;
    }
    fix = argv[1];
    snprintf(iso_dir, sizeof(iso_dir), "%s", fix);
    snprintf(iso_path, sizeof(iso_path), "%s/test.iso", fix);
    snprintf(bin_path, sizeof(bin_path), "%s/test.bin", fix);

    /* ---- Ambil ELF asli + CRC asli (sebelum patch) ---- */
    {
        char err[128];
        unsigned char *b = NULL;
        size_t blen = 0;
        CHECK(ws_read_boot_elf(iso_dir, "test.iso", &b, &blen, err,
                               sizeof(err)) == 0,
              "baca boot ELF test.iso gagal: %s", err);
        orig_elf = b;
        orig_len = blen;
        orig_crc = ws_ps2crc(orig_elf, orig_len);
        printf("ELF asli: %lu byte, CRC %08X\n", (unsigned long)orig_len,
               orig_crc);
    }

    printf("T18a apply word/byte/short + backup .wsrest\n");
    memset(&report, 0, sizeof(report));
    rc = ws_iso_patch_apply(iso_dir, "test.iso", TEST_PNACH,
                            strlen(TEST_PNACH), &report, &n_applied,
                            &n_skipped);
    CHECK(rc == 0, "apply harus sukses, rc=%d", rc);
    CHECK(n_applied == 6, "apply: 6 patch, dapat %d", n_applied);
    CHECK(n_skipped == 2, "apply: 2 skip, dapat %d", n_skipped);
    if (rc != 0) {
        for (i = 0; i < report.count; i++) printf("  %s\n", report.lines[i]);
    }
    /* .wsrest ada, 6 record, isi = old bytes asli. */
    ws_wsrest_path(iso_dir, "test.iso", wsrest_path, sizeof(wsrest_path));
    CHECK(file_exists(wsrest_path), ".wsrest harus ada sesudah apply");
    {
        static ws_wsrest_rec_t recs[WS_WSREST_MAX_RECS];
        int n = ws_wsrest_read(wsrest_path, recs, WS_WSREST_MAX_RECS);
        CHECK(n == 6, ".wsrest: 6 record, dapat %d", n);
        for (i = 0; i < 6 && i < n; i++) {
            CHECK(recs[i].offset == APPLY_OFF[i],
                  "rec %d: offset 0x%x, ekspektasi 0x%x", i,
                  recs[i].offset, APPLY_OFF[i]);
            CHECK(recs[i].size == APPLY_SIZE[i],
                  "rec %d: size %u, ekspektasi %u", i, recs[i].size,
                  APPLY_SIZE[i]);
            CHECK(memcmp(recs[i].old_bytes, orig_elf + APPLY_OFF[i],
                         APPLY_SIZE[i]) == 0,
                  "rec %d: old bytes tidak sama dengan ELF asli", i);
        }
    }
    /* Byte baru ada di offset yang benar di ELF hasil patch. */
    {
        char err[128];
        CHECK(ws_read_boot_elf(iso_dir, "test.iso", &elf, &elf_len, err,
                               sizeof(err)) == 0,
              "baca ELF hasil patch gagal: %s", err);
        CHECK(elf_len == orig_len, "panjang ELF tidak berubah");
        for (i = 0; i < 6; i++) {
            CHECK(memcmp(elf + APPLY_OFF[i], APPLY_NEW[i], APPLY_SIZE[i])
                  == 0, "patch %d: byte baru tidak ada di offset 0x%x",
                  i, APPLY_OFF[i]);
        }
        CHECK(ws_ps2crc(elf, elf_len) != orig_crc,
              "CRC harus berubah sesudah patch");
        free(elf);
        elf = NULL;
    }
    /* T18c: tetangga byte/short tidak ikut ketulis (bug ManaGunZ). */
    {
        char err[128];
        CHECK(ws_read_boot_elf(iso_dir, "test.iso", &elf, &elf_len, err,
                               sizeof(err)) == 0,
              "baca ELF untuk cek tetangga gagal: %s", err);
        /* short di 0x300 (2 byte): tetangga 0x302, 0x303 harus asli. */
        CHECK(memcmp(elf + 0x302, orig_elf + 0x302, 2) == 0,
              "tetangga short (0x302) ikut ketulis");
        /* byte di 0x400: tetangga 0x401..0x403 harus asli. */
        CHECK(memcmp(elf + 0x401, orig_elf + 0x401, 3) == 0,
              "tetangga byte (0x401) ikut ketulis");
        /* extended 1-byte di 0x500: tetangga 0x501..0x503 harus asli. */
        CHECK(memcmp(elf + 0x501, orig_elf + 0x501, 3) == 0,
              "tetangga extended (0x501) ikut ketulis");
        free(elf);
        elf = NULL;
    }

    printf("T18d apply dua kali = already patched\n");
    memset(&report, 0, sizeof(report));
    rc = ws_iso_patch_apply(iso_dir, "test.iso", TEST_PNACH,
                            strlen(TEST_PNACH), &report, &n_applied,
                            &n_skipped);
    CHECK(rc == 1, "apply kedua harus 1 (already patched), rc=%d", rc);
    CHECK(n_applied == 0, "apply kedua: 0 ditulis, dapat %d", n_applied);

    printf("T18f scan .wsrest-first: PATCHED + CRC asli\n");
    {
        static ws_patch_index_t pindex;
        static ws_scan_result_t res;
        char patches_dir[1024];
        int found = 0;
        snprintf(patches_dir, sizeof(patches_dir), "%s/patches", fix);
        memset(&pindex, 0, sizeof(pindex));
        CHECK(ws_patch_index_load(&pindex, patches_dir) == 0,
              "indeks patches isopatch gagal dimuat");
        CHECK(ws_scan_dir(iso_dir, &pindex, patches_dir, NULL, NULL,
                          &res, NULL, NULL)
              == 0, "scan isopatch gagal");
        for (i = 0; i < res.count; i++) {
            if (strcmp(res.games[i].iso_name, "test.iso") == 0) {
                found = 1;
                CHECK(res.games[i].iso_patched == 1,
                      "test.iso harus iso_patched=1");
                CHECK(res.games[i].crc == orig_crc,
                      "test.iso CRC harus CRC asli %08X, dapat %08X",
                      orig_crc, res.games[i].crc);
                CHECK(res.games[i].patch_file[0] != 0,
                      "test.iso harus tetap ketemu patch file");
            }
        }
        CHECK(found, "test.iso tidak ketemu di scan");
        ws_patch_index_free(&pindex);
    }

    printf("T18b restore: byte-identik + .wsrest terhapus\n");
    memset(&report, 0, sizeof(report));
    rc = ws_iso_patch_restore(iso_dir, "test.iso", &report);
    CHECK(rc == 0, "restore harus sukses, rc=%d", rc);
    if (rc != 0) {
        for (i = 0; i < report.count; i++) printf("  %s\n", report.lines[i]);
    }
    CHECK(!file_exists(wsrest_path), ".wsrest harus terhapus sesudah restore");
    {
        char err[128];
        CHECK(ws_read_boot_elf(iso_dir, "test.iso", &elf, &elf_len, err,
                               sizeof(err)) == 0,
              "baca ELF sesudah restore gagal: %s", err);
        CHECK(elf_len == orig_len, "panjang ELF sesudah restore sama");
        CHECK(memcmp(elf, orig_elf, orig_len) == 0,
              "ELF sesudah restore harus byte-identik dengan asli");
        CHECK(ws_ps2crc(elf, elf_len) == orig_crc,
              "CRC sesudah restore harus kembali %08X", orig_crc);
        free(elf);
        elf = NULL;
    }

    printf("T18e BIN mentah 2352: patch 0x8000 mendarat di host offset benar\n");
    {
        ws_iso_t iso;
        unsigned extent = 0, elf_size = 0;
        unsigned char buf[4];
        long host_ok, host_wrong;
        /* Extent ELF di BIN (LBA sama seperti ISO). */
        CHECK(ws_iso_open(&iso, bin_path) == 0, "buka test.bin gagal");
        CHECK(ws_iso_file_offset(&iso, "SLUS_211.59", &extent, &elf_size)
              == 0, "extent ELF di BIN gagal");
        ws_iso_close(&iso);
        /* Apply ke BIN. */
        memset(&report, 0, sizeof(report));
        rc = ws_iso_patch_apply(iso_dir, "test.bin", TEST_PNACH,
                                strlen(TEST_PNACH), &report, &n_applied,
                                &n_skipped);
        CHECK(rc == 0, "apply BIN harus sukses, rc=%d", rc);
        CHECK(n_applied == 6, "apply BIN: 6 patch, dapat %d", n_applied);
        /* Host offset benar: extent*2352 + 24 + 0x8000 + (0x8000/2048)*304. */
        host_ok = (long)extent * 2352L + 24L + 0x8000L + (0x8000L / 2048L) * 304L;
        /* Host offset SALAH (flat, bug ManaGunZ): tanpa +4864. */
        host_wrong = (long)extent * 2352L + 24L + 0x8000L;
        CHECK(read_host_at(bin_path, host_ok, buf, 4) == 0,
              "baca host offset benar gagal");
        CHECK(memcmp(buf, APPLY_NEW[2], 4) == 0,
              "BIN: byte baru tidak ada di host offset benar");
        CHECK(read_host_at(bin_path, host_wrong, buf, 4) == 0,
              "baca host offset salah gagal");
        /* Byte di host offset SALAH (flat, bug ManaGunZ) harus BUKAN
           nilai baru — posisi itu jatuh di trailer sektor (nol di
           fixture ini), bukan di data ELF 0x8000. */
        CHECK(memcmp(buf, APPLY_NEW[2], 4) != 0,
              "BIN: byte di host offset salah (flat) tidak boleh jadi nilai baru");
        /* Restore BIN, lalu verifikasi byte-identik lagi. */
        memset(&report, 0, sizeof(report));
        rc = ws_iso_patch_restore(iso_dir, "test.bin", &report);
        CHECK(rc == 0, "restore BIN harus sukses, rc=%d", rc);
        CHECK(read_host_at(bin_path, host_ok, buf, 4) == 0,
              "baca host offset benar sesudah restore gagal");
        CHECK(memcmp(buf, orig_elf + 0x8000, 4) == 0,
              "BIN sesudah restore harus kembali ke byte asli");
    }

    printf("T19 editor Start (Fase 2v): teks hasil filter centangan -> ISO\n");
    {
        static ws_patch_item_t items[WS_MAX_PATCH_ITEMS];
        static int checked[WS_MAX_PATCH_ITEMS];
        char *ftext = NULL;
        size_t ftext_len = 0;
        int n_items, n_write = 0, n_skip = 0;
        n_items = ws_patch_list(TEST_PNACH, strlen(TEST_PNACH), items,
                                WS_MAX_PATCH_ITEMS);
        CHECK(n_items == 8, "patch list: 8 item, dapat %d", n_items);
        /* Centang 3 dari 8: word 0x100 + short 0x300 + byte 0x400.
         * Sisanya (word 0x200/0x8000 + extended 0x500 + 2 skip)
         * tidak boleh ketulis. */
        memset(checked, 0, sizeof(checked));
        checked[0] = 1;
        checked[3] = 1;
        checked[4] = 1;
        CHECK(ws_patch_filtered_text(TEST_PNACH, strlen(TEST_PNACH),
                                     items, checked, n_items,
                                     &ftext, &ftext_len) == 0,
              "filtered text gagal dibangun");
        memset(&report, 0, sizeof(report));
        rc = ws_iso_patch_preview(iso_dir, "test.iso", ftext, ftext_len,
                                  &report, &n_write, &n_skip);
        CHECK(rc == 0, "preview teks filter harus 0, rc=%d", rc);
        CHECK(n_write == 3, "preview: 3 tulis, dapat %d", n_write);
        CHECK(n_skip == 0, "preview: 0 skip, dapat %d", n_skip);
        memset(&report, 0, sizeof(report));
        rc = ws_iso_patch_apply(iso_dir, "test.iso", ftext, ftext_len,
                                &report, &n_applied, &n_skipped);
        CHECK(rc == 0, "apply teks filter harus sukses, rc=%d", rc);
        CHECK(n_applied == 3, "apply: 3 patch, dapat %d", n_applied);
        CHECK(n_skipped == 0, "apply: 0 skip, dapat %d", n_skipped);
        free(ftext);
        /* 3 yang dicentang ketulis; 3 lain yang applicable tetap asli. */
        {
            char err[128];
            CHECK(ws_read_boot_elf(iso_dir, "test.iso", &elf, &elf_len,
                                   err, sizeof(err)) == 0,
                  "baca ELF sesudah apply filter gagal: %s", err);
            CHECK(memcmp(elf + 0x100, APPLY_NEW[0], 4) == 0,
                  "filter: word 0x100 tidak ketulis");
            CHECK(memcmp(elf + 0x300, APPLY_NEW[3], 2) == 0,
                  "filter: short 0x300 tidak ketulis");
            CHECK(memcmp(elf + 0x400, APPLY_NEW[4], 1) == 0,
                  "filter: byte 0x400 tidak ketulis");
            CHECK(memcmp(elf + 0x200, orig_elf + 0x200, 4) == 0,
                  "filter: word 0x200 (tidak dicentang) ikut ketulis");
            CHECK(memcmp(elf + 0x8000, orig_elf + 0x8000, 4) == 0,
                  "filter: word 0x8000 (tidak dicentang) ikut ketulis");
            CHECK(memcmp(elf + 0x500, orig_elf + 0x500, 1) == 0,
                  "filter: extended 0x500 (tidak dicentang) ikut ketulis");
            free(elf);
            elf = NULL;
        }
        /* .wsrest 3 record; restore kembalikan byte-identik. */
        {
            static ws_wsrest_rec_t recs[WS_WSREST_MAX_RECS];
            int n = ws_wsrest_read(wsrest_path, recs, WS_WSREST_MAX_RECS);
            CHECK(n == 3, ".wsrest filter: 3 record, dapat %d", n);
        }
        memset(&report, 0, sizeof(report));
        rc = ws_iso_patch_restore(iso_dir, "test.iso", &report);
        CHECK(rc == 0, "restore sesudah filter harus sukses, rc=%d", rc);
        {
            char err[128];
            CHECK(ws_read_boot_elf(iso_dir, "test.iso", &elf, &elf_len,
                                   err, sizeof(err)) == 0,
                  "baca ELF sesudah restore filter gagal: %s", err);
            CHECK(memcmp(elf, orig_elf, orig_len) == 0,
                  "ELF sesudah restore filter harus byte-identik");
            free(elf);
            elf = NULL;
        }
    }

    free(orig_elf);
    if (failures == 0) printf("T18 ISO patch: semua lolos\n");
    else printf("T18 ISO patch: %d KEGAGALAN\n", failures);
    return failures ? 1 : 0;
}
