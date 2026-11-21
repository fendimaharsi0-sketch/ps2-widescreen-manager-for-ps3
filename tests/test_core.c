/* Tes host untuk core portabel (Fase 1). Dijalankan lewat tests/run_tests.sh.
 * Tidak ada dependensi PS3: dikompilasi dengan gcc sistem.
 *
 * T1 CRC32 vektor standar.
 * T2 ISO9660: buka fixture, baca SYSTEM.CNF + ELF, cek ELF.
 * T3 Scan: game_a.iso (via SYSTEM.CNF) -> Title ID, CRC, widescreen ADA.
 * T4 Scan: game_b.iso (tanpa SYSTEM.CNF) -> ketemu lewat nama serial.
 * T5 Scan: game_c.iso (boot bukan ELF) -> WS_AVAIL_ERROR.
 * T6 Lookup CRC ganda: serial cocok -> file yang benar; serial tidak
 *    cocok -> AMBIGUOUS.
 * T6c Pnach user (Fase 3k): nama file bebas, identitas dari gametitle
 *    (serial) + nama file (CRC); match longgar serial ATAU CRC; user
 *    menang atas system di scan (game_a pakai file shadow user).
 * T6d Duplikat (Fase 3l): kandidat CRC/serial, header pnach
 *    (gametitle + comment + // sebelum grup pertama), load satu file
 *    ke game; scan game_d (boot SLUS-99999, ELF beda) -> AMBIGUOUS,
 *    dup_count 2.
 * T7 Patch asli per-file (Fase 0): MotoGP 4 = 5 baris, BR4 = 1,
 *    Downhill = 35, Xena (disabled) = 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "patchfile.h"
#include "crc32.h"
#include "elf.h"
#include "iso9660.h"
#include "scan.h"

static int failures;

#define CHECK(name, cond) do { \
    printf("[%s] %s\n", (cond) ? "OK " : "FAIL", (name)); \
    if (!(cond)) failures++; \
} while (0)

static const char *FIX;

static void t1_crc(void)
{
    CHECK("T1 CRC32(\"123456789\") == 0xCBF43926",
          ws_crc32("123456789", 9) == 0xCBF43926u);
    CHECK("T1 CRC32(\"\") == 0",
          ws_crc32("", 0) == 0);
    /* CRC game PS2 = XOR-fold word LE: "123456789" -> word "1234"
     * (0x34333231) ^ word "5678" (0x38373635) = 0x0C040404; byte
     * "9" terakhir diabaikan (bukan kelipatan 4). */
    CHECK("T1b ps2crc(\"123456789\") == 0x0C040404",
          ws_ps2crc("123456789", 9) == 0x0C040404u);
    CHECK("T1b ps2crc(\"\") == 0",
          ws_ps2crc("", 0) == 0);
    CHECK("T1b ps2crc(\"1234\") == 0x34333231",
          ws_ps2crc("1234", 4) == 0x34333231u);
}

static void t2_iso(void)
{
    char path[1024];
    ws_iso_t iso;
    unsigned char *cnf = NULL, *elf = NULL;
    size_t cnf_len = 0, elf_len = 0;
    snprintf(path, sizeof(path), "%s/ps2iso/game_a.iso", FIX);
    CHECK("T2 ISO open game_a", ws_iso_open(&iso, path) == 0);
    CHECK("T2 baca SYSTEM.CNF",
          ws_iso_read_file(&iso, "SYSTEM.CNF", &cnf, &cnf_len) == 0 &&
          cnf_len > 0 && strstr((char *)cnf, "BOOT2") != NULL);
    CHECK("T2 baca SLUS_211.59",
          ws_iso_read_file(&iso, "SLUS_211.59", &elf, &elf_len) == 0 &&
          elf_len == 1024);
    CHECK("T2 ELF 32-bit valid", ws_elf_check(elf, elf_len));
    CHECK("T2 file tidak ada -> gagal",
          ws_iso_read_file(&iso, "TIDAK_ADA.BIN", &cnf, &cnf_len) != 0);
    free(cnf);
    free(elf);
    ws_iso_close(&iso);
}

static uint32_t fake_crc(void)
{
    char path[1024], buf[16];
    FILE *f;
    snprintf(path, sizeof(path), "%s/fake_elf_crc.txt", FIX);
    f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return (uint32_t)strtoul(buf, NULL, 16);
}

/* CRC game_d (ELF varian, Fase 3l). */
static uint32_t fake_crc_d(void)
{
    char path[1024], buf[16];
    FILE *f;
    snprintf(path, sizeof(path), "%s/fake_elf_crc_d.txt", FIX);
    f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return (uint32_t)strtoul(buf, NULL, 16);
}

