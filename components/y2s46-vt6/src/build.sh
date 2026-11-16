#!/bin/bash
# How the two files of this directory's payload are built (y2s42; the recipe of y2s38-y2s41, written down).  Nothing in the shared Mesa
# build tree (gpu/build/y2s-stage0/build-nord2) is written: its own compile and link commands are taken from ninja and run with the source,
# the object and the output replaced by ours.
#   bash build.sh SRC OUT        SRC: a directory with air_metal.c (+ air_isp.c, air_metal.h), air_to_nir.c, air_intrinsics.c, redwooddrawable.m
#                                OUT: a new directory; gets obj/ and payload/Nord2Metal.bundle/{libredwoodair.dylib,Nord2Metal}
# The control of every delivery: SRC = the installed version's src must give the installed pair again (y2s45-vt5: c2ea609e..., bec01874...).
set -euo pipefail
W=$NORD2_WORK
B=$W/gpu/build; BN=$B/y2s-stage0/build-nord2; SRC=$(cd "$1" && pwd); mkdir -p "$2"; OUT=$(cd "$2" && pwd)
mkdir -p $OUT/obj/air $OUT/payload/Nord2Metal.bundle
cd $BN
[ "$(ninja -n src/gallium/drivers/panfrost/libredwoodair.dylib | tail -1)" = "ninja: no work to do." ]      # the tree is what it was
edit() {  # edit TARGET [-c SOURCE] -o OUTPUT [extra include]: ninja's command for TARGET with its source and output replaced
  python3 -I - "$@" <<'PY'
import subprocess, sys, shlex
target, args = sys.argv[1], sys.argv[2:]
cmd = shlex.split(subprocess.run(['ninja', '-t', 'commands', target], capture_output=True, text=True, check=True).stdout.strip().splitlines()[-1])
out, i = [], 0
src = args[args.index('-c') + 1] if '-c' in args else None
dst = args[args.index('-o') + 1]
inc = args[args.index('-I') + 1] if '-I' in args else None
while i < len(cmd):
    t = cmd[i]
    if t == '-MD': i += 1; continue
    if t in ('-MQ', '-MF'): i += 2; continue
    if t == '-o': out += ['-o', dst]; i += 2; continue
    if t == '-c' and src: out += ['-c', src]; i += 2; continue
    out.append(t); i += 1
if inc: out.insert(1, '-I' + inc)
for a, b in zip(args, args[1:]):
    if a == '-s':                       # -s OLD=NEW: replace one whole token
        old, new = b.split('=', 1)
        assert out.count(old) == 1, old
        out[out.index(old)] = new
if '-x' in args: out += shlex.split(args[args.index('-x') + 1])
assert not any(t.startswith('src/') and t.endswith('.dylib') for t in out[out.index('-o'):out.index('-o') + 2])
print(shlex.join(out))
PY
}
AM='src/gallium/drivers/panfrost/libredwoodair.dylib.p/.._.._.._compiler_air_air_metal.c.o'
eval "$(edit "$AM" -c $SRC/air_metal.c -o $OUT/obj/air_metal.o)"
for f in air_to_nir air_intrinsics; do
  eval "$(edit src/compiler/air/libair.a.p/$f.c.o -c $SRC/$f.c -o $OUT/obj/air/$f.c.o -I ../mesa-25.0.7/src/compiler/air)"
done
cp src/compiler/air/libair.a $OUT/obj/libair.a
llvm-ar r $OUT/obj/libair.a $OUT/obj/air/air_to_nir.c.o $OUT/obj/air/air_intrinsics.c.o
# (the output must be NAMED libredwoodair.dylib: the ad-hoc signature carries the name)
eval "$(edit src/gallium/drivers/panfrost/libredwoodair.dylib -o $OUT/payload/Nord2Metal.bundle/libredwoodair.dylib \
        -s "$AM=$OUT/obj/air_metal.o" -s src/compiler/air/libair.a=$OUT/obj/libair.a -x -Wl,-adhoc_codesign)"
chmod 644 $OUT/payload/Nord2Metal.bundle/libredwoodair.dylib
# Nord2Metal: redwooddrawable.m of SRC, the other four objects as they have been since y2s33/y2s37/y2s38
SDK=$B/apple-toolchain/sdks/iPhoneOS16.5.sdk
clang -target arm64-apple-ios16.0 -isysroot $SDK -O1 -Wno-deprecated-declarations -DRedwoodMetalDevice=TeguComputeDevice \
  -include $W/gpu/y2s/nord2_rw_compat.h -I$B/y2s33/src -c $SRC/redwooddrawable.m -o $OUT/obj/redwooddrawable.o
clang -target arm64-apple-ios16.0 -isysroot $SDK -fuse-ld=lld -bundle -Wl,-adhoc_codesign $B/y2s38-gpuycbcr/obj/redwoodmetal.o \
  $OUT/obj/redwooddrawable.o $B/y2s33/obj/nord2_srcprobe.o $B/y2s33/obj/nord2_fscache.o $B/y2s37-ycbcr/obj/nord2_ycbcr.o \
  -framework Foundation -framework IOKit -framework Metal -lobjc -o $OUT/payload/Nord2Metal.bundle/Nord2Metal
chmod 755 $OUT/payload/Nord2Metal.bundle/Nord2Metal
sha256sum $OUT/payload/Nord2Metal.bundle/*
