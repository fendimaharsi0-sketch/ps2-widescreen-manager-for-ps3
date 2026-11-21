/* Pad: ioPadInit + poll satu port, dengan edge detection (pressed).
 * Pola dari fork (ps3/src/input.c). */
#ifndef WS_PAD_H
#define WS_PAD_H

#include <stdint.h>

#define PAD_UP     (1u << 0)
#define PAD_DOWN   (1u << 1)
#define PAD_LEFT   (1u << 2)
#define PAD_RIGHT  (1u << 3)
#define PAD_CROSS  (1u << 4)
#define PAD_CIRCLE (1u << 5)
#define PAD_SQUARE (1u << 6)
#define PAD_TRIANGLE (1u << 7)
#define PAD_START  (1u << 8)
#define PAD_SELECT (1u << 9)
#define PAD_L1     (1u << 10)
#define PAD_R1     (1u << 11)

void pad_init(void);
/* Kembalikan bitmask tombol yang BARU ditekan di frame ini (0 kalau
   tidak ada pad atau tidak ada perubahan). */
uint32_t pad_pressed(void);
/* Kembalikan bitmask tombol yang sedang ditahan. */
uint32_t pad_held(void);

#endif
