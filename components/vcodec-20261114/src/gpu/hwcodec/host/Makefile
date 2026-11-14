# SPDX-License-Identifier: MIT
# gpu/hwcodec/host/Makefile -- the track V/T source set, the runner and the library.
#
# Everything is built under $(OUT) (default /tmp/n2hw-iv); nothing is written into the work tree.
#
#   make            sanitizer (-O1, ASan+UBSan) and -O2 runner + both libn2vhost.a
#   make san / o2 / tsan   one host configuration
#   make lib        the two static libraries (what the daemon, package IP, links)
#   make ios        compile check for arm64-apple-ios16.0 (objects only, $(OUT)/ios)
#   make test       runs host/test_iv/run_tests.sh
#   make clean
#
# A package source that is not in the tree yet is simply not compiled; n2vhost's weak shim-table fallbacks
# keep the link working so the runner can report the first missing piece.  Every source that *is* present
# is compiled with the project flags and -Werror, so a package that does not compile fails this build (a
# finding to report, not a reason to weaken the flags).

HWD  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
OUT  ?= /tmp/n2hw-iv
ifeq ($(origin CC),default)
CC := clang
endif
NICE := nice -n 10
JOBS ?= 2

INCLUDES := -I$(HWD) -I$(HWD)/include -I$(HWD)/tests/hw -I$(HWD)/tests/emu -I$(HWD)/vcu -I$(HWD)/ap \
            -I$(HWD)/fakevenc
WARN     := -Wall -Wextra -Werror
# POSIX declarations (sigaction in V02's core, clock_gettime in the shims) need these with -std=c11.
FEATURES := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
SANFLAGS := -std=c11 -O1 -g $(WARN) $(FEATURES) -fsanitize=address,undefined -fno-sanitize-recover=all
O2FLAGS  := -std=c11 -O2 -g $(WARN) $(FEATURES)
# Threaded code (guest threads + the AP drive): a TSan configuration of the same sources.  V02's
# known non-atomic stop_request read (V32 F8) can be reported at teardown; that finding is V02's.
TSANFLAGS := -std=c11 -O1 -g $(WARN) $(FEATURES) -fsanitize=thread
# V02's mem.c trips -Wtautological-constant-out-of-range-compare on some clang versions (recorded by T01);
# a host build detail, applied to every source because the flag is valid everywhere.
EXTRA_WARN := -Wno-tautological-constant-out-of-range-compare

EMU_SRCS    := $(sort $(wildcard $(HWD)/emu/n2a32_*.c))
LOADER_SRCS := $(sort $(wildcard $(HWD)/loader/n2elf.c) $(wildcard $(HWD)/loader/n2elf_reloc.c))
SHIM_SRCS   := $(sort $(wildcard $(HWD)/shim/n2shim_*.c) $(wildcard $(HWD)/shim/n2vfs.c))
HW_SRCS     := $(sort $(wildcard $(HWD)/tests/hw/n2vc_hw_fake.c) $(wildcard $(HWD)/fakevenc/n2venc_fake.c))
VCU_SRCS    := $(sort $(wildcard $(HWD)/vcu/n2vcu.c))
AP_SRCS     := $(sort $(wildcard $(HWD)/ap/n2ap_venc.c) $(wildcard $(HWD)/ap/n2vc_dma.c))
HOST_SRCS   := $(HWD)/host/n2vhost.c

# V40 is enabled only when its header, its source and the n2vfs it registers into are all present:
# n2vhost.c then includes the real header instead of guessing a prototype.  V41 likewise.
VCU_OK := $(if $(and $(wildcard $(HWD)/vcu/n2vcu.h),$(wildcard $(HWD)/vcu/n2vcu.c),$(wildcard $(HWD)/shim/n2vfs.c)),1,)
AP_OK  := $(if $(and $(wildcard $(HWD)/ap/n2ap_venc.h),$(wildcard $(HWD)/ap/n2ap_venc.c)),1,)
FAKEVENC_OK := $(if $(and $(wildcard $(HWD)/fakevenc/n2venc_fake.h),$(wildcard $(HWD)/fakevenc/n2venc_fake.c)),1,)
DEFS   := $(if $(VCU_OK),-DN2VHOST_HAVE_VCU=1,) $(if $(AP_OK),-DN2VHOST_HAVE_AP=1,) \
          $(if $(FAKEVENC_OK),-DN2VHOST_HAVE_FAKEVENC=1,)

