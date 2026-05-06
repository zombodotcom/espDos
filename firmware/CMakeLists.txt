cmake_minimum_required(VERSION 3.16)

# Restrict build to only what `main` and its dependencies actually need.
# The default (no COMPONENTS set) builds the entire IDF component tree —
# which on this toolchain triggers a GCC internal compiler error (segfault)
# while building esp_lcd_panel_rgb.c, a component we don't use. Listing
# only the components we touch sidesteps that and shortens build times.
# (Required IDF components like freertos, log, esp_system, etc. get
# pulled in transitively via REQUIRES inside each component manifest.)
set(COMPONENTS main emu8086 kernel_blob)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espdos)
