/*
 * display_internal.h — types and constants shared between
 * display_log.c, display.c, render_sharp.c, render_subpixel.c.
 * Not part of the public API.
 */
#pragma once

#include <stdint.h>

/* Panel geometry in landscape (rotation 3 per LilyGO driver). */
#define DISPLAY_W              160
#define DISPLAY_H              80

/* Cell metrics. Status bar uses sharp 6x8 (26 cols x 1 row).
 * Log uses subpixel 6sub x 8 (80 cols x 9 rows below the status bar). */
#define DISPLAY_STATUS_ROWS    1
#define DISPLAY_STATUS_COLS    26      /* 160px / 6px */
#define DISPLAY_LOG_ROWS       9       /* (80px - 8px status) / 8px */
#define DISPLAY_LOG_COLS       80      /* 480 subpixels / 6 subpixels */

/* Log ring buffer. One slot per visible row. Each row stores raw
 * printable bytes; renderers convert to glyphs at flush time. */
typedef struct {
    char     rows[DISPLAY_LOG_ROWS][DISPLAY_LOG_COLS + 1]; /* +1: NUL */
    uint8_t  lengths[DISPLAY_LOG_ROWS];
    uint8_t  oldest;       /* index of oldest visible row */
    uint8_t  cur_col;      /* write column in current (newest) row */
    uint8_t  dirty_mask;   /* bit i = row i needs flush */
} display_log_t;

/* ANSI strip state. */
typedef enum {
    DISPLAY_ANSI_GROUND  = 0,
    DISPLAY_ANSI_ESC     = 1,
    DISPLAY_ANSI_CSI     = 2,
} display_ansi_state_t;

/* Status bar contents. */
typedef struct {
    char    program[24];
    uint32_t beat;
    uint8_t  dirty;        /* nonzero when status row needs flush */
} display_status_t;

/* Internal API used by display.c, render_*.c, and host tests. */
void                  display_log_reset(display_log_t *log);
display_ansi_state_t  display_log_putc(display_log_t *log,
                                       display_ansi_state_t state,
                                       uint8_t ch);
