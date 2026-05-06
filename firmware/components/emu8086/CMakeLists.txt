# emu8086 — Adrian Cable's 8086tiny adapted as an ESP-IDF library
# component. The vendored 8086tiny.c is included unmodified except for a
# few `/* PATCH (...) */`-tagged additions: ESP-IDF-stub for <sys/timeb.h>,
# MinGW-only includes for kbhit/getch (skipped on Xtensa), and a
# #ifndef EMU_LIBRARY_MODE guard around its standalone main() so the
# function decoder + executor are usable as a library.
#
# Plan 2a: this component compiles and links into the firmware. The
# emulator itself is not yet driven from app_main (see emu8086.h).
# Plan 2b: extract emu_init() and emu_step() from 8086tiny's main()
# loop body so we can drive instruction execution per-step.

idf_component_register(
    SRCS
        "8086tiny.c"
        "emu8086.c"
    INCLUDE_DIRS "include"
    REQUIRES log bios
    EMBED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/8086tiny/bios.bin"
)

# Tell 8086tiny.c to omit its standalone main() so it can be used as a
# library, and to skip its SDL graphics path.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    EMU_LIBRARY_MODE=1
    NO_GRAPHICS=1
)

# 8086tiny is heavy on comma-operator chains, fall-through switches,
# and operator-precedence shortcuts that ESP-IDF's -Werror rejects.
# These are intentional in the upstream design (Adrian Cable went for
# minimum source size), not bugs. Relax just for this component.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=unused-value
    -Wno-error=parentheses
    -Wno-error=implicit-fallthrough
    -Wno-error=logical-not-parentheses
    -Wno-unused-value
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-logical-not-parentheses
    # 8086tiny depends on `char` being signed (sign-extension in some
    # of its compact opcode-decoding expressions). x86 GCC defaults to
    # signed char and the host test passes; Xtensa-ESP-ELF GCC defaults
    # to unsigned char and the same .bin only printed 'e' before walking
    # off into zero memory. Reproduced on host by adding -funsigned-char,
    # fixed by -fsigned-char. Keeps this component aligned with upstream
    # 8086tiny's assumed environment.
    -fsigned-char
)
