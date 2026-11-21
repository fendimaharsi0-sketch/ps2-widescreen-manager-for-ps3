/* Operasi file toggle Fase 2. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toggle.h"

int ws_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int ws_toggle_write_config(const char *cfg_path,
                           const unsigned char *data, size_t len)
{
    FILE *f = fopen(cfg_path, "wb");
    if (!f) return -1;
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) return -1;
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    unsigned char buf[8192];
    size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    if (ferror(in)) {
        fclose(in);
        fclose(out);
        return -1;
    }
    fclose(in);
    if (fclose(out) != 0) return -1;
    return 0;
}

int ws_toggle_backup_custom_config(const char *cfg_path,
                                   const char *orig_path,
                                   int app_made, int *backed_up)
{
    *backed_up = 0;
    if (app_made) return 0;  /* buatan app: timpa aman, tanpa backup */
    if (!ws_file_exists(cfg_path)) return 0;  /* belum ada yang ditimpa */
    if (ws_file_exists(orig_path)) return 0;  /* .orig pertama dipertahankan */
    if (copy_file(cfg_path, orig_path) != 0) return -1;
    *backed_up = 1;
    return 0;
}

int ws_toggle_remove_config(const char *cfg_path, const char *orig_path,
                            int app_made, int *backed_up)
{
    *backed_up = 0;
    if (!app_made && !ws_file_exists(orig_path)) {
        if (copy_file(cfg_path, orig_path) != 0) return -1;
        *backed_up = 1;
    }
    if (remove(cfg_path) != 0) return -1;
    return 0;
}

int ws_toggle_restore_orig(const char *cfg_path, const char *orig_path)
{
    if (!ws_file_exists(orig_path)) return 0;  /* tidak ada yang dikembalikan */
    if (rename(orig_path, cfg_path) != 0) return -1;
    return 1;
}
