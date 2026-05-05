# Kernel Host Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Verify that jgbarah's C translation of 86DOS.asm correctly executes real file-system workloads (create / write / read / search / delete / rename) before any ESP32 work begins. Patch the translation as needed when tests reveal bugs.

**Architecture:** A single host-only test binary runs the vendored DOS kernel against an in-memory FAT12 image, exercising each kernel entry point through its public C API. Each test resets the image and re-`dos_init()`s for isolation. Test failures imply kernel-translation bugs; those get patched in `vendor/msdos-1.0/c/` and committed alongside the test.

**Tech Stack:** C99 (MinGW-w64 GCC), `mingw32-make`, the vendored kernel sources, no third-party libraries.

---

## File Structure

After this plan:

```
esp-dos/
├── Makefile                    # adds host_tests.exe target
├── host/
│   ├── main.c                  # smoke test (already exists; minor refactor)
│   ├── host_bios.c / .h        # NEW: shared BIOS vtable + RAM disk
│   ├── fat12_image.c / .h      # NEW: build a valid empty FAT12 320KB image
│   ├── tests.c / .h            # NEW: minimal test harness (TEST/ASSERT macros)
│   └── test_kernel.c           # NEW: integration tests; has main()
└── vendor/msdos-1.0/c/         # patched in-place when tests reveal bugs
```

Responsibilities:

- `host_bios.c/h` — single source of BIOS truth: stdio console, RAM disk, helpers to reset disk image
- `fat12_image.c/h` — pure function that fills a 320 KB byte buffer with a valid empty FAT12 layout (boot sector zero, FAT initialized with media descriptor + reserved entries, root directory zeroed)
- `tests.c/h` — `TEST(name)` registers a test fn; `ASSERT(cond, msg)` records failure; `int run_all_tests(void)` runs them with reset-between
- `test_kernel.c` — actual tests, one TEST() per kernel behavior

Geometry (320 KB DD, matches spec):
`SECSIZ=512, SPC=2, FIRFAT=1, FATCNT=2, MAXENT=112, DSKSIZ=640, MEDIA_DESC=0xFF`

---

## Task 1: Extract BIOS vtable + RAM disk into shared module

**Files:**
- Create: `host/host_bios.h`, `host/host_bios.c`
- Modify: `host/main.c` (consume the new header)
- Modify: `Makefile` (compile `host_bios.c`)

- [ ] **Step 1: Write header `host/host_bios.h`**

```c
#ifndef HOST_BIOS_H
#define HOST_BIOS_H

#include "bios.h"

/* Standard 320 KB DD floppy: 640 sectors * 512 bytes */
#define HOST_DISK_SECSIZ   512u
#define HOST_DISK_DSKSIZ   640u
#define HOST_DISK_BYTES    (HOST_DISK_SECSIZ * HOST_DISK_DSKSIZ)

/* The shared RAM-backed disk image, exported so tests can reset/inspect it. */
extern byte host_disk_image[HOST_DISK_BYTES];

/* Populated BIOS vtable: stdio console, RAM-backed disk, no printer/aux. */
extern bios_vtable_t host_bios;

/* Init table for one drive (drive 0 = 320 KB DD); points into a static
 * buffer owned by host_bios.c. Pass straight to dos_init(). */
byte *host_init_table(void);

#endif
```

- [ ] **Step 2: Write `host/host_bios.c`**

```c
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
```

- [ ] **Step 3: Replace `host/main.c` with a thin smoke driver**

```c
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
```

- [ ] **Step 4: Update `Makefile` to compile the new module**

