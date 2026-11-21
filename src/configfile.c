/* Entry CONFIG webMAN per-file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configfile.h"

static int read_one(const char *path, unsigned char **out, size_t *out_len)
{
    FILE *f;
    long sz;
    unsigned char *buf;
    *out = NULL;
    *out_len = 0;
    f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    buf = (unsigned char *)malloc((size_t)sz ? (size_t)sz : 1);
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

int ws_configfile_find(const char *dir, const char *title_id,
                       unsigned char **out, size_t *out_len,
                       const char **sub_out)
{
    char path[1024];
    if (sub_out) *sub_out = NULL;
    /* NET didahulukan (sama seperti blob dulu yang menyimpan NET dulu). */
    snprintf(path, sizeof(path), "%s/NET/%s.CONFIG", dir, title_id);
    if (read_one(path, out, out_len) == 0) {
        if (sub_out) *sub_out = "NET";
        return 0;
    }
    snprintf(path, sizeof(path), "%s/CUSTOM/%s.CONFIG", dir, title_id);
    if (read_one(path, out, out_len) == 0) {
        if (sub_out) *sub_out = "CUSTOM";
        return 0;
    }
    return -1;
}

void ws_title_id_dbform(const char *dash_form, char *out, size_t out_size)
{
    /* "SLUS-21159" -> "SLUS_211.59": huruf + '_' + 3 digit + '.' + 2 digit. */
    const char *dash = strchr(dash_form, '-');
    size_t letters, i, o = 0;
    if (!dash || strlen(dash) != 6) {
        /* Bukan bentuk dash yang dikenal — salin apa adanya. */
        strncpy(out, dash_form, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    letters = (size_t)(dash - dash_form);
    for (i = 0; i < letters && o + 1 < out_size; i++) out[o++] = dash_form[i];
    if (o + 1 < out_size) out[o++] = '_';
    for (i = 0; i < 3 && o + 1 < out_size; i++) out[o++] = dash[1 + i];
    if (o + 1 < out_size) out[o++] = '.';
    for (i = 0; i < 2 && o + 1 < out_size; i++) out[o++] = dash[4 + i];
    out[o] = '\0';
}
