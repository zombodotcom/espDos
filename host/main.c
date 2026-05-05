/*
 * host/main.c -- minimal smoke test: just verify dos_init() runs end-to-end.
 * Real kernel verification lives in test_kernel.c.
 */
#include <stdio.h>
#include <string.h>

#include "host_bios.h"
#include "dos.h"

int main(void) {
    memset(host_disk_image, 0, sizeof(host_disk_image));
    fprintf(stderr, "[host] calling dos_init()...\n");
    dos_init(&host_bios, host_init_table());
    fprintf(stderr, "\n[host] dos_init() returned cleanly\n");
    fprintf(stderr, "[host]   NUMDRV=%u  MAXSEC=%u  DATE=0x%04x\n",
            dos->NUMDRV, dos->MAXSEC, dos->DATE);
    return 0;
}