```makefile
VENDOR := vendor/msdos-1.0/c
KERNEL_SRCS := \
    $(VENDOR)/src/fat.c \
    $(VENDOR)/src/disk.c \
    $(VENDOR)/src/directory.c \
    $(VENDOR)/src/file.c \
    $(VENDOR)/src/io.c \
    $(VENDOR)/src/console.c \
    $(VENDOR)/src/fcb_util.c \
    $(VENDOR)/src/syscall.c \
    $(VENDOR)/src/init.c

HOST_LIB_SRCS := host/host_bios.c
SMOKE_SRCS    := host/main.c $(HOST_LIB_SRCS)

CFLAGS := -std=c99 -Wall -I$(VENDOR)/include -Ihost -g

all: host_smoke.exe

host_smoke.exe: $(KERNEL_SRCS) $(SMOKE_SRCS)
	gcc $(CFLAGS) $^ -o $@

clean:
	rm -f host_smoke.exe host_tests.exe

.PHONY: all clean
```

- [ ] **Step 5: Build and re-run the smoke test to confirm refactor is non-breaking**

Run: `cd esp-dos && mingw32-make clean && mingw32-make && echo 1-1-81 | ./host_smoke.exe`

Expected output (final two lines):
```
[host] dos_init() returned cleanly
[host]   NUMDRV=1  MAXSEC=512  DATE=0x0221
```

- [ ] **Step 6: Commit**

```bash
git add host/host_bios.h host/host_bios.c host/main.c Makefile
git commit -m "host: extract BIOS vtable into reusable module"
```

---

## Task 2: FAT12 image initializer

**Files:**
- Create: `host/fat12_image.h`, `host/fat12_image.c`
- Modify: `Makefile` (compile `fat12_image.c` into `HOST_LIB_SRCS`)

- [ ] **Step 1: Write header `host/fat12_image.h`**

```c
#ifndef FAT12_IMAGE_H
#define FAT12_IMAGE_H

#include "dos_types.h"

/* Initialize the byte buffer as an empty, valid FAT12 320 KB DD volume:
 *   - boot sector (sector 0) zeroed
 *   - FAT 1 (sectors 1-2) starts with media descriptor 0xFF + 0xFF 0xFF
 *     (entries 0 and 1 are reserved); rest zero
 *   - FAT 2 (sectors 3-4) identical copy
 *   - root directory (sectors 5-11) zeroed (empty)
 *   - data area zeroed
 *
 * `image` must point to at least 320 KB (HOST_DISK_BYTES).
 */
void fat12_init_empty_320kb(byte *image);

#endif
```

- [ ] **Step 2: Write `host/fat12_image.c`**

```c
#include <string.h>

#include "fat12_image.h"
#include "host_bios.h"

void fat12_init_empty_320kb(byte *image) {
    memset(image, 0, HOST_DISK_BYTES);

    /* FAT 1 starts at sector 1, FAT 2 at sector 3; each is 1 sector long
     * for this geometry (320 KB / 1024 bytes per cluster ≈ 320 clusters →
     * ~480 bytes of FAT12, fits in one 512-byte sector).  The kernel's
     * FATSIZ converges to 1 for these parameters.
     */
    const byte media = 0xFF;          /* DOS 1.0 320 KB DD */
    byte fat_head[3] = { media, 0xFF, 0xFF };

    memcpy(image + 1 * HOST_DISK_SECSIZ, fat_head, 3);  /* FAT 1 */
    memcpy(image + 3 * HOST_DISK_SECSIZ, fat_head, 3);  /* FAT 2 */
}
```

- [ ] **Step 3: Update `Makefile` HOST_LIB_SRCS**

Change `HOST_LIB_SRCS` line to:

```makefile
HOST_LIB_SRCS := host/host_bios.c host/fat12_image.c
```

- [ ] **Step 4: Confirm smoke test still builds and runs**

Run: `cd esp-dos && mingw32-make clean && mingw32-make && echo 1-1-81 | ./host_smoke.exe | tail -2`

Expected last line:
```
[host]   NUMDRV=1  MAXSEC=512  DATE=0x0221
```

- [ ] **Step 5: Commit**

```bash
git add host/fat12_image.h host/fat12_image.c Makefile
git commit -m "host: add FAT12 320KB empty-image initializer"
```

---

## Task 3: Test harness

