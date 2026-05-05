#include <stdio.h>
#include <string.h>

#include "host_bios.h"
#include "dos.h"

byte host_disk_image[HOST_DISK_BYTES];

static byte host_stat(void)             { return 0; }
static byte host_in(void)               {
    int c = getchar();
    if (c == EOF) return 0x1A;
    if (c == '\n') return '\r';
    return (byte)c;
}
static void host_out(byte ch)           {
    if (ch == '\r') return;
    putchar(ch); fflush(stdout);
}
static void host_print(byte ch)         { (void)ch; }
static byte host_auxin(void)            { return 0; }
static void host_auxout(byte ch)        { (void)ch; }

static int host_disk_read(byte drive, byte *buf, word count, word sector) {
    (void)drive;
    if ((unsigned long)(sector + count) * HOST_DISK_SECSIZ > sizeof(host_disk_image))
        return 1;
    memcpy(buf, host_disk_image + (unsigned long)sector * HOST_DISK_SECSIZ,
           (size_t)count * HOST_DISK_SECSIZ);
    return 0;
}
static int host_disk_write(byte drive, byte *buf, word count, word sector) {
    (void)drive;
    if ((unsigned long)(sector + count) * HOST_DISK_SECSIZ > sizeof(host_disk_image))
        return 1;
    memcpy(host_disk_image + (unsigned long)sector * HOST_DISK_SECSIZ, buf,
           (size_t)count * HOST_DISK_SECSIZ);
    return 0;
}
static int host_dskchg(byte drive)      { (void)drive; return 1; }

bios_vtable_t host_bios = {
    .stat = host_stat, .in = host_in, .out = host_out,
    .print = host_print, .auxin = host_auxin, .auxout = host_auxout,
    .read = host_disk_read, .write = host_disk_write, .dskchg = host_dskchg,
};

/*
 * Init table per init.c: byte NUMDRV;  word ptr_to_dpt (offset);
 *   then DPT: word SECSIZ; byte SPC; word FIRFAT; byte FATCNT;
 *             word MAXENT; word DSKSIZ.
 * Drive 0 = 320 KB DD: SECSIZ=512, SPC=2, FIRFAT=1, FATCNT=2,
 *                      MAXENT=112, DSKSIZ=640.
 */
static byte init_table_buf[] = {
    /* NUMDRV */   1,
    /* dpt off */  0x03, 0x00,
    /* SECSIZ */   0x00, 0x02,
    /* SPC    */   0x02,
    /* FIRFAT */   0x01, 0x00,
    /* FATCNT */   0x02,
    /* MAXENT */   0x70, 0x00,
    /* DSKSIZ */   0x80, 0x02,
};

byte *host_init_table(void) { return init_table_buf; }