PKG_SRCS   := $(EMU_SRCS) $(LOADER_SRCS) $(SHIM_SRCS) $(HW_SRCS) $(VCU_SRCS) $(AP_SRCS)
# iOS compile check (objects only).  Two toolchains are tried, best first:
#   1. the real iPhoneOS SDK the other hwcodec packages use (N2_IOS_SDK, default
#      gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk) with -isysroot;
#   2. gpu/video/build_ios.sh's freestanding toolchain (-nostdinc + clang resource headers + the hand-written
#      gpu/video/sdk/sysinc).  That sysinc covers the plug-in's small libc surface, not the POSIX headers these
#      packages use (setjmp, sys/mman, fenv, sched, ...), so it is the fallback, not the default.
IOS_TARGET := arm64-apple-ios16.0
IOS_ROOT   := $(abspath $(HWD)/../..)
IOS_SDK    := $(if $(N2_IOS_SDK),$(N2_IOS_SDK),$(IOS_ROOT)/gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk)
IOS_RES    := $(shell $(CC) -print-resource-dir)/include
IOS_SYSINC := $(HWD)/../video/sdk/sysinc
IOS_INC    := -I$(HWD) -I$(HWD)/include -I$(HWD)/tests/hw -I$(HWD)/tests/emu -I$(HWD)/vcu -I$(HWD)/ap \
              -I$(HWD)/fakevenc \
              $(if $(VCU_OK),-DN2VHOST_HAVE_VCU=1,) $(if $(AP_OK),-DN2VHOST_HAVE_AP=1,) \
              $(if $(FAKEVENC_OK),-DN2VHOST_HAVE_FAKEVENC=1,)
IOS_DEFS   := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE
ifeq ($(wildcard $(IOS_SDK)/usr/include),)
IOS_CFLAGS := --target=$(IOS_TARGET) -std=c11 -O1 -g $(WARN) $(IOS_DEFS) -nostdinc \
              -isystem $(IOS_RES) -isystem $(IOS_SYSINC) $(IOS_INC)
IOS_MODE   := sysinc (no SDK at $(IOS_SDK))
else
IOS_CFLAGS := --target=$(IOS_TARGET) -isysroot $(IOS_SDK) -std=c11 -O1 -g $(WARN) $(IOS_DEFS) $(IOS_INC)
IOS_MODE   := SDK $(notdir $(IOS_SDK))
endif

FLAGS_san := $(SANFLAGS)
FLAGS_o2  := $(O2FLAGS)
FLAGS_tsan := $(TSANFLAGS)

.PHONY: all san o2 tsan lib ios test clean list
# The runner is a prerequisite of libn2vhost.a and also a goal of `make all`; without this make treats it as
# an intermediate file and deletes the san/o2 runner after the library is archived (seen with -j2).
.PRECIOUS: $(OUT)/%/n2vhost
all: san o2 lib

# ------------------------------------------------------------------------------------------- information
list:
	@echo "package sources: $(words $(PKG_SRCS)), host: 1"
	@echo "emu:    $(notdir $(EMU_SRCS))"
	@echo "loader: $(notdir $(LOADER_SRCS))"
	@echo "shim:   $(notdir $(SHIM_SRCS))"
	@echo "hw:     $(notdir $(HW_SRCS))"
	@echo "vcu:    $(notdir $(VCU_SRCS)) enable=$(if $(VCU_OK),yes,no)"
	@echo "ap:     $(notdir $(AP_SRCS)) enable=$(if $(AP_OK),yes,no)"

# ---------------------------------------------------------------------------------------------- host build
san o2 tsan:
	@$(MAKE) --no-print-directory OUT=$(OUT) $(OUT)/$@/n2vhost $(OUT)/$@/libn2vhost.a

