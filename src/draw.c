/* Gambar teks + kotak ke framebuffer CPU. Blending meniru fork
 * (ps3/src/draw.c ovl_blend/ovl_text). */
#include <string.h>

#include "draw.h"
#include "font_data.h"

static inline uint32_t blend(uint32_t dst, uint32_t src, unsigned a)
{
    unsigned da = dst >> 24, oa = a > da ? a : da;
    unsigned r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * (255 - a)) / 255;
    unsigned g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * (255 - a)) / 255;
    unsigned b = ((src & 255) * a + (dst & 255) * (255 - a)) / 255;
    return oa << 24 | r << 16 | g << 8 | b;
}

void draw_fill(uint32_t *fb, unsigned w, unsigned h,
               int x, int y, int ww, int hh, uint32_t argb)
{
    unsigned a = argb >> 24;
    int i, j;
    if (x < 0) { ww += x; x = 0; }
    if (y < 0) { hh += y; y = 0; }
    if ((unsigned)(x + ww) > w) ww = (int)w - x;
    if ((unsigned)(y + hh) > h) hh = (int)h - y;
    if (ww <= 0 || hh <= 0) return;
    for (j = 0; j < hh; j++)
        for (i = 0; i < ww; i++) {
            uint32_t *p = &fb[(y + j) * w + x + i];
            *p = a == 255 ? argb : blend(*p, argb, a);
        }
}

int draw_text_width(const char *s)
{
    int n = 0;
    for (; *s; s++) n++;
    return n * FONT_W;
}

void draw_pixel(uint32_t *fb, unsigned w, unsigned h,
                int x, int y, uint32_t argb)
{
    unsigned a;
    uint32_t *p;
    if (x < 0 || y < 0 || (unsigned)x >= w || (unsigned)y >= h) return;
    a = argb >> 24;
    p = &fb[y * w + x];
    *p = a == 255 ? argb : blend(*p, argb, a);
}

/* Blok thick x thick berpusat di (x, y) — satu titik garis tebal. */
static void draw_dot(uint32_t *fb, unsigned w, unsigned h,
                     int x, int y, int thick, uint32_t argb)
{
    int i, j, o = thick / 2;
    for (j = -o; j < thick - o; j++)
        for (i = -o; i < thick - o; i++)
            draw_pixel(fb, w, h, x + i, y + j, argb);
}

void draw_line(uint32_t *fb, unsigned w, unsigned h,
               int x0, int y0, int x1, int y1, int thick, uint32_t argb)
{
    int dx = x1 - x0, dy = y1 - y0, sx, sy, err, e2;
    if (thick < 1) thick = 1;
    sx = dx > 0 ? 1 : -1;
    sy = dy > 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = dx > dy ? dx : -dy;
    err /= 2;
    for (;;) {
        draw_dot(fb, w, h, x0, y0, thick, argb);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

void draw_circle(uint32_t *fb, unsigned w, unsigned h,
                 int cx, int cy, int r, int thick, uint32_t argb)
{
    int rr, x, y, d;
    if (r < 1 || thick < 1) return;
    /* Tebal = beberapa lingkaran konsentris (outline rapi). */
    for (rr = r; rr > r - thick && rr >= 1; rr--) {
        x = 0;
        y = rr;
        d = 3 - 2 * rr;
        while (x <= y) {
            draw_pixel(fb, w, h, cx + x, cy + y, argb);
            draw_pixel(fb, w, h, cx - x, cy + y, argb);
            draw_pixel(fb, w, h, cx + x, cy - y, argb);
            draw_pixel(fb, w, h, cx - x, cy - y, argb);
            draw_pixel(fb, w, h, cx + y, cy + x, argb);
            draw_pixel(fb, w, h, cx - y, cy + x, argb);
            draw_pixel(fb, w, h, cx + y, cy - x, argb);
            draw_pixel(fb, w, h, cx - y, cy - x, argb);
            if (d < 0) d += 4 * x + 6;
            else { d += 4 * (x - y) + 10; y--; }
            x++;
        }
    }
}

/* Segitiga outline: puncak (ax, ay), alas dari (bx, by) ke (cx, cy). */
static void draw_tri_outline(uint32_t *fb, unsigned w, unsigned h,
                             int ax, int ay, int bx, int by,
                             int cx, int cy, int thick, uint32_t argb)
{
    draw_line(fb, w, h, ax, ay, bx, by, thick, argb);
    draw_line(fb, w, h, bx, by, cx, cy, thick, argb);
    draw_line(fb, w, h, cx, cy, ax, ay, thick, argb);
}

void draw_button(uint32_t *fb, unsigned w, unsigned h,
                 int cx, int cy, ws_btn_t btn)
{
    const int r = 11, t = 3, ar = 7;
    switch (btn) {
    case WS_BTN_CROSS:
        draw_line(fb, w, h, cx - r, cy - r, cx + r, cy + r, t,
                  WS_BTN_COL_CROSS);
        draw_line(fb, w, h, cx - r, cy + r, cx + r, cy - r, t,
                  WS_BTN_COL_CROSS);
        break;
    case WS_BTN_CIRCLE:
        draw_circle(fb, w, h, cx, cy, r, t, WS_BTN_COL_CIRCLE);
        break;
    case WS_BTN_TRIANGLE:
        draw_tri_outline(fb, w, h, cx, cy - r, cx + r, cy + r,
                         cx - r, cy + r, t, WS_BTN_COL_TRIANGLE);
        break;
    case WS_BTN_SQUARE:
        draw_line(fb, w, h, cx - r, cy - r, cx + r, cy - r, t,
                  WS_BTN_COL_SQUARE);
        draw_line(fb, w, h, cx + r, cy - r, cx + r, cy + r, t,
                  WS_BTN_COL_SQUARE);
        draw_line(fb, w, h, cx + r, cy + r, cx - r, cy + r, t,
                  WS_BTN_COL_SQUARE);
        draw_line(fb, w, h, cx - r, cy + r, cx - r, cy - r, t,
                  WS_BTN_COL_SQUARE);
        break;
    case WS_BTN_UPDOWN:
        draw_tri_outline(fb, w, h, cx - 8, cy - ar, cx - 8 + ar, cy + ar,
                         cx - 8 - ar, cy + ar, t, WS_BTN_COL_ARROW);
        draw_tri_outline(fb, w, h, cx + 8, cy + ar, cx + 8 + ar, cy - ar,
                         cx + 8 - ar, cy - ar, t, WS_BTN_COL_ARROW);
        break;
    default:
        break;
    }
}

void draw_text(uint32_t *fb, unsigned w, unsigned h,
               int x, int y, const char *s, uint32_t argb)
{
    unsigned ca = argb >> 24;
    for (; *s; s++, x += FONT_W) {
        int c = (unsigned char)*s, r, k;
        const unsigned char *g;
        if (c < 32 || c > 126) c = '?';
        if (x + FONT_W > (int)w) break;
        if (x < 0 || y < 0 || y + FONT_H > (int)h) continue;
        g = font_data[c - 32];
        for (r = 0; r < FONT_H; r++)
            for (k = 0; k < FONT_W; k++) {
                unsigned v = g[(r * FONT_W + k) >> 1];
                unsigned a = (k & 1) ? (v & 15) : (v >> 4);
                if (a) {
                    uint32_t *p = &fb[(y + r) * w + x + k];
                    *p = blend(*p, argb, a * 17 * ca / 255);
                }
            }
    }
}
