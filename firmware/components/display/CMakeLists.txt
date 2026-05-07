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
        "esp_lcd_st7735_vendor.c"
    )
else()
    set(srcs)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    # esp_lcd, driver, freertos are private (not exposed in display.h).
    # log is also private; display.h only exposes esp_err_t which comes
    # from esp_common (auto-required by IDF for every component).
    PRIV_REQUIRES esp_lcd driver log freertos
)
