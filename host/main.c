/*
 * host/main.c -- host smoke test for jgbarah's MS-DOS 1.0 C translation.
 *
 * Validates that dos_init() runs end-to-end with a stdio-backed console
 * and a RAM-backed 160KB DOS 1.0 single-sided floppy image, before we
 * commit to porting any of this to ESP32.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios.h"
#include "dos.h"

/* ---- 160KB DOS 1.0 single-sided floppy: 320 sectors * 512 bytes -------- */
#define DISK_SECSIZ   512
#define DISK_DSKSIZ   320
static byte disk_image[DISK_SECSIZ * DISK_DSKSIZ];

/* ---- BIOS vtable: stdio for console, RAM for disk --------------------- */

static byte host_stat(void) {
    return 0;                       /* never claim a key is buffered */
}

static byte host_in(void) {
    int c = getchar();
    if (c == EOF) return 0x1A;       /* Ctrl-Z */
    if (c == '\n') return '\r';      /* kernel expects CR */
    return (byte)c;
}

static void host_out(byte ch) {
    if (ch == '\r') return;          /* CR collapsed; LF below */
    putchar(ch);
    fflush(stdout);
}

static void host_print(byte ch)        { (void)ch; }
static byte host_auxin(void)           { return 0; }
static void host_auxout(byte ch)       { (void)ch; }

static int host_disk_read(byte drive, byte *buf, word count, word sector) {
    (void)drive;
    if ((unsigned long)(sector + count) * DISK_SECSIZ > sizeof(disk_image))
        return 1;
    memcpy(buf, disk_image + (unsigned long)sector * DISK_SECSIZ,
           (size_t)count * DISK_SECSIZ);
    return 0;
}

static int host_disk_write(byte drive, byte *buf, word count, word sector) {
    (void)drive;
    if ((unsigned long)(sector + count) * DISK_SECSIZ > sizeof(disk_image))
        return 1;
    memcpy(disk_image + (unsigned long)sector * DISK_SECSIZ, buf,
           (size_t)count * DISK_SECSIZ);
    return 0;
}

static int host_dskchg(byte drive) {
    (void)drive;
    return 1;                        /* AH>0: disk not changed */
}

static bios_vtable_t host_bios = {
    .stat   = host_stat,
    .in     = host_in,
    .out    = host_out,
    .print  = host_print,
    .auxin  = host_auxin,
    .auxout = host_auxout,
    .read   = host_disk_read,
    .write  = host_disk_write,
    .dskchg = host_dskchg,
};

/*
 * Init table per init.c contract:
 *   byte  NUMDRV
 *   for each drive: word ptr_to_dpt (offset within init_table)
 *   then DPT(s): word SECSIZ; byte SPC; word FIRFAT; byte FATCNT;
 *                word MAXENT; word DSKSIZ
 *
 * Drive 0 = 160KB DOS 1.0 floppy: SECSIZ 512, SPC 1, FIRFAT 1, FATCNT 2,
 *                                 MAXENT 64,  DSKSIZ 320.
 */
static byte init_table[] = {
    /* 0x00 NUMDRV               */ 1,
    /* 0x01 drv0 DPT offset = 3  */ 0x03, 0x00,
    /* 0x03 SECSIZ = 512         */ 0x00, 0x02,
    /* 0x05 SPC = 1              */ 0x01,
    /* 0x06 FIRFAT = 1           */ 0x01, 0x00,
    /* 0x08 FATCNT = 2           */ 0x02,
    /* 0x09 MAXENT = 64          */ 0x40, 0x00,
    /* 0x0B DSKSIZ = 320         */ 0x40, 0x01,
};

int main(void) {
    fprintf(stderr, "[host] calling dos_init()...\n");
    dos_init(&host_bios, init_table);
    fprintf(stderr, "\n[host] dos_init() returned cleanly\n");
    fprintf(stderr, "[host]   NUMDRV=%u  MAXSEC=%u  DATE=0x%04x  CURDRVPT=%p\n",
            dos->NUMDRV, dos->MAXSEC, dos->DATE, (void*)dos->CURDRVPT);
    return 0;
}
