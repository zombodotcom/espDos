# ESP32-C5 dual-target with ST7735 status+log mirror — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the LilyGO T-Dongle-C5 (ESP32-C5) as a second supported target for espDos alongside the existing T-Display-S3, with a status-bar + scrolling-log mirror of BIOS console output on the C5's onboard 0.96" ST7735 LCD using subpixel rendering for full 80-column DOS width.

**Architecture:** Single source tree, target picked at configure time via `idf.py set-target esp32{s3,c5}`. New `firmware/components/display/` provides a no-op API on S3 and a real ST7735 driver on C5. Display is a subscriber to `bios_out`, not coupled — `bios.c` keeps writing every byte to USB-Serial-JTAG and additionally calls `display_putc(ch)` (compiles out on S3 via Kconfig). Subpixel rasterizer pre-computed offline by a Python script; on-chip path is a pure table lookup.

**Tech Stack:** ESP-IDF v6.0, esp_lcd, FreeRTOS xTimer, Spleen 6×8 bitmap font, Python 3 (offline only), existing `tests/emu/` host harness.

**Spec:** `docs/superpowers/specs/2026-05-07-c5-dual-target-design.md`

---

## File map

**Created:**
- `firmware/sdkconfig.defaults.esp32s3` — S3-specific overlay (octal PSRAM)
- `firmware/sdkconfig.defaults.esp32c5` — C5-specific overlay (quad PSRAM, display=y)
- `firmware/components/display/Kconfig`
- `firmware/components/display/CMakeLists.txt`
- `firmware/components/display/include/display.h` — public API + S3 no-op stubs
- `firmware/components/display/include/display_internal.h` — shared types
- `firmware/components/display/include/font_6x8.h` — Spleen 6×8 bitmap (committed)
- `firmware/components/display/include/font_6x8_subpixel.h` — generated table (committed)
- `firmware/components/display/display_log.c` — pure C ANSI strip + ring buffer (host-testable)
- `firmware/components/display/display.c` — ESP-IDF init, FreeRTOS timer, dispatch
- `firmware/components/display/render_sharp.c` — pure C, glyph → RGB565 (status row)
- `firmware/components/display/render_subpixel.c` — pure C, glyph → BGR-mapped RGB565 (log)
- `firmware/components/display/st7735_panel.c` — esp_lcd wrapper: SPI, MADCTL, offsets, DMA
- `tools/build_subpixel_font.py` — offline rasterizer
- `tests/emu/test_display_ansi_strip.c`
- `tests/emu/test_display_ring_buffer.c`
- `tests/emu/test_subpixel_glyph_table.c`

**Modified:**
- `firmware/sdkconfig.defaults` — strip S3-specific keys; share-only base
- `firmware/components/bios/bios.c` — call `display_putc(ch)` after JTAG write
- `firmware/components/bios/CMakeLists.txt` — add `display` to REQUIRES
- `firmware/main/espdos.c` — call `display_set_program/_beat` from app_main
- `firmware/main/CMakeLists.txt` — add `display` to REQUIRES
- `tests/emu/Makefile` — add three display test targets
- `README.md` — short C5 build subsection

---

## Task 1: Split sdkconfig.defaults into shared + S3 overlay

**Files:**
- Create: `firmware/sdkconfig.defaults.esp32s3`
- Modify: `firmware/sdkconfig.defaults`

Foundational, reversible. The current `sdkconfig.defaults` file mixes target-agnostic keys (flash size, partition table, optimization) with S3-specific keys (octal PSRAM). After this task, S3-specific keys live in the overlay and the base file is target-agnostic.

- [ ] **Step 1: Create `firmware/sdkconfig.defaults.esp32s3`**

```
# S3-specific overlay for sdkconfig.defaults.
# Loaded automatically by `idf.py set-target esp32s3`.

CONFIG_IDF_TARGET="esp32s3"

# Octal PSRAM: T-Display-S3 default. C5 cannot do octal — it uses quad
# (see sdkconfig.defaults.esp32c5).
CONFIG_SPIRAM_MODE_OCT=y

# Display is not wired up on the T-Display-S3 in espDos. Compiles out.
CONFIG_ESPDOS_HAS_DISPLAY=n
```

- [ ] **Step 2: Replace `firmware/sdkconfig.defaults` with the target-agnostic base**

```
# espdos shared sdkconfig defaults.
# Per-target overlays in sdkconfig.defaults.esp32{s3,c5} pick PSRAM
# mode and display enablement.

# Performance: -O2 + 240 MHz CPU. Both S3 and C5 support 240 MHz.
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240

# Enable PSRAM. Mode (octal vs quad) is set by the target overlay.
CONFIG_SPIRAM=y
CONFIG_SPIRAM_TYPE_AUTO=y
CONFIG_SPIRAM_SPEED_80M=y

# Allow heap_caps_malloc(MALLOC_CAP_SPIRAM, ...) at runtime.
CONFIG_SPIRAM_USE_MALLOC=y

# Allow EXT_RAM_BSS_ATTR-tagged statics (esp8086.c's mem[]) to land in
# PSRAM. Without this, EXT_RAM_BSS_ATTR silently does nothing and the
# 1 MB mem[] array tries to fit in internal DRAM, which fails.
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y

# Don't fail to boot if PSRAM init fails (QEMU may differ from real chip).
CONFIG_SPIRAM_IGNORE_NOTFOUND=y

# Larger main task stack — emulator-driven workloads may want it.
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192

# 16 MB flash on both LilyGO boards. Default sdkconfig says 2 MB,
# which can't hold the 3 MB factory app + dos_disk partition.
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"

# Custom partition table (adds dos_disk for the FAT12 floppy image).
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"
```

- [ ] **Step 3: Regenerate sdkconfig and build for S3**

`firmware/sdkconfig` was generated against the old combined defaults. `idf.py fullclean` does NOT delete `sdkconfig`. Delete it manually so the new defaults take effect, then build.

```powershell
C:\Users\zombo\esp\v6.0\esp-idf\export.ps1
cd C:\Users\zombo\Desktop\Programming\espDos\firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Expected: build succeeds. The `CONFIG_ESPDOS_HAS_DISPLAY=n` line will warn ("unknown symbol") because we haven't created the Kconfig that declares it yet — that's fine, the symbol will be a no-op until Task 3.

- [ ] **Step 4: Commit**

```bash
git add firmware/sdkconfig.defaults firmware/sdkconfig.defaults.esp32s3
git commit -m "firmware: split sdkconfig.defaults into shared + S3 overlay

Foundational change for adding ESP32-C5 as a second target. Shared
keys (flash size, partitions, performance) stay in defaults; S3
specifics (octal PSRAM) move to defaults.esp32s3, picked up
automatically by idf.py set-target esp32s3."
```

---

## Task 2: Add C5 sdkconfig overlay; verify C5 boots without display

**Files:**
- Create: `firmware/sdkconfig.defaults.esp32c5`

This task gets the existing emulator + kernel + transients running on C5 silicon **with display disabled**, before any LCD work begins. That isolates "did we break the port" from "did we break the display."

- [ ] **Step 1: Create `firmware/sdkconfig.defaults.esp32c5`**

```
# C5-specific overlay for sdkconfig.defaults.
# Loaded automatically by `idf.py set-target esp32c5`.

CONFIG_IDF_TARGET="esp32c5"

# Quad SPI PSRAM. ESP32-C5 silicon only supports quad (no octal mode
# like S3). Pin assignments are chip defaults; the LilyGO T-Dongle-C5
# uses the standard quad SPI psram pinout.
CONFIG_SPIRAM_MODE_QUAD=y

# Display is wired on T-Dongle-C5 (ST7735 over SPI2, GPIO 0/1/2/3/6/10).
# Subpixel rendering on by default; flip ESPDOS_DISPLAY_SUBPIXEL=n in
# menuconfig to fall back to sharp 26-col mode.
CONFIG_ESPDOS_HAS_DISPLAY=y
CONFIG_ESPDOS_DISPLAY_SUBPIXEL=y
```

- [ ] **Step 2: Switch target and build**

```powershell
cd C:\Users\zombo\Desktop\Programming\espDos\firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: build succeeds. The two `CONFIG_ESPDOS_*` lines warn ("unknown symbol") — fine, fixed in Task 3.

- [ ] **Step 3: Hardware Gate 2 — flash and verify minimal boot on C5**

Plug in the T-Dongle-C5 over USB. Identify its COM port (Device Manager → Ports → "USB JTAG/serial debug unit").

```powershell
idf.py -p COM<n> flash monitor
```

Expected log lines (substrings):
- `chip: ESP32-C5`
- `kernel_blob: 6341 bytes embedded`
- `running emulator: CS:IP=0040:0100`
- `86-DOS version 1.00`
- `Enter today's date (m-d-y): 1-1-80`
- MANDEL fractal renders (default loader is MANDEL)

If MANDEL renders, the emulator + flash partition + USB-Serial-JTAG console all work on C5 silicon. Exit monitor with `Ctrl-]`.

If boot fails: triage. Common issues — wrong PSRAM mode (re-check QUAD vs OCT), partition table flash address mismatch, USB-JTAG enumeration. Do not proceed until this gate passes.

- [ ] **Step 4: Commit**

```bash
git add firmware/sdkconfig.defaults.esp32c5
git commit -m "firmware: add ESP32-C5 sdkconfig overlay; enables MANDEL boot on T-Dongle-C5

Quad PSRAM (C5 silicon doesn't support octal). Display Kconfig keys
declared but symbols not wired yet (Task 3) — they warn as unknown
during build, harmless. Verified MANDEL renders end-to-end on
T-Dongle-C5 hardware over USB-Serial-JTAG."
```

- [ ] **Step 5: Switch back to S3 default**

We don't want subsequent tasks to default to C5 builds while developing the display component (S3 build is the regression check). Switch back:

