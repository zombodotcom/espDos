VENDOR := vendor/msdos-1.0/c
KERNEL_SRCS := \
    $(VENDOR)/src/fat.c \
    $(VENDOR)/src/disk.c \
    $(VENDOR)/src/directory.c \
    $(VENDOR)/src/file.c \
    $(VENDOR)/src/io.c \
    $(VENDOR)/src/console.c \
    $(VENDOR)/src/fcb_util.c \
    $(VENDOR)/src/syscall.c \
    $(VENDOR)/src/init.c

HOST_SRC := host/main.c

CFLAGS := -std=c99 -Wall -I$(VENDOR)/include -g

all: host_smoke.exe

host_smoke.exe: $(KERNEL_SRCS) $(HOST_SRC)
	gcc $(CFLAGS) $^ -o $@

clean:
	rm -f host_smoke.exe

.PHONY: all clean
