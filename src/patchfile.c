/* Patch widescreen per-file: indeks nama + baca satu file. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "patchfile.h"

/* Parse nama file pnach: "<SERIAL>_<CRC>.pnach" atau "<CRC>.pnach".
 * CRC = 7-8 digit hex (dua file di repo CRC-nya 7 digit: SLES-50396
 * B6F7BA5 dan SLKA-25059 3CA4D49 — di Fase 0 diperbaiki dengan
 * left-pad '0'; di sini dibaca apa adanya, nilainya sama).
 * Serial dinormalisasi (alnum, uppercase).
 * Kembalikan 0 kalau nama valid, -1 kalau bukan file pnach yang
 * dikenali (diabaikan). */
static int parse_pnach_name(const char *name, uint32_t *crc_out,
                            char *serial_norm_out)
{
    size_t n = strlen(name);
    const char *dot, *us;
    char crc_str[9];
    size_t crc_len;
    char *end = NULL;
    unsigned long v;
    if (n < 13 || strcasecmp(name + n - 6, ".pnach") != 0) return -1;
    dot = name + n - 6;  /* posisi ".pnach" */
    /* CRC = hex antara '_' terakhir (atau awal nama) dan ".pnach". */
    us = strrchr(name, '_');
    if (us && us < dot) {
        crc_len = (size_t)(dot - (us + 1));
        if (crc_len < 7 || crc_len > 8) return -1;
        memcpy(crc_str, us + 1, crc_len);
        crc_str[crc_len] = '\0';
    } else {
        /* File CRC-only: seluruh nama sebelum ".pnach" adalah CRC. */
        crc_len = (size_t)(dot - name);
        if (crc_len != 8) return -1;
        memcpy(crc_str, name, 8);
        crc_str[8] = '\0';
        us = NULL;
    }
    v = strtoul(crc_str, &end, 16);
    if (!end || *end) return -1;
    *crc_out = (uint32_t)v;
    serial_norm_out[0] = '\0';
    if (us) {
        /* Serial = sebelum '_' terakhir. */
        char serial[16];
        size_t sl = (size_t)(us - name);
        if (sl > sizeof(serial) - 1) sl = sizeof(serial) - 1;
        memcpy(serial, name, sl);
        serial[sl] = '\0';
        ws_serial_norm(serial, serial_norm_out, 12);
    }
    return 0;
}

int ws_patch_index_load(ws_patch_index_t *index, const char *dir)
{
    DIR *d;
    struct dirent *de;
    memset(index, 0, sizeof(*index));
    d = opendir(dir);
    if (!d) return -1;
    index->entries = (ws_patch_entry_t *)malloc(
        WS_PATCH_MAX_FILES * sizeof(ws_patch_entry_t));
    if (!index->entries) {
        closedir(d);
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        ws_patch_entry_t *e;
        uint32_t crc;
        char serial_norm[12];
        if (index->count >= WS_PATCH_MAX_FILES) break;
        if (parse_pnach_name(de->d_name, &crc, serial_norm) != 0) continue;
        e = &index->entries[index->count];
        e->crc = crc;
        strncpy(e->serial_norm, serial_norm, sizeof(e->serial_norm) - 1);
        e->serial_norm[sizeof(e->serial_norm) - 1] = '\0';
        strncpy(e->filename, de->d_name, sizeof(e->filename) - 1);
        e->filename[sizeof(e->filename) - 1] = '\0';
        index->count++;
    }
    closedir(d);
    return 0;
}

void ws_patch_index_free(ws_patch_index_t *index)
{
    free(index->entries);
    memset(index, 0, sizeof(*index));
}

void ws_serial_norm(const char *serial, char *out, size_t out_size)
{
    size_t o = 0;
    if (!serial) serial = "";
    for (; *serial && o + 1 < out_size; serial++) {
        if (isalnum((unsigned char)*serial))
            out[o++] = (char)toupper((unsigned char)*serial);
    }
    out[o] = '\0';
}

