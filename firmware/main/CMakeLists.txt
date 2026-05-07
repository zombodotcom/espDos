idf_component_register(
    SRCS "espdos.c"
    INCLUDE_DIRS "."
    REQUIRES esp8086 kernel_blob bios display
)

# Optional: build with the confidence-harness payload (asm/hello.bin)
# instead of the real 86-DOS kernel.
#   idf.py build -DESPDOS_PAYLOAD_HELLO=1
# Then `idf.py flash monitor` runs hello.bin on the device and you
# should see deterministic output matching the host test_hello run.
if(ESPDOS_PAYLOAD_HELLO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_PAYLOAD_HELLO=1)
    message(STATUS "espdos: building with hello.bin as payload "
                   "(confidence harness)")
endif()

# Default: SHELL.COM — interactive menu, dispatches to HELLO/COUNT/
# MANDEL/JULIA/LIFE based on a single keystroke. The flags below opt
# OUT of the menu and boot directly into one program (skip-the-menu
# is useful for QEMU runs and demo recordings).

# Boot directly into HELLO.COM (sector 11): banner box.
#   idf.py build -DESPDOS_LOADER_HELLO=1
if(ESPDOS_LOADER_HELLO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_HELLO=1)
    message(STATUS "espdos: building with HELLO.COM loader (skips SHELL menu)")
endif()

# Boot directly into COUNT.COM (sector 13): counts 1..50.
#   idf.py build -DESPDOS_LOADER_COUNT=1
if(ESPDOS_LOADER_COUNT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_COUNT=1)
    message(STATUS "espdos: building with COUNT.COM loader (skips SHELL menu)")
endif()

# Boot directly into MANDEL.COM (sector 12): Q4.12 fixed-point ASCII Mandelbrot.
#   idf.py build -DESPDOS_LOADER_MANDEL=1
if(ESPDOS_LOADER_MANDEL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_MANDEL=1)
    message(STATUS "espdos: building with MANDEL.COM loader (skips SHELL menu)")
endif()

# Boot directly into SHELL.COM (sector 14). This is the default already;
# the flag exists for explicit/QEMU invocations and back-compat.
#   idf.py build -DESPDOS_LOADER_SHELL=1
if(ESPDOS_LOADER_SHELL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_SHELL=1)
    message(STATUS "espdos: SHELL.COM loader (default; flag is a no-op alias)")
endif()

# Boot directly into JULIA.COM (sectors 15..17): animated 16-color Julia set.
#   idf.py build -DESPDOS_LOADER_JULIA=1
if(ESPDOS_LOADER_JULIA)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_JULIA=1)
    message(STATUS "espdos: building with JULIA.COM loader (skips SHELL menu)")
endif()

# Boot directly into LIFE.COM (sectors 18..19): Conway's Life with color cycling.
#   idf.py build -DESPDOS_LOADER_LIFE=1
if(ESPDOS_LOADER_LIFE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_LIFE=1)
    message(STATUS "espdos: building with LIFE.COM loader (skips SHELL menu)")
endif()

# Optional: pre-feed an extra keystroke to BIOSIN after the date.
# Used by SHELL.COM in QEMU mode (no interactive terminal); pass
# the digit as the value, e.g. -DESPDOS_AUTOPICK=3 to launch MANDEL.
if(DEFINED ESPDOS_AUTOPICK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ESPDOS_AUTOPICK=${ESPDOS_AUTOPICK})
    message(STATUS "espdos: BIOSIN will auto-feed '${ESPDOS_AUTOPICK}' "
                   "after the date prompt")
endif()
