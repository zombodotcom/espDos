#include "display_internal.h"
#include "font_6x8.h"  /* used by render_*.c; included here so the build exercises the header */

/* Implementations land in Tasks 6 and 7. */

void display_log_reset(display_log_t *log) {
    (void)log;
}

display_ansi_state_t display_log_putc(display_log_t *log,
                                       display_ansi_state_t state,
                                       uint8_t ch) {
    (void)log; (void)ch;
    return state;
}
