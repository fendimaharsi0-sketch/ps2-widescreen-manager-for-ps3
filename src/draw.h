/* Gambar teks + kotak ke framebuffer CPU (ARGB). Font bitmap dari fork
 * (ps3/src/font_data.h, DejaVu Sans Mono Bold, Bitstream Vera licence):
 * FONT_W 16, FONT_H 30, alpha 4-bit, dua piksel per byte (nibble tinggi
 * dulu), karakter 32..126. */
#ifndef WS_DRAW_H
#define WS_DRAW_H

#include <stdint.h>

/* Tombol PlayStation (Fase 2q): simbol digambar sebagai bentuk vektor
 * — font bitmap cuma ASCII, tidak punya glyph ✕○△□. Warna klasik
 * PlayStation. Ukuran simbol tetap (tidak diskalakan h/720), sama
 * seperti font bitmap 16x30. */
typedef enum {
    WS_BTN_NONE = 0,
    WS_BTN_CROSS,    /* ✕ biru */
    WS_BTN_CIRCLE,   /* ○ merah */
    WS_BTN_TRIANGLE, /* △ hijau */
    WS_BTN_SQUARE,   /* □ pink */
    WS_BTN_UPDOWN    /* ▲▼ putih (pasangan, untuk Up/Down) */
} ws_btn_t;

/* Lebar slot horizontal yang dipakai satu simbol di footer (px). */
#define WS_BTN_SLOT 26
#define WS_BTN_SLOT_UPDOWN 34

#define WS_BTN_COL_CROSS   0xFF508CFFu /* biru */
#define WS_BTN_COL_CIRCLE  0xFFE63C3Cu /* merah */
#define WS_BTN_COL_TRIANGLE 0xFF00C86Eu /* hijau */
#define WS_BTN_COL_SQUARE  0xFFE66EC8u /* pink */
#define WS_BTN_COL_ARROW   0xFFE8E8E8u /* putih */

void draw_fill(uint32_t *fb, unsigned w, unsigned h,
               int x, int y, int ww, int hh, uint32_t argb);
void draw_text(uint32_t *fb, unsigned w, unsigned h,
               int x, int y, const char *s, uint32_t argb);
int draw_text_width(const char *s);

/* Satu piksel (dipotong di tepi framebuffer). */
void draw_pixel(uint32_t *fb, unsigned w, unsigned h,
                int x, int y, uint32_t argb);
/* Garis Bresenham, tebal `thick` px (blok thick x thick per titik). */
void draw_line(uint32_t *fb, unsigned w, unsigned h,
               int x0, int y0, int x1, int y1, int thick, uint32_t argb);
/* Lingkaran outline (midpoint), tebal `thick` px. */
void draw_circle(uint32_t *fb, unsigned w, unsigned h,
                 int cx, int cy, int r, int thick, uint32_t argb);
/* Simbol tombol, tengah di (cx, cy). Radius tombol muka 11 px,
 * segitiga ▲▼ 7 px. */
void draw_button(uint32_t *fb, unsigned w, unsigned h,
                 int cx, int cy, ws_btn_t btn);

#endif
