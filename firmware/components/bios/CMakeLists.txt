idf_component_register(
    SRCS "bios.c"
    INCLUDE_DIRS "include"
    REQUIRES log driver esp_driver_usb_serial_jtag
             esp_partition spi_flash
)