$(OUT)/%/n2vhost: $(HOST_SRCS) $(PKG_SRCS)
	@mkdir -p $(OUT)/$*/obj
	@rm -f $(OUT)/$*/obj/*.failed
	@set -e; n=0; run=0; \
	for f in $(PKG_SRCS); do \
	  b=$$(basename $$f .c); \
	  if [ -f $(OUT)/$*/obj/$$b.o ] && [ $(OUT)/$*/obj/$$b.o -nt $$f ]; then \
	    n=$$((n+1)); \
	  else \
	  ( $(NICE) $(CC) $(FLAGS_$*) $(DEFS) $(INCLUDES) $(EXTRA_WARN) -c $$f -o $(OUT)/$*/obj/$$b.o \
	      2> $(OUT)/$*/obj/$$b.log \
	    || { echo "N2VHOST build: $$f FAILED (see $(OUT)/$*/obj/$$b.log)"; \
	         head -3 $(OUT)/$*/obj/$$b.log; : > $(OUT)/$*/obj/$$b.failed; } ) & \
	  run=$$((run+1)); \
	  if [ $$run -ge $(JOBS) ]; then wait -n || true; run=$$((run-1)); fi; \
	  fi; \
	done; wait; \
	if [ -f $(OUT)/$*/obj/n2vhost.o ] && [ $(OUT)/$*/obj/n2vhost.o -nt $(HOST_SRCS) ]; then :; \
	elif ! $(NICE) $(CC) $(FLAGS_$*) $(DEFS) $(INCLUDES) $(EXTRA_WARN) -c $(HOST_SRCS) \
	      -o $(OUT)/$*/obj/n2vhost.o 2> $(OUT)/$*/obj/n2vhost.log; then \
	  echo "N2VHOST build: $(HOST_SRCS) FAILED (see $(OUT)/$*/obj/n2vhost.log)"; \
	  head -3 $(OUT)/$*/obj/n2vhost.log; exit 1; \
	fi; \
	bad=$$(ls $(OUT)/$*/obj/*.failed 2>/dev/null | wc -l); objs=$$(ls $(OUT)/$*/obj/*.o | wc -l); \
	echo "N2VHOST build: $* $$objs/$$(( $(words $(PKG_SRCS)) + 1 )) objects, $$bad package source(s) failed"; \
	[ $$bad = 0 ] || echo "N2VHOST build: package objects missing, the runner reports them"; \
	$(NICE) $(CC) $(FLAGS_$*) $(DEFS) $(OUT)/$*/obj/*.o -pthread -lm -o $(OUT)/$*/n2vhost

# The static library the daemon links: the objects of the same configuration (already built for the
# runner) plus a n2vhost.o compiled without the runner's main.  The fake VENC model is test-only and is
# filtered out (the daemon must never carry it; n2vhost.o with -DN2VHOST_NO_MAIN does not reference it).
$(OUT)/%/libn2vhost.a: $(OUT)/%/n2vhost
	@mkdir -p $(OUT)/$*/lib
	@$(NICE) $(CC) $(FLAGS_$*) $(DEFS) $(INCLUDES) $(EXTRA_WARN) -DN2VHOST_NO_MAIN -c $(HOST_SRCS) \
	  -o $(OUT)/$*/lib/n2vhost.o
	@rm -f $@ && ar rcs $@ $(filter-out $(OUT)/$*/obj/n2vhost.o $(OUT)/$*/obj/n2venc_fake.o,$(wildcard $(OUT)/$*/obj/*.o)) $(OUT)/$*/lib/n2vhost.o

lib: $(OUT)/san/libn2vhost.a $(OUT)/o2/libn2vhost.a

# ------------------------------------------------------------------------------------------------ iOS check
ios:
	@mkdir -p $(OUT)/ios
	@echo "N2VHOST ios: toolchain $(IOS_MODE)"
	@set -e; n=0; bad=0; \
	for f in $(PKG_SRCS) $(HOST_SRCS); do \
	  b=$$(basename $$f .c); \
	  [ "$$b" = n2venc_fake ] && continue; \
	  extra=""; \
	  [ "$$b" = n2vhost ] && extra="-DN2VHOST_NO_MAIN"; \
	  if $(NICE) $(CC) $(IOS_CFLAGS) $(EXTRA_WARN) $$extra -c $$f -o $(OUT)/ios/$$b.o 2> $(OUT)/ios/$$b.log; then \
	    n=$$((n+1)); \
	  else \
	    bad=$$((bad+1)); echo "N2VHOST ios: $$f FAILED (see $(OUT)/ios/$$b.log)"; head -3 $(OUT)/ios/$$b.log; \
	  fi; \
	done; \
	echo "N2VHOST ios: $$n objects compiled for $(IOS_TARGET), $$bad failed (compile check only)"

# The arm64 archive a native daemon links (package IP's build_ios.sh takes it through N2VHOST_IOS_LIB).
# llvm-ar, not GNU ar: the members are Mach-O and ld64 wants the BSD symbol index.
AR ?= llvm-ar
ios-lib: ios
	@rm -f $(OUT)/ios/libn2vhost.a
	@$(AR) rcs $(OUT)/ios/libn2vhost.a $(OUT)/ios/*.o
	@echo "N2VHOST ios-lib: $(OUT)/ios/libn2vhost.a ($$(ls $(OUT)/ios/*.o | wc -l) objects, $(IOS_TARGET))"

test:
	@bash $(HWD)/host/test_iv/run_tests.sh

clean:
	rm -rf $(OUT)/san $(OUT)/o2 $(OUT)/ios
