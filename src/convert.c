/* Konverter pnach -> CONFIG. Port dari pnach2config.cpp. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "convert.h"
#include "elf.h"

static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hex_digit_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

/* Parse hex, toleransi prefix 0x. Saturasi saat overflow (pemanggil hanya
 * butuh low bits / cek >32-bit). Kembalikan 0 kalau bukan hex. */
static int hex_val(const char *text, uint64_t *out)
{
    uint64_t v = 0;
    const char *p = text;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!*p) return 0;
    for (; *p; p++) {
        unsigned d;
        if (!is_hex_digit(*p)) return 0;
        d = (unsigned)hex_digit_val(*p);
        if (v > (UINT64_MAX - d) / 16) v = UINT64_MAX;
        else v = v * 16 + d;
    }
    *out = v;
    return 1;
}

static void trim(char *s)
{
    size_t a = 0, b = strlen(s), n;
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    n = b - a;
    memmove(s, s + a, n);
    s[n] = '\0';
}

/* old dari komentar: token hex pertama yang panjangnya 8, atau satu-satunya
 * token hex (<= 8 digit) kalau cuma ada satu. */
static int old_from_comment(const char *comment, uint32_t *out)
{
    char hex_tokens[32][16];
    int n_tokens = 0;
    const char *p = comment;
    int i;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        {
            const char *start = p;
            char tok[32];
            size_t tl;
            int all_hex = 1;
            while (*p && !isspace((unsigned char)*p)) p++;
            tl = (size_t)(p - start);
            if (tl >= 2 && start[0] == '0' &&
                (start[1] == 'x' || start[1] == 'X')) {
                start += 2;
                tl -= 2;
            }
            if (tl == 0 || tl > 15) continue;
            for (i = 0; (size_t)i < tl; i++)
                if (!is_hex_digit(start[i])) all_hex = 0;
            if (!all_hex) continue;
            if (n_tokens < 32) {
                memcpy(tok, start, tl);
                tok[tl] = '\0';
                strcpy(hex_tokens[n_tokens], tok);
                n_tokens++;
            }
        }
    }
    for (i = 0; i < n_tokens; i++) {
        if (strlen(hex_tokens[i]) == 8) {
            uint64_t v = 0;
            hex_val(hex_tokens[i], &v);
            *out = (uint32_t)v;
            return 1;
        }
    }
    if (n_tokens == 1 && strlen(hex_tokens[0]) <= 8) {
        uint64_t v = 0;
        hex_val(hex_tokens[0], &v);
        *out = (uint32_t)v;
        return 1;
    }
    return 0;
}

/* Ukuran efektif tulis: word 4, short 2, byte 1, extended dari prefix
 * alamat (0x2 -> 4, 0x1 -> 2, selainnya 1). -1 kalau tidak dikenal.
 * Dipakai konverter (.CONFIG, word-only) dan ISO patch (Fase 2t,
 * byte/short ikut di-apply — direct write tidak punya batasan
 * word-only). */
int ws_patch_effective_size(const char *ptype, uint64_t raw_addr)
{
    if (strcmp(ptype, "word") == 0) return 4;
    if (strcmp(ptype, "short") == 0) return 2;
    if (strcmp(ptype, "byte") == 0) return 1;
    if (strcmp(ptype, "extended") == 0) {
        if (raw_addr & 0x20000000ULL) return 4;
        if (raw_addr & 0x10000000ULL) return 2;
        return 1;
    }
    return -1;
}

static int is_word_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* Serial "(XXXX-12345)" atau "[XXXX-12345]" -> "XXXX_NNN.NN". */
static int serial_from_parens(const char *s, char open, char close,
                              char *out, size_t out_size)
{
    size_t i, n = strlen(s);
    int k;
    for (i = 0; i + 11 < n; i++) {
        int ok = 1;
        if (s[i] != open) continue;
        for (k = 0; k < 4; k++)
            if (!isalpha((unsigned char)s[i + 1 + k])) ok = 0;
        if (!ok || s[i + 5] != '-') continue;
        for (k = 0; k < 5; k++)
            if (!isdigit((unsigned char)s[i + 6 + k])) ok = 0;
        if (!ok || s[i + 11] != close) continue;
        snprintf(out, out_size, "%c%c%c%c_%c%c%c.%c%c",
                 (char)toupper((unsigned char)s[i + 1]),
                 (char)toupper((unsigned char)s[i + 2]),
                 (char)toupper((unsigned char)s[i + 3]),
                 (char)toupper((unsigned char)s[i + 4]),
                 s[i + 6], s[i + 7], s[i + 8], s[i + 9], s[i + 10]);
        return 1;
    }
    return 0;
}

