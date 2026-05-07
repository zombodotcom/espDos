/*
 * render_subpixel.c — TEMPORARY stub returning all-black.
 *
 * The real BGR-mapped subpixel rasterizer lands in Task 12. Until
 * then, display.c with CONFIG_ESPDOS_DISPLAY_SUBPIXEL=y needs SOME
 * definition of this symbol to link. Black-filled output produces a
 * blank-but-functional log on hardware; the status bar still renders
 * via render_sharp.c.
 */
#include <stdint.h>

#include "display_internal.h"

void render_subpixel_scanline(const char *text, uint8_t length,
                               int row, uint16_t out[160]) {
    (void)text; (void)length; (void)row;
    for (int x = 0; x < 160; x++) out[x] = 0x0000;
}
