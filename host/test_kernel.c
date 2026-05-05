/* test_kernel.c — kernel integration tests.
 * Tests are added one per task in this plan. */
#include <stdio.h>
#include "tests.h"

#include <string.h>
#include "host_bios.h"
#include "fcb.h"
#include "dos.h"

/* Build a user-mode "search" FCB: drive=0 (current), name+ext = "????????.???". */
static void build_wildcard_fcb(byte fcb[FCB_SIZE]) {
    memset(fcb, 0, FCB_SIZE);
    fcb[FCB_DRIVE] = 0;
    memset(fcb + FCB_NAME, '?', 8);    /* 8 chars, all wildcard */
    memset(fcb + FCB_EXT,  '?', 3);    /* 3 chars, all wildcard */
}

static void test_srchfrst_empty_volume_no_match(void) {
    byte fcb[FCB_SIZE];
    build_wildcard_fcb(fcb);
    byte result = fn_srchfrst(fcb);
    /* DOS convention: AL = 0xFF when no match. */
    ASSERT(result == 0xFF);
}
REGISTER_TEST(test_srchfrst_empty_volume_no_match);

int main(void) {
    /* dos_init() prompts for date; pre-stage the file the harness reads. */
    FILE *f = fopen("date_input.tmp", "w");
    if (!f) { perror("date_input.tmp"); return 2; }
    fputs("1-1-81\n", f);
    fclose(f);

    return run_all_tests();
}