```powershell
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Expected: S3 builds clean.

---

## Task 3: Display component skeleton with no-op API; bios + main hooks

**Files:**
- Create: `firmware/components/display/Kconfig`
- Create: `firmware/components/display/CMakeLists.txt`
- Create: `firmware/components/display/include/display.h`
- Create: `firmware/components/display/include/display_internal.h`
- Create: `firmware/components/display/display_log.c` (empty stub)
- Create: `firmware/components/display/display.c` (empty stub)
- Modify: `firmware/components/bios/bios.c`
- Modify: `firmware/components/bios/CMakeLists.txt`
- Modify: `firmware/main/espdos.c`
- Modify: `firmware/main/CMakeLists.txt`

After this task: both targets build, both targets behave identically to before. Display is wired in but no-op. Hardware Gate 1 (S3 regression) passes.

- [ ] **Step 1: Create `firmware/components/display/Kconfig`**

```
menu "espDos display"

    config ESPDOS_HAS_DISPLAY
        bool "Enable LCD display mirror"
        default n
        help
            Enable mirroring of BIOS console output to an onboard LCD.
            On targets with no LCD wired (e.g. T-Display-S3 in espDos
            today), leave this off — the display API compiles out to
            inline no-ops.

    config ESPDOS_DISPLAY_SUBPIXEL
        bool "Use subpixel-rendered 80-column log"
        depends on ESPDOS_HAS_DISPLAY
        default y
        help
            When on, log rows are rendered with Bowman/ClearType-style
            subpixel anti-aliasing, giving 80 columns on a 160-pixel-
            wide ST7735 (true DOS width). Status bar always uses sharp
            26-column rendering so the always-visible chrome has no
            color fringing. When off, both regions use sharp 26-col.

    config ESPDOS_DISPLAY_CALIBRATION
        bool "Render subpixel calibration pattern at boot"
        depends on ESPDOS_HAS_DISPLAY
        default n
        help
            On boot, render three horizontal bars colored exclusively
            in subpixel 0/1/2 of each pixel triplet, then sleep 5 s
            before normal display init. Use to verify panel stripe
            order (BGR vs RGB). Should normally be off.

endmenu
```

- [ ] **Step 2: Create `firmware/components/display/CMakeLists.txt`**

```cmake
# Component is gated on CONFIG_ESPDOS_HAS_DISPLAY. When disabled, no
# sources compile and the public API in display.h resolves to inline
# no-ops; bios.c and espdos.c need no #ifdef of their own.
if(CONFIG_ESPDOS_HAS_DISPLAY)
    set(srcs
        "display_log.c"
        "display.c"
        "render_sharp.c"
        "render_subpixel.c"
        "st7735_panel.c"
    )
    set(reqs esp_lcd driver log freertos)
else()
    set(srcs)
    set(reqs)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${reqs}
)
```

The `render_sharp.c`, `render_subpixel.c`, and `st7735_panel.c` source files are referenced here but won't exist until later tasks. CMake won't fail at configure time as long as they're added before the next `idf.py build` runs after enabling display. We'll create them empty in this task to keep the build clean from the start.

- [ ] **Step 3: Create `firmware/components/display/include/display.h`**

```c
/*
 * display.h — espDos display mirror, public API.
 *
 * On targets where ESPDOS_HAS_DISPLAY=n, every entry point resolves
 * to an inline no-op below, so call sites in bios.c and espdos.c
 * compile cleanly without their own #ifdefs.
 */
#pragma once

#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ESPDOS_HAS_DISPLAY

#include "esp_err.h"

/* Initialize SPI, ST7735 panel, ring buffer, and the 30 Hz flush
 * timer. Call once from app_main before any display_putc / set_*
 * calls. Safe to call multiple times (idempotent). */
esp_err_t display_init(void);

/* Append one byte from the BIOS console stream to the log. ANSI
 * escape sequences are stripped (ESC[...letter and ESC<letter>);
 * '\r' resets column, '\n' advances row (scrolls if full). Lock-
 * free single-producer; safe to call from app_main. */
void display_putc(uint8_t ch);

/* Set the program-name slot in the status bar (e.g., "MANDEL.COM").
 * Truncates if longer than the bar can render. */
void display_set_program(const char *name);

/* Set the beat-counter slot in the status bar. Updated whenever
 * espdos.c heartbeat fires. */
void display_set_beat(uint32_t beat);

#else /* !CONFIG_ESPDOS_HAS_DISPLAY */

/* No-op stubs. The compiler eliminates calls to these. */
static inline int   display_init(void)                     { return 0; }
static inline void  display_putc(uint8_t ch)               { (void)ch; }
static inline void  display_set_program(const char *name)  { (void)name; }
static inline void  display_set_beat(uint32_t beat)        { (void)beat; }

#endif /* CONFIG_ESPDOS_HAS_DISPLAY */

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Create `firmware/components/display/include/display_internal.h`**

```c
/*
 * display_internal.h — types and constants shared between
 * display_log.c, display.c, render_sharp.c, render_subpixel.c.
 * Not part of the public API.
 */
#pragma once

#include <stdint.h>

/* Panel geometry in landscape (rotation 3 per LilyGO driver). */
#define DISPLAY_W              160
#define DISPLAY_H              80

/* Cell metrics. Status bar uses sharp 6x8 (26 cols x 1 row).
 * Log uses subpixel 6sub x 8 (80 cols x 9 rows below the status bar). */
#define DISPLAY_STATUS_ROWS    1
#define DISPLAY_STATUS_COLS    26      /* 160px / 6px */
#define DISPLAY_LOG_ROWS       9       /* (80px - 8px status) / 8px */
#define DISPLAY_LOG_COLS       80      /* 480 subpixels / 6 subpixels */

/* Log ring buffer. One slot per visible row. Each row stores raw
 * printable bytes; renderers convert to glyphs at flush time. */
typedef struct {
    char     rows[DISPLAY_LOG_ROWS][DISPLAY_LOG_COLS + 1]; /* +1: NUL */
    uint8_t  lengths[DISPLAY_LOG_ROWS];
    uint8_t  oldest;       /* index of oldest visible row */
    uint8_t  cur_col;      /* write column in current (newest) row */
    uint8_t  dirty_mask;   /* bit i = row i needs flush */
} display_log_t;

/* ANSI strip state. */
typedef enum {
    DISPLAY_ANSI_GROUND  = 0,
    DISPLAY_ANSI_ESC     = 1,
    DISPLAY_ANSI_CSI     = 2,
} display_ansi_state_t;

/* Status bar contents. */
typedef struct {
    char    program[24];
    uint32_t beat;
    uint8_t  dirty;        /* nonzero when status row needs flush */
} display_status_t;

/* Internal API used by display.c, render_*.c, and host tests. */
void                  display_log_reset(display_log_t *log);
display_ansi_state_t  display_log_putc(display_log_t *log,
                                       display_ansi_state_t state,
                                       uint8_t ch);
```

- [ ] **Step 5: Create empty stub files**

`firmware/components/display/display_log.c`:

```c
#include "display_internal.h"

/* Implementations land in Tasks 6 and 7. */

void display_log_reset(display_log_t *log) {
    (void)log;
}

display_ansi_state_t display_log_putc(display_log_t *log,
                                       display_ansi_state_t state,
                                       uint8_t ch) {
    (void)log; (void)ch;
    return state;
}
```

`firmware/components/display/display.c`:

```c
#include "esp_err.h"
#include "display.h"

/* Real init, timer, and dispatch land in Task 11. */

esp_err_t display_init(void) {
    return 0;
}

void display_putc(uint8_t ch) {
    (void)ch;
}

void display_set_program(const char *name) {
    (void)name;
}

void display_set_beat(uint32_t beat) {
    (void)beat;
}
```

`firmware/components/display/render_sharp.c`, `render_subpixel.c`, and `st7735_panel.c` — empty files with one comment:

```c
/* Filled in by Tasks 10 (sharp), 12 (subpixel), 9 (panel). */
```

- [ ] **Step 6: Hook bios.c**

In `firmware/components/bios/bios.c`, find `bios_out` (around line 176) and add the `display_putc` call after the existing JTAG write:

```c
#include "display.h"  /* near the top, with the other includes */
```

```c
static void bios_out(uint8_t ch) {
    /* Write to JTAG non-blocking. (existing comment block kept) */
    usb_serial_jtag_write_bytes(&ch, 1, 0);

    /* Mirror to the LCD on targets that have one. The call is an
     * inline no-op when CONFIG_ESPDOS_HAS_DISPLAY=n. */
    display_putc(ch);

#ifdef ESPDOS_LOG_OUT
    /* (existing log-out block unchanged) */
    ...
#endif
}
```

In `firmware/components/bios/CMakeLists.txt`, add `display` to REQUIRES:

```cmake
idf_component_register(
    SRCS "bios.c"
    INCLUDE_DIRS "include"
    REQUIRES log driver esp_driver_usb_serial_jtag
             esp_partition spi_flash display
)
```

- [ ] **Step 7: Hook main/espdos.c**

In `firmware/main/espdos.c`, near the top:

```c
#include "display.h"
```

In `app_main`, after `bios_init()` and after picking the loader define block:

```c
display_init();
display_set_program(
#if defined(ESPDOS_LOADER_HELLO)
    "HELLO.COM"
#elif defined(ESPDOS_LOADER_COUNT)
    "COUNT.COM"
#elif defined(ESPDOS_LOADER_SHELL)
    "SHELL.COM"
#elif defined(ESPDOS_LOADER_JULIA)
    "JULIA.COM"
#elif defined(ESPDOS_LOADER_LIFE)
    "LIFE.COM"
#else
    "MANDEL.COM"
#endif
);
```

Inside the `for (;;)` heartbeat loop in `app_main`, add:

```c
    int beat = 0;
    for (;;) {
        int still_running = emu_run_n(5000);
        beat++;
        display_set_beat((uint32_t)beat);
#ifdef ESPDOS_HEARTBEAT
        ...
#endif
        if (!still_running) {
            ...
        }
        vTaskDelay(1);
    }
```

In `firmware/main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "espdos.c"
    INCLUDE_DIRS "."
    REQUIRES esp8086 kernel_blob bios display
)
```

- [ ] **Step 8: Build for both targets**

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Expected: S3 builds clean. The display component compiles to nothing; the inline no-ops in display.h are inlined at every call site.

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: C5 builds clean. The display component compiles its source files (which are stubs that do nothing).

- [ ] **Step 9: Hardware Gate 1 — S3 regression**

Switch back to S3, flash, verify SHELL still runs:

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py fullclean
idf.py build "-DESPDOS_LOADER_SHELL=1"
idf.py -p COM<n> flash monitor
```

Expected: 86-DOS banner, date prompt auto-feeds 1-1-80, SHELL menu shows up. Pick MANDEL — fractal renders. `Ctrl-]` exits. If anything regresses, do not proceed.

- [ ] **Step 10: Run host tests as a regression check**

```powershell
cd ..\tests\emu
mingw32-make run | Select-String "PASS|FAIL|exit="
```

Expected: 10 PASS lines, 0 FAIL lines, all `exit=0`. The bios.c change touches a header; this confirms it didn't break anything host-testable.

- [ ] **Step 11: Commit**

```bash
git add firmware/components/display/ firmware/components/bios/ firmware/main/
git commit -m "display: scaffold component with no-op API; hook bios + main

