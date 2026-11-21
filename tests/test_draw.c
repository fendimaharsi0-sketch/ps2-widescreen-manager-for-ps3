/* Tes host untuk primitif gambar + simbol tombol (Fase 2q).
 * Dijalankan lewat tests/run_tests.sh. Tidak ada dependensi PS3:
 * dikompilasi dengan gcc sistem.
 *
 * T15 draw_button: tiap simbol punya piksel khas di tempat yang
 * benar (dan tengah kosong untuk outline) + warna klasik PS.
 * T16 draw_line/draw_pixel: ujung garis tepat, piksel di luar
 * garis tidak kena, gambar di luar framebuffer tidak crash.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"

static int failures;

#define CHECK(name, cond) do { \
    printf("[%s] %s\n", (cond) ? "OK " : "FAIL", (name)); \
    if (!(cond)) failures++; \
} while (0)

#define FB_W 128
#define FB_H 64

static uint32_t fb[FB_W * FB_H];

static uint32_t px(int x, int y)
{
    return fb[y * FB_W + x];
}

static void clear_fb(void)
{
    memset(fb, 0, sizeof(fb));
}

int main(void)
{
    /* T15: simbol tombol, tengah (64,32), r=11. */
    clear_fb();
    draw_button(fb, FB_W, FB_H, 64, 32, WS_BTN_CROSS);
    CHECK("T15 cross: tengah terisi", px(64, 32) == WS_BTN_COL_CROSS);
    CHECK("T15 cross: ujung diagonal terisi",
          px(64 - 11, 32 - 11) == WS_BTN_COL_CROSS);
    CHECK("T15 cross: atas-tengah kosong (bukan garis vertikal)",
          px(64, 32 - 11) == 0);

    clear_fb();
    draw_button(fb, FB_W, FB_H, 64, 32, WS_BTN_CIRCLE);
    CHECK("T15 circle: titik atas terisi",
          px(64, 32 - 11) == WS_BTN_COL_CIRCLE);
    CHECK("T15 circle: tengah kosong (outline saja)", px(64, 32) == 0);
    CHECK("T15 circle: titik kanan terisi",
          px(64 + 11, 32) == WS_BTN_COL_CIRCLE);

    clear_fb();
    draw_button(fb, FB_W, FB_H, 64, 32, WS_BTN_TRIANGLE);
    CHECK("T15 triangle: puncak terisi",
          px(64, 32 - 11) == WS_BTN_COL_TRIANGLE);
    CHECK("T15 triangle: tengah kosong (outline saja)", px(64, 32) == 0);
    CHECK("T15 triangle: alas terisi",
          px(64, 32 + 11) == WS_BTN_COL_TRIANGLE);

    clear_fb();
    draw_button(fb, FB_W, FB_H, 64, 32, WS_BTN_SQUARE);
    CHECK("T15 square: tengah sisi atas terisi",
          px(64, 32 - 11) == WS_BTN_COL_SQUARE);
    CHECK("T15 square: tengah kosong (outline saja)", px(64, 32) == 0);
    CHECK("T15 square: sudut terisi",
          px(64 - 11, 32 - 11) == WS_BTN_COL_SQUARE);

    clear_fb();
    draw_button(fb, FB_W, FB_H, 64, 32, WS_BTN_UPDOWN);
    CHECK("T15 updown: puncak segitiga atas terisi",
          px(64 - 8, 32 - 7) == WS_BTN_COL_ARROW);
    CHECK("T15 updown: puncak segitiga bawah terisi",
          px(64 + 8, 32 + 7) == WS_BTN_COL_ARROW);
    CHECK("T15 updown: tengah kosong (di antara dua segitiga)",
          px(64, 32) == 0);

    /* T16: garis + piksel + batas framebuffer. */
    clear_fb();
    draw_line(fb, FB_W, FB_H, 10, 10, 20, 10, 1, 0xFFFFFFFFu);
    CHECK("T16 line horizontal: tengah terisi", px(15, 10) != 0);
    CHECK("T16 line horizontal: baris sebelah kosong", px(15, 11) == 0);
    CHECK("T16 line horizontal: ujung awal terisi", px(10, 10) != 0);
    CHECK("T16 line horizontal: ujung akhir terisi", px(20, 10) != 0);

    clear_fb();
    draw_line(fb, FB_W, FB_H, 0, 0, 10, 10, 1, 0xFFFFFFFFu);
    CHECK("T16 line diagonal: tengah terisi", px(5, 5) != 0);
    CHECK("T16 line diagonal: luar garis kosong", px(5, 6) == 0);

    clear_fb();
    draw_pixel(fb, FB_W, FB_H, -1, -1, 0xFFFFFFFFu);
    draw_pixel(fb, FB_W, FB_H, FB_W, FB_H, 0xFFFFFFFFu);
    draw_button(fb, FB_W, FB_H, -5, -5, WS_BTN_CROSS);
    draw_button(fb, FB_W, FB_H, 200, 100, WS_BTN_CIRCLE);
    draw_button(fb, FB_W, FB_H, 0, 0, WS_BTN_TRIANGLE);
    /* Segitiga di (0,0) menjangkau x -11..11, y -11..11 — (30,30)
     * dan (64,32) jauh dari ketiga gambar di atas. */
    CHECK("T16 di luar framebuffer: tidak crash + piksel jauh tetap kosong",
          px(64, 32) == 0 && px(30, 30) == 0);

    if (failures == 0)
        printf("Semua tes gambar lolos: 0 kegagalan\n");
    else
        printf("Tes gambar: %d kegagalan\n", failures);
    return failures ? 1 : 0;
}
