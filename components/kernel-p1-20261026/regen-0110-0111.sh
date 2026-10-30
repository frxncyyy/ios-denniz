#!/bin/bash
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0110.patch s109 s110a bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c iokit/Kernel/arm/Nord2GPU.cpp
gen 0111.patch s110a s110 bsd/dev/nord2_gpu_transport.h iokit/Kernel/arm/Nord2GPU.cpp
gen cand0110.patch c108 c110a gpu/include/nord2_gpu_core.h gpu/src/nord2_gpu_core.c gpu/src/Nord2GPU.cpp shim/nord2-xnu/nord2_gpu_core.h shim/nord2-xnu/nord2_gpu_core.c shim/nord2-xnu/Nord2GPU.cpp gpu/tests/nord2_core_test.c
gen cand0111.patch c110a c110 gpu/include/nord2_gpu_transport.h gpu/src/Nord2GPU.cpp shim/nord2-xnu/nord2_gpu_transport.h shim/nord2-xnu/Nord2GPU.cpp gpu/tests/nord2_trap_abi_test.c
rm -rf v16; cp -r s109 v16; (cd v16 && patch -p1 -s < $S/0110.patch && patch -p1 -s < $S/0111.patch)
for f in bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c bsd/dev/nord2_gpu_transport.h iokit/Kernel/arm/Nord2GPU.cpp; do cmp v16/$f s110/$f; done
rm -rf vc16; cp -r c108 vc16; (cd vc16 && patch -p1 -s < $S/cand0110.patch && patch -p1 -s < $S/cand0111.patch)
for f in gpu/include/nord2_gpu_core.h gpu/include/nord2_gpu_transport.h gpu/src/nord2_gpu_core.c gpu/src/Nord2GPU.cpp shim/nord2-xnu/Nord2GPU.cpp gpu/tests/nord2_core_test.c gpu/tests/nord2_trap_abi_test.c; do cmp vc16/$f c110/$f; done
cmp c110/gpu/src/Nord2GPU.cpp s110/iokit/Kernel/arm/Nord2GPU.cpp
cp 0110.patch $K/0110-scoped-invalidate-by-write-mask-n2wm.patch; cp 0111.patch $K/0111-iokit-trap-fast-path.patch
cp cand0110.patch $K/candidate-gpu-source-0110.patch; cp cand0111.patch $K/candidate-gpu-source-0111.patch
for n in 0110 0111; do rm -rf $K/patched-sources-$n; mkdir -p $K/patched-sources-$n/bsd/dev $K/patched-sources-$n/iokit/Kernel/arm; done
cp s110a/bsd/dev/nord2_gpu_core.h s110a/bsd/dev/nord2_gpu_core.c $K/patched-sources-0110/bsd/dev/; cp s110a/iokit/Kernel/arm/Nord2GPU.cpp $K/patched-sources-0110/iokit/Kernel/arm/
cp s110/bsd/dev/nord2_gpu_transport.h $K/patched-sources-0111/bsd/dev/; cp s110/iokit/Kernel/arm/Nord2GPU.cpp $K/patched-sources-0111/iokit/Kernel/arm/
cp c110/gpu/tests/nord2_core_test.c $K/tests-new/nord2_core_test-0110.c; cp c110/gpu/tests/nord2_trap_abi_test.c $K/tests-new/nord2_trap_abi_test.c
cp regen12.sh $K/regen-0110-0111.sh
sha256sum $K/0110-*.patch $K/0111-*.patch $K/candidate-gpu-source-0110.patch $K/candidate-gpu-source-0111.patch | cut -c1-16,65-