Adds the firmware/components/display/ skeleton: Kconfig with
ESPDOS_HAS_DISPLAY (default n) and ESPDOS_DISPLAY_SUBPIXEL,
display.h public API that resolves to inline no-ops on disabled
targets, and stub .c files for the display_log / render / panel
layers that subsequent tasks fill in.

bios.c's bios_out and main's app_main now call display_putc /
display_set_program / display_set_beat. On S3 (HAS_DISPLAY=n) the
calls compile out to nothing; on C5 (HAS_DISPLAY=y) they hit the
stubs that don't do anything yet.

Verified: S3 builds, S3 SHELL+MANDEL boot unchanged on hardware,
all 10 host tests still PASS."
```

---

## Task 4: Add Spleen 6×8 font as font_6x8.h

**Files:**
- Create: `firmware/components/display/include/font_6x8.h`

Spleen is a public-domain monospace bitmap font (BSD-2-Clause). Its 6×12 variant is the smallest "official" size; we want 6×8. The simplest path: take the upstream 6×12 BDF, drop the top and bottom 2 rows of each glyph (Spleen's 6×12 has padding rows top and bottom that the 6×8 form doesn't need), commit the resulting bitmap as a `.h`.

Alternatively, use `tom-thumb` (4×6 monospace, public domain) zero-padded to 6×8 — also acceptable. The choice doesn't affect any other task; pick one and stick with it.

- [ ] **Step 1: Generate or obtain the font data**

Run this Python snippet to extract Spleen 6×12 to a 6×8 bitmap. Save as `tools/extract_spleen_6x8.py` (one-shot helper, doesn't need to be committed):

```python
#!/usr/bin/env python3
"""Convert Spleen 6x12 BDF to a 6x8 packed-byte C header.

Drop rows 2 (above ascender) and 11 (descender padding) from each
glyph, leaving a clean 6x8. Pack each row as one byte where the high
6 bits are pixels left-to-right; bits 1..0 are zero.
"""
import sys, urllib.request, re

URL = "https://raw.githubusercontent.com/fcambus/spleen/master/spleen-6x12.bdf"
TARGET_ROWS_TO_DROP = (0, 1, 10, 11)   # crop top 2 + bottom 2

bdf = urllib.request.urlopen(URL).read().decode("ascii", "replace")

# Each glyph: ENCODING <n> ... BITMAP\n<12 rows of hex>\nENDCHAR
glyph_re = re.compile(
    r"ENCODING\s+(\d+).*?BITMAP\s*\n((?:[0-9A-Fa-f]+\n){12})ENDCHAR",
    re.DOTALL,
)

glyphs = {n: [int(b, 16) for b in bm.strip().split()]
          for n, bm in ((int(n), bm) for n, bm in glyph_re.findall(bdf))}

print("/* Auto-generated from Spleen 6x12 BDF; do not edit by hand. */")
print("#pragma once")
print("#include <stdint.h>")
print("static const uint8_t font_6x8[256][8] = {")
for code in range(256):
    g = glyphs.get(code, [0] * 12)
    rows = [r for i, r in enumerate(g) if i not in TARGET_ROWS_TO_DROP]
    rows = rows[:8] + [0] * (8 - len(rows))
    cells = ", ".join(f"0x{r:02x}" for r in rows)
    print(f"    [0x{code:02x}] = {{ {cells} }},")
print("};")
```

Run it:

```powershell
py tools\extract_spleen_6x8.py > firmware\components\display\include\font_6x8.h
```

The generated file should be ~260 lines and start:

```c
/* Auto-generated from Spleen 6x12 BDF; do not edit by hand. */
#pragma once
#include <stdint.h>
static const uint8_t font_6x8[256][8] = {
    [0x00] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ...
    [0x41] = { 0x00, 0x70, 0x88, 0x88, 0xf8, 0x88, 0x88, 0x00 },  /* 'A' */
    ...
};
```

- [ ] **Step 2: Sanity-check 'A' visually**

```powershell
py -c "import importlib.util, sys; spec = importlib.util.spec_from_file_location('f', 'firmware/components/display/include/font_6x8.h'); print('header looks like:', open('firmware/components/display/include/font_6x8.h').read()[:300])"
```

Or eyeball the file: `'A'` (0x41) row 1 should be `0x70` (binary `01110000` — three pixels in the middle of the top of the A bowl). Row 4 should be `0xf8` (binary `11111000` — the cross-bar).

- [ ] **Step 3: Verify it compiles**

`firmware/components/display/include/font_6x8.h` is included by render_sharp.c (currently empty) and render_subpixel.c (currently empty). Add a quick include from `display_log.c` so the build exercises it:

In `firmware/components/display/display_log.c`, add at the top:

```c
#include "font_6x8.h"  /* used by render_*.c, included here as build check */
#include "display_internal.h"
```

Then build for C5:

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: builds clean. The font occupies about 2 KB of flash.

- [ ] **Step 4: Commit**

```bash
git add firmware/components/display/include/font_6x8.h firmware/components/display/display_log.c
git commit -m "display: add Spleen 6x8 bitmap font

Public-domain Spleen 6x12 BDF cropped to 6x8 (drop top 2 + bottom 2
rows per glyph). 256 glyphs x 8 bytes = 2 KB. Used as the source
bitmap for both sharp 26-col rendering (Task 10) and as input to
the offline subpixel rasterizer (Task 5)."
```

---

## Task 5: Subpixel font generator + commit `font_6x8_subpixel.h`

**Files:**
- Create: `tools/build_subpixel_font.py`
- Create: `firmware/components/display/include/font_6x8_subpixel.h`

The script reads the committed `font_6x8.h`, applies the BGR-aware [1, 2, 1] subpixel filter, and writes `font_6x8_subpixel.h`. It also has a `--check` mode used by Task 8's host test to verify the committed table matches what the script would generate.

- [ ] **Step 1: Create `tools/build_subpixel_font.py`**

```python
#!/usr/bin/env python3
"""Generate font_6x8_subpixel.h from font_6x8.h.

For each of 256 glyphs, each of 8 rows:
1. Upsample horizontally by 3 (each source pixel becomes 3 subpixel
   samples).
2. Apply a 3-tap [1, 2, 1] filter centered on each subpixel slot.
3. Group into 6 BGR triplets per row (panel is BGR, MADCTL bit 0x08
   set always per the LilyGO ST7735 driver).
4. Pack each triplet as RGB565 (5 bits R, 6 G, 5 B). Note: even
   though the panel is BGR, esp_lcd treats the framebuffer as RGB565
   and applies the BGR ordering at scan time — so we still pack
   high bits = R, but assign the SOURCE channel mapping such that
   the panel renders subpixel 0 -> blue, 1 -> green, 2 -> red.

Filter:
   row[i] = (3 * source bit i) packed left-to-right -> 18 samples.
   subpixel_intensity[k] = (row[3k-1] + 2*row[3k] + row[3k+1]) / 4
                           with edge-clamp at k=0 and k=17.

Output: static const uint16_t font_subpixel[256][8][6].
"""

import argparse, hashlib, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC  = ROOT / "firmware/components/display/include/font_6x8.h"
DST  = ROOT / "firmware/components/display/include/font_6x8_subpixel.h"

def parse_font_6x8(path):
    """Return list of 256 lists of 8 ints from font_6x8.h."""
    text = path.read_text()
    rx = re.compile(r"\[0x([0-9a-fA-F]{2})\]\s*=\s*\{([^}]*)\}")
    out = [[0]*8 for _ in range(256)]
    for m in rx.finditer(text):
        idx = int(m.group(1), 16)
        bytes_ = [int(b.strip(), 0) for b in m.group(2).split(",") if b.strip()]
        out[idx] = bytes_[:8] + [0]*(8 - len(bytes_))
    return out

def render_glyph(rows):
    """Return [8][6] uint16 RGB565 with BGR-mapped subpixel intensities."""
    out = [[0]*6 for _ in range(8)]
    for y, row_byte in enumerate(rows):
        # 6 source pixels (high 6 bits of byte), each contributes 3 subpixel samples.
        s = []
        for x in range(6):
            bit = (row_byte >> (7 - x)) & 1
            s.extend([bit, bit, bit])
        # s is now length 18.
        # 3-tap [1,2,1]/4 filter, edge-clamp.
        f = [0]*18
        for k in range(18):
            left  = s[k-1] if k > 0    else s[0]
            ctr   = s[k]
            right = s[k+1] if k < 17   else s[17]
            f[k]  = (left + 2*ctr + right) / 4.0
        # Pack: 6 cells per row. Each cell occupies 3 subpixels.
        # Panel is BGR -> subpixel 0 of each cell drives BLUE, 1 -> GREEN, 2 -> RED.
        for cell in range(6):
            sub_b = f[cell*3 + 0]
            sub_g = f[cell*3 + 1]
            sub_r = f[cell*3 + 2]
            r5 = int(round(sub_r * 31)) & 0x1F
            g6 = int(round(sub_g * 63)) & 0x3F
            b5 = int(round(sub_b * 31)) & 0x1F
            out[y][cell] = (r5 << 11) | (g6 << 5) | b5
    return out