static void t3_t4_t5_scan(ws_patch_index_t *pindex, const char *patches_dir,
                          ws_user_index_t *uindex,
                          const char *user_patches_dir)
{
    char path[1024];
    ws_scan_result_t res;
    snprintf(path, sizeof(path), "%s/ps2iso", FIX);
    /* T20 (Fase 2x): hitung ISO untuk total progress bar — 4 file
     * .iso, bukan_iso.txt tidak kehitung, tanpa baca isi file. */
    CHECK("T20 count ISO ps2iso == 4",
          ws_count_iso_files(path) == 4);
    CHECK("T20 count ISO dir tidak ada == -1",
          ws_count_iso_files("/tidak/ada/direktori/ini") == -1);
    CHECK("T3/T4/T5 scan dir",
          ws_scan_dir(path, pindex, patches_dir, uindex, user_patches_dir,
                      &res, NULL, NULL) == 0);
    CHECK("T3/T4/T5 4 ISO terbaca", res.count == 4);
    /* Diurutkan berdasarkan title; ketiganya bertitle sama (gametitle
       pnach) atau nama ISO — cari per nama file. */
    {
        int i, found_a = 0, found_b = 0, found_c = 0, found_d = 0;
        for (i = 0; i < res.count; i++) {
            ws_game_t *g = &res.games[i];
            if (strcmp(g->iso_name, "game_a.iso") == 0) {
                found_a = 1;
                CHECK("T3 game_a Title ID SLUS-21159",
                      strcmp(g->title_id, "SLUS-21159") == 0);
                CHECK("T3 game_a CRC == CRC ELF palsu",
                      g->crc == fake_crc());
                CHECK("T3 game_a widescreen ADA (2 baris)",
                      g->avail == WS_AVAIL_YES && g->ws_lines == 2);
                CHECK("T3 game_a title dari gametitle",
                      strcmp(g->title, "Moto GP 4 (U)(SLUS-21159)") == 0);
                /* Fase 3k: game_a juga ada di database system — tapi
                 * file shadow user (CRC ELF palsu) harus menang. */
                CHECK("T3 game_a patch dari user (prioritas)",
                      g->user_patch == 1);
                {
                    char shadow[32];
                    snprintf(shadow, sizeof(shadow), "%08X.pnach",
                             fake_crc());
                    CHECK("T3 game_a patch_file = shadow user",
                          strcmp(g->patch_file, shadow) == 0);
                }
            } else if (strcmp(g->iso_name, "game_b.iso") == 0) {
                found_b = 1;
                CHECK("T4 game_b (tanpa SYSTEM.CNF) Title ID SLUS-21159",
                      strcmp(g->title_id, "SLUS-21159") == 0);
                CHECK("T4 game_b CRC sama, widescreen ADA",
                      g->crc == fake_crc() && g->avail == WS_AVAIL_YES);
            } else if (strcmp(g->iso_name, "game_c.iso") == 0) {
                found_c = 1;
                CHECK("T5 game_c (boot bukan ELF) -> ERROR",
                      g->avail == WS_AVAIL_ERROR);
            } else if (strcmp(g->iso_name, "game_d.iso") == 0) {
                found_d = 1;
                CHECK("T6d game_d Title ID SLUS-99999",
                      strcmp(g->title_id, "SLUS-99999") == 0);
                CHECK("T6d game_d CRC == CRC game_d, lookup AMBIGUOUS",
                      g->crc == fake_crc_d() &&
                      g->avail == WS_AVAIL_AMBIGUOUS);
                CHECK("T6d game_d dup_count == 2",
                      g->dup_count == 2);
                CHECK("T6d game_d belum punya patch_file",
                      g->patch_file[0] == '\0');
            }
        }
        CHECK("T3/T4/T5/T6d keempatnya ditemukan",
              found_a && found_b && found_c && found_d);
    }
}

