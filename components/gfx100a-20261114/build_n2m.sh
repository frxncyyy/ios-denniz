#!/bin/bash
# gfx100a: Nord2Metal ONLY, by the exact commands of y2s99b-shaders/merged-src/build.sh (the library is not rebuilt: the phone keeps
# shader99b's libredwoodair.dylib 7c0c5aba). Control: SRC = y2s99b merged-src must give fe4ecc9f (shader99b's Nord2Metal).
#   bash build_n2m.sh SRC OUT
set -euo pipefail
W=$NORD2_WORK
B=$W/gpu/build; SRC=$(cd "$1" && pwd); mkdir -p "$2"; OUT=$(cd "$2" && pwd)
mkdir -p $OUT/obj $OUT/payload/Nord2Metal.bundle
SDK=$B/apple-toolchain/sdks/iPhoneOS16.5.sdk
clang -target arm64-apple-ios16.0 -isysroot $SDK -O1 -Wno-deprecated-declarations -DRedwoodMetalDevice=TeguComputeDevice \
  -include $W/gpu/y2s/nord2_rw_compat.h -I$B/y2s33/src -c $SRC/redwooddrawable.m -o $OUT/obj/redwooddrawable.o 2> $OUT/obj/redwooddrawable.err
clang -target arm64-apple-ios16.0 -isysroot $SDK -O1 -Wno-deprecated-declarations -DRedwoodMetalDevice=TeguComputeDevice \
  -include $W/gpu/y2s/nord2_rw_compat.h -I$B/y2s33/src -c $SRC/redwoodmetal.m -o $OUT/obj/redwoodmetal.o 2> $OUT/obj/redwoodmetal.err
clang -target arm64-apple-ios16.0 -isysroot $SDK -fuse-ld=lld -bundle -Wl,-adhoc_codesign $OUT/obj/redwoodmetal.o \
  $OUT/obj/redwooddrawable.o $B/y2s33/obj/nord2_srcprobe.o $B/y2s33/obj/nord2_fscache.o $B/y2s37-ycbcr/obj/nord2_ycbcr.o \
  -framework Foundation -framework IOKit -framework Metal -lobjc -o $OUT/payload/Nord2Metal.bundle/Nord2Metal
chmod 755 $OUT/payload/Nord2Metal.bundle/Nord2Metal
sha256sum $OUT/payload/Nord2Metal.bundle/Nord2Metal
grep -c warning $OUT/obj/redwoodmetal.err || true
