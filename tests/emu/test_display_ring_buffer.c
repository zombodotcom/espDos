/*
 * test_display_ring_buffer — append 30 lines to a 9-row ring; assert
 * the oldest 21 are gone, newest 9 are in order, dirty_mask covers
 * the wrapped slot.
 */

#include <stdio.h>
#include <string.h>

#include "../../firmware/components/display/include/display_internal.h"

static int fails = 0;

static void T_EXPECT(const char *expr, int cond,
                     const char *file, int line) {
    if (cond) return;
    fprintf(stderr, "%s:%d: %s\n", file, line, expr);
    fails++;
}
#define EXPECT(c) T_EXPECT(#c, (c), __FILE__, __LINE__)

int main(void) {
    display_log_t log;
    display_log_reset(&log);

    display_ansi_state_t st = DISPLAY_ANSI_GROUND;
    for (int i = 0; i < 30; i++) {
        char line[16];
        snprintf(line, sizeof line, "line%02d", i);
        for (char *p = line; *p; p++)
            st = display_log_putc(&log, st, (uint8_t)*p);
        st = display_log_putc(&log, st, (uint8_t)'\n');
    }

    /* After 30 '\n', the active write row is empty (we just scrolled).
     * We start with oldest=1 (active row is 0), and after 30 newlines
     * oldest has advanced 30 times: oldest=(1+30)%9=4. The 9 visible
     * "completed" rows are line22..line29 + empty slot, in order from
     * oldest -> newest. */
    static const char *expected[] = {
        "line22", "line23", "line24", "line25", "line26",
        "line27", "line28", "line29"
    };

    for (int i = 0; i < DISPLAY_LOG_ROWS; i++) {
        uint8_t slot = (log.oldest + i) % DISPLAY_LOG_ROWS;
        if (i < DISPLAY_LOG_ROWS - 1) {
            EXPECT(strcmp(log.rows[slot], expected[i]) == 0);
        } else {
            /* The very last visible row is currently the empty
             * "active write" row after the 30th '\n'. */
            EXPECT(log.rows[slot][0] == '\0');
        }
    }

    /* dirty_mask should be all-ones — we scrolled. */
    EXPECT(log.dirty_mask == 0xFF);

    if (fails == 0) { printf("PASS\n"); return 0; }
    printf("FAIL (%d)\n", fails);
    return 1;
}
