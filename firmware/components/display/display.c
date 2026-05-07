/*
 * display.c — top-level init, 30 Hz flush, public API.
 *
 * Owns one display_log_t (the per-row ring buffer), one display_status_t
 * (program name + beat counter), and one row-strip framebuffer of
 * 160 px x 8 rows (RGB565). A FreeRTOS xTimer fires every 33 ms and
 * walks the dirty bits, rendering and SPI-DMA-pushing only the rows
 * that have changed since the last flush.
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

/* Render the status bar (cell row 0 of the panel = top 8 panel rows). */
static void render_status(uint16_t out[DISPLAY_W * 8]) {
    char text[DISPLAY_STATUS_COLS + 1];
    /* Beat field: 26 - 15 ("%-15s") - 2 (" b") = 9 chars max.
     * uint32_t fits in 10 decimal digits, so clamp to 9 with a tmp buf. */
    char beat_buf[11];
    snprintf(beat_buf, sizeof beat_buf, "%lu", (unsigned long)s_status.beat);
    snprintf(text, sizeof text, "%-15.15s b%-9.9s",
             s_status.program, beat_buf);
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
    /* Throttle: only mark dirty every 8 beats. The heartbeat is much
     * faster than the 30 Hz flush rate; redrawing the status bar at
     * heartbeat speed wastes SPI bandwidth. */
    if ((beat & 0x7) != 0) return;
    s_status.beat = beat;
    s_status.dirty = 1;
}

#endif  /* CONFIG_ESPDOS_HAS_DISPLAY */
