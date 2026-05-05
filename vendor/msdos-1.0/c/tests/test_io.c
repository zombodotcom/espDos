/*
 * test_io.c -- Unit tests for record I/O position calculation.
 *
 * Tests derived from the ASM specification in 86DOS.asm:
 *   GETREC   86DOS.asm:1741 range  (compute record position from FCB NR/EXTENT)
 *   SETUP    86DOS.asm:1333-1396   (byte-position from record position)
 *   SETNREX  86DOS.asm:1303-1317   (update FCB NR and EXTENT from record pos)
 *
 * These tests do not perform any actual disk I/O; they verify the arithmetic
 * that maps FCB record fields to byte positions and vice versa.
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

/* Stubs for functions pulled in by the linker from io.c / fat.c / disk.c  */
int   dread(byte *buf, word count, word sector, byte *bp)
    { (void)buf; (void)count; (void)sector; (void)bp; return -1; }
int   dwrite(byte *buf, word count, word sector, byte *bp)
    { (void)buf; (void)count; (void)sector; (void)bp; return -1; }
void  chkdirwrite(byte *bp) { (void)bp; }
void  dirwrite(byte al, byte *bp) { (void)al; (void)bp; }
void  dirread(byte al, byte *bp)  { (void)al; (void)bp; }
void  con_out(byte ch)              { (void)ch; }
void  con_crlf(void)                {}
void  con_outmes(const byte *msg)   { (void)msg; }

/* ----------------------------------------------------------------------- */
/* FCB helpers                                                               */
/* ----------------------------------------------------------------------- */

/*
 * make_fcb_io -- construct an FCB with RECSIZ, NR, EXTENT, and RR set.
 * FCB layout (86DOS.asm lines 60-73):
 *   [0]      drive code
 *   [1..11]  name (8) + ext (3), space-padded
 *   [12..13] EXTENT (word)
 *   [14..15] RECSIZ (word)   -- actually NR is at 32, RR at 33..35
 *   ...
 *   [32]     NR  (next record, byte)
 *   [33..35] RR  (random record, 3 bytes)
 */
static void make_fcb_io(byte *fcb, word recsiz, byte nr, word extent_lo,
                        dword rr)
{
    memset(fcb, 0, 36);
    fcb[0]  = 0;        /* default drive */
    memcpy(fcb + 1, "TEST    TXT", 11);
    /* EXTENT at offset 12 */
    fcb[12] = (byte)(extent_lo & 0xFF);
    fcb[13] = (byte)(extent_lo >> 8);
    /* RECSIZ at offset 14 */
    fcb[14] = (byte)(recsiz & 0xFF);
    fcb[15] = (byte)(recsiz >> 8);
    /* NR at offset 32 */
    fcb[32] = nr;
    /* RR at offset 33..35 */
    fcb[33] = (byte)(rr & 0xFF);
    fcb[34] = (byte)((rr >> 8) & 0xFF);
    fcb[35] = (byte)((rr >> 16) & 0xFF);
}

/* ----------------------------------------------------------------------- */
/* Test 1: fn_getrec -- record position from NR+EXTENT                      */
/*                                                                           */
/* ASM GETREC (86DOS.asm ~1741):                                             */
/*   AX = NR                                                                 */
/*   DX = EXTENT  (word from FCB)                                           */
/*   CX = ... combined, then expands to DX:AX = record position             */
/*                                                                           */
/* For RECSIZ=128, each extent holds 128 records (NR range 0..127).         */
/* record_pos = extent * 128 + NR                                            */
/* ----------------------------------------------------------------------- */
static int test_getrec_basic(void)
{
    byte  fcb[36];
    word  dx_out = 0;
    word  ax;

    /* extent=1, NR=5, RECSIZ=128 => record_pos = 1*128 + 5 = 133 */
    make_fcb_io(fcb, 128, 5, 1, 0);

    ax = fn_getrec(fcb, &dx_out);

    /* fn_getrec returns AX (low word of record pos), dx_out = high word */
    if (ax != 133 || dx_out != 0) {
        printf("FAIL test_getrec_basic: ax=%u dx=%u (expected ax=133 dx=0)\n",
               ax, dx_out);
        return 1;
    }
    printf("PASS test_getrec_basic\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test 2: fn_getrec -- extent=0, NR=0 => record_pos = 0                   */
/* ----------------------------------------------------------------------- */
static int test_getrec_zero(void)
{
    byte  fcb[36];
    word  dx_out = 0xFFFF;
    word  ax;

    make_fcb_io(fcb, 128, 0, 0, 0);
    ax = fn_getrec(fcb, &dx_out);

    if (ax != 0 || dx_out != 0) {
        printf("FAIL test_getrec_zero: ax=%u dx=%u\n", ax, dx_out);
        return 1;
    }
    printf("PASS test_getrec_zero\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test 3: fn_setrndrec -- set RR field from NR+EXTENT                      */
/* ASM: SETRNDREC  86DOS.asm:2545-2549                                      */
/*   CALL GETREC  ; AX:DX = record position                                 */
/*   MOV [DI+33],AX  ; RR bytes 0..1                                        */
/*   MOV [DI+35],DL  ; RR byte 2                                            */
/* ----------------------------------------------------------------------- */
static int test_setrndrec(void)
{
    byte  fcb[36];

    /* extent=2, NR=10, RECSIZ=128 => record_pos = 2*128 + 10 = 266 */
    make_fcb_io(fcb, 128, 10, 2, 0);
    fn_setrndrec(fcb);

    /* RR should now be 266 = 0x10A */
    {
        dword rr = fcb[33] | ((dword)fcb[34] << 8) | ((dword)fcb[35] << 16);
        if (rr != 266) {
            printf("FAIL test_setrndrec: RR=%lu, expected 266\n", (unsigned long)rr);
            return 1;
        }
    }
    printf("PASS test_setrndrec\n");
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
    dos->DMAADD = 0x80;

    failures += test_getrec_basic();
    failures += test_getrec_zero();
    failures += test_setrndrec();

    printf("\n%d test(s) failed\n", failures);
    return (failures == 0) ? 0 : 1;
}