def emit_header(font, out_path):
    lines = [
        "/* Auto-generated by tools/build_subpixel_font.py.",
        " * Do not edit by hand. Regenerate after editing font_6x8.h.",
        " */",
        "#pragma once",
        "#include <stdint.h>",
        "static const uint16_t font_subpixel[256][8][6] = {",
    ]
    for code in range(256):
        glyph = render_glyph(font[code])
        lines.append(f"    [0x{code:02x}] = {{")
        for row in glyph:
            cells = ", ".join(f"0x{c:04x}" for c in row)
            lines.append(f"        {{ {cells} }},")
        lines.append("    },")
    lines.append("};\n")
    out_path.write_text("\n".join(lines))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="Don't write; exit 1 if committed file differs.")
    args = ap.parse_args()

    font = parse_font_6x8(SRC)
    expected = []
    for code in range(256):
        glyph = render_glyph(font[code])
        expected.append(glyph)

    if args.check:
        # Hash the in-memory table, compare to a hash of the committed file
        # parsed back. Simplest: re-emit to a string buffer and string-compare.
        from io import StringIO
        buf = StringIO()
        # Re-implement emit to a string
        buf.write("/* Auto-generated by tools/build_subpixel_font.py.\n")
        buf.write(" * Do not edit by hand. Regenerate after editing font_6x8.h.\n")
        buf.write(" */\n#pragma once\n#include <stdint.h>\n")
        buf.write("static const uint16_t font_subpixel[256][8][6] = {\n")
        for code in range(256):
            buf.write(f"    [0x{code:02x}] = {{\n")
            for row in expected[code]:
                cells = ", ".join(f"0x{c:04x}" for c in row)
                buf.write(f"        {{ {cells} }},\n")
            buf.write("    },\n")
        buf.write("};\n")
        committed = DST.read_text() if DST.exists() else ""
        if buf.getvalue() != committed:
            sys.stderr.write("font_6x8_subpixel.h is out of date — "
                             "re-run tools/build_subpixel_font.py without --check\n")
            return 1
        print("font_6x8_subpixel.h matches generator output")
        return 0

    emit_header(font, DST)
    print(f"wrote {DST} ({len(font)} glyphs)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run the generator**

```powershell
py tools\build_subpixel_font.py
```

Expected output: `wrote .../font_6x8_subpixel.h (256 glyphs)`. The header file is ~14 KB.

- [ ] **Step 3: Verify --check round-trip**

```powershell
py tools\build_subpixel_font.py --check
```

Expected: `font_6x8_subpixel.h matches generator output` and exit 0. If the check fails immediately after a write, the script has a bug.

- [ ] **Step 4: Build C5 firmware to confirm header compiles**

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: builds clean. Adds ~14 KB to the binary.

- [ ] **Step 5: Commit**

```bash
git add tools/build_subpixel_font.py firmware/components/display/include/font_6x8_subpixel.h
git commit -m "display: subpixel font generator + committed font_6x8_subpixel.h

Offline Python script applies a [1,2,1]/4 filter to the 6x8 Spleen
glyphs, BGR-mapped per the LilyGO ST7735 panel (MADCTL bit 0x08 set
always). Output is uint16_t[256][8][6] of RGB565 words — pure lookup
on the chip, no runtime filtering.

--check mode (used by Task 8 host test) re-runs the generator and
diffs against the committed .h, catching the case where someone
hand-edits the table without re-running the script."
```

---

## Task 6: ANSI strip state machine + host test

**Files:**
- Create: `tests/emu/test_display_ansi_strip.c`
- Modify: `firmware/components/display/display_log.c`
- Modify: `tests/emu/Makefile`

Implements the per-byte state machine documented in the spec. Pure C, host-testable.

- [ ] **Step 1: Write the failing test**

`tests/emu/test_display_ansi_strip.c`:

```c
/*
 * test_display_ansi_strip — drive bytes through the ANSI strip state
 * machine and assert the log buffer contains exactly the printable
 * text.
 *
 * Covers:
 *   - plain ASCII passes through
 *   - CSI sequences (ESC [ ... letter) get stripped, including the
 *     final letter
 *   - simple ESC <letter> sequences (e.g. ESC c) get stripped
 *   - the regression case: '[' inside a CSI parameter list is NOT
 *     treated as a CSI terminator
 */

#include <stdio.h>
#include <string.h>

#include "../../firmware/components/display/include/display_internal.h"

static int fails = 0;

static void T_EXPECT_STREQ(const char *expr_a, const char *a,
                           const char *expr_b, const char *b,
                           const char *file, int line) {
    if (strcmp(a, b) == 0) return;
    fprintf(stderr, "%s:%d: %s != %s\n  got:      \"%s\"\n  expected: \"%s\"\n",
            file, line, expr_a, expr_b, a, b);
    fails++;
}
#define EXPECT_STREQ(a, b) T_EXPECT_STREQ(#a, (a), #b, (b), __FILE__, __LINE__)

static void run(const char *input, const char *expected_row0) {
    display_log_t log;
    display_log_reset(&log);
    display_ansi_state_t st = DISPLAY_ANSI_GROUND;
    for (const char *p = input; *p; p++) {
        st = display_log_putc(&log, st, (uint8_t)*p);
    }
    EXPECT_STREQ(log.rows[0], expected_row0);
}

int main(void) {
    /* Plain ASCII passes through. */
    run("hello", "hello");

    /* Bell, tab, backspace -> dropped (control chars below ESC). */
    run("a\bb", "ab");

    /* CSI color sequence stripped. */
    run("\x1b[31mred\x1b[0m", "red");

    /* CSI cursor home stripped. */
    run("\x1b[Hhi", "hi");

    /* The regression case: '[' inside a CSI parameter list is NOT
     * the terminator. CSI ends at the letter 'm'. */
    run("\x1b[1;31mok", "ok");

    /* Bare ESC <letter> stripped. */
    run("\x1b" "ckeep", "keep");

    /* Multiple CSIs, plain text in between. */
    run("\x1b[2Jclear\x1b[5;5Hhi", "clearhi");

    if (fails == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL (%d)\n", fails);
    return 1;
}
```

- [ ] **Step 2: Add Makefile rule**

In `tests/emu/Makefile`, change the `TESTS` list to include the three new display tests, and add their build rules. Display tests do **not** link `EMU_SRC` — they only need `display_log.c`:

```make
DISPLAY_LOG := ../../firmware/components/display/display_log.c
DISPLAY_INC := -I../../firmware/components/display/include

TESTS := test_jmp_near.exe test_kernel_first_step.exe test_bootstub.exe \
        test_hello.exe test_kernel_banner.exe test_memory_bounds.exe \
        test_fininit_stack.exe test_loader.exe test_mandel.exe \
        test_julia.exe \
        test_display_ansi_strip.exe \
        test_display_ring_buffer.exe \
        test_subpixel_glyph_table.exe

# (existing rules unchanged)

test_display_ansi_strip.exe: test_display_ansi_strip.c $(DISPLAY_LOG)
	gcc $(CFLAGS) $(DISPLAY_INC) $^ -o $@

test_display_ring_buffer.exe: test_display_ring_buffer.c $(DISPLAY_LOG)
	gcc $(CFLAGS) $(DISPLAY_INC) $^ -o $@

test_subpixel_glyph_table.exe: test_subpixel_glyph_table.c
	gcc $(CFLAGS) $(DISPLAY_INC) $^ -o $@
```

In the `run` target, add three lines:

```make
	@echo === test_display_ansi_strip ===
	@./test_display_ansi_strip.exe ; echo "exit=$$?"
	@echo === test_display_ring_buffer ===
	@./test_display_ring_buffer.exe ; echo "exit=$$?"
	@echo === test_subpixel_glyph_table ===
	@./test_subpixel_glyph_table.exe ; echo "exit=$$?"
```

- [ ] **Step 3: Run the test — expect FAIL**

```powershell
cd tests\emu
mingw32-make test_display_ansi_strip.exe
.\test_display_ansi_strip.exe
```

Expected: FAIL with multiple `rows[0] != expected` mismatches because `display_log_putc` is the empty stub.

- [ ] **Step 4: Implement the state machine**

Replace `firmware/components/display/display_log.c` with:

```c
#include <string.h>
#include "display_internal.h"
#include "font_6x8.h"  /* used by render_*.c, included here as build check */

void display_log_reset(display_log_t *log) {
    memset(log, 0, sizeof(*log));
}

display_ansi_state_t display_log_putc(display_log_t *log,
                                       display_ansi_state_t state,
                                       uint8_t ch) {
    /* Escape state machine. */
    if (state == DISPLAY_ANSI_ESC) {
        if (ch == '[') return DISPLAY_ANSI_CSI;
        /* Bare ESC <letter> — that one byte ends the sequence. */
        return DISPLAY_ANSI_GROUND;
    }
    if (state == DISPLAY_ANSI_CSI) {
        /* Final byte of a CSI sequence is in 0x40..0x7E. Anything
         * else (digits, ';', '?', etc.) is parameter data — keep
         * swallowing. */
        if (ch >= 0x40 && ch <= 0x7E) return DISPLAY_ANSI_GROUND;
        return DISPLAY_ANSI_CSI;
    }

    /* DISPLAY_ANSI_GROUND. */
    if (ch == 0x1B) return DISPLAY_ANSI_ESC;

    /* Special handling. */
    if (ch == '\n') {
        /* Advance to next row. Wrap by rotating oldest forward. */
        log->oldest = (log->oldest + 1) % DISPLAY_LOG_ROWS;
        uint8_t newest = (log->oldest + DISPLAY_LOG_ROWS - 1) % DISPLAY_LOG_ROWS;
        log->rows[newest][0] = '\0';
        log->lengths[newest] = 0;
        log->cur_col = 0;
        log->dirty_mask = 0xFF;  /* whole log scrolled -> all dirty */
        return DISPLAY_ANSI_GROUND;
    }
    if (ch == '\r') {
        log->cur_col = 0;
        return DISPLAY_ANSI_GROUND;
    }
    /* Drop other control chars below 0x20 — bell, tab, backspace,
     * etc. — to keep the log readable. */
    if (ch < 0x20 || ch == 0x7F) return DISPLAY_ANSI_GROUND;

    /* Append printable byte to the active (newest) row. */
    uint8_t newest = (log->oldest + DISPLAY_LOG_ROWS - 1) % DISPLAY_LOG_ROWS;
    if (log->cur_col < DISPLAY_LOG_COLS) {
        log->rows[newest][log->cur_col] = (char)ch;
        log->cur_col++;
        log->rows[newest][log->cur_col] = '\0';
        if (log->cur_col > log->lengths[newest])
            log->lengths[newest] = log->cur_col;
        log->dirty_mask |= (1u << newest);
    }
    return DISPLAY_ANSI_GROUND;
}
```

Note: this implementation reuses the same row indexing convention the ring-buffer task expects. `oldest` is the start of the visible window; the active write row is `(oldest + ROWS - 1) % ROWS` (the last visible row, where `\n` will scroll to next).

But wait — for *this* test, all 7 inputs fit in one row before any `\n`. So row 0 sees the writes regardless of which slot is "active." The `display_log_reset` zeroes `oldest`, so newest = `(0 + 9 - 1) % 9 = 8`. That writes to `log->rows[8]`, not `log->rows[0]`. The test asserts `log->rows[0]`. **The test will fail with a wrong row.**

Fix the test convention: `display_log_reset` should set up so the active write row is row 0. Adjust the implementation:

```c
void display_log_reset(display_log_t *log) {
    memset(log, 0, sizeof(*log));
    /* Active write row is rows[oldest]; newest = (oldest-1) mod ROWS.
     * Initialize so writes land in rows[0] until the first '\n'. */
    log->oldest = 1;  /* makes newest = 0 */
}
```

Update the `\n` advance: scroll by moving `oldest` forward, which makes a new `newest`:

```c
    if (ch == '\n') {
        /* Newest row was rows[(oldest-1) mod ROWS] = the line just
         * finished. Advance oldest -> the *previously oldest* row
         * is the new active row. */
        log->oldest = (log->oldest + 1) % DISPLAY_LOG_ROWS;
        /* The new active row is now rows[(oldest-1) mod ROWS] —
         * which is the row that was previously oldest. Clear it. */
        uint8_t new_newest = (log->oldest + DISPLAY_LOG_ROWS - 1) % DISPLAY_LOG_ROWS;
        memset(log->rows[new_newest], 0, sizeof(log->rows[new_newest]));
        log->lengths[new_newest] = 0;
        log->cur_col = 0;
        log->dirty_mask = 0xFF;
        return DISPLAY_ANSI_GROUND;
    }
```

Apply both fixes; in this test, `oldest = 1` after reset, `newest = 0`, all writes go to `log->rows[0]`. The assertion holds.

- [ ] **Step 5: Run the test — expect PASS**

```powershell
mingw32-make test_display_ansi_strip.exe
.\test_display_ansi_strip.exe
```

Expected: `PASS`, exit 0.

- [ ] **Step 6: Run the full test suite to confirm no regression**

```powershell
mingw32-make run | Select-String "PASS|FAIL|exit="
```

Expected: 11 PASS lines (10 emu + 1 display), 0 FAIL, all `exit=0`. The other two display tests don't exist yet so the Makefile rule for them won't run; that's fine. (Actually they will — make `test_display_ring_buffer.exe` won't exist since the .c isn't there yet. We'll get a build error.) — Mitigation: comment out the two unbuilt entries in TESTS and `run` for now; uncomment them in Tasks 7 and 8.

