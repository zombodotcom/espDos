/* test_kernel.c — kernel integration tests.
 * Tests are added one per task in this plan. */
#include <stdio.h>
#include "tests.h"

int main(void) {
    /* dos_init() prompts for date; pre-stage the file the harness reads. */
    FILE *f = fopen("date_input.tmp", "w");
    if (!f) { perror("date_input.tmp"); return 2; }
    fputs("1-1-81\n", f);
    fclose(f);

    return run_all_tests();
}
