/* Tes host konverter Fase 2: output C harus byte-identik dengan tool
 * Python (pnach2config.py) pada input yang sama (pnach tanpa //old +
 * ELF sintetis + folder configdb per-file yang sama dengan database/ tool).
 * Kasus ekspektasi dibuat tools/make_convert_fixtures.py.
 *
 * Kompilasi: gcc -O1 -Wall -Wextra -I../src test_convert.c
 *   ../src/patchfile.c ../src/configfile.c ../src/convert.c ../src/crc32.c
 *   ../src/dbmerge.c ../src/elf.c ../src/iso9660.c ../src/report.c
 *   ../src/scan.c -o test_convert
 * (tanpa main.c/video.c/draw.c/pad.c — PS3-only).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configfile.h"
#include "convert.h"
#include "dbmerge.h"
#include "elf.h"
#include "patchfile.h"
#include "report.h"
#include "scan.h"
#include "state.h"
#include "toggle.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(*len ? *len : 1);
    if (*len && fread(buf, 1, *len, f) != *len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    return buf;
}

/* Ekor ekspektasi mengandung Title ID? (cermin tail_has_title di
 * convert.c — tes harus independen dari helper implementasi.) */
static int exp_tail_has_title(const unsigned char *tail, size_t tail_len)
{
    int k;
    if (!tail || tail_len < 14) return 0;
    if (tail[0] || tail[1] || tail[2] || tail[3]) return 0;
    for (k = 0; k < 4; k++)
        if (tail[4 + k] < 'A' || tail[4 + k] > 'Z') return 0;
    if (tail[8] != '-') return 0;
    for (k = 0; k < 5; k++)
        if (tail[9 + k] < '0' || tail[9 + k] > '9') return 0;
    return 1;
}

/* Satu kasus konversi: aliran command output C harus byte-identik dengan
 * tool Python (file ekspektasi); ekornya harus mengikuti aturan
 * watermark Fase 3c (wm_title = Title ID dash yang diharapkan di ekor,
 * NULL = tanpa watermark, output byte-identik penuh). */
static void case_convert(const char *name, const char *pnach_path,
                         const char *expected_path,
                         const unsigned char *elf, size_t elf_len,
                         const char *configdb_dir, const char *title_fallback,
                         int expect_merged, const char *wm_title)
{
    size_t pn_len = 0, exp_len = 0;
    char *pn = (char *)read_file(pnach_path, &pn_len);
    unsigned char *exp = read_file(expected_path, &exp_len);
    unsigned char *out = NULL;
    size_t out_len = 0;
    int n_triples = -1, merged = -1;
    ws_report_t report;
    int rc, i;
    memset(&report, 0, sizeof(report));
    CHECK(pn != NULL, "%s: pnach tidak bisa dibaca", name);
    CHECK(exp != NULL, "%s: file ekspektasi tidak bisa dibaca", name);
    if (!pn || !exp) {
        free(pn);
        free(exp);
        return;
    }
    rc = ws_convert(pn, pn_len, elf, elf_len, "synthetic.elf", configdb_dir, 0, WS_OLD_PATCH_FIRST,
                    title_fallback, &out, &out_len, &n_triples, &merged,
                    &report);
    CHECK(rc == 0, "%s: ws_convert gagal (rc=%d)", name, rc);
    if (rc == 0) {
        /* Aliran command = file ekspektasi sampai awal ekornya. */
        ws_db_cmd_t *cmds = NULL;
        int n_cmds = 0;
        uint32_t *words_buf = NULL;
        const unsigned char *exp_tail = NULL;
        size_t exp_tail_len = 0, cmd_len = exp_len;
        char err[128];
        if (ws_db_walk(exp, exp_len, &cmds, &n_cmds, &words_buf,
                       &exp_tail, &exp_tail_len, err, sizeof(err)) == 0) {
            if (exp_tail) cmd_len = (size_t)(exp_tail - exp);
        } else {
            CHECK(0, "%s: file ekspektasi tidak bisa di-walk: %s", name,
                  err);
        }
        free(cmds);
        free(words_buf);
        CHECK(out_len >= cmd_len,
              "%s: output %d byte < aliran command %d byte", name,
              (int)out_len, (int)cmd_len);
        if (out_len >= cmd_len)
            CHECK(memcmp(out, exp, cmd_len) == 0,
                  "%s: aliran command beda dari tool Python", name);
        /* Ekor: aturan watermark Fase 3c. */
        {
            const unsigned char *our_tail = out + cmd_len;
            size_t our_tail_len = out_len - cmd_len;
            if (wm_title) {
                if (exp_tail_has_title(exp_tail, exp_tail_len)) {
                    /* Ekor DB sudah punya title: tempel "WSM1". */
                    CHECK(our_tail_len == exp_tail_len + 4,
                          "%s: ekor %d byte, ekspektasi %d + 4", name,
                          (int)our_tail_len, (int)exp_tail_len);
                    if (our_tail_len == exp_tail_len + 4) {
                        CHECK(memcmp(our_tail, exp_tail, exp_tail_len) == 0,
                              "%s: ekor DB harus dipertahankan", name);
                        CHECK(memcmp(our_tail + exp_tail_len, "WSM1", 4)
                                  == 0,
                              "%s: 4 byte terakhir ekor harus WSM1", name);
                    }
                } else {
                    /* Ekor tanpa title: ganti [0x00][title][WSM1]. */
                    CHECK(our_tail_len == 18,
                          "%s: ekor watermark 18 byte, dapat %d", name,
                          (int)our_tail_len);
                    if (our_tail_len == 18) {
                        CHECK(our_tail[0] == 0 && our_tail[1] == 0 &&
                                  our_tail[2] == 0 && our_tail[3] == 0,
                              "%s: ekor harus mulai 0x00", name);
                        CHECK(memcmp(our_tail + 4, wm_title, 10) == 0,
                              "%s: title di ekor harus %s", name, wm_title);
                        CHECK(memcmp(our_tail + 14, "WSM1", 4) == 0,
                              "%s: 4 byte terakhir ekor harus WSM1", name);
                    }
                }
            } else {
                CHECK(our_tail_len == exp_tail_len,
                      "%s: tanpa title, ekor harus identik (%d vs %d)",
                      name, (int)our_tail_len, (int)exp_tail_len);
                if (our_tail_len == exp_tail_len)
                    CHECK(memcmp(our_tail, exp_tail, exp_tail_len) == 0,
                          "%s: tanpa title, ekor harus byte-identik", name);
            }
        }
        CHECK(merged == expect_merged, "%s: merged=%d, ekspektasi %d",
              name, merged, expect_merged);
        if (out_len < cmd_len || memcmp(out, exp, cmd_len) != 0) {
            printf("  report %s:\n", name);
            for (i = 0; i < report.count; i++)
                printf("    %s\n", report.lines[i]);
        }
    } else {
        printf("  report %s:\n", name);
        for (i = 0; i < report.count; i++)
            printf("    %s\n", report.lines[i]);
    }
    free(pn);
    free(exp);
    free(out);
}

