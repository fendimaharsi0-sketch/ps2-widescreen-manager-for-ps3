/* Operasi file toggle Fase 2 (portabel, stdio saja):
 * tulis .CONFIG dan hapus .CONFIG dengan backup .CONFIG.orig.
 * Tidak menyentuh ISO maupun file emu — hanya .CONFIG di sebelah ISO. */
#ifndef WS_TOGGLE_H
#define WS_TOGGLE_H

#include <stddef.h>

/* Tulis byte CONFIG ke cfg_path (overwrite). 0 sukses, -1 gagal. */
int ws_toggle_write_config(const char *cfg_path,
                           const unsigned char *data, size_t len);

/* Backup .CONFIG custom SEBELUM ditimpa tulis (Fase 3b): salin ke
 * orig_path kalau app_made == 0 (file bukan buatan app) DAN file-nya
 * ada DAN orig_path belum ada (versi manual pertama dipertahankan).
 * *backed_up = 1 kalau backup terjadi. 0 sukses, -1 gagal (panggil
 * JANGAN lanjut tulis kalau ini gagal — file custom belum aman). */
int ws_toggle_backup_custom_config(const char *cfg_path,
                                   const char *orig_path,
                                   int app_made, int *backed_up);

/* Hapus cfg_path. Kalau app_made == 0 (file bukan buatan app), salin dulu
 * ke orig_path — tapi hanya kalau orig_path belum ada (versi manual
 * pertama dipertahankan). *backed_up = 1 kalau backup terjadi.
 * 0 sukses, -1 gagal (file TIDAK dihapus kalau backup gagal). */
int ws_toggle_remove_config(const char *cfg_path, const char *orig_path,
                            int app_made, int *backed_up);

/* Kembalikan backup .CONFIG.orig ke .CONFIG (Fase 3c): dipakai sesudah
 * .CONFIG buatan app dihapus, supaya .CONFIG custom (fix game) yang dulu
 * ditimpa app kembali hidup. 1 kalau direstore (orig dipindah, tidak
 * ada lagi), 0 kalau tidak ada .orig, -1 kalau rename gagal. */
int ws_toggle_restore_orig(const char *cfg_path, const char *orig_path);

/* 1 kalau file ada, 0 kalau tidak. */
int ws_file_exists(const char *path);

#endif
