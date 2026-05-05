# Makefile for the 86-DOS 1.00 Ada translation
#
# All compilation runs from inside build/ so that gnatmake places
# intermediates (.ali, .o) there automatically.  Final test binaries
# also land in build/.
#
# Usage (run from the ada/ directory):
#   make          -- compile all library sources
#   make check    -- build and run unit tests
#   make clean    -- remove all build artefacts

GNATMAKE  = gnatmake
GNATFLAGS = -gnat2012 -Wall -gnatwa

# Absolute path to this Makefile's directory (ada/)
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# -------------------------------------------------------------------
# Library source bodies (absolute paths for use from build/)
# -------------------------------------------------------------------
SRCS := \
	$(HERE)/src/dos86.adb             \
	$(HERE)/src/dos86-fat.adb         \
	$(HERE)/src/dos86-disk.adb        \
	$(HERE)/src/dos86-directory.adb   \
	$(HERE)/src/dos86-file_ops.adb    \
	$(HERE)/src/dos86-io_ops.adb      \
	$(HERE)/src/dos86-console.adb     \
	$(HERE)/src/dos86-fcb_util.adb    \
	$(HERE)/src/dos86-syscall.adb     \
	$(HERE)/src/dos86-init.adb

# -------------------------------------------------------------------
# Test sources
# -------------------------------------------------------------------
TEST_SRCS  := $(wildcard $(HERE)/tests/test_*.adb)
TEST_NAMES := $(patsubst $(HERE)/tests/%.adb, %, $(TEST_SRCS))
TEST_BINS  := $(addprefix $(HERE)/build/, $(TEST_NAMES))

# -------------------------------------------------------------------
# Default target: compile all library sources into build/
# -------------------------------------------------------------------
.PHONY: all
all: $(HERE)/build/.compiled

$(HERE)/build/.compiled: $(SRCS) | $(HERE)/build
	cd $(HERE)/build && \
	  for src in $(SRCS); do \
	    echo "  COMPILE $$src"; \
	    $(GNATMAKE) $(GNATFLAGS) -I$(HERE)/src -c $$src || exit 1; \
	  done
	touch $@

# -------------------------------------------------------------------
# Test binaries
# -------------------------------------------------------------------
.PHONY: check
check: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	    echo "=== $$t ==="; \
	    $$t; \
	done

$(TEST_BINS): $(HERE)/build/%: $(HERE)/tests/%.adb $(HERE)/build/.compiled
	cd $(HERE)/build && \
	  $(GNATMAKE) $(GNATFLAGS) -I$(HERE)/src -I$(HERE)/tests $< -o $@

# -------------------------------------------------------------------
# Build directory
# -------------------------------------------------------------------
$(HERE)/build:
	mkdir -p $(HERE)/build

# -------------------------------------------------------------------
# Clean
# -------------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf $(HERE)/build
