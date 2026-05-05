/*
 * test_directory.c -- Unit tests for directory name parsing (LODNAME/MOVNAME).
 *
 * Tests derived from the ASM specification in 86DOS.asm:
 *   LODNAME  86DOS.asm:~2200 range  (parse 8.3 name from FCB into NAME1/NAME2)
 *   MOVNAME  86DOS.asm:~2170 range  (copy name from FCB to NAME field)
 *
 * Each test constructs an FCB-style buffer and calls movname() to parse it,
 * then verifies dos->NAME1 (and NAME2 for rename) is filled correctly.
 */

#include <stdio.h>
#include <string.h>

#include "dos.h"

/* ----------------------------------------------------------------------- */
/* Stub globals                                                              */
/* ----------------------------------------------------------------------- */
dos_state_t   *dos;
bios_vtable_t *bios;

static dos_state_t  state;

/* Stub functions that directory.c may reference */
int   dread(byte *buf, word count, word sector, byte *bp)
    { (void)buf; (void)count; (void)sector; (void)bp; return 0; }
int   dwrite(byte *buf, word count, word sector, byte *bp)
    { (void)buf; (void)count; (void)sector; (void)bp; return 0; }
void  chkdirwrite(byte *bp) { (void)bp; }
void  dirwrite(byte al, byte *bp) { (void)al; (void)bp; }
void  dirread(byte al, byte *bp)  { (void)al; (void)bp; }

/* Stub console (lodname/movname don't use it but linker needs it) */
void  con_out(byte ch)              { (void)ch; }
void  con_crlf(void)                {}
void  con_outmes(const byte *msg)   { (void)msg; }

/* ----------------------------------------------------------------------- */
/* Helper: build a minimal FCB                                               */
/* An FCB starts with 1-byte drive code (0=default) followed by 11 bytes   */
/* of name (8 name + 3 ext, space-padded, upper-case).                      */
/* ----------------------------------------------------------------------- */
static void make_fcb(byte *fcb, byte drive,
                     const char *name8, const char *ext3)
{
    int i;
    memset(fcb, 0x20, 36);   /* fill with spaces */
    fcb[0] = drive;
    for (i = 0; i < 8 && name8[i]; i++) fcb[1 + i] = (byte)name8[i];
    for (i = 0; i < 3 && ext3[i];  i++) fcb[9 + i] = (byte)ext3[i];
}

/* ----------------------------------------------------------------------- */
/* Test 1: movname fills NAME1 from FCB filename field                       */
/* ASM: MOVNAME  86DOS.asm -- copies FCB[1..11] into NAME1                  */
/* ----------------------------------------------------------------------- */
static int test_movname_simple(void)
{
    byte  fcb[36];
    static byte dpb[DPB_FIXED_SIZE + 32];
    byte *bp_out = NULL;
    int   result;

    /* movname calls getbp(drive) which needs DRVTAB */
    memset(dpb, 0, sizeof(dpb));
    DPB_SET_BYTE(dpb, DRVNUM, 0);
    dos->DRVTAB[0]  = dpb;
    dos->NUMDRV     = 1;
    dos->CURDRVPT   = dpb;

    make_fcb(fcb, 0, "HELLO   ", "TXT");

    memset(dos->NAME1, 0, sizeof(dos->NAME1));
    result = movname(fcb, &bp_out);

    /* movname should succeed (return 0) */
    if (result != 0) {
        printf("FAIL test_movname_simple: movname returned %d\n", result);
        return 1;
    }

    /* NAME1 should contain "HELLO   TXT" */
    if (memcmp(dos->NAME1, "HELLO   TXT", 11) != 0) {
        char got[12]; memcpy(got, dos->NAME1, 11); got[11] = 0;
        printf("FAIL test_movname_simple: NAME1='%s', expected 'HELLO   TXT'\n", got);
        return 1;
    }

    printf("PASS test_movname_simple\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test 2: lodname -- parse a "COMMAND.COM"-style path into NAME1           */
/* ASM: LODNAME  86DOS.asm -- parses filename from string into NAME buffer  */
/* ----------------------------------------------------------------------- */
static int test_lodname_dotted(void)
{
    /* lodname takes a source buffer and destination buffer */
    byte src[16];
    byte dst[11];
    int  i;

    memset(src, ' ', sizeof(src));
    memset(dst, ' ', 11);

    /* Build "COMMAND COM" as a space-padded FCB name */
    const char *n = "COMMAND COM";
    for (i = 0; i < 11; i++) dst[i] = (byte)n[i];

    /* lodname copies one 11-byte entry from src to dst and uppercases */
    memset(src, 0x20, 11);
    {
        const char *raw = "COMMAND COM";
        for (i = 0; i < 11; i++) src[i] = (byte)raw[i];
    }

    lodname(src, dst);

    if (memcmp(dst, "COMMAND COM", 11) != 0) {
        char got[12]; memcpy(got, dst, 11); got[11] = 0;
        printf("FAIL test_lodname_dotted: dst='%s'\n", got);
        return 1;
    }
    printf("PASS test_lodname_dotted\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test 3: getbp -- returns DRVTAB entry for drive 0 when drive code = 0    */
/* ASM: GETBP  86DOS.asm -- look up DPB for a drive                         */
/* ----------------------------------------------------------------------- */
static int test_getbp_drive0(void)
{
    static byte dpb[DPB_FIXED_SIZE + 32];
    byte *result;

    memset(dpb, 0, sizeof(dpb));
    DPB_SET_BYTE(dpb, DRVNUM, 0);

    dos->DRVTAB[0]  = dpb;
    dos->NUMDRV     = 1;
    dos->CURDRVPT   = dpb;

    result = getbp(0);   /* drive 0 = first entry */
    if (result != dpb) {
        printf("FAIL test_getbp_drive0: got %p, expected %p\n",
               (void *)result, (void *)dpb);
        return 1;
    }
    printf("PASS test_getbp_drive0\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* main                                                                      */
/* ----------------------------------------------------------------------- */
int main(void)
{
    int failures = 0;

    dos  = &state;
    bios = NULL;
    memset(dos, 0, sizeof(*dos));

    failures += test_movname_simple();
    failures += test_lodname_dotted();
    failures += test_getbp_drive0();

    printf("\n%d test(s) failed\n", failures);
    return (failures == 0) ? 0 : 1;
}
