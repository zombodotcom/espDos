#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "host_bios.h"
#include "fat12_image.h"
#include "dos.h"

/* ---- Test registry (capped; raise if needed) ---- */
#define MAX_TESTS 64
static const char *test_names[MAX_TESTS];
static test_fn_t   test_fns[MAX_TESTS];
static int         test_count = 0;

/* ---- Per-test failure tracking ---- */
static int current_test_failures = 0;

void register_test(const char *name, test_fn_t fn) {
    if (test_count < MAX_TESTS) {
        test_names[test_count] = name;
        test_fns[test_count]   = fn;
        test_count++;
    }
}

void test_assert(int cond, const char *expr, const char *file, int line) {
    if (!cond) {
        current_test_failures++;
        fprintf(stderr, "  ASSERT FAIL: %s  (%s:%d)\n", expr, file, line);
    }
}

void reset_kernel_state(void) {
    /* Re-zero the kernel's static state by re-running dos_init().
     * This is sufficient because init.c keeps state in static storage
     * and dos_init() begins with memset(dos, 0, sizeof(*dos)). */
    fat12_init_empty_320kb(host_disk_image);
    /* dos_init prints a banner and prompts for date; redirect stdin
     * to feed it a date and stdout to /dev/null-equivalent so the test
     * runner output stays clean. We just feed via freopen on stdin. */
    freopen("date_input.tmp", "r", stdin);
    /* The caller is responsible for ensuring date_input.tmp exists with
     * "1-1-81\n". main() in test_kernel.c writes it once at startup. */
    dos_init(&host_bios, host_init_table());
}

int run_all_tests(void) {
    int total_failed = 0;
    for (int i = 0; i < test_count; i++) {
        current_test_failures = 0;
        fprintf(stderr, "[test] %s\n", test_names[i]);
        reset_kernel_state();
        test_fns[i]();
        if (current_test_failures > 0) {
            fprintf(stderr, "[test] %s: %d FAIL\n", test_names[i],
                    current_test_failures);
            total_failed++;
        } else {
            fprintf(stderr, "[test] %s: PASS\n", test_names[i]);
        }
    }
    fprintf(stderr, "\n%d / %d tests passed\n",
            test_count - total_failed, test_count);
    return total_failed;
}
