# SPDX-License-Identifier: MIT
# build.mk -- the interpreter core's source list and flags.  V02.
#
# Usage from another package's Makefile (any directory):
#
#     include ../../build.mk
#     cc = $(CC) $(N2A32_CFLAGS)
#     prog: $(N2A32_CORE_SRCS) my_class.c
#
# or from a shell test script:
#
#     . ../../emu/build.mk.sh        # not provided; parse the two variables instead
#
# The variables are absolute where it matters, so the including Makefile can live anywhere.
# N2A32_CORE_SRCS: the core, memory, exclusive monitor and weak class stubs.
# N2A32_CFLAGS:    C11, warnings, the frozen include directory.  Add -O1/-O2/-g and sanitizers yourself.
# N2A32_LDLIBS:    -pthread (host-call registry, exclusive monitor, signal install).
# N2A32_INCLUDE:   the directory that holds n2a32.h.

N2A32_EMU_DIR       := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
N2A32_HWCODEC_DIR   := $(abspath $(N2A32_EMU_DIR)/..)
N2A32_INCLUDE       := $(N2A32_HWCODEC_DIR)/include
N2A32_CORE_SRCS     := $(N2A32_EMU_DIR)/n2a32_core.c \
                       $(N2A32_EMU_DIR)/n2a32_mem.c \
                       $(N2A32_EMU_DIR)/n2a32_excl.c \
                       $(N2A32_EMU_DIR)/n2a32_stub_classes.c
N2A32_CFLAGS        := -std=c11 -g -Wall -Wextra -Werror -I$(N2A32_INCLUDE)
N2A32_LDLIBS        := -pthread

# Sanitizer presets used by the packages' run_tests.sh (keep the optimisation levels the rules ask for):
N2A32_SAN_FLAGS     := -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
N2A32_OPT_FLAGS     := -O2
N2A32_TSAN_FLAGS    := -O1 -fsanitize=thread -fno-omit-frame-pointer