Practical fix: in the Makefile, list `test_display_ring_buffer.exe` and `test_subpixel_glyph_table.exe` in TESTS but defer running them in the `run` target until they exist. Or simpler — in this task, only add the `test_display_ansi_strip.exe` target to TESTS and `run`. Tasks 7 and 8 each add their own line.

- [ ] **Step 7: Commit**

```bash
git add firmware/components/display/display_log.c \
        firmware/components/display/include/display_internal.h \
        tests/emu/test_display_ansi_strip.c tests/emu/Makefile
git commit -m "display: ANSI strip state machine + host test

Pure C state machine swallows ESC <letter> and ESC [ ... letter
sequences, drops control chars, lands printable bytes in a per-row
ring buffer. The regression case from spec self-review (treating
'[' as a CSI terminator) gets an explicit test.

11/11 host tests PASS."
```

---

## Task 7: Ring-buffer rotation + host test

**Files:**
- Create: `tests/emu/test_display_ring_buffer.c`
- Modify: `firmware/components/display/display_log.c` (already has the buffer; refine if needed)
- Modify: `tests/emu/Makefile` (add `test_display_ring_buffer.exe` to `run`)

Task 6 already implements the buffer mechanics. This task adds the test that pins the rotation behavior.

- [ ] **Step 1: Write the failing test**

`tests/emu/test_display_ring_buffer.c`:

```c
/*
 * test_display_ring_buffer — append 30 lines to a 9-row ring; assert
 * the oldest 21 are gone, newest 9 are in order, dirty_mask covers
 * the wrapped slot.
 */

#include <stdio.h>
#include <string.h>

#include "../../firmware/components/display/include/display_internal.h"

static int fails = 0;

static void T_EXPECT(const char *expr, int cond,
                     const char *file, int line) {
    if (cond) return;
    fprintf(stderr, "%s:%d: %s\n", file, line, expr);
    fails++;
}
#define EXPECT(c) T_EXPECT(#c, (c), __FILE__, __LINE__)

int main(void) {
    display_log_t log;
    display_log_reset(&log);

    display_ansi_state_t st = DISPLAY_ANSI_GROUND;
    for (int i = 0; i < 30; i++) {
        char line[16];
        snprintf(line, sizeof line, "line%02d", i);
        for (char *p = line; *p; p++)
            st = display_log_putc(&log, st, (uint8_t)*p);
        st = display_log_putc(&log, st, (uint8_t)'\n');
    }

    /* After 30 '\n', the active write row is empty (we just scrolled).
     * The 9 visible "completed" rows are line21..line29 in order from
     * oldest -> newest. */
    static const char *expected[] = {
        "line21", "line22", "line23", "line24", "line25",
        "line26", "line27", "line28", "line29"
    };

    for (int i = 0; i < DISPLAY_LOG_ROWS; i++) {
        uint8_t slot = (log.oldest + i) % DISPLAY_LOG_ROWS;
        if (i < 9 - 1) {
            EXPECT(strcmp(log.rows[slot], expected[i]) == 0);
        } else {
            /* The very last visible row is currently the empty
             * "active write" row after the 30th '\n'. */
            EXPECT(log.rows[slot][0] == '\0');
        }
    }

    /* dirty_mask should be all-ones — we scrolled. */
    EXPECT(log.dirty_mask == 0xFF);

    if (fails == 0) { printf("PASS\n"); return 0; }
    printf("FAIL (%d)\n", fails);
    return 1;
}
```

- [ ] **Step 2: Run the test**

```powershell
cd tests\emu
mingw32-make test_display_ring_buffer.exe
.\test_display_ring_buffer.exe
```

Expected: PASS (the implementation in display_log.c was completed in Task 6 and already supports rotation). If it fails, debug the indexing in the `\n` branch.

- [ ] **Step 3: Add to `run` target**

Uncomment / add these two lines in `tests/emu/Makefile`'s `run` target:

```make
	@echo === test_display_ring_buffer ===
	@./test_display_ring_buffer.exe ; echo "exit=$$?"
```

- [ ] **Step 4: Run all tests**

```powershell
mingw32-make run | Select-String "PASS|FAIL|exit="
```

Expected: 12 PASS lines (10 emu + 2 display), 0 FAIL, all `exit=0`.

- [ ] **Step 5: Commit**

```bash
git add tests/emu/test_display_ring_buffer.c tests/emu/Makefile
git commit -m "display: ring-buffer rotation host test

12/12 host tests PASS."
```

---

## Task 8: Subpixel glyph-table cross-check + host test

**Files:**
- Create: `tests/emu/test_subpixel_glyph_table.c`
- Modify: `tests/emu/Makefile` (add to `run`)

Reimplements the same `[1, 2, 1]/4` filter in C and asserts the committed `font_6x8_subpixel.h` matches what the filter produces from `font_6x8.h`. Catches both "someone hand-edited the .h" and "someone changed the filter in the Python script without re-running it."

- [ ] **Step 1: Write the test**

`tests/emu/test_subpixel_glyph_table.c`:

```c
/*
 * test_subpixel_glyph_table — re-run the [1,2,1]/4 BGR-mapped filter
 * in C against font_6x8.h, compare against font_6x8_subpixel.h.
 *
 * Spot-checks 5 representative glyphs: space (all zero), 'A' (mid-
 * weight letter), '8' (curves), 0x7F (high-bit), 0xDB (full-block-
 * like, mostly-on row pattern).
 *
 * If this fails, run:  py tools/build_subpixel_font.py
 * to regenerate the committed header.
 */

#include <stdio.h>
#include <stdint.h>

#include "font_6x8.h"
#include "font_6x8_subpixel.h"

static int fails = 0;

/* Reimplement the filter in C. Must match build_subpixel_font.py
 * line-for-line in semantics. */
static uint16_t pack_rgb565(double sub_b, double sub_g, double sub_r) {
    int r5 = (int)(sub_r * 31.0 + 0.5); if (r5 > 31) r5 = 31; if (r5 < 0) r5 = 0;
    int g6 = (int)(sub_g * 63.0 + 0.5); if (g6 > 63) g6 = 63; if (g6 < 0) g6 = 0;
    int b5 = (int)(sub_b * 31.0 + 0.5); if (b5 > 31) b5 = 31; if (b5 < 0) b5 = 0;
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void render_glyph_c(uint8_t code, uint16_t out[8][6]) {
    for (int y = 0; y < 8; y++) {
        uint8_t row_byte = font_6x8[code][y];
        int s[18];
        for (int x = 0; x < 6; x++) {
            int bit = (row_byte >> (7 - x)) & 1;
            s[x*3 + 0] = bit;
            s[x*3 + 1] = bit;
            s[x*3 + 2] = bit;
        }
        double f[18];
        for (int k = 0; k < 18; k++) {
            int left  = (k > 0)  ? s[k-1] : s[0];
            int ctr   =           s[k];
            int right = (k < 17) ? s[k+1] : s[17];
            f[k] = (left + 2*ctr + right) / 4.0;
        }
        for (int cell = 0; cell < 6; cell++) {
            double sub_b = f[cell*3 + 0];
            double sub_g = f[cell*3 + 1];
            double sub_r = f[cell*3 + 2];
            out[y][cell] = pack_rgb565(sub_b, sub_g, sub_r);
        }
    }
}

static void check_glyph(uint8_t code) {
    uint16_t expected[8][6];
    render_glyph_c(code, expected);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 6; x++) {
            if (font_subpixel[code][y][x] != expected[y][x]) {
                fprintf(stderr, "glyph 0x%02x [%d][%d]: got 0x%04x, want 0x%04x\n",
                        code, y, x, font_subpixel[code][y][x], expected[y][x]);
                fails++;
            }
        }
    }
}

int main(void) {
    check_glyph(0x20);   /* space */
    check_glyph(0x41);   /* 'A' */
    check_glyph(0x38);   /* '8' */
    check_glyph(0x7F);   /* DEL */
    check_glyph(0xDB);   /* full block */

    if (fails == 0) { printf("PASS\n"); return 0; }
    printf("FAIL (%d) — run: py tools/build_subpixel_font.py\n", fails);
    return 1;
}
```

- [ ] **Step 2: Run the test**

```powershell
cd tests\emu
mingw32-make test_subpixel_glyph_table.exe
.\test_subpixel_glyph_table.exe
```

Expected: PASS. If it fails, the C and Python filter disagree on rounding — adjust the C `pack_rgb565` rounding to match Python's `round()` banker's-rounding-or-not behavior, or run `py tools/build_subpixel_font.py` to regenerate.

- [ ] **Step 3: Add to `run` target**

```make
	@echo === test_subpixel_glyph_table ===
	@./test_subpixel_glyph_table.exe ; echo "exit=$$?"
```

- [ ] **Step 4: Run all tests**

```powershell
mingw32-make run | Select-String "PASS|FAIL|exit="
```

Expected: 13 PASS lines, 0 FAIL.

