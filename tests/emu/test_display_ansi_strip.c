/*
 * test_display_ansi_strip — drive bytes through the ANSI strip state
 * machine and assert the log buffer contains exactly the printable
 * text.
 *
 * Covers:
 *   - plain ASCII passes through
 *   - CSI sequences (ESC [ ... letter) get stripped, including the
 *     final letter
 *   - simple ESC <letter> sequences (e.g. ESC c) get stripped
 *   - the regression case: '[' inside a CSI parameter list is NOT
 *     treated as a CSI terminator
 */

#include <stdio.h>
#include <string.h>

#include "../../firmware/components/display/include/display_internal.h"

static int fails = 0;

static void T_EXPECT_STREQ(const char *expr_a, const char *a,
                           const char *expr_b, const char *b,
                           const char *file, int line) {
    if (strcmp(a, b) == 0) return;
    fprintf(stderr, "%s:%d: %s != %s\n  got:      \"%s\"\n  expected: \"%s\"\n",
            file, line, expr_a, expr_b, a, b);
    fails++;
}
#define EXPECT_STREQ(a, b) T_EXPECT_STREQ(#a, (a), #b, (b), __FILE__, __LINE__)

static void run(const char *input, const char *expected_row0) {
    display_log_t log;
    display_log_reset(&log);
    display_ansi_state_t st = DISPLAY_ANSI_GROUND;
    for (const char *p = input; *p; p++) {
        st = display_log_putc(&log, st, (uint8_t)*p);
    }
    EXPECT_STREQ(log.rows[0], expected_row0);
}

int main(void) {
    /* Plain ASCII passes through. */
    run("hello", "hello");

    /* Bell, tab, backspace -> dropped (control chars below ESC). */
    run("a\bb", "ab");

    /* CSI color sequence stripped. */
    run("\x1b[31mred\x1b[0m", "red");

    /* CSI cursor home stripped. */
    run("\x1b[Hhi", "hi");

    /* The regression case: '[' inside a CSI parameter list is NOT
     * the terminator. CSI ends at the letter 'm'. */
    run("\x1b[1;31mok", "ok");

    /* Bare ESC <letter> stripped. */
    run("\x1b" "ckeep", "keep");

    /* Multiple CSIs, plain text in between. */
    run("\x1b[2Jclear\x1b[5;5Hhi", "clearhi");

    if (fails == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL (%d)\n", fails);
    return 1;
}