**Files:**
- Create: `host/tests.h`, `host/tests.c`
- Modify: `Makefile` (add `host_tests.exe` target)

- [ ] **Step 1: Write header `host/tests.h`**

```c
#ifndef TESTS_H
#define TESTS_H

/* Minimal test harness. Each test is a void(void) function registered with
 * REGISTER_TEST(); ASSERT(cond, msg) records failures without aborting so
 * a single test can report multiple problems. run_all_tests() runs every
 * registered test, calling reset_kernel_state() before each, and returns
 * the count of failed tests. */

typedef void (*test_fn_t)(void);

void register_test(const char *name, test_fn_t fn);
void test_assert(int cond, const char *expr, const char *file, int line);
int  run_all_tests(void);

/* Reset RAM disk + kernel global state to "freshly initialized 320 KB
 * empty volume". Called between tests for isolation. */
void reset_kernel_state(void);

#define REGISTER_TEST(fn) \
    static void __attribute__((constructor)) _register_##fn(void) { \
        register_test(#fn, fn); \
    }

#define ASSERT(cond) test_assert((cond), #cond, __FILE__, __LINE__)

#endif
```

- [ ] **Step 2: Write `host/tests.c`**

```c
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
```

- [ ] **Step 3: Create a placeholder `host/test_kernel.c` so the harness compiles before tests exist**

```c
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
```

- [ ] **Step 4: Update `Makefile` with the tests target**

Append after `host_smoke.exe` rule:

```makefile
TEST_SRCS := host/test_kernel.c host/tests.c $(HOST_LIB_SRCS)

host_tests.exe: $(KERNEL_SRCS) $(TEST_SRCS)
	gcc $(CFLAGS) $^ -o $@

test: host_tests.exe
	./host_tests.exe

.PHONY: all clean test
```

- [ ] **Step 5: Build and run with zero tests (must succeed and report 0/0)**

Run: `cd esp-dos && mingw32-make host_tests.exe && ./host_tests.exe`

Expected stderr final line:
```
0 / 0 tests passed
```

Exit code: `0`.

- [ ] **Step 6: Commit**

```bash
git add host/tests.h host/tests.c host/test_kernel.c Makefile
git commit -m "host: minimal test harness with kernel reset between tests"
```

---

## Task 4: Test — `srchfrst` on empty volume returns no match

**Files:**
- Modify: `host/test_kernel.c`

- [ ] **Step 1: Add the test**

Append to `host/test_kernel.c`, above `int main`:

```c
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
```

- [ ] **Step 2: Build**

Run: `cd esp-dos && mingw32-make host_tests.exe`

Expected: builds with warnings only, no errors.

- [ ] **Step 3: Run**

Run: `cd esp-dos && ./host_tests.exe 2>&1 | tail -5`

Expected (test should PASS — empty volume genuinely has no matches):
```
[test] test_srchfrst_empty_volume_no_match
[test] test_srchfrst_empty_volume_no_match: PASS

1 / 1 tests passed
```

If the test fails, the kernel is finding ghost directory entries in the all-zero root directory. Investigate `fn_srchfrst` in `vendor/msdos-1.0/c/src/fcb_util.c`; expected behavior is to skip entries whose first byte is 0 (terminator) or 0xE5 (deleted).

- [ ] **Step 4: Commit**

```bash
git add host/test_kernel.c
git commit -m "test: srchfrst on empty volume returns no match"
```

---

## Task 5: Test — create a file, then `srchfrst` finds it

**Files:**
- Modify: `host/test_kernel.c`

- [ ] **Step 1: Add the test**

Append to `host/test_kernel.c` before `int main`:

