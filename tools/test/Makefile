# tools/test/Makefile — host build + run of the link-layer unit tests (Phase 2.5).
#
#   make            build and run
#   make clean
#
# Uses the host compiler (cc/clang/gcc), NOT the RISC-V cross toolchain: the
# link layer is pure C with no target dependencies, which is the point.

CC     ?= cc
ROOT   := ../..
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -I$(ROOT)/src -I.

SRCS := $(ROOT)/src/link/link.c \
        $(ROOT)/src/link/link_control.c \
        $(ROOT)/src/util/crc.c \
        mock_engine.c \
        test_link.c

.PHONY: all test clean isolation proto-sync
all: test

test: isolation proto-sync run_tests
	./run_tests

run_tests: $(SRCS) $(wildcard *.h) $(wildcard $(ROOT)/src/link/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

# ROADMAP 2.5 acceptance: link/ must not depend on transport/ or usb/.
isolation:
	@if grep -REn '#[ \t]*include[ \t]+["<](transport|usb)/' $(ROOT)/src/link/ ; then \
	    echo "FAIL: src/link/ includes transport/ or usb/"; exit 1; \
	fi
	@echo "isolation: src/link/ is transport-agnostic"

# src/link/proto.h and tools/proto/proto.h must stay byte-identical.
proto-sync:
	@if ! diff -u $(ROOT)/src/link/proto.h $(ROOT)/tools/proto/proto.h ; then \
	    echo "FAIL: src/link/proto.h and tools/proto/proto.h differ"; exit 1; \
	fi
	@echo "proto-sync: proto.h copies identical"

clean:
	rm -rf run_tests run_tests.dSYM
