cmake_minimum_required(VERSION 3.16)

# Restrict build to only what `main` and its dependencies actually need.
# The default (no COMPONENTS set) builds the entire IDF component tree —
# which on this toolchain triggers a GCC internal compiler error (segfault)
# while building esp_lcd_panel_rgb.c, a component we don't use. Listing
# only the components we touch sidesteps that and shortens build times.
# (Required IDF components like freertos, log, esp_system, etc. get
# pulled in transitively via REQUIRES inside each component manifest.)
#
# IMPORTANT: esp_psram MUST be listed explicitly here. It is not a
# transitive dependency of esp8086 — but its Kconfig defines
# CONFIG_SPIRAM and friends. Excluding it makes kconfig silently drop
# `CONFIG_SPIRAM=y` from sdkconfig.defaults, so EXT_RAM_BSS_ATTR turns
# into a no-op and esp8086.c's 1 MB mem[] tries to fit in 512 KB DRAM,
# overflowing by ~900 KB at link time.
set(COMPONENTS main esp8086 kernel_blob bios esp_psram)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espdos)

# ESPDOS_AUTOPICK is consumed by both espdos.c (program-selection ifdef)
# and bios.c (autodate-feed extension). Apply it project-wide so the
# bios component sees it too — main/CMakeLists.txt's
# target_compile_definitions(${COMPONENT_LIB} ...) only reaches the main
# component.
if(DEFINED ESPDOS_AUTOPICK)
    add_compile_definitions(ESPDOS_AUTOPICK=${ESPDOS_AUTOPICK})
endif()

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
