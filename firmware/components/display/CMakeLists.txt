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