/* Serial bare "XXXX_NNN.NN" dengan word boundary -> sama. */
static int serial_from_bare(const char *s, char *out, size_t out_size)
{
    size_t i, n = strlen(s);
    int k;
    for (i = 0; i + 10 < n; i++) {
        int ok = 1;
        if (i > 0 && is_word_char(s[i - 1])) continue;
        for (k = 0; k < 4; k++)
            if (!isalpha((unsigned char)s[i + k])) ok = 0;
        if (!ok || s[i + 4] != '_') continue;
        for (k = 0; k < 3; k++)
            if (!isdigit((unsigned char)s[i + 5 + k])) ok = 0;
        if (!ok || s[i + 8] != '.') continue;
        for (k = 0; k < 2; k++)
            if (!isdigit((unsigned char)s[i + 9 + k])) ok = 0;
        if (!ok) continue;
        if (i + 11 < n && is_word_char(s[i + 11])) continue;
        snprintf(out, out_size, "%c%c%c%c_%c%c%c.%c%c",
                 (char)toupper((unsigned char)s[i]),
                 (char)toupper((unsigned char)s[i + 1]),
                 (char)toupper((unsigned char)s[i + 2]),
                 (char)toupper((unsigned char)s[i + 3]),
                 s[i + 5], s[i + 6], s[i + 7], s[i + 9], s[i + 10]);
        return 1;
    }
    return 0;
}

/* Parser satu baris patch (cerminan PATCH_RE / parsePatchLine).
 * Kembalikan 1 kalau ini baris patch (terisi addr/type/value/
 * comment), 0 kalau bukan baris patch. Fase 3o: status patch=0/1
 * diabaikan — keduanya baris patch yang aktif. */
static int parse_patch_line(const char *line,
                            char *addr_tok, size_t addr_size,
                            char *ptype, size_t ptype_size,
                            char *value_tok, size_t value_size,
                            const char **comment)
{
    size_t pos = 0, n = strlen(line);
    /* helper inline: skip ws */
#define SKIP_WS() while (pos < n && (line[pos] == ' ' || line[pos] == '\t' || \
        line[pos] == '\r' || line[pos] == '\n' || line[pos] == '\v' || \
        line[pos] == '\f')) pos++
#define EXPECT_CH(c) do { if (pos >= n || line[pos] != (c)) return 0; pos++; } while (0)
    if (strncmp(line, "patch", 5) != 0) return 0;
    pos = 5;
    SKIP_WS();
    EXPECT_CH('=');
    SKIP_WS();
    if (pos >= n || (line[pos] != '0' && line[pos] != '1')) return 0;
    pos++;
    SKIP_WS();
    EXPECT_CH(',');
    SKIP_WS();
    if (strncmp(line + pos, "EE", 2) != 0) return 0;
    pos += 2;
    SKIP_WS();
    EXPECT_CH(',');
    SKIP_WS();
    {
        size_t start = pos;
        while (pos < n && (is_hex_digit(line[pos]) || line[pos] == 'x' ||
                           line[pos] == 'X'))
            pos++;
        if (pos == start) return 0;
        if (pos - start > addr_size - 1) return 0;
        memcpy(addr_tok, line + start, pos - start);
        addr_tok[pos - start] = '\0';
    }
    SKIP_WS();
    EXPECT_CH(',');
    SKIP_WS();
    {
        size_t start = pos;
        while (pos < n && isalpha((unsigned char)line[pos])) pos++;
        if (pos == start) return 0;
        if (pos - start > ptype_size - 1) return 0;
        memcpy(ptype, line + start, pos - start);
        ptype[pos - start] = '\0';
    }
    SKIP_WS();
    EXPECT_CH(',');
    SKIP_WS();
    {
        size_t start = pos;
        while (pos < n && (is_hex_digit(line[pos]) || line[pos] == 'x' ||
                           line[pos] == 'X'))
            pos++;
        if (pos == start) return 0;
        if (pos - start > value_size - 1) return 0;
        memcpy(value_tok, line + start, pos - start);
        value_tok[pos - start] = '\0';
    }
    SKIP_WS();
    if (pos == n) {
        *comment = NULL;
        return 1;
    }
    if (strncmp(line + pos, "//", 2) != 0) return 0;
    *comment = line + pos + 2;
    return 1;
