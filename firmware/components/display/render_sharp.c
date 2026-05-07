/*
 * render_sharp.c — pure C, glyph -> RGB565.
 *
 * One source pixel = one panel pixel (no subpixel). 26 cols x N rows
 * in landscape (160px wide / 6px = 26 cols, +4px tail padded black).
 * Always white-on-black for now; color extension is a follow-up.
 *
 * Used always for the status bar; used for log rows when
 * CONFIG_ESPDOS_DISPLAY_SUBPIXEL is off.
 */
#include <stdint.h>
#include "font_6x8.h"
#include "display_internal.h"

#define WHITE_565   0xFFFF
#define BLACK_565   0x0000

/* Render one row of one glyph into 6 RGB565 pixels.
 * row in 0..7. */
void render_sharp_row(uint8_t code, int row, uint16_t out[6]) {
    uint8_t bits = font_6x8[code][row];
    for (int x = 0; x < 6; x++) {
        int on = (bits >> (7 - x)) & 1;
        out[x] = on ? WHITE_565 : BLACK_565;
    }
}

/* Render one full text row (26 chars) into a 160-pixel scanline.
 * Caller calls 8 times (rows 0..7) to cover one cell row. */
void render_sharp_scanline(const char *text, uint8_t length,
                            int row, uint16_t out[160]) {
    for (int col = 0; col < 26; col++) {
        uint8_t code = (col < length) ? (uint8_t)text[col] : 0x20;
        uint16_t cell[6];
        render_sharp_row(code, row, cell);
        for (int x = 0; x < 6; x++) out[col * 6 + x] = cell[x];
    }
    /* 26 * 6 = 156. Pad last 4 pixels black. */
    for (int x = 156; x < 160; x++) out[x] = BLACK_565;
}
