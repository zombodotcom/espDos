#include <string.h>
#include "display_internal.h"
#include "font_6x8.h"  /* used by render_*.c, included here so the build exercises the header */

void display_log_reset(display_log_t *log) {
    memset(log, 0, sizeof(*log));
    /* Active write row is rows[(oldest - 1) mod ROWS]. Initialize
     * oldest = 1 so the active row is rows[0] until the first '\n'. */
    log->oldest = 1;
}

display_ansi_state_t display_log_putc(display_log_t *log,
                                       display_ansi_state_t state,
                                       uint8_t ch) {
    /* Escape state machine. */
    if (state == DISPLAY_ANSI_ESC) {
        if (ch == '[') return DISPLAY_ANSI_CSI;
        /* Bare ESC <letter> — that one byte ends the sequence. */
        return DISPLAY_ANSI_GROUND;
    }
    if (state == DISPLAY_ANSI_CSI) {
        /* Final byte of a CSI sequence is in 0x40..0x7E. Anything
         * else (digits, ';', '?', etc.) is parameter data — keep
         * swallowing. */
        if (ch >= 0x40 && ch <= 0x7E) return DISPLAY_ANSI_GROUND;
        return DISPLAY_ANSI_CSI;
    }

    /* DISPLAY_ANSI_GROUND. */
    if (ch == 0x1B) return DISPLAY_ANSI_ESC;

    /* Newline: scroll. Advance oldest forward; the previously-oldest
     * row becomes the new active write row, so clear it. */
    if (ch == '\n') {
        log->oldest = (log->oldest + 1) % DISPLAY_LOG_ROWS;
        uint8_t new_newest = (log->oldest + DISPLAY_LOG_ROWS - 1) % DISPLAY_LOG_ROWS;
        memset(log->rows[new_newest], 0, sizeof(log->rows[new_newest]));
        log->lengths[new_newest] = 0;
        log->cur_col = 0;
        log->dirty_mask = 0xFF;  /* whole log scrolled -> all dirty */
        return DISPLAY_ANSI_GROUND;
    }
    if (ch == '\r') {
        log->cur_col = 0;
        return DISPLAY_ANSI_GROUND;
    }
    /* Drop other control chars below 0x20 — bell, tab, backspace,
     * etc. — to keep the log readable. */
    if (ch < 0x20 || ch == 0x7F) return DISPLAY_ANSI_GROUND;

    /* Append printable byte to the active (newest) row. */
    uint8_t newest = (log->oldest + DISPLAY_LOG_ROWS - 1) % DISPLAY_LOG_ROWS;
    if (log->cur_col < DISPLAY_LOG_COLS) {
        log->rows[newest][log->cur_col] = (char)ch;
        log->cur_col++;
        log->rows[newest][log->cur_col] = '\0';
        if (log->cur_col > log->lengths[newest])
            log->lengths[newest] = log->cur_col;
        log->dirty_mask |= (1u << newest);
    }
    return DISPLAY_ANSI_GROUND;
}