```c
/* Fill name/ext from a NUL-terminated 11-char "FOOBAR  TXT" style string.
 * Caller passes name (8 chars, space-padded) and ext (3 chars, space-padded). */
static void fcb_set_name(byte fcb[FCB_SIZE], const char *name8, const char *ext3) {
    memset(fcb, 0, FCB_SIZE);
    fcb[FCB_DRIVE] = 0;
    memcpy(fcb + FCB_NAME, name8, 8);
    memcpy(fcb + FCB_EXT,  ext3, 3);
}

static void test_create_then_srchfrst_finds_file(void) {
    byte fcb[FCB_SIZE];
    fcb_set_name(fcb, "TEST    ", "TXT");

    byte rc = fn_create(fcb);
    ASSERT(rc == 0);                /* 0 = success */

    rc = fn_close(fcb);
    ASSERT(rc == 0);

    byte search[FCB_SIZE];
    fcb_set_name(search, "TEST    ", "TXT");
    rc = fn_srchfrst(search);
    ASSERT(rc != 0xFF);             /* found */
}
REGISTER_TEST(test_create_then_srchfrst_finds_file);
```

- [ ] **Step 2: Build**

Run: `cd esp-dos && mingw32-make host_tests.exe`

Expected: builds.

- [ ] **Step 3: Run and observe**

Run: `cd esp-dos && ./host_tests.exe 2>&1 | tail -10`

Expected: PASS if kernel handles fn_create cleanly; FAIL otherwise.

If FAIL: examine the assertion that fired. Likely culprits:
- `fn_create` returning non-zero (open `vendor/msdos-1.0/c/src/file.c` and trace `fn_create` — search for paths that return error)
- `fn_srchfrst` not finding the freshly written entry (check whether `fn_close` flushes the directory; see `fat_write_all` in `vendor/msdos-1.0/c/src/fat.c`)

If a kernel patch is needed, fix it in place under `vendor/msdos-1.0/c/`, rebuild, re-run, then in step 4 commit BOTH the test and the kernel patch with a clear message.

- [ ] **Step 4: Commit**

```bash
git add host/test_kernel.c
# If a kernel patch was needed:
# git add vendor/msdos-1.0/c/src/<file>.c
git commit -m "test: create + srchfrst finds new file"
```

---

## Task 6: Test — write then read sequential round-trip

**Files:**
- Modify: `host/test_kernel.c`

This is the test most likely to expose the DMAADD truncation bug noted during planning. If it crashes or returns garbage, the fix is to widen the kernel's DMA addressing so a host pointer survives.

- [ ] **Step 1: Add the test**

Append:

```c
static void test_write_read_roundtrip_128_bytes(void) {
    /* Create file, write 128 bytes (one default RECSIZ record), close.
     * Reopen, read 128 bytes back, compare. */
    byte payload[128];
    for (int i = 0; i < 128; i++) payload[i] = (byte)(i * 7 + 3);

    byte fcb[FCB_SIZE];
    fcb_set_name(fcb, "RT      ", "BIN");

    byte rc = fn_create(fcb);
    ASSERT(rc == 0);

    /* fn_setdma(seg, off): seg is the user-segment base, off is the offset
     * within it. On host (flat memory) we pass seg=&payload[0], off=0. */
    fn_setdma(payload, 0);

    /* fn_seqwrt writes one record of size FCB.RECSIZ (default 128). */
    rc = fn_seqwrt(fcb);
    ASSERT(rc == 0);

    rc = fn_close(fcb);
    ASSERT(rc == 0);

    /* Read back. */
    byte fcb2[FCB_SIZE];
    fcb_set_name(fcb2, "RT      ", "BIN");
    rc = fn_open(fcb2);
    ASSERT(rc == 0);

    byte readbuf[128];
    memset(readbuf, 0xAA, sizeof(readbuf));
    fn_setdma(readbuf, 0);
    rc = fn_seqrd(fcb2);
    ASSERT(rc == 0);

    ASSERT(memcmp(readbuf, payload, 128) == 0);

    rc = fn_close(fcb2);
    ASSERT(rc == 0);
}
REGISTER_TEST(test_write_read_roundtrip_128_bytes);
```

- [ ] **Step 2: Build**

