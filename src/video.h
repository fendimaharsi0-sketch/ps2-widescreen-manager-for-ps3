/* Lapisan video minimal (Fase 1): RSX init ala fork (video.c), dua display
 * buffer, gambar langsung ditulis CPU ke back buffer, lalu flip.
 * Tidak ada shader, tidak ada tekstur — menu teks saja. */
#ifndef WS_VIDEO_H
#define WS_VIDEO_H

#include <stddef.h>
#include <stdint.h>

/* video_init: 0 = OK, >0 = nomor step yang gagal (Fase 3g/3h, lihat
 * video.c) — main.c mencetak nomornya ke wsm.log. */
int video_init(void);
/* Layar kecil (Fase 3i): 1 kalau tinggi display < 720 (480p/576p) —
 * layout ringkas main.c memakainya. Hanya valid sesudah video_init
 * (sebelumnya scr_h = 0, jadi 1 — semua pemakai ada sesudah init). */
int video_small(void);
void video_shutdown(void);
/* Buffer belakang untuk digambar + ukurannya. Buffer aman ditulis sampai
   video_present dipanggil. */
uint32_t *video_frame(unsigned *w, unsigned *h);
/* Flush tulisan CPU (dcbst) lalu flip. */
void video_present(uint32_t *buf);
/* Flip tanpa gambar ulang / flush: buffer belakang isinya frame lama
 * (layar statis), jadi tidak ada yang perlu di-flush. Dipakai saat
 * XMB buka (Fase 2u) — queue flip tetap bergerak dengan kerja CPU
 * praktis nol. */
void video_present_unchanged(void);

#endif
