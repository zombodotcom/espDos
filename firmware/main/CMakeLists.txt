idf_component_register(
    SRCS "espdos.c"
    INCLUDE_DIRS "."
    REQUIRES emu8086 kernel_blob bios
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
