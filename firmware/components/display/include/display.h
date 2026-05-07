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