ws_lookup_status_t ws_patch_lookup(ws_patch_index_t *index, uint32_t crc,
                                   const char *serial_norm,
                                   char *filename_out, size_t filename_size)
{
    size_t i;
    const ws_patch_entry_t *serial_hit = NULL, *plain_hit = NULL;
    const ws_patch_entry_t *only = NULL;
    int matches = 0;
    if (!serial_norm) serial_norm = "";
    for (i = 0; i < index->count; i++) {
        const ws_patch_entry_t *e = &index->entries[i];
        if (e->crc != crc) continue;
        matches++;
        if (serial_norm[0] && e->serial_norm[0] &&
            strcmp(e->serial_norm, serial_norm) == 0)
            serial_hit = e;
        if (!e->serial_norm[0])
            plain_hit = e;
        only = e;
    }
    if (matches == 0) {
        /* Fase 3j — fallback TERAKHIR: tidak ada file untuk CRC ini
         * sama sekali. Cari file yang serialnya cocok (CRC beda):
         * tepat satu -> pakai; 2+ -> ambiguous; 0 -> not found.
         * Serial game kosong ("") tidak ikut fallback. */
        const ws_patch_entry_t *serial_only = NULL;
        int serial_matches = 0;
        if (serial_norm[0]) {
            for (i = 0; i < index->count; i++) {
                const ws_patch_entry_t *e = &index->entries[i];
                if (!e->serial_norm[0]) continue;
                if (strcmp(e->serial_norm, serial_norm) != 0) continue;
                serial_matches++;
                serial_only = e;
            }
        }
        if (serial_matches == 1) {
            strncpy(filename_out, serial_only->filename,
                    filename_size - 1);
            filename_out[filename_size - 1] = '\0';
            return WS_LOOKUP_SERIAL_ONLY;
        }
        if (serial_matches > 1) return WS_LOOKUP_AMBIGUOUS;
        return WS_LOOKUP_NOT_FOUND;
    }
    if (serial_hit) {
        strncpy(filename_out, serial_hit->filename, filename_size - 1);
        filename_out[filename_size - 1] = '\0';
        return WS_LOOKUP_FOUND;
    }
    if (plain_hit) {
        strncpy(filename_out, plain_hit->filename, filename_size - 1);
        filename_out[filename_size - 1] = '\0';
        return WS_LOOKUP_FOUND;
    }
    if (matches == 1) {
        /* Satu-satunya file untuk CRC ini (serial berbeda dengan game,
           mis. revisi lain) — pakai saja. */
        strncpy(filename_out, only->filename, filename_size - 1);
        filename_out[filename_size - 1] = '\0';
        return WS_LOOKUP_FOUND;
    }
    return WS_LOOKUP_AMBIGUOUS;
}

/* ---- Pnach user (Fase 3k) ---- */

/* Serial dari gametitle: "(XXXX-12345)", "[XXXX-12345]", atau bare
 * "XXXX_NNN.NN" — bentuk yang sama dengan convert.c (serial_from_*,
 * pelajaran Downhill 2026-11-02: kurung siku ikut dikenali). Hasil =
 * bentuk dash "XXXX-12345" (pemanggil yang normalisasi). */