- [ ] **Step 5: Commit**

```bash
git add tests/emu/test_subpixel_glyph_table.c tests/emu/Makefile
git commit -m "display: subpixel glyph table cross-check host test

Re-runs the [1,2,1]/4 BGR-mapped filter in C against font_6x8.h
and verifies font_6x8_subpixel.h matches. Catches: hand-edits to
the committed .h; filter divergence between Python script and the
table semantics. 13/13 host tests PASS."
```

---

## Task 9: ST7735 panel driver — SPI init, MADCTL, offsets, DMA

**Files:**
- Create: `firmware/components/display/st7735_panel.c` (replace empty stub from Task 3)

This task provides the lowest-level on-chip code: SPI bus setup, ST7735 init sequence, MADCTL=0xA8 (rotation 3 + BGR bit), INVON, COLSTART=26, ROWSTART=1, address-window writes via DMA. Backlight (GPIO 0) driven high; SD CS (GPIO 23) driven high once at boot.

No host test for this — it's hardware-only. Verified at Hardware Gate 3 in Task 11.

- [ ] **Step 1: Write the panel driver**

```c
/*
 * st7735_panel.c — ST7735 80x160 driver via esp_lcd, plus the two
 * bonus GPIOs (LCD backlight + SD CS deselect).
 *
 * Wiring (ESP32-C5 / LilyGO T-Dongle-C5, from pin_config.h):
 *   MOSI=2, SCK=6, CS=10, DC=3, RST=1, BL=0, SD_CS=23.
 *   SPI clock 40 MHz.
 *
 * Panel quirks (from lib/lcd_st7735/st7735.{h,cpp}):
 *   - BGR color order: MADCTL bit 0x08 always set.
 *   - Inverted display: send INVON during init.
 *   - Address-window offsets: COLSTART=26, ROWSTART=1.
 *   - Landscape (160x80) uses MADCTL = 0xA0 | 0x08 = 0xA8 (rotation 3).
 */
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "display_internal.h"

#if CONFIG_ESPDOS_HAS_DISPLAY

#define ST7735_HOST           SPI2_HOST
#define PIN_LCD_MOSI          2
#define PIN_LCD_SCK           6
#define PIN_LCD_CS            10
#define PIN_LCD_DC            3
#define PIN_LCD_RST           1
#define PIN_LCD_BL            0
#define PIN_SD_CS             23
#define LCD_SPI_HZ            (40 * 1000 * 1000)

#define LCD_COLSTART          26
#define LCD_ROWSTART          1
/* Landscape rotation 3 + BGR bit. */
#define LCD_MADCTL_LANDSCAPE  0xA8

static const char *TAG = "st7735";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;

static void deselect_sd_card(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_SD_CS,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_SD_CS, 1);   /* not selected */
}

static void backlight_on(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_LCD_BL, 1);
}

esp_err_t st7735_init(void) {
    deselect_sd_card();

    spi_bus_config_t buscfg = {
        .mosi_io_num     = PIN_LCD_MOSI,
        .miso_io_num     = -1,                /* LCD is write-only */
        .sclk_io_num     = PIN_LCD_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DISPLAY_W * 8 * sizeof(uint16_t),  /* 1 row of 160px x 8 rows */
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ST7735_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num         = PIN_LCD_CS,
        .dc_gpio_num         = PIN_LCD_DC,
        .spi_mode            = 0,
        .pclk_hz             = LCD_SPI_HZ,
        .trans_queue_depth   = 10,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ST7735_HOST,
                                                  &io_cfg, &s_io),
                        TAG, "new_panel_io_spi");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num   = PIN_LCD_RST,
        .color_space      = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel   = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7735(s_io, &panel_cfg, &s_panel),
                        TAG, "new_panel_st7735");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init");

    /* Apply panel-specific gap (COLSTART=26, ROWSTART=1) and inverted
     * display. esp_lcd_panel_set_gap takes (x_gap, y_gap). In landscape
     * (rotation 3) the panel's column origin maps to our y, so the
     * gap call uses (y_gap=ROWSTART, x_gap=COLSTART). esp_lcd handles
     * the rotation internally; we just pass landscape orientation. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_ROWSTART, LCD_COLSTART),
                        TAG, "panel_set_gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true),
                        TAG, "panel_invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true),
                        TAG, "panel_swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false),
                        TAG, "panel_mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true),
                        TAG, "panel_on");

    backlight_on();

    ESP_LOGI(TAG, "ST7735 ready: %dx%d landscape, BGR, COLSTART=%d ROWSTART=%d",
             DISPLAY_W, DISPLAY_H, LCD_COLSTART, LCD_ROWSTART);
    return ESP_OK;
}

/* Push one row of the framebuffer (160 px × N rows, RGB565). */
esp_err_t st7735_draw_rows(int y0, int y1, const uint16_t *pixels) {
    return esp_lcd_panel_draw_bitmap(s_panel, 0, y0, DISPLAY_W, y1, pixels);
}

#endif  /* CONFIG_ESPDOS_HAS_DISPLAY */
```

The exact `swap_xy` / `mirror` combination achieving rotation 3 may need tuning on first flash; if the screen is upside-down or mirrored, flip the booleans. The panel-specific `set_gap` ordering (x, y vs y, x after rotation) likewise — verify at Hardware Gate 3.

- [ ] **Step 2: Build for C5**

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: builds clean. If `esp_lcd_new_panel_st7735` is not declared, check that `REQUIRES esp_lcd` is in the display CMakeLists (it is, from Task 3).

- [ ] **Step 3: Build for S3 (regression)**

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Expected: builds clean. The `st7735_panel.c` file is excluded from S3 builds via the `if(CONFIG_ESPDOS_HAS_DISPLAY)` guard in display/CMakeLists.txt.

- [ ] **Step 4: Commit**

```bash
git add firmware/components/display/st7735_panel.c
git commit -m "display: ST7735 panel driver (SPI, MADCTL, offsets, BL, SD CS)

Wraps esp_lcd_panel_st7735 with the LilyGO T-Dongle-C5 specifics
from lib/lcd_st7735/: BGR color space, INVON, COLSTART=26,
ROWSTART=1, landscape via swap_xy + mirror. Drives backlight high
(LilyGO Adafruit driver leaves it untouched). Drives SD_CS high
once at boot so the SD slave on the shared SPI bus stays
deselected during LCD writes.

No host test (hardware-only). Verified at Gate 3 in Task 11.
Builds clean on both targets."
```

---

## Task 10: Sharp 26-col renderer

**Files:**
- Create: `firmware/components/display/render_sharp.c` (replace empty stub)

Pure C function that converts a glyph index + cell position to 6 RGB565 pixels (one cell row). Used by the status bar always; used by the log when subpixel mode is off.

- [ ] **Step 1: Implement**

```c
/*
 * render_sharp.c — pure C, glyph -> RGB565.
 * One source pixel = one panel pixel (no subpixel). 26 cols x N rows
 * in landscape. Always white-on-black for now; color extension is a
 * follow-up.
 */
#include <stdint.h>
#include "font_6x8.h"
#include "display_internal.h"

#define WHITE_565   0xFFFF
#define BLACK_565   0x0000

/* Render one row of one glyph into 6 RGB565 pixels.
 * row in 0..7. */
void render_sharp_row(uint8_t code, int row, uint16_t out[6]) {
    uint8_t bits = font_6x8[code][row];
    for (int x = 0; x < 6; x++) {
        int on = (bits >> (7 - x)) & 1;
        out[x] = on ? WHITE_565 : BLACK_565;
    }
}

/* Render one full text row (26 chars) into a 160-pixel scanline.
 * Caller calls 8 times (rows 0..7) to cover one cell row. */
void render_sharp_scanline(const char *text, uint8_t length,
                            int row, uint16_t out[160]) {
    for (int col = 0; col < 26; col++) {
        uint8_t code = (col < length) ? (uint8_t)text[col] : 0x20;
        uint16_t cell[6];
        render_sharp_row(code, row, cell);
        for (int x = 0; x < 6; x++) out[col*6 + x] = cell[x];
    }
    /* 26 * 6 = 156. Pad last 4 pixels black. */
    for (int x = 156; x < 160; x++) out[x] = BLACK_565;
}
```

- [ ] **Step 2: Build for C5**

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: builds clean.

- [ ] **Step 3: Commit**

```bash
git add firmware/components/display/render_sharp.c
git commit -m "display: sharp 26-col renderer

Pure C, glyph -> 6 RGB565 pixels per cell row. Used by the status
bar and as the fallback log renderer when ESPDOS_DISPLAY_SUBPIXEL=n."
```

---

## Task 11: Wire display_init + 30 Hz flush; Hardware Gate 3 (sharp mode)

**Files:**
- Modify: `firmware/components/display/display.c` (replace empty stub)

This task connects every layer: `display_init` calls `st7735_init`, allocates frame state, kicks a FreeRTOS xTimer for 30 Hz dirty-row flush. `display_putc` advances the ANSI strip state and updates `display_log_t`. `display_set_program/_beat` update the status bar. The flush fires every ~33 ms, walks the dirty mask, calls `render_sharp_scanline` to build one row's worth of scanlines and pushes via `st7735_draw_rows`.

After this task: `ESPDOS_DISPLAY_SUBPIXEL=n` (set via menuconfig) gives a fully working LCD mirror in 26-col sharp mode. Hardware Gate 3 verifies this.

- [ ] **Step 1: Implement display.c**

