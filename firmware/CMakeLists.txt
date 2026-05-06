cmake_minimum_required(VERSION 3.16)

# Restrict build to only what `main` and its dependencies actually need.
# The default (no COMPONENTS set) builds the entire IDF component tree —
# which on this toolchain triggers a GCC internal compiler error (segfault)
# while building esp_lcd_panel_rgb.c, a component we don't use. Listing
# only the components we touch sidesteps that and shortens build times.
# (Required IDF components like freertos, log, esp_system, etc. get
# pulled in transitively via REQUIRES inside each component manifest.)
set(COMPONENTS main emu8086 kernel_blob bios)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espdos)

# Flash the FAT12 disk image (built by tools/build_disk.py) into the
# dos_disk partition whenever `idf.py flash` runs. Without this step,
# `bios_read` would return error and the kernel can't get past init.
# Build the image first if it doesn't exist:
#   python tools/build_disk.py build/disk.img
set(DISK_IMG "${CMAKE_SOURCE_DIR}/../build/disk.img")
if(EXISTS "${DISK_IMG}")
    esptool_py_flash_to_partition(flash "dos_disk" "${DISK_IMG}")
else()
    message(WARNING
        "Disk image not found at ${DISK_IMG} — "
        "run tools/build_disk.py before idf.py flash, "
        "or bios_read will return error and the kernel won't boot.")
endif()
