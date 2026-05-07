/*
 * test_subpixel_glyph_table — re-run the [1,2,1]/4 BGR-mapped filter
 * in C against font_6x8.h, compare against font_6x8_subpixel.h.
 *
 * Spot-checks 5 representative glyphs: space (all zero), 'A' (mid-
 * weight letter), '8' (curves), 0x7F (high-bit), 0xDB (full-block-
 * like, mostly-on row pattern).
 *
 * If this fails, run:  python tools/build_subpixel_font.py
 * to regenerate the committed header. The script and this test must
 * use the same filter algorithm (no upsampling, [1,2,1]/4 on 6
 * source samples directly, edge-clamp).
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "font_6x8.h"
#include "font_6x8_subpixel.h"

static int fails = 0;

/* Match Python's round() banker's rounding for ties. CPython's round()
 * does banker's rounding, but for our small integer-domain values
 * (multiplying by 31 or 63 with intermediate values 0/0.25/0.5/0.75/
 * 1.0), simple round-half-to-nearest with C's lround works because
 * the only halfway case is 0.5 * 63 = 31.5 → Python rounds to 32
 * (even), and lround also rounds to 32 (away from zero). For r5/b5
 * (* 31), all halves are integer (0.5 * 31 = 15.5) and both Python
 * and lround give 16. So lround is safe here. */
static int round_clamp(double v, int hi) {
    long n = lround(v);
    if (n < 0)  n = 0;
    if (n > hi) n = hi;
    return (int)n;
}

static uint16_t pack_rgb565(double sub_b, double sub_g, double sub_r) {
    int r5 = round_clamp(sub_r * 31.0, 31);
    int g6 = round_clamp(sub_g * 63.0, 63);
    int b5 = round_clamp(sub_b * 31.0, 31);
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void render_glyph_c(uint8_t code, uint16_t out[8][2]) {
    for (int y = 0; y < 8; y++) {
        uint8_t row_byte = font_6x8[code][y];
        int s[6];
        for (int x = 0; x < 6; x++) {
            s[x] = (row_byte >> (7 - x)) & 1;
        }
        double f[6];
        for (int k = 0; k < 6; k++) {
            int left  = (k > 0) ? s[k - 1] : s[0];
            int ctr   =           s[k];
            int right = (k < 5) ? s[k + 1] : s[5];
            f[k] = (left + 2 * ctr + right) / 4.0;
        }
        for (int cell = 0; cell < 2; cell++) {
            double sub_b = f[cell * 3 + 0];
            double sub_g = f[cell * 3 + 1];
            double sub_r = f[cell * 3 + 2];
            out[y][cell] = pack_rgb565(sub_b, sub_g, sub_r);
        }
    }
}

static void check_glyph(uint8_t code) {
    uint16_t expected[8][2];
    render_glyph_c(code, expected);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 2; x++) {
            if (font_subpixel[code][y][x] != expected[y][x]) {
                fprintf(stderr, "glyph 0x%02x [%d][%d]: got 0x%04x, want 0x%04x\n",
                        code, y, x, font_subpixel[code][y][x], expected[y][x]);
                fails++;
            }
        }
    }
}

int main(void) {
    check_glyph(0x20);   /* space */
    check_glyph(0x41);   /* 'A' */
    check_glyph(0x38);   /* '8' */
    check_glyph(0x7F);   /* DEL */
    check_glyph(0xDB);   /* full block */

    if (fails == 0) { printf("PASS\n"); return 0; }
    printf("FAIL (%d) - run: python tools/build_subpixel_font.py\n", fails);
    return 1;
}