static int serial_from_gametitle(const char *s, char *out, size_t out_size)
{
    size_t i, n = strlen(s);
    int k;
    for (i = 0; i + 11 < n; i++) {
        int ok = 1;
        if (s[i] != '(' && s[i] != '[') continue;
        for (k = 0; k < 4; k++)
            if (!isalpha((unsigned char)s[i + 1 + k])) ok = 0;
        if (!ok || s[i + 5] != '-') continue;
        for (k = 0; k < 5; k++)
            if (!isdigit((unsigned char)s[i + 6 + k])) ok = 0;
        if (!ok || (s[i + 11] != ')' && s[i + 11] != ']')) continue;
        snprintf(out, out_size, "%c%c%c%c-%c%c%c%c%c",
                 (char)toupper((unsigned char)s[i + 1]),
                 (char)toupper((unsigned char)s[i + 2]),
                 (char)toupper((unsigned char)s[i + 3]),
                 (char)toupper((unsigned char)s[i + 4]),
                 s[i + 6], s[i + 7], s[i + 8], s[i + 9], s[i + 10]);
        return 1;
    }
    for (i = 0; i + 10 < n; i++) {
        int ok = 1;
        if (i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_'))
            continue;
        for (k = 0; k < 4; k++)
            if (!isalpha((unsigned char)s[i + k])) ok = 0;
        if (!ok || s[i + 4] != '_') continue;
        for (k = 0; k < 3; k++)
            if (!isdigit((unsigned char)s[i + 5 + k])) ok = 0;
        if (!ok || s[i + 8] != '.') continue;
        for (k = 0; k < 2; k++)
            if (!isdigit((unsigned char)s[i + 9 + k])) ok = 0;
        if (!ok) continue;
        if (i + 11 < n && isalnum((unsigned char)s[i + 11])) continue;
        snprintf(out, out_size, "%c%c%c%c-%c%c%c%c%c",
                 (char)toupper((unsigned char)s[i]),
                 (char)toupper((unsigned char)s[i + 1]),
                 (char)toupper((unsigned char)s[i + 2]),
                 (char)toupper((unsigned char)s[i + 3]),
                 s[i + 5], s[i + 6], s[i + 7], s[i + 9], s[i + 10]);
        return 1;
    }
    return 0;
}

/* Serial dari nama file: 4 huruf + pemisah opsional ('-', '_', ' ',
 * '.') + 5 digit, '.' opsional di tengah digit ("SLUS_211.59").
 * "SLUS-21564.pnach" dan "SLUS_21564.pnach" sama-sama ketemu. */
static int serial_from_filename(const char *name, char *out, size_t out_size)
{
    size_t i, n = strlen(name);
    int k;
    for (i = 0; i + 9 < n; i++) {
        int ok = 1, ndig = 0, dot_seen = 0;
        size_t j;
        if (i > 0 && isalpha((unsigned char)name[i - 1])) continue;
        for (k = 0; k < 4; k++)
            if (!isalpha((unsigned char)name[i + k])) ok = 0;
        if (!ok) continue;
        j = i + 4;
        if (name[j] == '-' || name[j] == '_' || name[j] == ' ' ||
            name[j] == '.')
            j++;
        /* Kumpulkan tepat 5 digit, '.' sekali di tengah diabaikan. */
        {
            char dig[6];
            while (j < n && ndig < 5) {
                if (isdigit((unsigned char)name[j])) {
                    dig[ndig++] = name[j++];
                } else if (name[j] == '.' && !dot_seen && ndig > 0) {
                    dot_seen = 1;
                    j++;
                } else break;
            }
            if (ndig != 5) continue;
            if (j < n && isdigit((unsigned char)name[j])) continue;
            snprintf(out, out_size, "%c%c%c%c-%c%c%c%c%c",
                     (char)toupper((unsigned char)name[i]),
                     (char)toupper((unsigned char)name[i + 1]),
                     (char)toupper((unsigned char)name[i + 2]),
                     (char)toupper((unsigned char)name[i + 3]),
                     dig[0], dig[1], dig[2], dig[3], dig[4]);
            return 1;
        }
    }
    return 0;
}

/* CRC dari nama file: run digit hex sepanjang 7-8, dibatasi karakter
 * non-hex di kedua sisi. "4CCC9212.pnach" -> 0x4CCC9212;
 * "SLUS-21564.pnach" tidak punya run 7-8 (cuma "21564"). */
static int crc_from_filename(const char *name, uint32_t *crc_out)
{
    size_t i = 0, n = strlen(name);
    while (i < n) {
        size_t j = i;
        char tok[9];
        size_t tlen;
        char *end = NULL;
        unsigned long v;
        if (!isxdigit((unsigned char)name[i])) {
            i++;
            continue;
        }
        while (j < n && isxdigit((unsigned char)name[j])) j++;
        tlen = j - i;
        if (tlen == 7 || tlen == 8) {
            memcpy(tok, name + i, tlen);
            tok[tlen] = '\0';
            v = strtoul(tok, &end, 16);
            if (end && !*end) {
                *crc_out = (uint32_t)v;
                return 1;
            }
        }
        i = j;
    }
    return 0;
}

int ws_user_index_load(ws_user_index_t *index, const char *dir)
{
    DIR *d;
    struct dirent *de;
    memset(index, 0, sizeof(*index));
    d = opendir(dir);
    if (!d) return -1;
    index->entries = (ws_user_patch_entry_t *)malloc(
        WS_USER_PATCH_MAX_FILES * sizeof(ws_user_patch_entry_t));
    if (!index->entries) {
        closedir(d);
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        char *text = NULL;
        size_t text_len = 0;
        char title[512];
        char serial[16] = "", serial_norm[12] = "";
        uint32_t crc = 0;
        int has_crc = 0;
        ws_user_patch_entry_t *e;
        if (index->count >= WS_USER_PATCH_MAX_FILES) break;
        if (n < 7 || strcasecmp(de->d_name + n - 6, ".pnach") != 0)
            continue;
        /* Isi file dibaca di sini: serial adanya di gametitle di
         * DALAM file — nama file user bebas apa saja. */
        if (ws_patch_read_file(dir, de->d_name, &text, &text_len) != 0)
            continue;
        ws_gametitle(text, text_len, title, sizeof(title));
        free(text);
        if (title[0] &&
            serial_from_gametitle(title, serial, sizeof(serial)))
            ws_serial_norm(serial, serial_norm, sizeof(serial_norm));
        if (!serial_norm[0] &&
            serial_from_filename(de->d_name, serial, sizeof(serial)))
            ws_serial_norm(serial, serial_norm, sizeof(serial_norm));
        has_crc = crc_from_filename(de->d_name, &crc);
        if (!serial_norm[0] && !has_crc) continue;
        e = &index->entries[index->count];
        e->crc = crc;
        e->has_crc = has_crc;
        strncpy(e->serial_norm, serial_norm, sizeof(e->serial_norm) - 1);
        e->serial_norm[sizeof(e->serial_norm) - 1] = '\0';
        strncpy(e->filename, de->d_name, sizeof(e->filename) - 1);
        e->filename[sizeof(e->filename) - 1] = '\0';
        index->count++;
    }
    closedir(d);
    return 0;
}

void ws_user_index_free(ws_user_index_t *index)
{
    free(index->entries);
    memset(index, 0, sizeof(*index));
}

ws_lookup_status_t ws_user_lookup(ws_user_index_t *index, uint32_t crc,
                                  const char *serial_norm,
                                  char *filename_out, size_t filename_size)
{
    size_t i;
    const ws_user_patch_entry_t *best = NULL;
    int best_rank = 0;
    if (!index) return WS_LOOKUP_NOT_FOUND;
    if (!serial_norm) serial_norm = "";
    for (i = 0; i < index->count; i++) {
        const ws_user_patch_entry_t *e = &index->entries[i];
        int rank = 0;
        int serial_ok = serial_norm[0] && e->serial_norm[0] &&
                        strcmp(e->serial_norm, serial_norm) == 0;
        int crc_ok = e->has_crc && e->crc == crc;
        if (serial_ok && crc_ok) rank = 3;
        else if (crc_ok) rank = 2;
        else if (serial_ok) rank = 1;
        else continue;
        if (rank > best_rank ||
            (rank == best_rank && best &&
             strcmp(e->filename, best->filename) < 0)) {
            best = e;
            best_rank = rank;
        }
    }
    if (!best) return WS_LOOKUP_NOT_FOUND;
    strncpy(filename_out, best->filename, filename_size - 1);
    filename_out[filename_size - 1] = '\0';
    return WS_LOOKUP_USER;
}

int ws_patch_read_file(const char *dir, const char *filename,
                       char **out, size_t *out_len)
{
    char path[1024];
    FILE *f;
    long sz;
    char *buf;
    *out = NULL;
    *out_len = 0;
    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    buf = (char *)malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

/* Satu baris teks (tanpa '\n'), dengan penunjuk + panjang. */
static int next_line(const char *text, size_t len, size_t *pos,
                     ws_text_line_t *ln)
{
    size_t start = *pos, i;
    if (start >= len) return 0;
    for (i = start; i < len; i++) {
        if (text[i] == '\n') break;
    }
    ln->s = text + start;
    ln->n = i - start;
    if (ln->n && ln->s[ln->n - 1] == '\r') ln->n--;
    *pos = (i < len) ? i + 1 : len;
    return 1;
}

/* Salin baris yang sudah di-strip (spasi depan/belakang) ke buf. */
static void strip_copy(const ws_text_line_t *ln, char *buf, size_t buf_size)
{
    size_t a = 0, b = ln->n, n;
    while (a < b && isspace((unsigned char)ln->s[a])) a++;
    while (b > a && isspace((unsigned char)ln->s[b - 1])) b--;
    n = b - a;
    if (n > buf_size - 1) n = buf_size - 1;
    memcpy(buf, ln->s + a, n);
    buf[n] = '\0';
}

static int is_group_header(const char *s, char *name, size_t name_size)
{
    size_t n = strlen(s), i;
    if (n < 3 || s[0] != '[' || s[n - 1] != ']') return 0;
    n -= 2;
    if (n > name_size - 1) n = name_size - 1;
    for (i = 0; i < n; i++) name[i] = s[i + 1];
    name[n] = '\0';
    return 1;
}

/* Substring case-insensitive (needle sudah huruf kecil). */
static int contains_ci(const char *hay, const char *needle)
{
    size_t hn = strlen(hay), nn = strlen(needle), i, j;
    if (nn == 0 || nn > hn) return 0;
    for (i = 0; i + nn <= hn; i++) {
        int ok = 1;
        for (j = 0; j < nn; j++)
            if (tolower((unsigned char)hay[i + j]) != needle[j]) {
                ok = 0;
                break;
            }
        if (ok) return 1;
    }
    return 0;
}

int ws_group_kind(const char *group)
{
    /* Lapis 1 dulu: rasio 16:9 dalam bentuk apa pun. */
    if (contains_ci(group, "16:9") || contains_ci(group, "16/9") ||
        contains_ci(group, "16_9") || contains_ci(group, "16 9"))
        return 2;
    /* Lapis 2: kata "wide" apa pun ("widescreen", "wide screen",
     * "wide view"). */
    if (contains_ci(group, "wide"))
        return 1;
    return 0;
}

int ws_ws_lines(const char *text, size_t len,
                ws_text_line_t lines[], int max_lines)
{
    size_t pos = 0;
    ws_text_line_t ln;
    char stripped[512], group[128] = "";
    int n = 0, in_ws = 0;
    while (next_line(text, len, &pos, &ln)) {
        strip_copy(&ln, stripped, sizeof(stripped));
        if (!stripped[0] || strncmp(stripped, "//", 2) == 0) continue;
        if (is_group_header(stripped, group, sizeof(group))) {
            in_ws = ws_group_kind(group) == 2;
            continue;
        }
        /* Fase 3o: status patch=0/1 diabaikan — keduanya dihitung. */
        if (in_ws && (strncmp(stripped, "patch=1,", 8) == 0 ||
                      strncmp(stripped, "patch=0,", 8) == 0) &&
            n < max_lines) {
            lines[n++] = ln;
        }
    }
    return n;
}

void ws_gametitle(const char *text, size_t len, char *out, size_t out_size)
{
    size_t pos = 0;
    ws_text_line_t ln;
    char stripped[512];
    out[0] = '\0';
    while (next_line(text, len, &pos, &ln)) {
        strip_copy(&ln, stripped, sizeof(stripped));
        if (!stripped[0] || strncmp(stripped, "//", 2) == 0) continue;
        if (strncmp(stripped, "gametitle=", 10) == 0) {
            size_t n = strlen(stripped + 10);
            if (n > out_size - 1) n = out_size - 1;
            memcpy(out, stripped + 10, n);
            out[n] = '\0';
            return;
        }
    }
}

/* Header pnach (Fase 3l): gametitle= + comment= + baris // sebelum
 * grup pertama. Berhenti di header grup pertama — isinya bukan
 * penjelasan file. Baris kosong + baris lain (gsaspectratio=,
 * author=, dsb.) di-skip. */
int ws_patch_header(const char *text, size_t len,
                    char lines[][WS_PATCH_HEADER_LINE_LEN], int max_lines)
{
    size_t pos = 0;
    ws_text_line_t ln;
    char stripped[512];
    char group[128];
    int n = 0;
    if (max_lines > WS_PATCH_HEADER_MAX_LINES)
        max_lines = WS_PATCH_HEADER_MAX_LINES;
    while (next_line(text, len, &pos, &ln)) {
        const char *val = NULL;
        if (n >= max_lines) break;
        strip_copy(&ln, stripped, sizeof(stripped));
        if (!stripped[0]) continue;
        if (is_group_header(stripped, group, sizeof(group))) break;
        if (strncmp(stripped, "gametitle=", 10) == 0) {
            val = stripped + 10;
        } else if (strncmp(stripped, "comment=", 8) == 0) {
            val = stripped + 8;
        } else if (strncmp(stripped, "//", 2) == 0) {
            val = stripped + 2;
            while (*val == ' ' || *val == '\t') val++;
            if (!*val) continue;
        } else {
            continue;
        }
        /* Salin manual (bukan strncpy): strncpy di sini memicu
         * -Wstringop-truncation karena val menunjuk ke buffer 512. */
        {
            size_t vl = strlen(val);
            if (vl > WS_PATCH_HEADER_LINE_LEN - 1)
                vl = WS_PATCH_HEADER_LINE_LEN - 1;
            memcpy(lines[n], val, vl);
            lines[n][vl] = '\0';
        }
        n++;
    }
    return n;
}

/* Semua file kandidat untuk satu game (Fase 3l). */
int ws_patch_candidates(ws_patch_index_t *index, uint32_t crc,
                        const char *serial_norm,
                        char files[][128], int max_files)
{
    size_t i;
    int n = 0, crc_any = 0;
    if (!index) return 0;
    if (!serial_norm) serial_norm = "";
    for (i = 0; i < index->count; i++)
        if (index->entries[i].crc == crc) crc_any = 1;
    for (i = 0; i < index->count; i++) {
        const ws_patch_entry_t *e = &index->entries[i];
        int hit = crc_any
                      ? (e->crc == crc)
                      : (serial_norm[0] && e->serial_norm[0] &&
                         strcmp(e->serial_norm, serial_norm) == 0);
        if (!hit) continue;
        if (files && n < max_files) {
            /* Salin manual (bukan strncpy): strncpy di sini memicu
             * -Wstringop-truncation. */
            size_t fl = strlen(e->filename);
            if (fl > 127) fl = 127;
            memcpy(files[n], e->filename, fl);
            files[n][fl] = '\0';
        }
        n++;
    }
    /* Urutkan alfabetis (deterministik, bukan urutan readdir). */
    if (files) {
        int m = n < max_files ? n : max_files, a, b;
        for (a = 1; a < m; a++) {
            for (b = a; b > 0 && strcmp(files[b - 1], files[b]) > 0; b--) {
                char tmp[128];
                memcpy(tmp, files[b - 1], sizeof(tmp));
                memcpy(files[b - 1], files[b], sizeof(tmp));
                memcpy(files[b], tmp, sizeof(tmp));
            }
        }
    }
    return n;
}

/* Parse alamat + tipe dari baris patch "patch=1/0,EE,ADDR,TYPE,VALUE".
 * addr = raw & 0x0FFFFFFF (sama seperti konverter). convertible = 1
 * kalau konverter bisa menulisnya: word, atau extended prefix 0x2;
 * bukan kondisional 0xE, bukan sub-word, alamat < 0x2000000.
 * (Cermin effective_size + skip di convert.c.) */
static void parse_patch_addr(const char *line, uint32_t *addr_out,
                             int *conv_out)
{
    const char *c1, *c2, *c3, *c4;
    char addr_tok[16], ptype[16];
    size_t alen, tlen, i;
    uint64_t raw = 0;
    int hex_ok = 1;
    *addr_out = 0;
    *conv_out = 0;
    c1 = strchr(line, ',');
    if (!c1) return;
    c2 = strchr(c1 + 1, ',');
    if (!c2) return;
    c3 = strchr(c2 + 1, ',');
    if (!c3) return;
    c4 = strchr(c3 + 1, ',');
    if (!c4) return;
    alen = (size_t)(c3 - (c2 + 1));
    tlen = (size_t)(c4 - (c3 + 1));
    if (alen == 0 || alen >= sizeof(addr_tok) ||
        tlen == 0 || tlen >= sizeof(ptype))
        return;
    memcpy(addr_tok, c2 + 1, alen);
    addr_tok[alen] = '\0';
    memcpy(ptype, c3 + 1, tlen);
    ptype[tlen] = '\0';
    for (i = 0; i < alen; i++)
        if (!isxdigit((unsigned char)addr_tok[i])) hex_ok = 0;
    if (!hex_ok) return;
    raw = strtoul(addr_tok, NULL, 16);
    *addr_out = (uint32_t)(raw & 0x0FFFFFFFULL);
    if (strcmp(ptype, "word") == 0) {
        *conv_out = (*addr_out < 0x2000000);
    } else if (strcmp(ptype, "extended") == 0) {
        if ((raw & 0xF0000000ULL) == 0xE0000000ULL) *conv_out = 0;
        else if (raw & 0x20000000ULL) *conv_out = (*addr_out < 0x2000000);
        else *conv_out = 0;
    } else {
        *conv_out = 0;
    }
}

int ws_patch_list(const char *text, size_t len,
                  ws_patch_item_t items[], int max_items)
{
    size_t pos = 0;
    ws_text_line_t ln;
    char stripped[512], group[128] = "";
    int n = 0;
    while (next_line(text, len, &pos, &ln)) {
        int enabled;
        strip_copy(&ln, stripped, sizeof(stripped));
        if (!stripped[0] || strncmp(stripped, "//", 2) == 0) continue;
        if (is_group_header(stripped, group, sizeof(group))) continue;
        /* Baris patch: "patch=1," atau "patch=0," (spasi sesudah
         * koma tidak ada di database; yang lain bukan baris patch).
         * Fase 3o: status diabaikan — keduanya aktif (patch=0 di
         * PCSX2 artinya nonaktif default, tidak relevan buat
         * proyek ini). */
        if (strncmp(stripped, "patch=1,", 8) == 0) enabled = 1;
        else if (strncmp(stripped, "patch=0,", 8) == 0) enabled = 1;
        else continue;
        if (n >= max_items) break;
        memset(&items[n], 0, sizeof(items[n]));
        strncpy(items[n].group, group, sizeof(items[n].group) - 1);
        strncpy(items[n].line, stripped, sizeof(items[n].line) - 1);
        items[n].enabled = enabled;
        items[n].ws_kind = ws_group_kind(group);
        parse_patch_addr(stripped, &items[n].addr, &items[n].convertible);
        n++;
    }
    return n;
}

int ws_patch_filtered_text(const char *text, size_t len,
                           const ws_patch_item_t *items,
                           const int *checked, int n,
                           char **out, size_t *out_len)
{
    char title[512];
    size_t cap = 1024, used = 0;
    char *buf;
    int i;
    ws_gametitle(text, len, title, sizeof(title));
    buf = (char *)malloc(cap);
    if (!buf) return -1;
    buf[0] = '\0';
    if (title[0]) {
        int w = snprintf(buf + used, cap - used, "gametitle=%s\n", title);
        if (w < 0 || (size_t)w >= cap - used) { free(buf); return -1; }
        used += (size_t)w;
    }
    for (i = 0; i < n; i++) {
        size_t ll;
        if (!checked[i]) continue;
        ll = strlen(items[i].line);
        if (used + ll + 2 > cap) {
            size_t ncap = cap * 2;
            char *nb;
            while (used + ll + 2 > ncap) ncap *= 2;
            nb = (char *)realloc(buf, ncap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
            cap = ncap;
        }
        memcpy(buf + used, items[i].line, ll);
        used += ll;
        buf[used++] = '\n';
        buf[used] = '\0';
    }
    *out = buf;
    *out_len = used;
    return 0;
}
