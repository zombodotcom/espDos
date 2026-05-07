idf_component_register(
    SRCS "espdos.c"
    INCLUDE_DIRS "."
    REQUIRES esp8086 kernel_blob bios
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

# Optional: load HELLO.COM (sector 11) via bootstub instead of the
# default MANDEL.COM (sector 12). Picks the bootstub-hello variant so
# the kernel's transient loader reads cluster 2 not cluster 3.
#   idf.py build -DESPDOS_LOADER_HELLO=1
if(ESPDOS_LOADER_HELLO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_HELLO=1)
    message(STATUS "espdos: building with HELLO.COM loader "
                   "(default is MANDEL.COM)")
endif()

# Optional: load COUNT.COM (sector 13) — counts 1..50 in decimal.
#   idf.py build -DESPDOS_LOADER_COUNT=1
if(ESPDOS_LOADER_COUNT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_COUNT=1)
    message(STATUS "espdos: building with COUNT.COM loader "
                   "(default is MANDEL.COM)")
endif()

# Optional: load SHELL.COM (sector 14) — interactive program selector.
# Reads a digit (1=HELLO, 2=COUNT, 3=MANDEL) and dispatches.
#   idf.py build -DESPDOS_LOADER_SHELL=1
if(ESPDOS_LOADER_SHELL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPDOS_LOADER_SHELL=1)
    message(STATUS "espdos: building with SHELL.COM loader "
                   "(default is MANDEL.COM)")
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
