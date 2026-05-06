# Host test harness for the 8086tiny instruction interpreter.
# Iteration loop: edit a test, `mingw32-make`, see pass/fail in <1s.
#
# We compile the SAME 8086tiny.c as the firmware (with the same patches,
# which are gated on ESP_PLATFORM so they only apply in firmware
# builds). Each test_*.c contains a small bytecode program, runs it,
# and asserts emulator state via the public helpers in 8086tiny.c
# (emu_run_n, emu_set_cs_ip, emu_get_cs/ip/ax, etc.).

EMU_SRC := ../../firmware/components/emu8086/8086tiny.c
BIOS    := ../../third_party/8086tiny/bios.bin

CFLAGS := -std=c99 -Wall \
          -DEMU_LIBRARY_MODE=1 \
          -DNO_GRAPHICS=1 \
          -Wno-unused-value -Wno-parentheses -Wno-implicit-fallthrough \
          -Wno-logical-not-parentheses -Wno-unused-but-set-variable \
          -Wno-unused-variable -Wno-unused-function

TESTS := test_jmp_near.exe test_kernel_first_step.exe test_bootstub.exe \
        test_hello.exe test_kernel_banner.exe

all: $(TESTS)

test_jmp_near.exe: test_jmp_near.c $(EMU_SRC) test_helpers.c
	gcc $(CFLAGS) $^ -o $@

test_kernel_first_step.exe: test_kernel_first_step.c $(EMU_SRC) test_helpers.c
	gcc $(CFLAGS) $^ -o $@

test_bootstub.exe: test_bootstub.c $(EMU_SRC) test_helpers.c
	gcc $(CFLAGS) $^ -o $@

test_hello.exe: test_hello.c $(EMU_SRC) test_helpers.c
	gcc $(CFLAGS) $^ -o $@

test_kernel_banner.exe: test_kernel_banner.c $(EMU_SRC) test_helpers.c
	gcc $(CFLAGS) $^ -o $@

run: all
	@echo === test_jmp_near ===
	@./test_jmp_near.exe ; echo "exit=$$?"
	@echo === test_kernel_first_step ===
	@./test_kernel_first_step.exe ; echo "exit=$$?"
	@echo === test_bootstub ===
	@./test_bootstub.exe ; echo "exit=$$?"
	@echo === test_hello ===
	@./test_hello.exe ; echo "exit=$$?"
	@echo === test_kernel_banner ===
	@./test_kernel_banner.exe ; echo "exit=$$?"

clean:
	rm -f *.exe

.PHONY: all run clean
