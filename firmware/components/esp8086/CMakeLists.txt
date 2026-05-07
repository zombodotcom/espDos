# esp8086 — 8086 instruction interpreter for espDos.
#
# Forked from Adrian Cable's 8086tiny v1.25 (MIT-licensed). The fork
# fixes three structural issues with using upstream verbatim on
# Xtensa-ESP-ELF:
#   1. RAM is now sized to fit the full 1 MB 8086 address space
#      (+64 KB margin), placed in PSRAM via EXT_RAM_BSS_ATTR.
#   2. char-signedness independence — every signed-8-bit cast is now
#      written with (int8_t), so no -fsigned-char compiler crutch.
#   3. SDL/conio paths kept under their existing #ifdef guards;
#      EMU_LIBRARY_MODE excludes the standalone main().

idf_component_register(
    SRCS
        "esp8086.c"
        "esp8086_facade.c"
    INCLUDE_DIRS "include"
    REQUIRES log bios esp_psram
    EMBED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/8086tiny/bios.bin"
)

# Library mode (no standalone main), no SDL graphics path.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    EMU_LIBRARY_MODE=1
    NO_GRAPHICS=1
)

# 8086tiny's compact decoder uses comma-operator chains, fall-through
# switches, and operator-precedence shortcuts that ESP-IDF's -Werror
# rejects. These are intentional in the upstream design (Adrian Cable
# went for minimum source size), not bugs. Relax just for this
# component. Note: NO -fsigned-char — esp8086.c is signedness-agnostic
# by construction.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=unused-value
    -Wno-error=parentheses
    -Wno-error=implicit-fallthrough
    -Wno-error=logical-not-parentheses
    -Wno-unused-value
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-logical-not-parentheses
)