static void t6_dup_crc(ws_patch_index_t *pindex, const char *patches_dir)
{
    char filename[64];
    char *text = NULL;
    size_t len = 0;
    ws_lookup_status_t st;
    /* CRC 12345678 punya dua file: SLUS-20795 dan SLPM-65499. */
    st = ws_patch_lookup(pindex, 0x12345678u, "SLUS20795", filename,
                         sizeof(filename));
    CHECK("T6 dup CRC + serial SLUS-20795 -> file SLUS",
          st == WS_LOOKUP_FOUND &&
          strcmp(filename, "SLUS-20795_12345678.pnach") == 0);
    CHECK("T6 isi file SLUS bisa dibaca",
          ws_patch_read_file(patches_dir, filename, &text, &len) == 0 &&
          text && strstr(text, "SLUS-20795") != NULL);
    free(text);
    text = NULL;
    st = ws_patch_lookup(pindex, 0x12345678u, "SLPM65499", filename,
                         sizeof(filename));
    CHECK("T6 dup CRC + serial SLPM-65499 -> file SLPM",
          st == WS_LOOKUP_FOUND &&
          strcmp(filename, "SLPM-65499_12345678.pnach") == 0);
    CHECK("T6 isi file SLPM bisa dibaca",
          ws_patch_read_file(patches_dir, filename, &text, &len) == 0 &&
          text && strstr(text, "SLPM-65499") != NULL);
    free(text);
    text = NULL;
    st = ws_patch_lookup(pindex, 0x12345678u, "SLUS99999", filename,
                         sizeof(filename));
    CHECK("T6 dup CRC + serial tidak cocok -> AMBIGUOUS",
          st == WS_LOOKUP_AMBIGUOUS);
}

/* Fase 3j: fallback serial — jalan HANYA kalau tidak ada file untuk
 * CRC game sama sekali. */
static void t6b_serial_fallback(ws_patch_index_t *pindex)
{
    char filename[64];
    ws_lookup_status_t st;
    /* Serial SLUS-99999 cuma punya satu file (CRC AAAAAAAA). CRC
       game beda -> fallback serial, bukan not found. */
    st = ws_patch_lookup(pindex, 0xDEADBEEFu, "SLUS99999", filename,
                         sizeof(filename));
    CHECK("T6b serial cocok + CRC beda, satu file -> SERIAL_ONLY",
          st == WS_LOOKUP_SERIAL_ONLY &&
          strcmp(filename, "SLUS-99999_AAAAAAAA.pnach") == 0);
    /* Serial SLPM-65499 punya dua file beda CRC (12345678,
       11111111): CRC game beda -> ambiguous, bukan asal ambil. */
    st = ws_patch_lookup(pindex, 0xDEADBEEFu, "SLPM65499", filename,
                         sizeof(filename));
    CHECK("T6b serial cocok + CRC beda, dua file -> AMBIGUOUS",
          st == WS_LOOKUP_AMBIGUOUS);
    /* Serial tidak ada di database sama sekali -> not found. */
    st = ws_patch_lookup(pindex, 0xDEADBEEFu, "SLUS00000", filename,
                         sizeof(filename));
    CHECK("T6b serial tidak ada + CRC beda -> NOT_FOUND",
          st == WS_LOOKUP_NOT_FOUND);
    /* CRC cocok tetap menang atas fallback: SLUS-20795 + CRC
       12345678 -> FOUND biasa, bukan serial-only. */
    st = ws_patch_lookup(pindex, 0x12345678u, "SLUS20795", filename,
                         sizeof(filename));
    CHECK("T6b CRC cocok -> FOUND (bukan SERIAL_ONLY)",
          st == WS_LOOKUP_FOUND &&
          strcmp(filename, "SLUS-20795_12345678.pnach") == 0);
}

/* Fase 3k: pnach user — nama file bebas, identitas dari gametitle
 * (serial) + nama file (CRC), match longgar serial ATAU CRC. */