#undef SKIP_WS
#undef EXPECT_CH
}

int ws_parse_pnach(const char *text, size_t len, ws_pnach_t *out,
                   ws_report_t *report)
{
    size_t pos = 0;
    int lineno = 0;
    memset(out, 0, sizeof(*out));
    if (!text || len == 0) return -1;
    while (pos <= len) {
        size_t nl, linelen;
        char raw[1024];
        char line[1024];
        if (pos >= len) break;
        nl = pos;
        while (nl < len && text[nl] != '\n') nl++;
        linelen = nl - pos;
        if (linelen > sizeof(raw) - 1) linelen = sizeof(raw) - 1;
        memcpy(raw, text + pos, linelen);
        raw[linelen] = '\0';
        pos = (nl < len) ? nl + 1 : len;
        lineno++;
        if (linelen && raw[linelen - 1] == '\r') raw[linelen - 1] = '\0';
        strcpy(line, raw);
        trim(line);
        if (!line[0]) continue;
        if (strncmp(line, "//", 2) == 0 || line[0] == '#' || line[0] == ';')
            continue;
        if (strncmp(line, "gametitle", 9) == 0) {
            size_t p = 9;
            while (p < strlen(line) && (line[p] == ' ' || line[p] == '\t' ||
                                         line[p] == '\r'))
                p++;
            if (p < strlen(line) && line[p] == '=') {
                char title[512];
                char found[16];
                strncpy(title, line + p + 1, sizeof(title) - 1);
                title[sizeof(title) - 1] = '\0';
                trim(title);
                if (serial_from_parens(title, '(', ')', found, sizeof(found)) ||
                    serial_from_parens(title, '[', ']', found, sizeof(found)) ||
                    serial_from_bare(title, found, sizeof(found)))
                    strcpy(out->title_id, found);
            }
            continue;
        }
        {
            char addr_tok[32], ptype[16], value_tok[32];
            const char *comment = NULL;
            if (!parse_patch_line(line, addr_tok, sizeof(addr_tok),
                                  ptype, sizeof(ptype), value_tok,
                                  sizeof(value_tok), &comment))
                continue;
            {
                uint64_t raw_addr = 0, value64 = 0;
                size_t k;
                for (k = 0; ptype[k]; k++)
                    ptype[k] = (char)tolower((unsigned char)ptype[k]);
                if (!hex_val(addr_tok, &raw_addr) ||
                    !hex_val(value_tok, &value64)) {
                    ws_report_add(report,
                                  "warning: line %d: bad hex, skipped: %s",
                                  lineno, line);
                    continue;
                }
                if (value64 > 0xFFFFFFFFULL) {
                    ws_report_add(report,
                                  "warning: line %d: value > 32-bit, "
                                  "skipped",
                                  lineno);
                    continue;
                }
                if (out->n_patches < WS_MAX_PNACH_PATCHES) {
                    ws_patch_t *p = &out->patches[out->n_patches++];
                    p->addr = (uint32_t)(raw_addr & 0x0FFFFFFFULL);
                    p->raw_addr = raw_addr;
                    strcpy(p->ptype, ptype);
                    p->size = ws_patch_effective_size(ptype, raw_addr);
                    p->value = (uint32_t)value64;
                    p->has_old = comment ?
                        old_from_comment(comment, &p->old) : 0;
                    p->lineno = lineno;
                }
            }
        }
    }
    return 0;
}

