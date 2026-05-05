/*
 * test_fat.c -- Unit tests for FAT12 pack/unpack routines.
 *
 * Tests derived from the ASM specification in 86DOS.asm:
 *   UNPACK  86DOS.asm:369-395
 *   PACK    86DOS.asm:397-422
 *
 * The FAT encoding rule (12-bit entries, two per three bytes):
 *   For even cluster BX:  entry occupies bits [11:0]  of bytes [BX + BX/2]
 *   For odd  cluster BX:  entry occupies bits [15:4]  of bytes [BX + BX/2]
 *
 * Each test function returns 0 on pass, 1 on failure.
 * main() runs all tests and returns the total failure count.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "dos.h"

/* ----------------------------------------------------------------------- */
/* Minimal stub environment required by fat.c                               */
/* ----------------------------------------------------------------------- */
dos_state_t   *dos;
bios_vtable_t *bios;

static dos_state_t  state;

/*
 * make_dpb -- build a minimal DPB suitable for FAT tests.
 * maxclus = total clusters + 1.
 */
static void make_dpb(byte *bp, word maxclus, word secsiz,
                     byte clusmsk, byte clusshft)
{
    memset(bp, 0, DPB_FIXED_SIZE + 256);
    DPB_SET_BYTE(bp, DRVNUM,   0);
    DPB_SET_WORD(bp, SECSIZ,   secsiz);
    DPB_SET_BYTE(bp, CLUSMSK,  clusmsk);
    DPB_SET_BYTE(bp, CLUSSHFT, clusshft);
    DPB_SET_WORD(bp, FIRFAT,   1);
    DPB_SET_BYTE(bp, FATCNT,   2);
    DPB_SET_WORD(bp, MAXENT,   64);
    DPB_SET_WORD(bp, FIRREC,   10);
    DPB_SET_WORD(bp, MAXCLUS,  maxclus);
    DPB_SET_BYTE(bp, FATSIZ,   1);
    DPB_SET_WORD(bp, FIRDIR,   3);
    DPB_SET_BYTE(bp, DIRTYFAT, DIRTYFAT_UNREAD);
}

/* ----------------------------------------------------------------------- */
/* Test 1: unpack an even cluster                                            */
/* ----------------------------------------------------------------------- */
static int test_unpack_even(void)
{
    byte fat_mem[DPB_FIXED_SIZE + 256];
    byte *bp  = fat_mem;
    byte *fat = DPB_FAT_PTR(bp);
    word result;

    make_dpb(bp, 10, 512, 0, 0);

    /* Manually encode cluster 2 (even) = 0x123
     * bytes at offset 2 + 2/2 = 3: fat[3] holds low byte, fat[4] holds hi
     * 12-bit entry at even cluster BX = 2:
     *   offset = BX + BX/2 = 2 + 1 = 3
     *   fat[3] = low byte (0x23), fat[4] low nibble = high nibble (0x1x)
     */
    memset(fat, 0, 64);
    fat[3] = 0x23;
    fat[4] = 0x01;   /* little-endian word at offset 3 = 0x0123; AND 0xFFF = 0x123 */

    result = fat_unpack(fat, 2, bp);
    if (result != 0x123) {
        printf("FAIL test_unpack_even: expected 0x123, got 0x%03X\n", result);
        return 1;
    }
    printf("PASS test_unpack_even\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test 2: unpack an odd cluster                                             */
/* ----------------------------------------------------------------------- */
static int test_unpack_odd(void)
{
    byte fat_mem[DPB_FIXED_SIZE + 256];
    byte *bp  = fat_mem;
    byte *fat = DPB_FAT_PTR(bp);
    word result;

    make_dpb(bp, 10, 512, 0, 0);

    /* Encode cluster 3 (odd) = 0xABC
     * offset = BX + BX/2 = 3 + 1 = 4
     * fat[4] high nibble = low nibble of value (0xC), fat[5] = high byte (0xAB)
     * Wait -- ASM UNPACK for odd:
     *   DI = [SI+BX+BX/2] (word read)
     *   JNC => DI >>= 4   (for odd: carry set from SHR BX means odd)
     * Let's set fat[4] = 0xCx (where x is low nibble of cluster 2),
     *                fat[5] = 0xAB
     * The word read at offset 4 is fat[4] | (fat[5]<<8) = 0xABCx
     * Then SHR 4 times => 0x0ABC
     */
    memset(fat, 0, 64);
    fat[4] = 0xC0;   /* low nibble don't-care = 0, high nibble = 0xC */
    fat[5] = 0xAB;

    result = fat_unpack(fat, 3, bp);
    if (result != 0xABC) {
        printf("FAIL test_unpack_odd: expected 0xABC, got 0x%03X\n", result);
        return 1;
    }
    printf("PASS test_unpack_odd\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test 3: pack/unpack round-trip for every cluster 2..9                    */
/* ----------------------------------------------------------------------- */
static int test_pack_unpack_roundtrip(void)
{
    byte fat_mem[DPB_FIXED_SIZE + 256];
    byte *bp  = fat_mem;
    byte *fat = DPB_FAT_PTR(bp);
    word bx, expected, got;
    int  failures = 0;

    make_dpb(bp, 12, 512, 0, 0);
    memset(fat, 0, 64);

    /* Write values and read them back */
    for (bx = 2; bx <= 9; bx++) {
        expected = (word)(0x100 + bx);   /* 0x102..0x109 */
        fat_pack(fat, bx, expected);
        got = fat_unpack(fat, bx, bp);
        if (got != expected) {
            printf("FAIL roundtrip cluster %u: expected 0x%03X, got 0x%03X\n",
                   bx, expected, got);
            failures++;
        }
    }
    if (failures == 0)
        printf("PASS test_pack_unpack_roundtrip\n");
    return (failures > 0) ? 1 : 0;
}

/* ----------------------------------------------------------------------- */
/* Test 4: EOF marker round-trip                                             */
/* ----------------------------------------------------------------------- */
static int test_pack_eof(void)
{
    byte fat_mem[DPB_FIXED_SIZE + 256];
    byte *bp  = fat_mem;
    byte *fat = DPB_FAT_PTR(bp);
    word got;

    make_dpb(bp, 12, 512, 0, 0);
    memset(fat, 0, 64);

    fat_pack(fat, 4, FAT_EOF);
    got = fat_unpack(fat, 4, bp);
    if (got != FAT_EOF) {
        printf("FAIL test_pack_eof: expected 0xFFF, got 0x%03X\n", got);
        return 1;
    }
    printf("PASS test_pack_eof\n");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test 5: free cluster (0) round-trip; unpack should set zero flag         */
/* ----------------------------------------------------------------------- */
static int test_pack_free(void)
{
    byte fat_mem[DPB_FIXED_SIZE + 256];
    byte *bp  = fat_mem;
    byte *fat = DPB_FAT_PTR(bp);
    word got;

    make_dpb(bp, 12, 512, 0, 0);
    memset(fat, 0, 64);

    fat_pack(fat, 5, FAT_FREE);
    got = fat_unpack(fat, 5, bp);
    if (got != FAT_FREE) {
        printf("FAIL test_pack_free: expected 0x000, got 0x%03X\n", got);
        return 1;
    }
    printf("PASS test_pack_free\n");
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

    failures += test_unpack_even();
    failures += test_unpack_odd();
    failures += test_pack_unpack_roundtrip();
    failures += test_pack_eof();
    failures += test_pack_free();

    printf("\n%d test(s) failed\n", failures);
    return (failures == 0) ? 0 : 1;
}
