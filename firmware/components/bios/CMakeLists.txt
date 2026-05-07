idf_component_register(
    SRCS "bios.c"
    INCLUDE_DIRS "include"
    REQUIRES log driver esp_driver_usb_serial_jtag
             esp_partition spi_flash display
)

# Auto-feed extension: SHELL.COM reads a digit after the date prompt.
# Pass via -DESPDOS_AUTOPICK=2 (or any digit) at idf.py invocation;
# the value is appended verbatim to the autodate buffer in bios.c.
if(DEFINED ESPDOS_AUTOPICK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ESPDOS_AUTOPICK=${ESPDOS_AUTOPICK})
endif()

# Mirror BIOS console output through ESP_LOGI in addition to the raw
# JTAG byte stream. Useful for QEMU `-serial file:` capture where the
# JTAG channel isn't redirected. Off by default — keeps hardware
# output clean of "I (...) bios: " prefixes that would interleave with
# ANSI escape sequences and break cursor-positioning.
#   idf.py build -DESPDOS_LOG_OUT=1
if(ESPDOS_LOG_OUT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOG_OUT=1)
endif()