Run: `cd esp-dos && mingw32-make host_tests.exe`

Expected: builds with warnings only.

- [ ] **Step 3: Run; if it crashes, capture the crash address**

Run: `cd esp-dos && ./host_tests.exe 2>&1 | tail -20`

If it crashes (likely, due to DMAADD truncation): re-run under gdb to capture where:

```
gdb -batch -ex "run" -ex "bt 12" ./host_tests.exe
```

Expected stack frame: somewhere inside `io.c` doing `(byte *)(uintptr_t)dos->DMAADD + ...`.

- [ ] **Step 4: Patch the kernel — widen DMA addressing**

The kernel's `dos_state` declares `DMAADD` and `DMASEG` as `word` (16-bit). Several call sites cast `dos->DMAADD` directly to `byte *`, which only works when buffers live in low real-mode addresses. Fix:

In `vendor/msdos-1.0/c/include/dos.h`, locate:

```c
    word  DMAADD;       /* 3220: user's DMA (disk transfer) address         */
    word  DMASEG;       /* 3222: segment of DMA address                     */
```

Replace with:

```c
    word   DMAADD;       /* 3220: 16-bit offset within DMA buffer (kernel arithmetic) */
    word   DMASEG;       /* 3222: legacy; unused on host                     */
    byte  *DMABASE;      /* host pointer to user's DMA buffer (added for host) */
```

In `vendor/msdos-1.0/c/src/io.c`, locate `fn_setdma`:

```c
byte fn_setdma(byte *seg, word dx)
{
    dos->DMAADD  = dx;
    dos->DMASEG  = (word)(uintptr_t)seg;
    return 0;
}
```

Replace with:

```c
byte fn_setdma(byte *seg, word dx)
{
    dos->DMABASE = seg;
    dos->DMAADD  = dx;
    dos->DMASEG  = 0;   /* legacy field, unused */
    return 0;
}
```

In `vendor/msdos-1.0/c/src/io.c`, every place that casts `dos->DMAADD` to a pointer:

```c
(byte *)(uintptr_t)dos->DMAADD
```

becomes

```c
(dos->DMABASE + dos->DMAADD)
```

Specifically, edit these lines (use `grep -n '(byte *)(uintptr_t)dos->DMAADD' vendor/msdos-1.0/c/src/io.c` to find them all). Each one wants `DMABASE + DMAADD` as the buffer base; the trailing arithmetic with `NEXTADD - DMAADD` is unchanged because that's an offset difference and DMAADD's role as the original offset is preserved.

Also update `init.c` near the end of `dos_init()`:

```c
    dos->DMAADD   = 0x0080;
```

becomes

```c
    dos->DMAADD   = 0x0080;
    dos->DMABASE  = NULL;       /* caller must call fn_setdma before any I/O */
```

- [ ] **Step 5: Rebuild and rerun**

Run: `cd esp-dos && mingw32-make clean && mingw32-make host_tests.exe && ./host_tests.exe 2>&1 | tail -15`

Expected: all currently-registered tests PASS.

- [ ] **Step 6: Commit**

```bash
git add host/test_kernel.c \
        vendor/msdos-1.0/c/include/dos.h \
        vendor/msdos-1.0/c/src/io.c \
        vendor/msdos-1.0/c/src/init.c
git commit -m "kernel: widen DMA addressing for host pointers; test write/read roundtrip"
```

---

## Task 7: Test — delete removes a file

**Files:**
- Modify: `host/test_kernel.c`

- [ ] **Step 1: Add the test**

Append:

