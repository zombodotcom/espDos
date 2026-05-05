# Makefile for the 86-DOS 1.00 C translation
#
# Builds a static library (build/libdos.a) containing all translated
# kernel modules, and a small test harness binary (build/run_tests)
# if tests are present.
#
# Usage (run from the c/ directory):
#   make          -- build the library
#   make check    -- build and run unit tests
#   make clean    -- remove all build artefacts

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic \
          -Wmissing-prototypes -Wstrict-prototypes \
          -I include \
          -g

AR      = ar
ARFLAGS = rcs

# -------------------------------------------------------------------
# Source files
# -------------------------------------------------------------------
SRCS := src/fat.c       \
        src/disk.c      \
        src/directory.c \
        src/file.c      \
        src/io.c        \
        src/console.c   \
        src/fcb_util.c  \
        src/syscall.c   \
        src/init.c

OBJS := $(patsubst src/%.c, build/%.o, $(SRCS))

# -------------------------------------------------------------------
# Test files
# -------------------------------------------------------------------
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c, build/%, $(TEST_SRCS))

# -------------------------------------------------------------------
# Default target
# -------------------------------------------------------------------
.PHONY: all
all: build/libdos.a

# -------------------------------------------------------------------
# Library
# -------------------------------------------------------------------
build/libdos.a: $(OBJS) | build
	$(AR) $(ARFLAGS) $@ $^

# -------------------------------------------------------------------
# Object files
# -------------------------------------------------------------------
build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------------------------------------------------------
# Tests
# -------------------------------------------------------------------
.PHONY: check
check: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	    echo "=== $$t ==="; \
	    $$t; \
	done

$(TEST_BINS): build/%: tests/%.c build/libdos.a | build
	$(CC) $(CFLAGS) $< -L build -ldos -o $@

# -------------------------------------------------------------------
# Build directory
# -------------------------------------------------------------------
build:
	mkdir -p build

# -------------------------------------------------------------------
# Clean
# -------------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf build
