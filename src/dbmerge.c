/* Parser CONFIG database + merge GUSUR. Port dari pnach2config.cpp. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbmerge.h"

#define CMD_TYPE1 0x09
#define CMD_PATCH 0x0a
#define CMD_OVERLAY 0x42
#define CMD_TITLE 0x00
#define CMD_REV 0x3d

typedef enum { SP_FIXED, SP_COUNT, SP_OB, SP_X12, SP_X42, SP_FOURX, SP_UNKNOWN } spec_kind_t;

typedef struct {
    spec_kind_t kind;
    int n;  /* Fixed: jumlah param. Count: word per entri. */
} spec_t;

/* Tabel command 0x00..0x50 — dari psdevwiki Config Commands, divalidasi
 * dengan jalan di semua 1549 entry database. */
static spec_t cmd_spec(uint32_t cmd)
{
    static const spec_t table[0x51] = {
        {SP_UNKNOWN, 0},  /* 0x00 */
        {SP_FIXED, 2},    /* 0x01 */
        {SP_FIXED, 1},    /* 0x02 */
        {SP_FIXED, 0},    /* 0x03 */
        {SP_FIXED, 1},    /* 0x04 */
        {SP_FIXED, 0},    /* 0x05 */
        {SP_FIXED, 0},    /* 0x06 */
        {SP_FIXED, 1},    /* 0x07 */
        {SP_FIXED, 8},    /* 0x08 */
        {SP_COUNT, 5},    /* 0x09 */
        {SP_COUNT, 3},    /* 0x0a */
        {SP_OB, 0},       /* 0x0b */
        {SP_FIXED, 1},    /* 0x0c */
        {SP_FIXED, 1},    /* 0x0d */
        {SP_FIXED, 1},    /* 0x0e */
        {SP_FIXED, 2},    /* 0x0f */
        {SP_FIXED, 2},    /* 0x10 */
        {SP_FIXED, 1},    /* 0x11 */
        {SP_X12, 0},      /* 0x12 */
        {SP_FIXED, 2},    /* 0x13 */
        {SP_FIXED, 0},    /* 0x14 */
        {SP_FIXED, 1},    /* 0x15 */
        {SP_FIXED, 0},    /* 0x16 */
        {SP_FIXED, 1},    /* 0x17 */
        {SP_FIXED, 0},    /* 0x18 */
        {SP_FIXED, 0},    /* 0x19 */
        {SP_FIXED, 0},    /* 0x1a */
        {SP_FIXED, 0},    /* 0x1b */
        {SP_FIXED, 1},    /* 0x1c */
        {SP_FIXED, 1},    /* 0x1d */
        {SP_FIXED, 1},    /* 0x1e */
        {SP_FIXED, 1},    /* 0x1f */
        {SP_FIXED, 2},    /* 0x20 */
        {SP_FIXED, 1},    /* 0x21 */
        {SP_FIXED, 0},    /* 0x22 */
        {SP_FIXED, 0},    /* 0x23 */
        {SP_FIXED, 2},    /* 0x24 */
        {SP_FIXED, 0},    /* 0x25 */
        {SP_FIXED, 2},    /* 0x26 */
        {SP_FIXED, 2},    /* 0x27 */
        {SP_FIXED, 1},    /* 0x28 */
        {SP_FIXED, 2},    /* 0x29 */
        {SP_FIXED, 0},    /* 0x2a */
        {SP_FIXED, 0},    /* 0x2b */
        {SP_FIXED, 1},    /* 0x2c */
        {SP_FIXED, 0},    /* 0x2d */
        {SP_FIXED, 1},    /* 0x2e */
        {SP_FIXED, 1},    /* 0x2f */
        {SP_FIXED, 0},    /* 0x30 */
        {SP_FIXED, 0},    /* 0x31 */
        {SP_FIXED, 0},    /* 0x32 */
        {SP_FIXED, 0},    /* 0x33 */
        {SP_FIXED, 0},    /* 0x34 */
        {SP_FIXED, 0},    /* 0x35 */
        {SP_FIXED, 0},    /* 0x36 */
        {SP_FIXED, 0},    /* 0x37 */
        {SP_FIXED, 0},    /* 0x38 */
        {SP_FIXED, 0},    /* 0x39 */
        {SP_FIXED, 0},    /* 0x3a */
        {SP_FIXED, 0},    /* 0x3b */
        {SP_FIXED, 0},    /* 0x3c */
        {SP_FIXED, 1},    /* 0x3d */
        {SP_FIXED, 0},    /* 0x3e */
        {SP_FIXED, 1},    /* 0x3f */
        {SP_FIXED, 0},    /* 0x40 */
        {SP_FIXED, 0},    /* 0x41 */
        {SP_X42, 0},      /* 0x42 */
        {SP_FIXED, 1},    /* 0x43 */
        {SP_FIXED, 0},    /* 0x44 */
        {SP_FIXED, 0},    /* 0x45 */
        {SP_FIXED, 0},    /* 0x46 */
        {SP_FIXED, 0},    /* 0x47 */
        {SP_FIXED, 2},    /* 0x48 */
        {SP_FIXED, 0},    /* 0x49 */
        {SP_FIXED, 0},    /* 0x4a */
        {SP_FOURX, 0},    /* 0x4b */
        {SP_FOURX, 0},    /* 0x4c */
        {SP_FIXED, 1},    /* 0x4d */
        {SP_FIXED, 0},    /* 0x4e */
        {SP_FIXED, 0},    /* 0x4f */
        {SP_FIXED, 0},    /* 0x50 */
    };
    if (cmd > 0x50) return (spec_t){SP_UNKNOWN, 0};
    return table[cmd];
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Ekor (sesudah 0x00) sah kalau kosong, semua nol, atau string Title ID
 * "XXXX-12345". */
static int tail_title_ok(const unsigned char *rest, size_t n)
{
    size_t i;
    int all_zero = 1, k;
    if (n == 0) return 1;
    for (i = 0; i < n; i++)
        if (rest[i] != 0) all_zero = 0;
    if (all_zero) return 1;
    if (n < 10) return 0;
    for (k = 0; k < 4; k++)
        if (rest[k] < 'A' || rest[k] > 'Z') return 0;
    if (rest[4] != '-') return 0;
    for (k = 0; k < 5; k++)
        if (rest[5 + k] < '0' || rest[5 + k] > '9') return 0;
    return 1;
}

int ws_db_walk(const unsigned char *data, size_t len,
               ws_db_cmd_t **cmds_out, int *n_cmds,
               uint32_t **words_buf,
               const unsigned char **tail_out, size_t *tail_len,
               char *err, size_t err_size)
{
    size_t nwords, cap = 16, count = 0, i = 0;
    uint32_t *words;
    ws_db_cmd_t *cmds;
    *cmds_out = NULL;
    *n_cmds = 0;
    *words_buf = NULL;
    *tail_out = NULL;
    *tail_len = 0;
    if (len < 4) {
        snprintf(err, err_size, "file too short");
        return -1;
    }
    nwords = len / 4;
    words = (uint32_t *)malloc(nwords * sizeof(uint32_t));
    cmds = (ws_db_cmd_t *)malloc(cap * sizeof(ws_db_cmd_t));
    if (!words || !cmds) {
        free(words);
        free(cmds);
        snprintf(err, err_size, "out of memory");
        return -1;
    }
    for (i = 0; i < nwords; i++) words[i] = le32(data + i * 4);
    i = 0;
    while (i < nwords) {
        uint32_t cmd = words[i];
        spec_t spec;
        uint64_t step = 0;
        if (cmd == CMD_TITLE) {
            size_t off = i * 4 + 4;
            const unsigned char *rest = off < len ? data + off : NULL;
            size_t rest_n = off < len ? len - off : 0;
            if (tail_title_ok(rest, rest_n)) {
                *cmds_out = cmds;
                *n_cmds = (int)count;
                *words_buf = words;
                *tail_out = data + i * 4;
                *tail_len = len - i * 4;
                return 0;
            }
            snprintf(err, err_size,
                     "0x00 tail is not a TitleID string or zeros");
            free(words);
            free(cmds);
            return -1;
        }
        spec = cmd_spec(cmd);
        if (spec.kind == SP_UNKNOWN) {
            snprintf(err, err_size, "unknown command 0x%02x at word %llu",
                     (unsigned)cmd, (unsigned long long)i);
            free(words);
            free(cmds);
            return -1;
        }
        if (spec.kind == SP_COUNT) {
            if (i + 1 >= nwords) {
                snprintf(err, err_size,
                         "truncated count for command 0x%02x",
                         (unsigned)cmd);
                free(words);
                free(cmds);
                return -1;
            }
            step = 2 + (uint64_t)words[i + 1] * (uint64_t)spec.n;
        } else if (spec.kind == SP_OB) {
            uint64_t cnt, j, k;
            if (i + 1 >= nwords) {
                snprintf(err, err_size, "truncated count for command 0x0b");
                free(words);
                free(cmds);
                return -1;
            }
            cnt = words[i + 1];
            j = i + 2;
            for (k = 0; k < cnt; k++) {
                if (j + 2 >= nwords) {
                    snprintf(err, err_size, "truncated 0x0b entry");
                    free(words);
                    free(cmds);
                    return -1;
                }
                j += 3 + words[j + 2] / 2;
            }
            step = j - i;
        } else if (spec.kind == SP_X12) {
            uint64_t cnt;
            if (i + 1 >= nwords) {
                snprintf(err, err_size, "truncated count for command 0x12");
                free(words);
                free(cmds);
                return -1;
            }
            cnt = words[i + 1];
            if (cnt < 2) {
                snprintf(err, err_size, "0x12 count %llu < 2",
                         (unsigned long long)cnt);
                free(words);
                free(cmds);
                return -1;
            }
            step = cnt + 2;
        } else if (spec.kind == SP_X42) {
            if (i + 2 >= nwords) {
                snprintf(err, err_size, "truncated command 0x42");
                free(words);
                free(cmds);
                return -1;
            }
            step = 3 + words[i + 2];
        } else if (spec.kind == SP_FOURX) {
            uint32_t p2;
            uint64_t nid;
            if (i + 2 >= nwords) {
                snprintf(err, err_size, "truncated command 0x%02x",
                         (unsigned)cmd);
                free(words);
                free(cmds);
                return -1;
            }
            p2 = words[i + 2];
            nid = (p2 >= 1 && p2 <= 8) ? p2 : 3;
            step = 3 + nid + 1;
        } else {
            step = 1 + (uint64_t)spec.n;
        }
        if (i + step > nwords) {
            snprintf(err, err_size, "truncated command 0x%02x",
                     (unsigned)cmd);
            free(words);
            free(cmds);
            return -1;
        }
        if (count == cap) {
            ws_db_cmd_t *nc;
            cap *= 2;
            nc = (ws_db_cmd_t *)realloc(cmds, cap * sizeof(ws_db_cmd_t));
            if (!nc) {
                free(words);
                free(cmds);
                snprintf(err, err_size, "out of memory");
                return -1;
            }
            cmds = nc;
        }
        cmds[count].id = cmd;
        cmds[count].words = words + i;
        cmds[count].nwords = (size_t)step;
        count++;
        i += (size_t)step;
    }
    if (len % 4) {
        snprintf(err, err_size, "trailing bytes without a 0x00 tail");
        free(words);
        free(cmds);
        return -1;
    }
    *cmds_out = cmds;
    *n_cmds = (int)count;
    *words_buf = words;
    *tail_out = NULL;
    *tail_len = 0;
    return 0;
}

/* Range alamat EE yang ditulis satu triple 0x0a database.
 * Kembalikan 1 kalau range diketahui, 0 kalau mode tidak dikenal. */
static int triple_range(uint32_t wordA, uint32_t wordB, uint32_t wordC,
                        uint64_t *lo, uint64_t *hi)
{
    uint32_t mode = wordA >> 28;
    uint64_t addr = wordA & 0x01FFFFFFULL;
    if (mode == 0) {
        *lo = addr;
        *hi = addr;
        return 1;
    }
    if (mode == 1) {
        *lo = addr;
        *hi = addr + ((uint64_t)(wordC & 0xFFFF) + 1) * 8 - 1;
        return 1;
    }
    if (mode == 2) {
        *lo = addr;
        *hi = wordB >= addr ? (uint64_t)wordB : addr;
        return 1;
    }
    return 0;
}

static void hex8(char *buf, size_t buf_size, uint32_t v)
{
    snprintf(buf, buf_size, "0x%08x", (unsigned)v);
}

int ws_db_merge(const ws_triple_t *pnach, int n_pnach,
                const unsigned char *db_data, size_t db_len,
                const char *db_sub, const char *db_display,
                ws_report_t *report,
                uint32_t **out_words, size_t *out_nwords,
                const unsigned char **tail, size_t *tail_len)
{
    ws_db_cmd_t *cmds = NULL;
    uint32_t *words_buf = NULL;
    int n_cmds = 0, i, j;
    char err[128];
    const unsigned char *db_tail = NULL;
    size_t db_tail_len = 0;
    ws_triple_t *kept_triples = NULL;
    size_t kept_triples_n = 0, kept_triples_cap = 0;
    ws_db_cmd_t *kept_cmds = NULL;
    size_t kept_cmds_n = 0;
    int dropped_type1 = 0;
    uint32_t *out = NULL;
    size_t out_n = 0, out_cap = 0;
    size_t total;

    *out_words = NULL;
    *out_nwords = 0;
    *tail = NULL;
    *tail_len = 0;

    if (ws_db_walk(db_data, db_len, &cmds, &n_cmds, &words_buf,
                   &db_tail, &db_tail_len, err, sizeof(err)) != 0) {
        ws_report_add(report,
                      "warning: database entry %s (%s, snapshot %s) could "
                      "NOT be parsed safely (%s); emitting patch-only "
                      "output — the community entry is NOT applied, delete "
                      "this file to fall back to the database",
                      db_display, db_sub, WS_DB_SNAPSHOT, err);
        return 0;
    }
    if (db_tail_len > 36) {
        ws_report_add(report,
                      "warning: database entry %s has an unusually long "
                      "tail (%d bytes); emitting patch-only output",
                      db_display, (int)db_tail_len);
        free(cmds);
        free(words_buf);
        return 0;
    }

    kept_cmds = (ws_db_cmd_t *)malloc((n_cmds ? (size_t)n_cmds : 1) *
                                      sizeof(ws_db_cmd_t));
    if (!kept_cmds) {
        free(cmds);
        free(words_buf);
        return 0;
    }

    for (i = 0; i < n_cmds; i++) {
        ws_db_cmd_t *c = &cmds[i];
        if (c->id == CMD_REV) continue;
        if (c->id == CMD_TYPE1) {
            dropped_type1++;
            continue;
        }
        if (c->id == CMD_PATCH) {
            uint32_t count = c->words[1];
            for (j = 0; (uint32_t)j < count; j++) {
                uint32_t wordA = c->words[2 + j * 3];
                uint32_t wordB = c->words[3 + j * 3];
                uint32_t wordC = c->words[4 + j * 3];
                uint32_t mode = wordA >> 28;
                uint32_t addr = wordA & 0x01FFFFFF;
                uint64_t lo, hi;
                int has_range = triple_range(wordA, wordB, wordC, &lo, &hi);
                int hit = 0, k;
                char hbuf[512], abuf[16];
                size_t hl = 0;
                if (has_range) {
                    for (k = 0; k < n_pnach; k++) {
                        uint32_t p = pnach[k].addr;
                        if (lo <= p && p <= hi) {
                            hit = 1;
                            hex8(abuf, sizeof(abuf), p);
                            hl += (size_t)snprintf(hbuf + hl,
                                                   sizeof(hbuf) - hl,
                                                   "%s%s", hl ? ", " : "",
                                                   abuf);
                        }
                    }
                }
                if (hit) {
                    ws_report_add(report,
                                  "warning: database 0x0a patch at "
                                  "0x%08x (mode %u) overlaps pnach "
                                  "patch(es) at %s — database patch "
                                  "DISPLACED, pnach wins",
                                  (unsigned)addr, (unsigned)mode, hbuf);
                    continue;
                }
                if (mode != 0 && mode != 1 && mode != 2)
                    ws_report_add(report,
                                  "info: database 0x0a patch at 0x%08x "
                                  "has unknown mode %u, kept as-is",
                                  (unsigned)addr, (unsigned)mode);
                if (kept_triples_n == kept_triples_cap) {
                    ws_triple_t *nk;
                    kept_triples_cap = kept_triples_cap ? kept_triples_cap * 2 : 16;
                    nk = (ws_triple_t *)realloc(kept_triples,
                                                kept_triples_cap *
                                                    sizeof(ws_triple_t));
                    if (!nk) {
                        free(kept_triples);
                        free(kept_cmds); free(cmds); free(words_buf);
                        return 0;
                    }
                    kept_triples = nk;
                }
                kept_triples[kept_triples_n].addr = wordA;
                kept_triples[kept_triples_n].old = wordB;
                kept_triples[kept_triples_n].value = wordC;
                kept_triples_n++;
            }
            continue;
        }
        if (c->id == CMD_OVERLAY) {
            uint32_t off = c->words[1];
            uint32_t cnt = c->words[2];
            int64_t end = (int64_t)off + (int64_t)cnt * 4 - 1;
            int hit = 0, k;
            char hbuf[512], abuf[16];
            size_t hl = 0;
            for (k = 0; k < n_pnach; k++) {
                int64_t p = (int64_t)pnach[k].addr;
                if (p >= (int64_t)off && p <= end) {
                    hit = 1;
                    hex8(abuf, sizeof(abuf), pnach[k].addr);
                    hl += (size_t)snprintf(hbuf + hl, sizeof(hbuf) - hl,
                                           "%s%s", hl ? ", " : "", abuf);
                }
            }
            if (hit) {
                ws_report_add(report,
                              "warning: database 0x42 overlay [0x%08x.."
                              "0x%08x] overlaps pnach patch(es) at %s — "
                              "whole overlay command DISPLACED, pnach wins",
                              (unsigned)off, (unsigned)end, hbuf);
                continue;
            }
            kept_cmds[kept_cmds_n++] = *c;
            continue;
        }
        kept_cmds[kept_cmds_n++] = *c;
    }
    if (dropped_type1)
        ws_report_add(report,
                      "warning: database entry had %d type-1 (0x09) "
                      "command(s) — DISPLACED so our type-2 patches can "
                      "register (one file cannot mix type 1 and 2)",
                      dropped_type1);

    total = kept_triples_n + (size_t)n_pnach;
    if (total > WS_MAX_PATCHES) {
        size_t excess = total - WS_MAX_PATCHES, k;
        for (k = kept_triples_n - excess; k < kept_triples_n; k++)
            ws_report_add(report,
                          "warning: database 0x0a patch at 0x%08x dropped "
                          "for the 32-patch cap (pnach patches take "
                          "priority)",
                          (unsigned)(kept_triples[k].addr & 0x01FFFFFF));
        kept_triples_n -= excess;
    }

    /* Ukuran output dihitung dari jumlah kata sebenarnya (overlay 0x42
       bisa puluhan kata — bukan perkiraan per command). */
    out_cap = 2 + 2 + (kept_triples_n + (size_t)n_pnach) * 3 + 4;
    for (i = 0; (size_t)i < kept_cmds_n; i++)
        out_cap += kept_cmds[i].nwords;
    out = (uint32_t *)malloc(out_cap * sizeof(uint32_t));
    if (!out) {
        free(kept_triples);
        free(kept_cmds); free(cmds); free(words_buf);
        return 0;
    }
    out[out_n++] = 0x3d;
    out[out_n++] = WS_NETEMU_REV;
    for (i = 0; (size_t)i < kept_cmds_n; i++) {
        size_t k;
        for (k = 0; k < kept_cmds[i].nwords; k++)
            out[out_n++] = kept_cmds[i].words[k];
    }
    {
        size_t all_count = kept_triples_n + (size_t)n_pnach;
        size_t k;
        out[out_n++] = CMD_PATCH;
        out[out_n++] = (uint32_t)all_count;
        for (k = 0; k < kept_triples_n; k++) {
            out[out_n++] = kept_triples[k].addr;
            out[out_n++] = kept_triples[k].old;
            out[out_n++] = kept_triples[k].value;
        }
        for (k = 0; (int)k < n_pnach; k++) {
            out[out_n++] = pnach[k].addr;
            out[out_n++] = pnach[k].old;
            out[out_n++] = pnach[k].value;
        }
    }
    ws_report_add(report,
                  "info: merged database entry %s (%s, snapshot %s): %d db "
                  "command(s) kept, %d db patch triple(s) kept, %d pnach "
                  "patch(es) added, %d total patch(es)",
                  db_display, db_sub, WS_DB_SNAPSHOT, (int)kept_cmds_n,
                  (int)kept_triples_n, n_pnach,
                  (int)(kept_triples_n + (size_t)n_pnach));
    free(kept_triples);
    free(kept_cmds);
    free(cmds);
    free(words_buf);
    *out_words = out;
    *out_nwords = out_n;
    *tail = db_tail;
    *tail_len = db_tail_len;
    return 1;
}

int ws_config_triple_addrs(const char *path, uint32_t *addrs, int max_addrs)
{
    FILE *f;
    unsigned char *data = NULL;
    size_t len = 0, cap = 0;
    ws_db_cmd_t *cmds = NULL;
    int n_cmds = 0, n = 0, i, j;
    uint32_t *words_buf = NULL;
    const unsigned char *tail = NULL;
    size_t tail_len = 0;
    char err[128];
    /* Baca seluruh file (CONFIG kecil, maks 32 triple + tail). */
    f = fopen(path, "rb");
    if (!f) return -1;
    for (;;) {
        unsigned char buf[4096];
        size_t got = fread(buf, 1, sizeof(buf), f);
        if (got == 0) break;
        if (len + got > cap) {
            size_t ncap = cap ? cap * 2 : 8192;
            unsigned char *nd;
            while (len + got > ncap) ncap *= 2;
            nd = (unsigned char *)realloc(data, ncap);
            if (!nd) { fclose(f); free(data); return -1; }
            data = nd;
            cap = ncap;
        }
        memcpy(data + len, buf, got);
        len += got;
        if (got < sizeof(buf)) break;
    }
    fclose(f);
    if (ws_db_walk(data, len, &cmds, &n_cmds, &words_buf,
                   &tail, &tail_len, err, sizeof(err)) != 0) {
        free(data);
        free(cmds);
        free(words_buf);
        return -1;
    }
    /* Kumpulkan alamat triple 0x0a (words[1] = count, lalu 3 word per
     * triple: addr|mode, old, new). Triple DB yang ikut merge juga
     * terbaca — alamatnya patch game yang sama, jadi dicentang juga
     * kalau ada baris pnach di alamat itu. */
    for (i = 0; i < n_cmds; i++) {
        uint32_t count;
        if (cmds[i].id != CMD_PATCH) continue;
        if (cmds[i].nwords < 2) continue;
        count = cmds[i].words[1];
        for (j = 0; (uint32_t)j < count; j++) {
            size_t w = 2 + (size_t)j * 3;
            uint32_t a;
            int k, dup = 0;
            if (w >= cmds[i].nwords) break;
            a = cmds[i].words[w] & 0x0FFFFFFFU;
            for (k = 0; k < n; k++)
                if (addrs[k] == a) dup = 1;
            if (!dup && n < max_addrs) addrs[n++] = a;
        }
    }
    free(data);
    free(cmds);
    free(words_buf);
    return n;
}

int ws_config_has_watermark(const char *path)
{
    FILE *f;
    unsigned char *data = NULL;
    size_t len = 0, cap = 0, i;
    ws_db_cmd_t *cmds = NULL;
    int n_cmds = 0;
    uint32_t *words_buf = NULL;
    const unsigned char *tail = NULL;
    size_t tail_len = 0;
    char err[128];
    int found = 0;
    /* Baca seluruh file (CONFIG kecil, maks 32 triple + tail). */
    f = fopen(path, "rb");
    if (!f) return -1;
    for (;;) {
        unsigned char buf[4096];
        size_t got = fread(buf, 1, sizeof(buf), f);
        if (got == 0) break;
        if (len + got > cap) {
            size_t ncap = cap ? cap * 2 : 8192;
            unsigned char *nd;
            while (len + got > ncap) ncap *= 2;
            nd = (unsigned char *)realloc(data, ncap);
            if (!nd) { fclose(f); free(data); return -1; }
            data = nd;
            cap = ncap;
        }
        memcpy(data + len, buf, got);
        len += got;
        if (got < sizeof(buf)) break;
    }
    fclose(f);
    if (ws_db_walk(data, len, &cmds, &n_cmds, &words_buf,
                   &tail, &tail_len, err, sizeof(err)) != 0) {
        free(data);
        free(cmds);
        free(words_buf);
        return -1;
    }
    /* Ekor yang lolos walk dan mengandung title: cari "WSM1" mulai
     * offset 14 (sesudah 0x00 + Title ID 10 byte). File tanpa title
     * di ekor tidak bisa mengandung watermark yang sah — walk akan
     * menolak ekor berisi "WSM1" tanpa title di depannya. */
    if (tail && tail_len >= 18) {
        for (i = 14; i + 4 <= tail_len; i++) {
            if (memcmp(tail + i, "WSM1", 4) == 0) { found = 1; break; }
        }
    }
    free(data);
    free(cmds);
    free(words_buf);
    return found;
}