static void append_u32le(unsigned char *out, size_t *pos, uint32_t v)
{
    out[(*pos)++] = (unsigned char)(v & 0xFF);
    out[(*pos)++] = (unsigned char)((v >> 8) & 0xFF);
    out[(*pos)++] = (unsigned char)((v >> 16) & 0xFF);
    out[(*pos)++] = (unsigned char)((v >> 24) & 0xFF);
}

/* Old value dari boot ELF (satu sumber; dipakai dua arah prioritas).
 * Kembalikan 1 kalau ketemu (log info), 0 kalau alamat tidak
 * tercakup segmen (bukan error — lanjut ke fallback/skip). */
static int old_from_elf(const unsigned char *elf, size_t elf_len,
                        const char *elf_name, uint32_t addr,
                        uint32_t *out, ws_report_t *report, int lineno)
{
    int r = ws_elf_word(elf, elf_len, addr, out);
    if (r == 1) {
        ws_report_add(report,
                      "info: line %d: old value 0x%08x taken from %s",
                      lineno, (unsigned)*out,
                      elf_name ? elf_name : "game file");
        return 1;
    }
    if (r < 0)
        ws_report_add(report,
                      "warning: %s: malformed ELF, cannot read old values",
                      elf_name ? elf_name : "game file");
    /* r == 0: alamat tidak tercakup segmen — bukan error. */
    return 0;
}

/* Ekor dimulai [0x00] + Title ID 10 byte ("XXXX-12345")? */
static int tail_has_title(const unsigned char *tail, size_t tail_len)
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

/* Title ID bentuk database ("SLUS_211.59") -> bentuk dash ("SLUS-21159")
 * untuk ekor watermark. 1 kalau hasilnya Title ID dash yang sah. */
static int title_dash_for_watermark(const char *db, char *dash,
                                    size_t dash_size)
{
    int k;
    if (!db || strlen(db) != 11 || dash_size < 11) return 0;
    for (k = 0; k < 4; k++)
        if (db[k] < 'A' || db[k] > 'Z') return 0;
    if (db[4] != '_' || db[8] != '.') return 0;
    for (k = 5; k < 8; k++)
        if (db[k] < '0' || db[k] > '9') return 0;
    for (k = 9; k < 11; k++)
        if (db[k] < '0' || db[k] > '9') return 0;
    memcpy(dash, db, 4);
    dash[4] = '-';
    memcpy(dash + 5, db + 5, 3);
    memcpy(dash + 8, db + 9, 2);
    dash[10] = '\0';
    return 1;
}