```c
static void test_delete_removes_file(void) {
    byte fcb[FCB_SIZE];
    fcb_set_name(fcb, "DELME   ", "TXT");

    byte rc = fn_create(fcb);
    ASSERT(rc == 0);
    rc = fn_close(fcb);
    ASSERT(rc == 0);

    /* Verify it's there first. */
    byte before[FCB_SIZE];
    fcb_set_name(before, "DELME   ", "TXT");
    ASSERT(fn_srchfrst(before) != 0xFF);

    /* Delete. fn_delete takes an FCB whose name fields are matched. */
    byte del[FCB_SIZE];
    fcb_set_name(del, "DELME   ", "TXT");
    rc = fn_delete(del);
    ASSERT(rc == 0);

    /* Should no longer be found. */
    byte after[FCB_SIZE];
    fcb_set_name(after, "DELME   ", "TXT");
    ASSERT(fn_srchfrst(after) == 0xFF);
}
REGISTER_TEST(test_delete_removes_file);
```

- [ ] **Step 2: Build**

Run: `cd esp-dos && mingw32-make host_tests.exe`

- [ ] **Step 3: Run**

Run: `cd esp-dos && ./host_tests.exe 2>&1 | tail -10`

Expected: PASS. If fail, investigate `fn_delete` in `vendor/msdos-1.0/c/src/file.c`; likely the directory entry isn't being marked deleted (first byte should be 0xE5).

- [ ] **Step 4: Commit**

```bash
git add host/test_kernel.c
# Plus any kernel patches if needed
git commit -m "test: delete removes file from directory"
```

---

## Task 8: Test — rename changes filename

**Files:**
- Modify: `host/test_kernel.c`

- [ ] **Step 1: Add the test**

`fn_rename` takes an FCB with the old name in the standard FCB_NAME/FCB_EXT slots and the new name at offset 16 (overlapping FILSIZ — this is per the original ASM convention, see `fn_rename` in file.c). The test uses a hand-laid byte buffer rather than the struct.

Append:

```c
static void test_rename_changes_filename(void) {
    byte fcb[FCB_SIZE];
    fcb_set_name(fcb, "OLD     ", "TXT");
    byte rc = fn_create(fcb);
    ASSERT(rc == 0);
    rc = fn_close(fcb);
    ASSERT(rc == 0);

    /* Build rename FCB: old name at offset 1, new name at offset 17.
     * fn_rename per ASM convention (see file.c::fn_rename). */
    byte ren[FCB_SIZE];
    memset(ren, 0, sizeof(ren));
    ren[FCB_DRIVE] = 0;
    memcpy(ren + 1,  "OLD     ", 8);
    memcpy(ren + 9,  "TXT",      3);
    memcpy(ren + 17, "NEW     ", 8);
    memcpy(ren + 25, "TXT",      3);

    rc = fn_rename(ren);
    ASSERT(rc == 0);

    /* Old name absent, new name present. */
    byte oldfcb[FCB_SIZE];
    fcb_set_name(oldfcb, "OLD     ", "TXT");
    ASSERT(fn_srchfrst(oldfcb) == 0xFF);

    byte newfcb[FCB_SIZE];
    fcb_set_name(newfcb, "NEW     ", "TXT");
    ASSERT(fn_srchfrst(newfcb) != 0xFF);
}
REGISTER_TEST(test_rename_changes_filename);
```

- [ ] **Step 2: Build**

Run: `cd esp-dos && mingw32-make host_tests.exe`

- [ ] **Step 3: Run**

Run: `cd esp-dos && ./host_tests.exe 2>&1 | tail -10`

Expected: PASS. Before declaring failure, double-check the rename FCB layout against `fn_rename` in `vendor/msdos-1.0/c/src/file.c` (lines around the `RENAME` ASM label — 86DOS.asm:2050+). The old name occupies the standard FCB name+ext slots; the new name follows at the FILSIZ field offset.

- [ ] **Step 4: Commit**

```bash
git add host/test_kernel.c
git commit -m "test: rename changes directory entry name"
```

---

## Task 9: Test — `fn_filesize` reports written byte count

**Files:**
- Modify: `host/test_kernel.c`

- [ ] **Step 1: Add the test**

Append:

