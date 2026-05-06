cmake_minimum_required(VERSION 3.16)

# Restrict build to only what `main` and its dependencies actually need.
# The default (no COMPONENTS set) builds the entire IDF component tree —
# which on this toolchain triggers a GCC internal compiler error (segfault)
# while building esp_lcd_panel_rgb.c, a component we don't use. Listing
# only `main` here sidesteps that and dramatically shortens build times.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espdos)
