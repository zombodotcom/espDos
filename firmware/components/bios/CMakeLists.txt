idf_component_register(
    SRCS "bios.c"
    INCLUDE_DIRS "include"
    REQUIRES log driver esp_driver_usb_serial_jtag
             esp_partition spi_flash
)

# Auto-feed extension: SHELL.COM reads a digit after the date prompt.
# Pass via -DESPDOS_AUTOPICK=2 (or any digit) at idf.py invocation;
# the value is appended verbatim to the autodate buffer in bios.c.
if(DEFINED ESPDOS_AUTOPICK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ESPDOS_AUTOPICK=${ESPDOS_AUTOPICK})
endif()