```c
static void test_filesize_reports_written_size(void) {
    /* Create file, write exactly one 128-byte record (default RECSIZ),
     * close, then fn_filesize on a fresh FCB; FILSIZ field must read 128. */
    byte payload[128];
    for (int i = 0; i < 128; i++) payload[i] = (byte)i;

    byte fcb[FCB_SIZE];
    fcb_set_name(fcb, "SIZE    ", "BIN");
    ASSERT(fn_create(fcb) == 0);
    fn_setdma(payload, 0);
    ASSERT(fn_seqwrt(fcb) == 0);
    ASSERT(fn_close(fcb) == 0);

    byte query[FCB_SIZE];
    fcb_set_name(query, "SIZE    ", "BIN");
    fn_filesize(query);                /* fills FCB.FILSIZ */
    dword sz = FCB_GET_DWORD(query, FILSIZ);
    ASSERT(sz == 128);
}
REGISTER_TEST(test_filesize_reports_written_size);
```

- [ ] **Step 2: Build**

Run: `cd esp-dos && mingw32-make host_tests.exe`

- [ ] **Step 3: Run**

Run: `cd esp-dos && ./host_tests.exe 2>&1 | tail -10`

Expected: PASS. If fail, the file-size accounting in `fn_close` or `fn_seqwrt` may not be persisting properly — investigate `vendor/msdos-1.0/c/src/io.c` lines around `io_store` and `io_setup`.

- [ ] **Step 4: Commit**

```bash
git add host/test_kernel.c
git commit -m "test: filesize reports written byte count"
```

---

## Task 10: Final integration — run the whole suite green

**Files:**
- (No new files; verification only)

- [ ] **Step 1: Clean rebuild**

Run: `cd esp-dos && mingw32-make clean && mingw32-make host_tests.exe`

Expected: clean build, warnings only.

- [ ] **Step 2: Run all tests**

Run: `cd esp-dos && ./host_tests.exe 2>&1 | tail -20`

Expected: every registered test PASS, summary line `6 / 6 tests passed` (six because we registered: srchfrst_empty, create_then_srchfrst, write_read_roundtrip, delete, rename, filesize). Exit code `0`.

- [ ] **Step 3: Tag this milestone in git**

```bash
git tag -a kernel-host-verified -m "kernel translation verified against host file-op tests"
```

- [ ] **Step 4: Update the spec's risk section**

In `docs/superpowers/specs/2026-05-04-esp32-dos-port-design.md`, in the "Risks & open questions" section, edit risk #1 to read:

```
1. **jgbarah's translation may have latent bugs.** *Resolved (2026-05-04):*
   host verification suite covers create / write+read roundtrip / search /
   delete / rename / filesize and is green. Patches required: see
   `kernel-host-verified` tag.
```

Commit:

```bash
git add docs/superpowers/specs/2026-05-04-esp32-dos-port-design.md
git commit -m "docs: mark kernel-translation risk resolved by host verification"
```

---

## Notes for the implementing engineer

- **The tests are an audit, not a feature.** Failing tests mean the kernel translation has a bug. Don't change the test to make it pass — patch the kernel. If a test reveals a bug that's deeper than a one-line fix, surface it to the user before sinking >1 hour into a rabbit hole.
- **DOS 1.0 conventions:** AL=0 means success for most file functions; AL=0xFF (255) means "not found" or "error" depending on the function. Check `fn_*` definitions in the kernel headers when in doubt.
- **FCB names are 8.3, space-padded.** Always 8 chars in name slot, always 3 chars in ext slot. No NUL terminator.
- **Default RECSIZ is 128 bytes**, set automatically by `fn_open` and `fn_create`. Tests that write multiples of 128 don't need to touch RECSIZ.
- **`fn_setdma` host call:** pass `(byte *)&buffer, 0` — the kernel will use base+offset internally after the Task 6 patch.
- **The kernel prints to stdout during `dos_init()`** (banner + date prompt). The harness redirects stdin from `date_input.tmp` so it doesn't block. Banner output appearing in test logs is expected and not a failure.
