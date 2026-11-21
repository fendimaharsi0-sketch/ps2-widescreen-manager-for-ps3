/* Lapisan video minimal: RSX init + double buffering + flip.
 * Pola diambil dari fork (ps3/src/video.c) — calls yang sama, tanpa
 * shader/teksur/overlay.
 * Fase 3h: ROLLBACK dari lapisan overlay 3d — overlay hang setelah
 * scan di hardware (wsm-8.log: scan 39/39 selesai, lalu diam; init
 * 3g sudah lolos, jadi hang-nya di present/dialog setelah scan).
 * Engine ini (yang jalan sampai 3c) dikembalikan apa adanya; satu
 * yang dipertahankan dari 3g: video_init mengembalikan NOMOR STEP
 * (0 = OK) supaya kegagalan init berikutnya ketahuan step-nya. */
#include <malloc.h>
#include <string.h>
#include <unistd.h>

#include <rsx/rsx.h>
#include <sysutil/video.h>

#include "video.h"

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)

static gcmContextData *ctx;
static uint32_t *fb[2];
static uint32_t fb_off[2];
static unsigned scr_w, scr_h;
static int disp = 0, pend = -1, cur = -1;

/* video_init: 0 = OK, >0 = nomor step yang gagal (Fase 3g/3h) —
 * 1 host/rsxInit, 2 videoGetState, 3 videoGetResolution,
 * 4 videoConfigure, 5 alokasi display buffer. main.c mencetak
 * nomornya ke wsm.log. */
int video_init(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    videoState st;
    videoResolution res;
    videoConfiguration vc;
    int i;
    if (!host || rsxInit(&ctx, CB_SIZE, HOST_SIZE, host) != 0 || !ctx)
        return 1;
    if (videoGetState(0, 0, &st) != 0 || st.state != 0) return 2;
    if (videoGetResolution(st.displayMode.resolution, &res) != 0) return 3;
    memset(&vc, 0, sizeof vc);
    vc.resolution = st.displayMode.resolution;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.pitch = res.width * 4;
    vc.aspect = st.displayMode.aspect;
    if (videoConfigure(0, &vc, NULL, 0) != 0) return 4;
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    scr_w = res.width;
    scr_h = res.height;
    for (i = 0; i < 2; i++) {
        fb[i] = (uint32_t *)rsxMemalign(64, scr_w * scr_h * 4);
        if (!fb[i]) return 5;
        memset(fb[i], 0, scr_w * scr_h * 4);
        rsxAddressToOffset(fb[i], &fb_off[i]);
        gcmSetDisplayBuffer(i, fb_off[i], scr_w * 4, scr_w, scr_h);
    }
    gcmResetFlipStatus();
    return 0;
}

void video_shutdown(void)
{
    /* Tidak ada yang perlu dilepas secara eksplisit; proses berakhir. */
}

int video_small(void)
{
    return scr_h < 720;
}

uint32_t *video_frame(unsigned *w, unsigned *h)
{
    int n = 0;
    if (pend >= 0) {
        while (gcmGetFlipStatus() != 0) {
            usleep(200);
            if (++n == 5000) break;  /* 1 dtk: flip normalnya 1-2 frame */
        }
        gcmResetFlipStatus();
        disp = pend;
        pend = -1;
    }
    cur = disp ^ 1;
    *w = scr_w;
    *h = scr_h;
    return fb[cur];
}

void video_present(uint32_t *buf)
{
    /* Flush tulisan CPU supaya RSX melihatnya (pola fork:
       video_flush_cpu_writes). */
    const char *a = (const char *)((uintptr_t)buf & ~(uintptr_t)127);
    const char *e = (const char *)buf + (size_t)scr_w * scr_h * 4;
    for (; a < e; a += 128)
        __asm__ volatile("dcbst 0,%0" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
    if (gcmSetFlip(ctx, cur) == 0) {
        rsxFlushBuffer(ctx);
        pend = cur;
    } else {
        rsxFlushBuffer(ctx);
    }
}

/* Flip tanpa flush (Fase 2u): isi buffer tidak berubah sejak flush
 * terakhir, jadi dcbst tidak ada gunanya — yang penting flip tetap
 * di-queue supaya queue flip bergerak saat XMB buka. */
void video_present_unchanged(void)
{
    if (gcmSetFlip(ctx, cur) == 0) {
        rsxFlushBuffer(ctx);
        pend = cur;
    } else {
        rsxFlushBuffer(ctx);
    }
}
