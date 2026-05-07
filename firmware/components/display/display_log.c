#include "display_internal.h"

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
