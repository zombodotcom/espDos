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
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
/* ST7735 dropped from in-tree esp_lcd in IDF 6.0; pulled in via
 * idf_component.yml -> waveshare/esp_lcd_st7735. */
#include "esp_lcd_st7735.h"
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
        .max_transfer_sz = DISPLAY_W * 8 * (int)sizeof(uint16_t),
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
        /* IDF 6.x renamed color_space → rgb_ele_order; value is BGR. */
        .rgb_ele_order    = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel   = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7735(s_io, &panel_cfg, &s_panel),
                        TAG, "new_panel_st7735");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init");

    /* Apply panel-specific gap (COLSTART=26, ROWSTART=1) and inverted
     * display. esp_lcd_panel_set_gap takes (x_gap, y_gap). The
     * landscape rotation is achieved via swap_xy + mirror. The exact
     * mirror combo may need tuning on first flash — verify at Gate 3
     * and flip booleans if the screen is upside-down. */
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

/* Push a strip of the framebuffer (160 px wide × (y1-y0) tall, RGB565). */
esp_err_t st7735_draw_rows(int y0, int y1, const uint16_t *pixels) {
    return esp_lcd_panel_draw_bitmap(s_panel, 0, y0, DISPLAY_W, y1, pixels);
}

#endif  /* CONFIG_ESPDOS_HAS_DISPLAY */