static void t6c_user_pnach(ws_user_index_t *uindex)
{
    char filename[128];
    ws_lookup_status_t st;
    /* 4 nama Atelier Iris 3 + 1 shadow MotoGP = 5 entri. File tanpa
     * identitas + notes.txt tidak masuk indeks. */
    CHECK("T6c indeks user == 5 entri", uindex->count == 5);
    /* CRC + serial cocok -> file 4CCC9212 (satu-satunya rank 3). */
    st = ws_user_lookup(uindex, 0x4CCC9212u, "SLUS21564", filename,
                        sizeof(filename));
    CHECK("T6c CRC+serial -> 4CCC9212.pnach",
          st == WS_LOOKUP_USER &&
          strcmp(filename, "4CCC9212.pnach") == 0);
    /* Serial saja (CRC game beda) -> 4 file rank 1, alfabetis
     * pertama menang ('4' < 'A' < 'S' — file 4CCC9212 ikut cocok
     * lewat serial gametitle-nya, walau CRC-nya beda). */
    st = ws_user_lookup(uindex, 0xDEADBEEFu, "SLUS21564", filename,
                        sizeof(filename));
    CHECK("T6c serial saja -> 4CCC9212.pnach",
          st == WS_LOOKUP_USER &&
          strcmp(filename, "4CCC9212.pnach") == 0);
    /* CRC saja (serial game kosong) -> 4CCC9212.pnach. */
    st = ws_user_lookup(uindex, 0x4CCC9212u, "", filename,
                        sizeof(filename));
    CHECK("T6c CRC saja -> 4CCC9212.pnach",
          st == WS_LOOKUP_USER &&
          strcmp(filename, "4CCC9212.pnach") == 0);
    /* Shadow MotoGP: CRC ELF palsu + serial SLUS-21159 -> rank 3. */
    st = ws_user_lookup(uindex, fake_crc(), "SLUS21159", filename,
                        sizeof(filename));
    {
        char shadow[32];
        snprintf(shadow, sizeof(shadow), "%08X.pnach", fake_crc());
        CHECK("T6c shadow MotoGP -> file CRC.pnach",
              st == WS_LOOKUP_USER && strcmp(filename, shadow) == 0);
    }
    /* Tidak cocok sama sekali -> NOT_FOUND. */
    st = ws_user_lookup(uindex, 0xDEADBEEFu, "SLUS00000", filename,
                        sizeof(filename));
    CHECK("T6c tidak cocok -> NOT_FOUND", st == WS_LOOKUP_NOT_FOUND);
    /* Dir tidak ada -> -1, bukan crash. */
    {
        ws_user_index_t bad;
        CHECK("T6c dir tidak ada -> -1",
              ws_user_index_load(&bad, "/tidak/ada/direktori/ini") != 0);
    }
}

/* Fase 3l: kandidat duplikat + header pnach + load satu file. */
static void t6d_dup_pick(ws_patch_index_t *pindex,
                         const char *patches_dir)
{
    char files[16][128];
    int n;
    /* CRC game_d punya 2 file (SLUS-20795 + SLPM-65499),
     * diurutkan alfabetis (SLPM < SLUS). */
    n = ws_patch_candidates(pindex, fake_crc_d(), "SLUS99999", files, 16);
    {
        char f0[32], f1[32];
        snprintf(f0, sizeof(f0), "SLPM-65499_%08X.pnach", fake_crc_d());
        snprintf(f1, sizeof(f1), "SLUS-20795_%08X.pnach", fake_crc_d());
        CHECK("T6d kandidat CRC game_d == 2, alfabetis",
              n == 2 && strcmp(files[0], f0) == 0 &&
              strcmp(files[1], f1) == 0);
    }
    /* CRC tidak ada -> jalur serial: SLPM-65499 punya 3 file
     * (11111111, 12345678, CRC game_d). */
    n = ws_patch_candidates(pindex, 0xDEADBEEFu, "SLPM65499", files, 16);
    CHECK("T6d kandidat serial SLPM-65499 == 3", n == 3);
    /* Tidak cocok sama sekali -> 0. */
    n = ws_patch_candidates(pindex, 0xDEADBEEFu, "SLUS00000", files, 16);
    CHECK("T6d kandidat tidak cocok == 0", n == 0);
    /* Header: gametitle + comment + // sebelum grup pertama; isi
     * grup tidak ikut. */
    {
        static const char fm5[] =
            "gametitle=Front Mission 5 (SLPM_662.05) (English patch CRC)\n"
            "comment=English translation, use on patched ISO\n"
            "//widescreen 16:9 hack by 99skull\n"
            "\n"
            "[Widescreen 16:9]\n"
            "gsaspectratio=16:9\ndescription=widescreen\n"
            "patch=1,EE,203D1A18,word,3F19999A //3F4CCCCD\n";
        char hdr[WS_PATCH_HEADER_MAX_LINES][WS_PATCH_HEADER_LINE_LEN];
        int nh = ws_patch_header(fm5, sizeof(fm5) - 1, hdr,
                                 WS_PATCH_HEADER_MAX_LINES);
        CHECK("T6d header FM5 == 3 baris", nh == 3);
        CHECK("T6d header baris 0 = gametitle English patch CRC",
              nh >= 1 &&
              strcmp(hdr[0],
                     "Front Mission 5 (SLPM_662.05) (English patch CRC)")
                  == 0);
        CHECK("T6d header baris 1 = comment",
              nh >= 2 &&
              strcmp(hdr[1], "English translation, use on patched ISO")
                  == 0);
        CHECK("T6d header baris 2 = catatan //",
              nh >= 3 &&
              strcmp(hdr[2], "widescreen 16:9 hack by 99skull") == 0);
    }
    /* Load satu file ke game (yang dilakukan picker sesudah pilih). */
    {
        ws_game_t g;
        memset(&g, 0, sizeof(g));
        CHECK("T6d load_patch SLUS-20795 -> YES 1 ws",
              ws_game_load_patch(&g, patches_dir,
                                 "SLUS-20795_12345678.pnach") == 0 &&
              g.avail == WS_AVAIL_YES && g.ws_lines == 1 &&
              strcmp(g.patch_file, "SLUS-20795_12345678.pnach") == 0);
        CHECK("T6d load_patch file tidak ada -> ERROR",
              ws_game_load_patch(&g, patches_dir, "TIDAK_ADA.pnach") != 0 &&
              g.avail == WS_AVAIL_ERROR);
    }
}