```c
/*
 * display.c — top-level init, 30 Hz flush, public API.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "display.h"
#include "display_internal.h"

#if CONFIG_ESPDOS_HAS_DISPLAY

extern esp_err_t  st7735_init(void);
extern esp_err_t  st7735_draw_rows(int y0, int y1, const uint16_t *pixels);

extern void render_sharp_scanline(const char *text, uint8_t length,
                                  int row, uint16_t out[160]);
#if CONFIG_ESPDOS_DISPLAY_SUBPIXEL
extern void render_subpixel_scanline(const char *text, uint8_t length,
                                     int row, uint16_t out[160]);
#endif

static const char *TAG = "display";
static TimerHandle_t s_flush_timer;
static display_log_t      s_log;
static display_status_t   s_status;
static display_ansi_state_t s_ansi = DISPLAY_ANSI_GROUND;
static volatile int       s_initialized;

/* One row's worth of pixels (160 wide x 8 tall) for SPI DMA. */
static uint16_t s_rowbuf[DISPLAY_W * 8];

/* Render the status bar (row 0 of the panel = top 8 pixels). */
static void render_status(uint16_t out[DISPLAY_W * 8]) {
    char text[DISPLAY_STATUS_COLS + 1];
    snprintf(text, sizeof text, "%-15s b%-7lu",
             s_status.program, (unsigned long)s_status.beat);
    text[DISPLAY_STATUS_COLS] = '\0';
    for (int y = 0; y < 8; y++) {
        render_sharp_scanline(text, (uint8_t)DISPLAY_STATUS_COLS, y,
                              &out[y * DISPLAY_W]);
    }
}

/* Render one log row (cell row r in 0..LOG_ROWS-1) at panel y = 8 + r*8. */
static void render_log_row(int r, uint16_t out[DISPLAY_W * 8]) {
    uint8_t slot = (s_log.oldest + r) % DISPLAY_LOG_ROWS;
    const char *line = s_log.rows[slot];
    uint8_t length = s_log.lengths[slot];
    for (int y = 0; y < 8; y++) {
#if CONFIG_ESPDOS_DISPLAY_SUBPIXEL
        render_subpixel_scanline(line, length, y, &out[y * DISPLAY_W]);
#else
        render_sharp_scanline(line, length, y, &out[y * DISPLAY_W]);
#endif
    }
}

static void flush_timer_cb(TimerHandle_t t) {
    (void)t;
    if (!s_initialized) return;

    if (s_status.dirty) {
        render_status(s_rowbuf);
        st7735_draw_rows(0, 8, s_rowbuf);
        s_status.dirty = 0;
    }
    if (s_log.dirty_mask) {
        for (int r = 0; r < DISPLAY_LOG_ROWS; r++) {
            uint8_t slot = (s_log.oldest + r) % DISPLAY_LOG_ROWS;
            if (!(s_log.dirty_mask & (1u << slot))) continue;
            render_log_row(r, s_rowbuf);
            int y0 = DISPLAY_STATUS_ROWS * 8 + r * 8;
            st7735_draw_rows(y0, y0 + 8, s_rowbuf);
        }
        s_log.dirty_mask = 0;
    }
}

esp_err_t display_init(void) {
    if (s_initialized) return ESP_OK;

    display_log_reset(&s_log);
    memset(&s_status, 0, sizeof s_status);
    s_status.dirty = 1;

    esp_err_t err = st7735_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "st7735_init failed: %d", err);
        return err;
    }

    s_flush_timer = xTimerCreate("disp", pdMS_TO_TICKS(33),
                                 pdTRUE, NULL, flush_timer_cb);
    if (!s_flush_timer) {
        ESP_LOGE(TAG, "xTimerCreate failed");
        return ESP_ERR_NO_MEM;
    }
    xTimerStart(s_flush_timer, 0);

    s_initialized = 1;
    ESP_LOGI(TAG, "display ready");
    return ESP_OK;
}

void display_putc(uint8_t ch) {
    if (!s_initialized) return;
    s_ansi = display_log_putc(&s_log, s_ansi, ch);
}

void display_set_program(const char *name) {
    if (!s_initialized) return;
    if (!name) name = "";
    strncpy(s_status.program, name, sizeof(s_status.program) - 1);
    s_status.program[sizeof(s_status.program) - 1] = '\0';
    s_status.dirty = 1;
}

void display_set_beat(uint32_t beat) {
    if (!s_initialized) return;
    /* Throttle: only mark dirty every 8 beats so we don't redraw the
     * status bar at the heartbeat rate (which is much faster than
     * 30 Hz). */
    if ((beat & 0x7) != 0) return;
    s_status.beat = beat;
    s_status.dirty = 1;
}

#endif  /* CONFIG_ESPDOS_HAS_DISPLAY */
```

- [ ] **Step 2: Build with `ESPDOS_DISPLAY_SUBPIXEL=n` for first hardware test**

We want to validate sharp mode end-to-end before subpixel enters the picture. Temporarily flip the Kconfig default:

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py menuconfig
```

In menuconfig: navigate to "espDos display" → uncheck "Use subpixel-rendered 80-column log" → save → exit.

```powershell
idf.py build
```

Expected: builds clean. `render_subpixel_scanline` is referenced in display.c only inside `#if CONFIG_ESPDOS_DISPLAY_SUBPIXEL`, so it doesn't need to exist yet.

Actually, that's wrong — render_subpixel.c was created empty in Task 3 and the linker may complain about an undefined symbol if any TU references it. Let's verify the conditional compile guard works. If the linker complains, add a placeholder definition in render_subpixel.c:

```c
#include <stdint.h>
#if 0   /* real implementation lands in Task 12 */
void render_subpixel_scanline(const char *text, uint8_t length,
                              int row, uint16_t out[160]) {}
#endif
```

If the `#if CONFIG_ESPDOS_DISPLAY_SUBPIXEL` guard in display.c is honored at preprocess time, no reference is emitted and the link succeeds. It should — sdkconfig.h defines symbols based on Kconfig values.

- [ ] **Step 3: Hardware Gate 3 — flash and verify sharp mode**

```powershell
idf.py -p COM<n> flash monitor
```

Expected:
- LCD lights up (backlight on).
- Top row shows status: `MANDEL.COM     b00000000` (or similar; last few chars depend on the beat counter).
- Below status: 9 rows of scrolling MANDEL output as the fractal renders.
- Text is white on black, no color fringes (sharp mode).
- USB-Serial-JTAG side still shows the full MANDEL output.

If the screen is upside-down, mirrored, or shows incorrect colors:
- **Upside down:** flip `esp_lcd_panel_mirror(s_panel, true, false)` to `(false, true)` in st7735_panel.c.
- **Colors red/blue swapped:** the panel was reading as RGB instead of BGR — change `ESP_LCD_COLOR_SPACE_BGR` to `ESP_LCD_COLOR_SPACE_RGB` in `panel_cfg`.
- **Garbage at edges:** COLSTART/ROWSTART are wrong; swap the `set_gap` argument order.
- **Nothing on screen:** check backlight wiring (GPIO 0), SPI bus init, panel reset.

Iterate until the image is correct, then commit the resulting st7735_panel.c (this may amend the Task 9 commit or be its own).

- [ ] **Step 4: Restore subpixel default**

After Gate 3 passes, revert the menuconfig change so the next build defaults to subpixel (Task 12):

```powershell
idf.py menuconfig    # re-check "Use subpixel-rendered 80-column log"
```

Or simply delete `firmware/sdkconfig` to fall back to defaults from `sdkconfig.defaults.esp32c5` (which has SUBPIXEL=y).

- [ ] **Step 5: Commit**

```bash
git add firmware/components/display/display.c firmware/components/display/st7735_panel.c
git commit -m "display: wire init + 30Hz flush + sharp-mode render

display_init brings up SPI + ST7735, allocates a 160x8 row buffer,
starts a FreeRTOS xTimer that flushes dirty rows every 33ms.
display_putc advances the ANSI strip state and updates the log;
display_set_program/_beat update the status bar.

Hardware Gate 3 passes on T-Dongle-C5 with ESPDOS_DISPLAY_SUBPIXEL=n:
status bar shows MANDEL.COM + beat counter; log scrolls the fractal
output below in sharp 26-col mode."
```

---

## Task 12: Subpixel 80-col renderer

**Files:**
- Modify: `firmware/components/display/render_subpixel.c` (replace empty stub)

Pure C, BGR-mapped table lookup. No filter math at runtime — just `font_subpixel[code][row][col]` indexing.

> **Important math note that Task 5's draft script got wrong and this task corrects:** 160 panel pixels × 3 subpixels = **480 horizontal subpixel slots**. A 6-subpixel-wide cell × 80 cols = 480 ✓. **Each character cell occupies 2 panel pixels** (3 subpixels = 1 panel pixel = 1 RGB565 word). So the table's per-row dimension is **2**, not 6. Total table size: 256 × 8 × 2 = 4 KB (not the 14 KB the draft script assumed). This task fixes the script + regenerates the header + adjusts the cross-check test.

- [ ] **Step 1: Patch `tools/build_subpixel_font.py` to use 2 cells per row**

In the `render_glyph` function, change the cell-pack loop:

```python
def render_glyph(rows):
    """Return [8][2] uint16 RGB565 with BGR-mapped subpixel intensities."""
    out = [[0]*2 for _ in range(8)]
    for y, row_byte in enumerate(rows):
        s = []
        for x in range(6):
            bit = (row_byte >> (7 - x)) & 1
            s.extend([bit, bit, bit])
        f = [0.0]*18
        for k in range(18):
            left  = s[k-1] if k > 0    else s[0]
            ctr   = s[k]
            right = s[k+1] if k < 17   else s[17]
            f[k]  = (left + 2*ctr + right) / 4.0
        # 2 panel pixels per character row. Each panel pixel = 3 subpixels.
        # Panel is BGR -> subpixel 0 = B, 1 = G, 2 = R.
        for cell in range(2):
            sub_b = f[cell*3 + 0]
            sub_g = f[cell*3 + 1]
            sub_r = f[cell*3 + 2]
            r5 = int(round(sub_r * 31)); r5 = max(0, min(31, r5))
            g6 = int(round(sub_g * 63)); g6 = max(0, min(63, g6))
            b5 = int(round(sub_b * 31)); b5 = max(0, min(31, b5))
            out[y][cell] = (r5 << 11) | (g6 << 5) | b5
    return out
```

In `emit_header` (and the `--check` mirror in `main`), change the table typedef and the cell-emit loop:

```python
    lines.append("static const uint16_t font_subpixel[256][8][2] = {")
    ...
    for row in glyph:
        cells = ", ".join(f"0x{c:04x}" for c in row)   # row is length 2
        lines.append(f"        {{ {cells} }},")
```

