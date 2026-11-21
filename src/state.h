/* State toggle: daftar ISO yang .CONFIG-nya dibuat app ini.
 * File teks sederhana di USRDIR: satu nama ISO per baris.
 * Dipakai membedakan .CONFIG buatan app vs .CONFIG manual pemakai
 * (yang manual di-backup ke .CONFIG.orig sebelum dihapus). */
#ifndef WS_STATE_H
#define WS_STATE_H

#include "scan.h"

typedef struct {
    char names[WS_MAX_GAMES][256];
    int count;
} ws_state_t;

int ws_state_load(ws_state_t *st, const char *path);
int ws_state_save(ws_state_t *st, const char *path);
int ws_state_has(ws_state_t *st, const char *iso_name);
void ws_state_add(ws_state_t *st, const char *iso_name);
void ws_state_remove(ws_state_t *st, const char *iso_name);

#endif