/* Cek satu CRC di indeks patch asli: lookup + baca file + hitung baris. */
static void t7_one(ws_patch_index_t *pindex, const char *patches_dir,
                   uint32_t crc, const char *serial, const char *label,
                   int expect_lines)
{
    char filename[64];
    char *text = NULL;
    size_t len = 0;
    ws_text_line_t lines[128];
    int n;
    ws_lookup_status_t st = ws_patch_lookup(pindex, crc, serial, filename,
                                            sizeof(filename));
    CHECK(label, st == WS_LOOKUP_FOUND);
    if (st != WS_LOOKUP_FOUND) return;
    if (ws_patch_read_file(patches_dir, filename, &text, &len) != 0) {
        printf("[FAIL] T7 baca %s\n", filename);
        failures++;
        return;
    }
    n = ws_ws_lines(text, len, lines, 128);
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s = %d baris widescreen", label,
                 expect_lines);
        CHECK(msg, n == expect_lines);
    }
    free(text);
}

static void t7_real_patches(const char *patches_dir)
{
    ws_patch_index_t pindex;
    /* Folder patch asli diteruskan lewat argv[3] oleh run_tests.sh. */
    if (ws_patch_index_load(&pindex, patches_dir) != 0) {
        printf("[FAIL] T7 load indeks patch asli\n");
        failures++;
        return;
    }
    t7_one(&pindex, patches_dir, 0xAF7BEB14u, "SLUS21159",
           "T7 MotoGP 4 lookup", 5);
    t7_one(&pindex, patches_dir, 0xC9F6F222u, "SLUS20795",
           "T7 BR4 lookup", 1);
    t7_one(&pindex, patches_dir, 0x5AE01D98u, "SCUS97177",
           "T7 Downhill lookup", 35);
    t7_one(&pindex, patches_dir, 0x6D980D22u, "",
           "T7 Xena (disabled) lookup", 0);
    /* CRC 7 digit (Fase 0: left-pad '0'): harus ketemu, bukan ter-skip. */
    t7_one(&pindex, patches_dir, 0x0B6F7BA5u, "SLES50396",
           "T7 Mike Tyson (CRC 7 digit) lookup", 6);
    t7_one(&pindex, patches_dir, 0x03CA4D49u, "SLKA25059",
           "T7 Pride FC (CRC 7 digit) lookup", 3);
    ws_patch_index_free(&pindex);
}

int main(int argc, char **argv)
{
    ws_patch_index_t pindex;
    ws_user_index_t uindex;
    char path[1024], upath[1024];
    if (argc < 3) {
        fprintf(stderr,
                "pakai: %s <fixtures-dir> <test-patches-dir> <real-patches-dir>\n",
                argv[0]);
        return 2;
    }
    FIX = argv[1];
    t1_crc();
    t2_iso();
    snprintf(path, sizeof(path), "%s/patches", FIX);
    if (ws_patch_index_load(&pindex, path) != 0) {
        printf("[FAIL] load indeks patch tes %s\n", path);
        return 1;
    }
    snprintf(upath, sizeof(upath), "%s/user_pnach", FIX);
    if (ws_user_index_load(&uindex, upath) != 0) {
        printf("[FAIL] load indeks user tes %s\n", upath);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/patches", FIX);
    t3_t4_t5_scan(&pindex, path, &uindex, upath);
    t6_dup_crc(&pindex, path);
    t6b_serial_fallback(&pindex);
    t6c_user_pnach(&uindex);
    t6d_dup_pick(&pindex, path);
    ws_patch_index_free(&pindex);
    ws_user_index_free(&uindex);
    t7_real_patches(argv[3]);
    printf("\n%s: %d kegagalan\n", failures ? "GAGAL" : "Semua tes lolos",
           failures);
    return failures ? 1 : 0;
}