The first 12 of the 18 filtered subpixels (indices 0–11 → cell ranges 0..1) cover the visible glyph. Subpixels 12..17 are unused (they correspond to the right-edge taper that's outside our 2-panel-pixel cell). This is fine — those samples were never going to be drawn.

- [ ] **Step 2: Regenerate the committed header**

```powershell
py tools\build_subpixel_font.py
```

Expected: `wrote .../font_6x8_subpixel.h (256 glyphs)`. The header is now ~4 KB instead of ~14 KB.

- [ ] **Step 3: Update `tests/emu/test_subpixel_glyph_table.c`**

Change three things in the test:

```c
/* Was: uint16_t expected[8][6]; */
    uint16_t expected[8][2];

/* Was: for (int cell = 0; cell < 6; cell++) */
        for (int cell = 0; cell < 2; cell++) {

/* Was: for (int x = 0; x < 6; x++) */
        for (int x = 0; x < 2; x++) {
```

(The `render_glyph_c` function inside the test has the same `cell in 0..2` loop already — just the array dimensions and the comparison loop change.)

- [ ] **Step 4: Run host tests — must still PASS**

```powershell
cd tests\emu
mingw32-make clean
mingw32-make run | Select-String "PASS|FAIL|exit="
```

Expected: 13/13 PASS. If `test_subpixel_glyph_table` fails, the C and Python filters disagree on rounding; pick one rounding rule (Python's banker's-rounding via `round()`, or `int(x + 0.5)` everywhere) and apply consistently to both sides.

- [ ] **Step 5: Write `render_subpixel.c`**

Replace the empty stub with:

```c
/*
 * render_subpixel.c — pure C, glyph -> BGR-mapped RGB565 from the
 * pre-computed subpixel table. 80 cols x N rows in landscape; each
 * character cell occupies 2 panel pixels horizontally (3 subpixels
 * each).
 */
#include <stdint.h>
#include "font_6x8_subpixel.h"
#include "display_internal.h"

/* Render one full text row (80 chars) into a 160-pixel scanline.
 * Caller calls 8 times (rows 0..7) to cover one cell row. */
void render_subpixel_scanline(const char *text, uint8_t length,
                               int row, uint16_t out[160]) {
    for (int col = 0; col < 80; col++) {
        uint8_t code = (col < length) ? (uint8_t)text[col] : 0x20;
        const uint16_t *cell = font_subpixel[code][row];
        out[col*2 + 0] = cell[0];
        out[col*2 + 1] = cell[1];
    }
}
```

- [ ] **Step 6: Build for both targets**

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: builds clean. C5 firmware now contains the corrected 4 KB table and the live subpixel renderer.

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Expected: S3 builds clean (display compiles out as before).

- [ ] **Step 2: Build for C5**

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build
```

Expected: builds clean.

- [ ] **Step 3: Run host tests**

```powershell
cd ..\tests\emu
mingw32-make run | Select-String "PASS|FAIL|exit="
```

Expected: 13/13 PASS.

- [ ] **Step 4: Commit**

Two logical commits since we touched multiple tasks:

```bash
# Fix #1: correct cell count in font generator + test
git add tools/build_subpixel_font.py \
        firmware/components/display/include/font_6x8_subpixel.h \
        tests/emu/test_subpixel_glyph_table.c
git commit -m "display: fix subpixel cell count (6 -> 2 per char row)

A character cell of 6 subpixels = 2 panel pixels (each panel pixel
holds 3 subpixels). Earlier draft over-allocated the table to
[256][8][6]; correct shape is [256][8][2]. Regenerated header,
updated cross-check test to match. Host tests 13/13 PASS."

# Fix #2: implement the renderer
git add firmware/components/display/render_subpixel.c
git commit -m "display: subpixel 80-col renderer

Pure C table lookup. font_subpixel[code][row][0..1] gives 2 RGB565
words per character row, BGR-pre-mapped by the offline script."
```

---

## Task 13: Hardware Gates 4 + 5 — calibration + full subpixel

**Files:**
- Modify: `firmware/components/display/display.c` (calibration block, gated on `CONFIG_ESPDOS_DISPLAY_CALIBRATION`)

- [ ] **Step 1: Hardware Gate 4 — subpixel calibration**

Build with the calibration Kconfig:

```powershell
cd firmware
idf.py menuconfig
```

In menuconfig: enable "Render subpixel calibration pattern at boot."

We need a calibration routine. Add a small block at the top of `display_init` in `display.c`:

```c
#if CONFIG_ESPDOS_DISPLAY_CALIBRATION
    /* Three horizontal bars: top = pure subpixel-0 (B in BGR),
     * middle = pure subpixel-1 (G), bottom = pure subpixel-2 (R).
     * If you see Red on top / Green middle / Blue bottom, the panel
     * is RGB and we need to flip MADCTL bit 0x08; if Blue/Green/Red
     * top-down, the panel is BGR and our font script is correct. */
    static uint16_t calbuf[DISPLAY_W * DISPLAY_H];
    for (int y = 0; y < DISPLAY_H; y++) {
        uint16_t color;
        if      (y < DISPLAY_H/3)   color = 0x001F;  /* B */
        else if (y < 2*DISPLAY_H/3) color = 0x07E0;  /* G */
        else                        color = 0xF800;  /* R */
        for (int x = 0; x < DISPLAY_W; x++) calbuf[y * DISPLAY_W + x] = color;
    }
    st7735_init();
    st7735_draw_rows(0, DISPLAY_H, calbuf);
    vTaskDelay(pdMS_TO_TICKS(5000));
#endif
```

Build, flash, observe:

```powershell
idf.py build flash monitor
```

Expected: top third blue, middle third green, bottom third red, held for 5 seconds, then normal display init runs.

If the colors are in a different order (e.g. red on top), the panel is not BGR as the LilyGO driver claimed — flip `ESP_LCD_COLOR_SPACE_BGR` to `ESP_LCD_COLOR_SPACE_RGB` in `st7735_panel.c`'s `panel_cfg` AND change the Python script's BGR assumption to RGB (swap the `cell*3 + 0` (B) and `cell*3 + 2` (R) assignments in `render_glyph`). Regenerate the header with `py tools/build_subpixel_font.py`. Re-flash. Re-verify.

If colors match expectation, no changes needed.

- [ ] **Step 2: Disable calibration, full subpixel build**

```powershell
idf.py menuconfig    # disable "Render subpixel calibration pattern at boot"
idf.py build "-DESPDOS_LOADER_SHELL=1" "-DESPDOS_AUTOPICK=4"
```

The autopick=4 makes SHELL launch JULIA — animated color, lots of ANSI, exercises the LCD log scrolling under load.

- [ ] **Step 3: Hardware Gate 5 — full C5 experience**

```powershell
idf.py -p COM<n> flash monitor
```

Verify on the LCD:
- Top row status bar shows `JULIA.COM      b00000000` (sharp 26-col, white-on-black).
- Below: 9 log rows scrolling JULIA's frame output. Text is fine and readable; color fringing visible at glyph edges (this is the subpixel rendering working).
- USB-JTAG side: full color JULIA animation as today.

Run for ~30 seconds, confirm the LCD log keeps up with the scroll rate (no visible lag/tearing). Look for any visible artifacts:
- Color cast on supposedly-white glyphs (check stripe order again if so).
- Glyphs touching neighbors (filter is too tight — switch to `[1, 2, 1]` if it isn't already).
- Glyphs blurry beyond reading (filter is too soft — try `[0, 1, 0]` in the Python script and regen).

Iterate the filter once if needed (rebuild + flash, ~2 min cycle). Then:

- [ ] **Step 4: Run SHELL navigation manually**

Reset the board (`Ctrl-T Ctrl-R` in monitor). Watch SHELL menu render on the LCD. Type `5` (LIFE), confirm the LCD log updates with the LIFE pattern. After LIFE finishes, confirm SHELL menu reappears.

- [ ] **Step 5: S3 regression once more**

After the C5 verification iterations, run S3 once more to make sure nothing regressed:

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py fullclean
idf.py build "-DESPDOS_LOADER_SHELL=1"
idf.py -p COM<n> flash monitor
```

Expected: SHELL boots on T-Display-S3 unchanged. No display code on this target.

- [ ] **Step 6: Commit any tuning changes**

If you flipped color space, regenerated the font, or otherwise tweaked during gates:

```bash
git add -p   # pick the changes
git commit -m "display: tune subpixel rendering against real T-Dongle-C5

(describe what changed: filter coefficients, color order, MADCTL,
panel offsets, etc.)

Hardware Gates 4 + 5 pass: calibration pattern correct, JULIA + LIFE
+ SHELL all render readable text on the LCD log with the status bar
showing program name + beat counter."
```

If nothing changed, no commit; just note "Gates 4+5 PASS" in the next commit's message body.

---

## Task 14: README C5 target subsection

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add the C5 subsection**

In `README.md`, after the existing "Build and flash" section, add:

```markdown
### Building for the LilyGO T-Dongle-C5

espDos also runs on the T-Dongle-C5 (ESP32-C5 with 8 MB quad PSRAM and a
0.96" ST7735 LCD). On this target the LCD mirrors BIOS console output as
a status bar plus an 80-column subpixel-rendered scrolling log; USB-Serial-
JTAG remains the primary I/O channel.

```powershell
cd firmware
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32c5
idf.py fullclean
idf.py build -DESPDOS_LOADER_SHELL=1
idf.py flash monitor
```

The LCD shows:

```
+--------------------------+
| MANDEL.COM    b 1284     |   <- status bar (sharp 26-col)
+--------------------------+
| ::---::%:.:.+........:+@ |   <- 9 rows of subpixel-rendered
| =.+:#:::::-:.........:.. |      80-col log, scrolling
| ...                      |
+--------------------------+
```

Subpixel rendering uses Bowman/ClearType-style 3-subpixel-per-pixel text
to fit a real 80-column DOS terminal on a 160-pixel-wide display. Toggle
sharp 26-col mode in `idf.py menuconfig` → "espDos display" → "Use
subpixel-rendered 80-column log".

To switch back to S3:

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
```
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "README: T-Dongle-C5 build + LCD mirror"
```

---

## Self-review checklist

After completing all tasks, run through:

- [ ] **Spec coverage:** every section in the spec maps to a task above.
- [ ] **Both targets build clean** with `Remove-Item sdkconfig; idf.py set-target esp32{s3,c5}; idf.py fullclean; idf.py build`.
- [ ] **All host tests PASS:** `cd tests\emu; mingw32-make run` shows 13 PASS lines, 0 FAIL.
- [ ] **Hardware gates pass:** S3 regression (Task 3 step 9 / Task 13 step 5), C5 minimal (Task 2 step 3), C5 sharp (Task 11 step 3), C5 calibration (Task 13 step 1), C5 full (Task 13 steps 3-4).
- [ ] **No commits include `--no-verify` or skip hooks.**
- [ ] **README updated.**

If any gate fails, do not check the box — fix the underlying issue first.

## Decisions deferred (out of scope this round)

These are documented in the spec's "Decisions deferred" section and remain follow-up sub-projects:
- ANSI color tracking in the LCD log (currently white-on-black).
- TF card backed FAT12 disk on the C5 (currently still flash partition).
- LCD output on the T-Display-S3.
- Per-program graphical takeover (MANDEL pixels, JULIA color frames, graphical SHELL).
- C5 QEMU recipe (Layer 2) — verified at implementation time per spec testing section.
