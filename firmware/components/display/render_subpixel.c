/*
 * render_subpixel.c — pure C, glyph -> BGR-mapped RGB565 from the
 * pre-computed subpixel table. 80 cols x N rows in landscape; each
 * character cell occupies 2 panel pixels horizontally (3 subpixels
 * = 1 RGB565 word per panel pixel, B in low bits, G mid, R high).
 *
 * The table in font_6x8_subpixel.h was generated offline by
 * tools/build_subpixel_font.py with the [1,2,1]/4 filter applied to
 * the 6 source bits of each Spleen 6x8 row (no upsampling — source
 * pixels map 1:1 to subpixels). The on-chip path is a pure lookup,
 * no runtime filtering.
 */
#include <stdint.h>
#include "font_6x8_subpixel.h"
#include "display_internal.h"

/* Render one full text row (80 chars) into a 160-pixel scanline.
 * Caller calls 8 times (rows 0..7) to cover one cell row.
 * Each character cell = 2 panel pixels = 2 RGB565 words. */
void render_subpixel_scanline(const char *text, uint8_t length,
                               int row, uint16_t out[160]) {
    for (int col = 0; col < 80; col++) {
        uint8_t code = (col < length) ? (uint8_t)text[col] : 0x20;
        const uint16_t *cell = font_subpixel[code][row];
        out[col * 2 + 0] = cell[0];
        out[col * 2 + 1] = cell[1];
    }
}
