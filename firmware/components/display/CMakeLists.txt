# Component is gated on CONFIG_ESPDOS_HAS_DISPLAY. When disabled, no
# sources compile and the public API in display.h resolves to inline
# no-ops; bios.c and espdos.c need no #ifdef of their own.
#
# IMPORTANT: IDF's requirements-gathering pass re-includes this file
# without sdkconfig variables, so CONFIG_ESPDOS_HAS_DISPLAY evaluates
# to false in that pass. Dependencies must be declared UNCONDITIONALLY
# so the include-path graph is correct regardless of that limitation.
# Sources are still gated on CONFIG_ESPDOS_HAS_DISPLAY as before.
if(CONFIG_ESPDOS_HAS_DISPLAY)
    set(srcs
        "display_log.c"
        "display.c"
        "render_sharp.c"
        "render_subpixel.c"
        "st7735_panel.c"
    )
else()
    set(srcs)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    # st7735_panel.c uses spi_master + gpio directly (ported from
    # LilyGO's Adafruit_ST7735), no esp_lcd dependency. IDF 6.0
    # split the old monolithic `driver` component into per-peripheral
    # esp_driver_* components, so we list them explicitly:
    #   - esp_driver_spi: spi_bus_initialize, spi_device_polling_transmit
    #   - esp_driver_gpio: gpio_config, gpio_set_level
    # log: ESP_LOGI / ESP_RETURN_ON_ERROR.
    # freertos: vTaskDelay + xTimerCreate (display.c).
    # esp_err_t comes from esp_common, auto-required.
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio log freertos
)
