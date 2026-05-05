HOST_DIR     := host
BUILD_DIR    := build
THIRDPARTY   := third_party
ASM_DIR      := asm

CFLAGS := -std=c99 -Wall -Wextra -I$(HOST_DIR) -I$(THIRDPARTY)/8086tiny -g

# Listed individually so the public surface is explicit.
HOST_SRCS := \
    $(HOST_DIR)/main.c

# Emulator + BIOS sources land here as Tasks 3-6 add them.
EMU_SRCS  :=
BIOS_SRCS :=

ALL_SRCS  := $(HOST_SRCS) $(EMU_SRCS) $(BIOS_SRCS)

all: $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/dos_host.exe

$(BUILD_DIR)/kernel.bin: | $(BUILD_DIR)
	$(ASM_DIR)/build_kernel.sh

$(BUILD_DIR)/dos_host.exe: $(ALL_SRCS) | $(BUILD_DIR)
	gcc $(CFLAGS) $^ -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