int ws_convert(const char *pnach_text, size_t pnach_len,
               const unsigned char *elf, size_t elf_len,
               const char *elf_name,
               const char *configdb_dir, int no_merge,
               ws_old_prio_t old_prio,
               const char *title_fallback,
               unsigned char **out, size_t *out_len,
               int *n_triples, int *merged,
               ws_report_t *report)
{
    /* Buffer besar di static (BSS), bukan stack — lihat main.c. */
    static ws_pnach_t pn;
    static ws_triple_t triples[WS_MAX_PATCHES + 64];
    int n_triples_local = 0;
    int i;
    uint32_t seen[WS_MAX_PNACH_PATCHES];
    int n_seen = 0;
    char title_id[16];
    uint32_t *merged_words = NULL;
    size_t merged_nwords = 0;
    const unsigned char *tail = NULL;
    size_t tail_len = 0;
    int did_merge = 0;
    unsigned char *data;
    size_t data_len, pos = 0;

    *out = NULL;
    *out_len = 0;
    *n_triples = 0;
    *merged = 0;
    if (ws_parse_pnach(pnach_text, pnach_len, &pn, report) != 0) return -1;
    if (pn.title_id[0]) {
        strcpy(title_id, pn.title_id);
    } else if (title_fallback && title_fallback[0]) {
        /* Fallback: Title ID dari scan (bentuk dash) -> bentuk database. */
        ws_title_id_dbform(title_fallback, title_id, sizeof(title_id));
        ws_report_add(report,
                      "warning: no Title ID in gametitle, using scan "
                      "Title ID for the lookup: %s",
                      title_id);
    } else {
        title_id[0] = '\0';
        ws_report_add(report,
                      "warning: no Title ID in gametitle, no database "
                      "lookup possible");
    }

    for (i = 0; i < pn.n_patches; i++) {
        ws_patch_t *p = &pn.patches[i];
        uint32_t old = 0;
        int have_old = 0, j;
        if (p->size < 0 || p->size != 4) {
            if (p->size < 0)
                ws_report_add(report,
                              "warning: line %d: patch skipped "
                              "(unsupported type %s)",
                              p->lineno, p->ptype);
            else
                ws_report_add(report,
                              "warning: line %d: patch skipped (%s "
                              "resolves to a %d-byte write (only 4-byte "
                              "word writes are supported))",
                              p->lineno, p->ptype, p->size);
            continue;
        }
        /* Kode kondisional PCSX2 (prefix 0xE): bukan tulis memori. */
        if (strcmp(p->ptype, "extended") == 0 &&
            (p->raw_addr & 0xF0000000ULL) == 0xE0000000ULL) {
            ws_report_add(report,
                          "info: line %d: conditional code (0xE address "
                          "prefix), not a memory write - skipped",
                          p->lineno);
            continue;
        }
        if (strcmp(p->ptype, "extended") == 0)
            ws_report_add(report,
                          "info: line %d: extended resolved to a word-size "
                          "write (0x2 address prefix)",
                          p->lineno);
        if (p->addr >= 0x2000000) {
            ws_report_add(report,
                          "warning: line %d: address 0x%08x out of the "
                          "25-bit range, skipped",
                          p->lineno, (unsigned)p->addr);
            continue;
        }
        for (j = 0; j < n_seen; j++)
            if (seen[j] == p->addr) break;
        if (j < n_seen)
            ws_report_add(report,
                          "warning: line %d: duplicate address 0x%08x "
                          "(second patch will likely be skipped at runtime "
                          "by the gate)",
                          p->lineno, (unsigned)p->addr);
        if (n_seen < WS_MAX_PNACH_PATCHES) seen[n_seen++] = p->addr;

        /* Prioritas old value (Fase 2s): patch-first = komentar dulu,
         * ELF fallback (perilaku lama); elf-first = ELF dulu, komentar
         * fallback. Komentar yang dipakai sebagai fallback dicatat di
         * log (jalur luar biasa); komentar sebagai sumber utama tidak
         * dicatat (jalur umum, biar log tidak penuh). */
        if (old_prio == WS_OLD_ELF_FIRST) {
            if (elf && elf_len)
                have_old = old_from_elf(elf, elf_len, elf_name, p->addr,
                                        &old, report, p->lineno);
            if (!have_old && p->has_old) {
                old = p->old;
                have_old = 1;
                ws_report_add(report,
                              "info: line %d: old value 0x%08x taken from "
                              "patch comment",
                              p->lineno, (unsigned)old);
            }
        } else {
            if (p->has_old) {
                old = p->old;
                have_old = 1;
            } else if (elf && elf_len) {
                have_old = old_from_elf(elf, elf_len, elf_name, p->addr,
                                        &old, report, p->lineno);
            }
        }
        if (!have_old) {
            ws_report_add(report,
                          "warning: line %d: old value unknown for address "
                          "0x%08x, patch skipped (give a //old comment or "
                          "a game .elf/.iso)",
                          p->lineno, (unsigned)p->addr);
            continue;
        }
        if (n_triples_local < (int)(sizeof(triples) / sizeof(triples[0]))) {
            triples[n_triples_local].addr = p->addr;
            triples[n_triples_local].old = old;
            triples[n_triples_local].value = p->value;
            n_triples_local++;
        }
    }

    if (n_triples_local > WS_MAX_PATCHES) {
        ws_report_add(report,
                      "error: %d patches exceed the netemu limit of %d per "
                      "file, refusing to write",
                      n_triples_local, WS_MAX_PATCHES);
        return -1;
    }
    if (n_triples_local == 0) {
        ws_report_add(report,
                      "error: no convertible patches, nothing written");
        return -1;
    }

    if (!no_merge && configdb_dir && title_id[0]) {
        unsigned char *entry = NULL;
        size_t entry_len = 0;
        const char *sub = "CUSTOM";
        if (ws_configfile_find(configdb_dir, title_id, &entry, &entry_len,
                               &sub) == 0) {
            char display[32];
            snprintf(display, sizeof(display), "%s.CONFIG", title_id);
            did_merge = ws_db_merge(triples, n_triples_local, entry,
                                    entry_len, sub, display, report,
                                    &merged_words, &merged_nwords,
                                    &tail, &tail_len);
            free(entry);
        } else {
            ws_report_add(report,
                          "warning: no database entry for %s, patch-only "
                          "output (nothing is shadowed)",
                          title_id);
        }
    } else if (!no_merge && configdb_dir && !title_id[0]) {
        /* Sudah di-warning di atas; patch-only. */
    } else if (no_merge) {
        ws_report_add(report,
                      "info: merge disabled (--no-merge): patch-only output");
    }

    /* Watermark (Fase 3c): tandai file bikinan app di ekornya, supaya
     * beda dari .CONFIG custom user tanpa ngandelin state.txt saja.
     * Bentuk ekor: [0x00][TitleID 10 byte][extra DB, kalau ada]["WSM1"].
     * Bukti ekor aman: 12 file database asli punya 3 byte ekstra
     * sesudah title (00 02 00 / 00 02 01) dan tetap jalan — netemu
     * membaca title lalu mengabaikan sisanya. Kalau ekor DB sudah
     * punya title, tempel "WSM1" di belakangnya (tidak lebih dari
     * 36 byte total, ikut batas merge). Kalau ekor tidak punya title
     * (cuma nol / tidak ada ekor), ganti dengan ekor baru
     * [0x00][title][WSM1] — nol tidak membawa info. Tanpa Title ID
     * yang sah: tanpa watermark (state.txt saja). */
    {
        int wm_append = 0;  /* tempel "WSM1" sesudah ekor yang ada */
        int wm_emit = 0;    /* tulis ekor baru [0x00][title][WSM1] */
        unsigned char wm_tail[18];
        const unsigned char *out_tail = tail;
        size_t out_tail_len = tail_len;
        char dash_title[16];
        if (tail && tail_has_title(tail, tail_len)) {
            if (tail_len + 4 <= 36) wm_append = 1;
            /* Ekor terlalu panjang: tanpa watermark, ekor DB utuh. */
        } else if (title_dash_for_watermark(title_id, dash_title,
                                            sizeof(dash_title))) {
            memset(wm_tail, 0, 4);
            memcpy(wm_tail + 4, dash_title, 10);
            memcpy(wm_tail + 14, "WSM1", 4);
            out_tail = wm_tail;
            out_tail_len = 18;
            wm_emit = 1;
        }
        if (wm_append || wm_emit)
            ws_report_add(report, "info: watermark WSM1 written in "
                          ".CONFIG tail");
        else
            ws_report_add(report, "info: no watermark (no Title ID or "
                          "tail too long)");
        data_len = (did_merge ? merged_nwords * 4
                              : (4 + (size_t)n_triples_local * 3) * 4) +
                   out_tail_len + (wm_append ? 4 : 0);
        data = (unsigned char *)malloc(data_len ? data_len : 1);
        if (!data) {
            free(merged_words);
            return -1;
        }
        if (did_merge) {
            size_t k;
            for (k = 0; k < merged_nwords; k++)
                append_u32le(data, &pos, merged_words[k]);
        } else {
            append_u32le(data, &pos, 0x3d);
            append_u32le(data, &pos, WS_NETEMU_REV);
            append_u32le(data, &pos, 0x0a);
            append_u32le(data, &pos, (uint32_t)n_triples_local);
            for (i = 0; i < n_triples_local; i++) {
                append_u32le(data, &pos, triples[i].addr);
                append_u32le(data, &pos, triples[i].old);
                append_u32le(data, &pos, triples[i].value);
            }
        }
        if (out_tail_len) {
            memcpy(data + pos, out_tail, out_tail_len);
            pos += out_tail_len;
        }
        if (wm_append) {
            memcpy(data + pos, "WSM1", 4);
            pos += 4;
        }
    }
    free(merged_words);
    ws_report_add(report, "OK: %d patch(es), %d bytes",
                  n_triples_local, (int)data_len);
    *out = data;
    *out_len = data_len;
    *n_triples = n_triples_local;
    *merged = did_merge;
    return 0;
}
