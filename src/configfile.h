/* Entry CONFIG webMAN per-file (pelajaran yang sama seperti patch):
 * SATU file .CONFIG per Title ID (nama <SERIAL>.CONFIG, persis
 * seperti di snapshot database webMAN) di data/configdb/NET/ dan
 * data/configdb/CUSTOM/ — BUKAN satu blob yang dimuat utuh.
 * File dibaca hanya saat toggle ON untuk game itu (satu file kecil),
 * NET didahulukan atas CUSTOM. Portabel: hanya stdio + memori. */
#ifndef WS_CONFIGFILE_H
#define WS_CONFIGFILE_H

#include <stddef.h>

/* Cari + baca SATU file CONFIG untuk Title ID bentuk database
 * ("SLUS_211.59"): coba NET dulu, lalu CUSTOM. *out dimalloc;
 * pemanggil yang free. *sub_out (boleh NULL) diisi "NET" atau
 * "CUSTOM" sesuai file yang ketemu. Kembalikan 0 kalau ketemu,
 * -1 kalau tidak ada. */
int ws_configfile_find(const char *dir, const char *title_id,
                       unsigned char **out, size_t *out_len,
                       const char **sub_out);

/* "SLUS-21159" (bentuk tampil) -> "SLUS_211.59" (bentuk database). */
void ws_title_id_dbform(const char *dash_form, char *out, size_t out_size);

#endif