/* Folder configdb per-file (snapshot database webMAN, NET + CUSTOM). */
#define CONFIGDB_DIR "/home/hatch/workspace/pnach2config/database"

int main(void)
{
    unsigned char *elf = NULL;
    size_t elf_len = 0;

    printf("T8 konverter: byte-identik vs tool Python\n");
    elf = read_file("convert/synthetic.elf", &elf_len);
    CHECK(elf != NULL, "ELF sintetis tidak bisa dibaca");

    /* Patch-only (tanpa entry DB). */
    case_convert("motogp", "convert/motogp.pnach",
                 "convert/expected/SLUS_211.59.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 0, "SLUS-21159");
    case_convert("br4", "convert/br4.pnach",
                 "convert/expected/SLUS_207.95.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 0, "SLUS-20795");
    case_convert("downhill", "convert/downhill.pnach",
                 "convert/expected/downhill.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 0, "SCUS-97177");
    /* Tanpa serial di gametitle -> patch-only. */
    case_convert("slus_203.14", "convert/slus_203.14.pnach",
                 "convert/expected/slus_203.14.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 0, NULL);
    /* Merge: triple DB dipertahankan, overlay dipertahankan, ekor Title ID. */
    case_convert("slus_213.26", "convert/slus_213.26.pnach",
                 "convert/expected/SLUS_213.26.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 1, "SLUS-21326");
    case_convert("slus_211.52", "convert/slus_211.52.pnach",
                 "convert/expected/SLUS_211.52.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 1, "SLUS-21152");
    /* Merge: command 0x09 digusur. */
    case_convert("scus_971.11", "convert/scus_971.11.pnach",
                 "convert/expected/SCUS_971.11.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 1, "SCUS-97111");
    /* BSS -> old 0. */
    case_convert("bss", "convert/bss.pnach",
                 "convert/expected/SLUS_999.99.CONFIG",
                 elf, elf_len, CONFIGDB_DIR, NULL, 0, "SLUS-99999");

    printf("T9 kasus error + fallback Title ID\n");
    {
        /* over32: 33 patch -> error keras. */
        size_t pn_len = 0;
        char *pn = (char *)read_file("convert/over32.pnach", &pn_len);
        unsigned char *out = NULL;
        size_t out_len = 0;
        int n_triples = -1, merged = -1;
        ws_report_t report;
        int rc;
        memset(&report, 0, sizeof(report));
        rc = ws_convert(pn, pn_len, NULL, 0, NULL, CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST, NULL,
                        &out, &out_len, &n_triples, &merged, &report);
        CHECK(rc != 0, "over32: harus gagal (33 patch)");
        CHECK(out == NULL, "over32: tidak boleh ada output");
        free(pn);
        free(out);
    }
    {
        /* noold tanpa ELF -> tidak ada patch yang bisa dikonversi. */
        size_t pn_len = 0;
        char *pn = (char *)read_file("convert/noold.pnach", &pn_len);
        unsigned char *out = NULL;
        size_t out_len = 0;
        int n_triples = -1, merged = -1;
        ws_report_t report;
        int rc;
        memset(&report, 0, sizeof(report));
        rc = ws_convert(pn, pn_len, NULL, 0, NULL, CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST, NULL,
                        &out, &out_len, &n_triples, &merged, &report);
        CHECK(rc != 0, "noold tanpa ELF: harus gagal");
        free(pn);
        free(out);
    }
    {
        /* noold DENGAN ELF -> 1 triple, old dari ELF. */
        size_t pn_len = 0;
        char *pn = (char *)read_file("convert/noold.pnach", &pn_len);
        unsigned char *out = NULL;
        size_t out_len = 0;
        int n_triples = -1, merged = -1;
        ws_report_t report;
        uint32_t old = 0;
        int rc;
        memset(&report, 0, sizeof(report));
        rc = ws_convert(pn, pn_len, elf, elf_len, "synthetic.elf", CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST,
                        NULL, &out, &out_len, &n_triples, &merged, &report);
        CHECK(rc == 0, "noold dengan ELF: harus sukses");
        CHECK(n_triples == 1, "noold dengan ELF: 1 triple, dapat %d",
              n_triples);
        /* 28 byte aliran command + 18 byte ekor watermark (Fase 3c:
           [0x00][SLUS-21159][WSM1]). */
        CHECK(out_len == 46, "noold dengan ELF: 46 byte, dapat %d",
              (int)out_len);
        if (out && out_len == 46) {
            uint32_t addr = (uint32_t)out[16] | ((uint32_t)out[17] << 8) |
                            ((uint32_t)out[18] << 16) |
                            ((uint32_t)out[19] << 24);
            uint32_t o = (uint32_t)out[20] | ((uint32_t)out[21] << 8) |
                         ((uint32_t)out[22] << 16) |
                         ((uint32_t)out[23] << 24);
            CHECK(addr == 0x00115768, "noold: alamat 0x%08x", (unsigned)addr);
            CHECK(ws_elf_word(elf, elf_len, 0x00115768, &old) == 1,
                  "noold: ELF tidak menjawab alamat");
            CHECK(o == old, "noold: old 0x%08x, ELF 0x%08x", (unsigned)o,
                  (unsigned)old);
        }
        free(pn);
        free(out);
    }
    printf("T17 prioritas old value: komentar vs ELF\n");
    {
        /* T17 (Fase 2s): komentar vs ELF beda nilai — prioritas
         * menentukan old yang dipakai. pnach dibuat inline dengan
         * old komentar = ~old ELF, jadi dua sumber pasti beda. */
        uint32_t elf_old = 0, comment_old;
        char pn[256];
        unsigned char *out = NULL;
        size_t out_len = 0;
        int n_triples = -1, merged = -1;
        ws_report_t report;
        int rc;
        CHECK(ws_elf_word(elf, elf_len, 0x00115768, &elf_old) == 1,
              "T17: ELF tidak menjawab 0x00115768");
        comment_old = ~elf_old;
        snprintf(pn, sizeof(pn),
                 "gametitle=Prio Test (SLUS-21159)\n\n"
                 "[Widescreen 16:9]\n"
                 "patch=1,EE,00115768,word,3c013fe3 //%08x\n",
                 (unsigned)comment_old);
        /* Patch-first: old dari komentar. */
        memset(&report, 0, sizeof(report));
        rc = ws_convert(pn, strlen(pn), elf, elf_len, "synthetic.elf",
                        NULL, 1, WS_OLD_PATCH_FIRST, NULL,
                        &out, &out_len, &n_triples, &merged, &report);
        CHECK(rc == 0, "T17 patch-first: harus sukses");
        CHECK(n_triples == 1, "T17 patch-first: 1 triple, dapat %d",
              n_triples);
        /* 28 byte aliran command + 18 byte ekor watermark (Fase 3c). */
        CHECK(out_len == 46, "T17 patch-first: 46 byte, dapat %d",
              (int)out_len);
        if (out && out_len == 46) {
            uint32_t o = (uint32_t)out[20] | ((uint32_t)out[21] << 8) |
                         ((uint32_t)out[22] << 16) |
                         ((uint32_t)out[23] << 24);
            CHECK(o == comment_old,
                  "T17 patch-first: old 0x%08x, komentar 0x%08x",
                  (unsigned)o, (unsigned)comment_old);
        }
        free(out);
        out = NULL;
        /* Elf-first: old dari ELF. */
        memset(&report, 0, sizeof(report));
        rc = ws_convert(pn, strlen(pn), elf, elf_len, "synthetic.elf",
                        NULL, 1, WS_OLD_ELF_FIRST, NULL,
                        &out, &out_len, &n_triples, &merged, &report);
        CHECK(rc == 0, "T17 elf-first: harus sukses");
        CHECK(n_triples == 1, "T17 elf-first: 1 triple, dapat %d",
              n_triples);
        CHECK(out_len == 46, "T17 elf-first: 46 byte, dapat %d",
              (int)out_len);
        if (out && out_len == 46) {
            uint32_t o = (uint32_t)out[20] | ((uint32_t)out[21] << 8) |
                         ((uint32_t)out[22] << 16) |
                         ((uint32_t)out[23] << 24);
            CHECK(o == elf_old,
                  "T17 elf-first: old 0x%08x, ELF 0x%08x",
                  (unsigned)o, (unsigned)elf_old);
        }
        free(out);
    }
    {
        /* Fallback Title ID dari scan: 203.14 tanpa serial gametitle,
           tapi scan tahu Title ID-nya -> merge jalan. */
        size_t pn_len = 0, exp_len = 0;
        char *pn = (char *)read_file("convert/slus_203.14.pnach", &pn_len);
        unsigned char *exp =
            read_file("convert/expected/slus_203.14.CONFIG", &exp_len);
        unsigned char *out = NULL;
        size_t out_len = 0;
        int n_triples = -1, merged = -1;
        ws_report_t report;
        int rc;
        memset(&report, 0, sizeof(report));
        rc = ws_convert(pn, pn_len, elf, elf_len, "synthetic.elf", CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST,
                        "SLUS-20314", &out, &out_len, &n_triples, &merged,
                        &report);
        CHECK(rc == 0, "fallback Title ID: harus sukses");
        CHECK(merged == 1, "fallback Title ID: merge harus jalan");
        CHECK(out_len != exp_len ||
                  (out && memcmp(out, exp, exp_len) != 0),
              "fallback Title ID: hasil merge harus beda dari patch-only");
        free(pn);
        free(exp);
        free(out);
    }

    free(elf);

    printf("T10 state file + operasi file toggle\n");
    {
        /* ws_config_path: nama ISO -> path .CONFIG. */
        char p[1024];
        ws_config_path("/dev_hdd0/PS2ISO", "MotoGP 4.iso", p, sizeof(p));
        CHECK(strcmp(p, "/dev_hdd0/PS2ISO/MotoGP 4.CONFIG") == 0,
              "config_path: %s", p);
        ws_config_path("/dev_hdd0/PS2ISO", "SLUS_211.59.iso", p, sizeof(p));
        CHECK(strcmp(p, "/dev_hdd0/PS2ISO/SLUS_211.59.CONFIG") == 0,
              "config_path: %s", p);
    }
    {
        /* State: add/remove/save/load roundtrip. */
        ws_state_t st, st2;
        const char *sp = "/tmp/ws_state_test.txt";
        remove(sp);
        ws_state_load(&st, sp);
        CHECK(st.count == 0, "state awal harus kosong");
        ws_state_add(&st, "MotoGP 4.iso");
        ws_state_add(&st, "Downhill.iso");
        ws_state_add(&st, "MotoGP 4.iso");  /* duplikat diabaikan */
        CHECK(st.count == 2, "state: 2 entri, dapat %d", st.count);
        CHECK(ws_state_has(&st, "MotoGP 4.iso"), "state: MotoGP harus ada");
        CHECK(ws_state_save(&st, sp) == 0, "state: save gagal");
        ws_state_load(&st2, sp);
        CHECK(st2.count == 2, "state: load 2 entri, dapat %d", st2.count);
        CHECK(ws_state_has(&st2, "Downhill.iso"), "state: Downhill harus ada");
        ws_state_remove(&st2, "MotoGP 4.iso");
        CHECK(!ws_state_has(&st2, "MotoGP 4.iso"), "state: MotoGP harus hilang");
        CHECK(st2.count == 1, "state: 1 entri sesudah remove");
        CHECK(ws_state_save(&st2, sp) == 0, "state: save kedua gagal");
        remove(sp);
    }
    {
        /* Toggle file ops: tulis, hapus (app-made, tanpa backup),
           hapus (manual, backup .orig, .orig dipertahankan). */
        const char *cfg = "/tmp/ws_toggle_test.CONFIG";
        const char *orig = "/tmp/ws_toggle_test.CONFIG.orig";
        unsigned char data[8] = {0x3d, 0, 0, 0, 0x57, 0x44, 0, 0};
        int backed = -1;
        remove(cfg);
        remove(orig);
        CHECK(ws_toggle_write_config(cfg, data, sizeof(data)) == 0,
              "toggle: tulis gagal");
        CHECK(ws_file_exists(cfg), "toggle: file harus ada sesudah tulis");
        CHECK(ws_toggle_remove_config(cfg, orig, 1, &backed) == 0,
              "toggle: hapus (app-made) gagal");
        CHECK(backed == 0, "toggle: app-made tidak boleh backup");
        CHECK(!ws_file_exists(cfg), "toggle: file harus hilang");
        CHECK(!ws_file_exists(orig), "toggle: .orig tidak boleh ada");
        /* Manual: tulis lagi, hapus -> backup. */
        CHECK(ws_toggle_write_config(cfg, data, sizeof(data)) == 0,
              "toggle: tulis kedua gagal");
        CHECK(ws_toggle_remove_config(cfg, orig, 0, &backed) == 0,
              "toggle: hapus (manual) gagal");
        CHECK(backed == 1, "toggle: manual harus backup");
        CHECK(ws_file_exists(orig), "toggle: .orig harus ada");
        CHECK(!ws_file_exists(cfg), "toggle: file harus hilang (manual)");
        /* .orig sudah ada: tulis + hapus lagi -> .orig TIDAK ditimpa. */
        {
            FILE *f = fopen(orig, "wb");
            const char mark[4] = {'A', 'B', 'C', 'D'};
            fwrite(mark, 1, 4, f);
            fclose(f);
        }
        CHECK(ws_toggle_write_config(cfg, data, sizeof(data)) == 0,
              "toggle: tulis ketiga gagal");
        CHECK(ws_toggle_remove_config(cfg, orig, 0, &backed) == 0,
              "toggle: hapus kedua (manual) gagal");
        CHECK(backed == 0, "toggle: .orig yang ada tidak boleh ditimpa");
        {
            FILE *f = fopen(orig, "rb");
            char mark[4] = {0};
            size_t got = fread(mark, 1, 4, f);
            (void)got;
            fclose(f);
            CHECK(memcmp(mark, "ABCD", 4) == 0,
                  "toggle: isi .orig harus tetap ABCD");
        }
        remove(cfg);
        remove(orig);
    }
    {
        /* Fase 3b: backup .orig SEBELUM tulis menimpa .CONFIG custom.
           Aturan sama kayak hapus: backup cuma kalau bukan buatan app
           dan .orig belum ada; .orig yang ada tidak ditimpa; file
           buatan app / file belum ada = tanpa backup. */
        const char *cfg = "/tmp/ws_toggle_test.CONFIG";
        const char *orig = "/tmp/ws_toggle_test.CONFIG.orig";
        unsigned char old_data[4] = {'O', 'L', 'D', '!'};
        unsigned char new_data[8] = {0x3d, 0, 0, 0, 0x57, 0x44, 0, 0};
        int backed = -1;
        remove(cfg);
        remove(orig);
        /* Belum ada file: backup no-op, tanpa .orig. */
        CHECK(ws_toggle_backup_custom_config(cfg, orig, 0, &backed) == 0,
              "3b: backup tanpa file gagal");
        CHECK(backed == 0, "3b: tanpa file tidak boleh backup");
        CHECK(!ws_file_exists(orig), "3b: .orig tidak boleh ada");
        /* Custom + belum ada .orig: backup jalan, lalu tulis baru. */
        CHECK(ws_toggle_write_config(cfg, old_data, sizeof(old_data)) == 0,
              "3b: tulis custom gagal");
        CHECK(ws_toggle_backup_custom_config(cfg, orig, 0, &backed) == 0,
              "3b: backup custom gagal");
        CHECK(backed == 1, "3b: custom harus backup");
        CHECK(ws_file_exists(orig), "3b: .orig harus ada");
        CHECK(ws_toggle_write_config(cfg, new_data, sizeof(new_data)) == 0,
              "3b: tulis baru gagal");
        {
            FILE *f = fopen(orig, "rb");
            char mark[4] = {0};
            size_t got = fread(mark, 1, 4, f);
            (void)got;
            fclose(f);
            CHECK(memcmp(mark, "OLD!", 4) == 0,
                  "3b: isi .orig harus tetap OLD!");
        }
        {
            FILE *f = fopen(cfg, "rb");
            unsigned char cur[8] = {0};
            size_t got = fread(cur, 1, 8, f);
            (void)got;
            fclose(f);
            CHECK(memcmp(cur, new_data, 8) == 0,
                  "3b: .CONFIG harus berisi data baru");
        }
        /* .orig sudah ada: backup dilewat, isinya dipertahankan. */
        CHECK(ws_toggle_backup_custom_config(cfg, orig, 0, &backed) == 0,
              "3b: backup kedua gagal");
        CHECK(backed == 0, "3b: .orig yang ada tidak boleh ditimpa");
        /* Buatan app: tanpa backup, .orig tidak dibuat. */
        remove(orig);
        CHECK(ws_toggle_backup_custom_config(cfg, orig, 1, &backed) == 0,
              "3b: backup app-made gagal");
        CHECK(backed == 0, "3b: app-made tidak boleh backup");
        CHECK(!ws_file_exists(orig), "3b: app-made tidak boleh buat .orig");
        remove(cfg);
        remove(orig);
    }

    printf("T11 editor patch: ws_patch_list (Downhill)\\n");
    {
        /* Downhill: 35 baris [Widescreen 16:9] + 1 [No-Interlacing],
           semuanya patch=1. */
        size_t pn_len = 0;
        char *pn = (char *)read_file("convert/downhill.pnach", &pn_len);
        static ws_patch_item_t items[WS_MAX_PATCH_ITEMS];
        int n, i, n_ws = 0, n_other = 0;
        CHECK(pn != NULL, "T11: downhill.pnach tidak bisa dibaca");
        n = ws_patch_list(pn, pn_len, items, WS_MAX_PATCH_ITEMS);
        CHECK(n == 36, "T11: 36 baris patch, dapat %d", n);
        for (i = 0; i < n; i++) {
            if (items[i].ws_kind == 2) n_ws++;
            else n_other++;
            CHECK(items[i].enabled == 1,
                  "T11: baris %d harus patch=1", i);
        }
        CHECK(n_ws == 35, "T11: 35 widescreen, dapat %d", n_ws);
        CHECK(n_other == 1, "T11: 1 non-widescreen, dapat %d", n_other);
        CHECK(strcmp(items[35].group, "No-Interlacing") == 0,
              "T11: baris terakhir grup %s", items[35].group);
        CHECK(strstr(items[0].line, "101B9EF0") != NULL,
              "T11: baris pertama harus ada alamatnya");
        free(pn);
    }

    printf("T12 editor patch: filter widescreen-only vs check-all\\n");
    {
        /* Default editor + toggle ON = widescreen-only: 25 triple,
           alamat No-Interlacing 0x243D34 TIDAK ikut. Check-all =
           perilaku lama: 26 triple, 0x243D34 ikut. */
        size_t pn_len = 0;
        char *pn = (char *)read_file("convert/downhill.pnach", &pn_len);
        static ws_patch_item_t items[WS_MAX_PATCH_ITEMS];
        static int checked[WS_MAX_PATCH_ITEMS];
        int n, i;
        /* ELF sudah di-free sesudah T9 — baca lagi untuk T12. */
        elf = read_file("convert/synthetic.elf", &elf_len);
        CHECK(elf != NULL, "T12: ELF sintetis tidak bisa dibaca");
        CHECK(pn != NULL, "T12: downhill.pnach tidak bisa dibaca");
        n = ws_patch_list(pn, pn_len, items, WS_MAX_PATCH_ITEMS);
        {
            /* Widescreen-only. */
            char *ftext = NULL;
            size_t ftext_len = 0;
            unsigned char *out = NULL;
            size_t out_len = 0;
            int n_triples = -1, merged = -1;
            ws_report_t report;
            int rc, has_ni = 0;
            memset(&report, 0, sizeof(report));
            for (i = 0; i < n; i++)
                checked[i] = items[i].ws_kind == 2 && items[i].enabled;
            CHECK(ws_patch_filtered_text(pn, pn_len, items, checked, n,
                                         &ftext, &ftext_len) == 0,
                  "T12 ws-only: filter gagal");
            rc = ws_convert(ftext, ftext_len, elf, elf_len,
                            "synthetic.elf", CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST, NULL,
                            &out, &out_len, &n_triples, &merged, &report);
            CHECK(rc == 0, "T12 ws-only: ws_convert gagal (rc=%d)", rc);
            CHECK(n_triples == 25, "T12 ws-only: 25 triple, dapat %d",
                  n_triples);
            if (out && n_triples > 0) {
                int j;
                for (j = 0; j < n_triples; j++) {
                    uint32_t a = (uint32_t)out[16 + j * 12] |
                                 ((uint32_t)out[17 + j * 12] << 8) |
                                 ((uint32_t)out[18 + j * 12] << 16) |
                                 ((uint32_t)out[19 + j * 12] << 24);
                    if ((a & 0x0FFFFFFF) == 0x00243D34) has_ni = 1;
                }
            }
            CHECK(!has_ni, "T12 ws-only: No-Interlacing tidak boleh ikut");
            free(ftext);
            free(out);
        }
        {
            /* Check-all (semua patch=1): 26 triple, 0x243D34 ikut. */
            char *ftext = NULL;
            size_t ftext_len = 0;
            unsigned char *out = NULL;
            size_t out_len = 0;
            int n_triples = -1, merged = -1;
            ws_report_t report;
            int rc, has_ni = 0;
            memset(&report, 0, sizeof(report));
            for (i = 0; i < n; i++) checked[i] = items[i].enabled;
            CHECK(ws_patch_filtered_text(pn, pn_len, items, checked, n,
                                         &ftext, &ftext_len) == 0,
                  "T12 all: filter gagal");
            rc = ws_convert(ftext, ftext_len, elf, elf_len,
                            "synthetic.elf", CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST, NULL,
                            &out, &out_len, &n_triples, &merged, &report);
            CHECK(rc == 0, "T12 all: ws_convert gagal (rc=%d)", rc);
            CHECK(n_triples == 26, "T12 all: 26 triple, dapat %d",
                  n_triples);
            if (out && n_triples > 0) {
                int j;
                for (j = 0; j < n_triples; j++) {
                    uint32_t a = (uint32_t)out[16 + j * 12] |
                                 ((uint32_t)out[17 + j * 12] << 8) |
                                 ((uint32_t)out[18 + j * 12] << 16) |
                                 ((uint32_t)out[19 + j * 12] << 24);
                    if ((a & 0x0FFFFFFF) == 0x00243D34) has_ni = 1;
                }
            }
            CHECK(has_ni, "T12 all: No-Interlacing harus ikut");
            free(ftext);
            free(out);
        }
        free(pn);
        free(elf);
    }

    printf("T13 baca balik .CONFIG (editor ingat pilihan user)\\n");
    {
        /* Downhill: 26 baris convertible (25 widescreen + 1 No-Interlacing),
           10 tidak (9 sub-word + 1 kondisional E). Baca balik .CONFIG
           penuh (26 triple) -> 26 alamat, 0x243D34 ikut. Baca balik
           .CONFIG widescreen-only (25 triple) -> 25 alamat, 0x243D34
           tidak ikut, dan matching editor memberi 25 centang. */
        size_t pn_len = 0;
        char *pn = (char *)read_file("convert/downhill.pnach", &pn_len);
        static ws_patch_item_t items[WS_MAX_PATCH_ITEMS];
        static int checked[WS_MAX_PATCH_ITEMS];
        static uint32_t addrs[64];
        int n, i, n_conv = 0, n_addr;
        CHECK(pn != NULL, "T13: downhill.pnach tidak bisa dibaca");
        n = ws_patch_list(pn, pn_len, items, WS_MAX_PATCH_ITEMS);
        for (i = 0; i < n; i++)
            if (items[i].convertible) n_conv++;
        CHECK(n_conv == 26, "T13: 26 convertible, dapat %d", n_conv);
        CHECK(items[35].convertible == 1,
              "T13: No-Interlacing (word) harus convertible");
        /* .CONFIG penuh (ekspektasi, 26 triple). */
        n_addr = ws_config_triple_addrs("convert/expected/downhill.CONFIG",
                                        addrs, 64);
        CHECK(n_addr == 26, "T13 full: 26 alamat, dapat %d", n_addr);
        {
            int has_ni = 0;
            for (i = 0; i < n_addr; i++)
                if (addrs[i] == 0x00243D34) has_ni = 1;
            CHECK(has_ni, "T13 full: 0x243D34 harus ada");
        }
        /* Matching editor terhadap .CONFIG penuh: 26 centang. */
        {
            int n_checked = 0;
            for (i = 0; i < n; i++) {
                int j, found = 0;
                if (!items[i].convertible) { checked[i] = 0; continue; }
                for (j = 0; j < n_addr; j++)
                    if (addrs[j] == items[i].addr) found = 1;
                checked[i] = found;
                if (found) n_checked++;
            }
            CHECK(n_checked == 26, "T13 full: 26 centang, dapat %d",
                  n_checked);
            CHECK(checked[35] == 1, "T13 full: No-Interlacing tercentang");
        }
        /* .CONFIG widescreen-only: bangun, tulis, baca balik. */
        {
            char *ftext = NULL;
            size_t ftext_len = 0;
            unsigned char *out = NULL;
            size_t out_len = 0;
            int n_triples = -1, merged = -1;
            ws_report_t report;
            int rc, n_addr2, n_checked = 0;
            FILE *f;
            memset(&report, 0, sizeof(report));
            elf = read_file("convert/synthetic.elf", &elf_len);
            CHECK(elf != NULL, "T13 ws: ELF tidak bisa dibaca");
            for (i = 0; i < n; i++)
                checked[i] = items[i].ws_kind == 2 && items[i].enabled;
            CHECK(ws_patch_filtered_text(pn, pn_len, items, checked, n,
                                         &ftext, &ftext_len) == 0,
                  "T13 ws: filter gagal");
            rc = ws_convert(ftext, ftext_len, elf, elf_len,
                            "synthetic.elf", CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST, NULL,
                            &out, &out_len, &n_triples, &merged, &report);
            CHECK(rc == 0, "T13 ws: ws_convert gagal (rc=%d)", rc);
            CHECK(n_triples == 25, "T13 ws: 25 triple, dapat %d",
                  n_triples);
            f = fopen("/tmp/ws_readback_ws.CONFIG", "wb");
            CHECK(f != NULL, "T13 ws: tidak bisa tulis .CONFIG sementara");
            if (f) { fwrite(out, 1, out_len, f); fclose(f); }
            n_addr2 = ws_config_triple_addrs("/tmp/ws_readback_ws.CONFIG",
                                             addrs, 64);
            CHECK(n_addr2 == 25, "T13 ws: 25 alamat, dapat %d", n_addr2);
            {
                int has_ni = 0;
                for (i = 0; i < n_addr2; i++)
                    if (addrs[i] == 0x00243D34) has_ni = 1;
                CHECK(!has_ni, "T13 ws: 0x243D34 tidak boleh ada");
            }
            for (i = 0; i < n; i++) {
                int j, found = 0;
                if (!items[i].convertible) { checked[i] = 0; continue; }
                for (j = 0; j < n_addr2; j++)
                    if (addrs[j] == items[i].addr) found = 1;
                checked[i] = found;
                if (found) n_checked++;
            }
            CHECK(n_checked == 25, "T13 ws: 25 centang, dapat %d",
                  n_checked);
            CHECK(checked[35] == 0,
                  "T13 ws: No-Interlacing tidak tercentang");
            free(ftext);
            free(out);
            free(elf);
            remove("/tmp/ws_readback_ws.CONFIG");
        }
        /* File tidak ada -> -1 (jatuh ke default). */
        CHECK(ws_config_triple_addrs("/tmp/ws_tidak_ada.CONFIG", addrs, 64)
                  == -1,
              "T13: file tidak ada harus -1");
        free(pn);
    }

    printf("T14 filter dua lapis (Fase 2p): 16:9 vs wide lain\n");
    {
        /* Lapis 1 (16:9) = nama grup mengandung bentuk rasio 16:9.
           Lapis 2 = mengandung "wide" tapi bukan 16:9. Default
           centang editor + toggle X = lapis 1 saja. */
        static const char pn14[] =
            "gametitle=Test Filter (SLUS-00000)\n"
            "\n[Widescreen 16:9]\n"
            "patch=1,EE,00100000,word,3C013F80\n"
            "\n[Widescreen 16:9 (Gameplay+HUD) (720p)]\n"
            "patch=1,EE,00100004,word,3C013F80\n"
            "\n[Widescreen 16:10]\n"
            "patch=1,EE,00100008,word,3C013F80 //00000000\n"
            "\n[Widescreen 15:10]\n"
            "patch=1,EE,0010000C,word,3C013F80\n"
            "\n[Widescreen 21:9]\n"
            "patch=1,EE,00100010,word,3C013F80\n"
            "\n[Wide Screen]\n"
            "patch=1,EE,00100014,word,3C013F80\n"
            "\n[60 FPS]\n"
            "patch=1,EE,00100018,word,3C013F80 //00000000\n"
            "\n[Widen headlight beam]\n"
            "patch=1,EE,0010001C,word,3C013F80\n";
        static ws_patch_item_t items[WS_MAX_PATCH_ITEMS];
        int n, i, n_k2 = 0, n_k1 = 0, n_k0 = 0;
        CHECK(ws_group_kind("Widescreen 16:9") == 2, "T14: 16:9 = 2");
        CHECK(ws_group_kind("Widescreen 16:9 2") == 2, "T14: 16:9 2 = 2");
        CHECK(ws_group_kind("Widescreen 16/9") == 2, "T14: 16/9 = 2");
        CHECK(ws_group_kind("Widescreen 16_9") == 2, "T14: 16_9 = 2");
        CHECK(ws_group_kind("WIDESCREEN 16:9") == 2, "T14: huruf besar = 2");
        CHECK(ws_group_kind("Widescreen 16:10") == 1, "T14: 16:10 = 1");
        CHECK(ws_group_kind("Widescreen 15:10") == 1, "T14: 15:10 = 1");
        CHECK(ws_group_kind("Widescreen 21:9") == 1, "T14: 21:9 = 1");
        CHECK(ws_group_kind("Wide Screen") == 1, "T14: Wide Screen = 1");
        CHECK(ws_group_kind("60 FPS") == 0, "T14: 60 FPS = 0");
        CHECK(ws_group_kind("No-Interlacing") == 0, "T14: No-Interlacing = 0");
        n = ws_patch_list(pn14, sizeof(pn14) - 1, items,
                          WS_MAX_PATCH_ITEMS);
        CHECK(n == 8, "T14: 8 baris, dapat %d", n);
        for (i = 0; i < n; i++) {
            if (items[i].ws_kind == 2) n_k2++;
            else if (items[i].ws_kind == 1) n_k1++;
            else n_k0++;
        }
        CHECK(n_k2 == 2, "T14: 2 lapis 1, dapat %d", n_k2);
        CHECK(n_k1 == 5, "T14: 5 lapis 2, dapat %d", n_k1);
        CHECK(n_k0 == 1, "T14: 1 bukan-wide, dapat %d", n_k0);
        /* Default centang = lapis 1 saja: baris 0-1 tercentang. */
        CHECK(items[0].ws_kind == 2 && items[1].ws_kind == 2,
              "T14: dua baris 16:9 harus lapis 1");
        CHECK(items[2].ws_kind == 1 && items[5].ws_kind == 1,
              "T14: 16:10 dan Wide Screen harus lapis 2");
        /* Aturan Fendi (2p): non-16:9 TIDAK di-gate — default OFF
           saja, user boleh centang manual di editor. Buktikan:
           centang hanya baris lapis 2 ([Widescreen 16:10]) +
           bukan-wide ([60 FPS]) — konversi harus menghasilkan
           2 triple (old value dari komentar //, tidak butuh ELF). */
        {
            static int checked2[WS_MAX_PATCH_ITEMS];
            char *ftext = NULL;
            size_t ftext_len = 0;
            unsigned char *out = NULL;
            size_t out_len = 0;
            int n_triples = -1, merged = -1;
            ws_report_t report;
            int rc, i2;
            memset(&report, 0, sizeof(report));
            for (i2 = 0; i2 < n; i2++) checked2[i2] = 0;
            checked2[2] = 1;  /* Widescreen 16:10 */
            checked2[6] = 1;  /* 60 FPS */
            CHECK(ws_patch_filtered_text(pn14, sizeof(pn14) - 1, items,
                                         checked2, n, &ftext,
                                         &ftext_len) == 0,
                  "T14 nogate: filter gagal");
            elf = read_file("convert/synthetic.elf", &elf_len);
            CHECK(elf != NULL, "T14 nogate: ELF tidak bisa dibaca");
            rc = ws_convert(ftext, ftext_len, elf, elf_len,
                            "synthetic.elf", CONFIGDB_DIR, 1, WS_OLD_PATCH_FIRST, NULL,
                            &out, &out_len, &n_triples, &merged, &report);
            CHECK(rc == 0, "T14 nogate: ws_convert gagal (rc=%d)", rc);
            CHECK(n_triples == 2, "T14 nogate: 2 triple, dapat %d",
                  n_triples);
            free(ftext);
            free(out);
            free(elf);
        }
    }

    printf("T21 watermark WSM1 di ekor .CONFIG (Fase 3c)\n");
    {
        /* Patch-only: ekor = [0x00][TitleID][WSM1], 18 byte.
           Deteksi: ws_config_has_watermark == 1; file tanpa marker
           == 0; file tidak ada / tidak bisa di-parse == -1.
           Baca balik triple tetap jalan di file ber-watermark. */
        char pn[256];
        unsigned char *out = NULL;
        size_t out_len = 0;
        int n_triples = -1, merged = -1;
        ws_report_t report;
        int rc;
        static uint32_t addrs[64];
        FILE *f;
        snprintf(pn, sizeof(pn),
                 "gametitle=Watermark Test (SLUS-21159)\n\n"
                 "[Widescreen 16:9]\n"
                 "patch=1,EE,00115768,word,3c013fe3 //00000000\n");
        memset(&report, 0, sizeof(report));
        rc = ws_convert(pn, strlen(pn), NULL, 0, NULL, NULL, 1,
                        WS_OLD_PATCH_FIRST, NULL,
                        &out, &out_len, &n_triples, &merged, &report);
        CHECK(rc == 0, "T21: ws_convert gagal (rc=%d)", rc);
        CHECK(n_triples == 1, "T21: 1 triple, dapat %d", n_triples);
        CHECK(out_len == 46, "T21: 28 + 18 ekor = 46 byte, dapat %d",
              (int)out_len);
        if (out && out_len == 46) {
            CHECK(out[28] == 0 && out[29] == 0 && out[30] == 0 &&
                      out[31] == 0,
                  "T21: ekor harus mulai 0x00");
            CHECK(memcmp(out + 32, "SLUS-21159", 10) == 0,
                  "T21: title di ekor harus SLUS-21159");
            CHECK(memcmp(out + 42, "WSM1", 4) == 0,
                  "T21: 4 byte terakhir harus WSM1");
            f = fopen("/tmp/ws_wm.CONFIG", "wb");
            CHECK(f != NULL, "T21: tidak bisa tulis .CONFIG sementara");
            if (f) { fwrite(out, 1, out_len, f); fclose(f); }
            CHECK(ws_config_has_watermark("/tmp/ws_wm.CONFIG") == 1,
                  "T21: file ber-watermark harus terdeteksi");
            {
                int n_addr = ws_config_triple_addrs("/tmp/ws_wm.CONFIG",
                                                    addrs, 64);
                CHECK(n_addr == 1, "T21 readback: 1 alamat, dapat %d",
                      n_addr);
                if (n_addr == 1)
                    CHECK(addrs[0] == 0x00115768,
                          "T21 readback: alamat 0x%08x", (unsigned)addrs[0]);
            }
            remove("/tmp/ws_wm.CONFIG");
            /* Tanpa 18 byte ekor: file valid tapi tanpa marker -> 0. */
            f = fopen("/tmp/ws_wm.CONFIG", "wb");
            CHECK(f != NULL, "T21: tidak bisa tulis file tanpa marker");
            if (f) { fwrite(out, 1, 28, f); fclose(f); }
            CHECK(ws_config_has_watermark("/tmp/ws_wm.CONFIG") == 0,
                  "T21: file tanpa marker harus 0");
            remove("/tmp/ws_wm.CONFIG");
        }
        free(out);
        /* File ekspektasi (tool Python, ekor Title ID tanpa WSM1): 0. */
        CHECK(ws_config_has_watermark(
                  "convert/expected/SLUS_211.52.CONFIG") == 0,
              "T21: file Python (tanpa WSM1) harus 0");
        /* File tidak ada / sampah: -1. */
        CHECK(ws_config_has_watermark("/tmp/ws_tidak_ada.CONFIG") == -1,
              "T21: file tidak ada harus -1");
        f = fopen("/tmp/ws_sampah.CONFIG", "wb");
        if (f) { const char j[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'}; fwrite(j, 1, 8, f); fclose(f); }
        CHECK(ws_config_has_watermark("/tmp/ws_sampah.CONFIG") == -1,
              "T21: file sampah harus -1");
        remove("/tmp/ws_sampah.CONFIG");
        /* Merge + watermark: ekor DB (punya title) + "WSM1". */
        {
            size_t pn_len = 0;
            char *pn2 = (char *)read_file("convert/slus_211.52.pnach",
                                          &pn_len);
            unsigned char *out2 = NULL;
            size_t out2_len = 0;
            int n2 = -1, m2 = -1;
            ws_report_t rep2;
            FILE *f2;
            CHECK(pn2 != NULL, "T21 merge: pnach tidak bisa dibaca");
            memset(&rep2, 0, sizeof(rep2));
            elf = read_file("convert/synthetic.elf", &elf_len);
            CHECK(elf != NULL, "T21 merge: ELF tidak bisa dibaca");
            rc = ws_convert(pn2, pn_len, elf, elf_len, "synthetic.elf",
                            CONFIGDB_DIR, 0, WS_OLD_PATCH_FIRST, NULL,
                            &out2, &out2_len, &n2, &m2, &rep2);
            CHECK(rc == 0, "T21 merge: ws_convert gagal (rc=%d)", rc);
            CHECK(m2 == 1, "T21 merge: harus merge, dapat %d", m2);
            if (out2 && out2_len >= 4) {
                CHECK(memcmp(out2 + out2_len - 4, "WSM1", 4) == 0,
                      "T21 merge: 4 byte terakhir harus WSM1");
                f2 = fopen("/tmp/ws_wm_merge.CONFIG", "wb");
                CHECK(f2 != NULL, "T21 merge: tidak bisa tulis file");
                if (f2) { fwrite(out2, 1, out2_len, f2); fclose(f2); }
                CHECK(ws_config_has_watermark("/tmp/ws_wm_merge.CONFIG")
                          == 1,
                      "T21 merge: file merge harus terdeteksi");
                remove("/tmp/ws_wm_merge.CONFIG");
            }
            free(pn2);
            free(out2);
            free(elf);
        }
    }

    printf("T22 restore .CONFIG.orig (Fase 3c)\n");
    {
        /* Kembalikan backup ke .CONFIG (dipindah, .orig hilang);
           tanpa .orig = no-op. */
        const char *cfg = "/tmp/ws_restore_test.CONFIG";
        const char *orig = "/tmp/ws_restore_test.CONFIG.orig";
        unsigned char data[4] = {'F', 'I', 'X', '!'};
        remove(cfg);
        remove(orig);
        CHECK(ws_toggle_restore_orig(cfg, orig) == 0,
              "T22: tanpa .orig harus 0");
        CHECK(!ws_file_exists(cfg), "T22: .CONFIG tidak boleh muncul");
        CHECK(ws_toggle_write_config(orig, data, sizeof(data)) == 0,
              "T22: tulis .orig gagal");
        CHECK(ws_toggle_restore_orig(cfg, orig) == 1,
              "T22: restore harus 1");
        CHECK(ws_file_exists(cfg), "T22: .CONFIG harus ada sesudah restore");
        CHECK(!ws_file_exists(orig),
              "T22: .orig harus hilang sesudah restore");
        {
            FILE *f = fopen(cfg, "rb");
            char mark[4] = {0};
            size_t got = fread(mark, 1, 4, f);
            (void)got;
            fclose(f);
            CHECK(memcmp(mark, "FIX!", 4) == 0,
                  "T22: isi .CONFIG harus FIX!");
        }
        remove(cfg);
        remove(orig);
    }

    if (failures == 0)
        printf("Semua tes konverter lolos: 0 kegagalan\n");
    else
        printf("Tes konverter: %d kegagalan\n", failures);
    return failures ? 1 : 0;
}
