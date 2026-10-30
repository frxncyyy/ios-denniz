#!/bin/bash
# Regenerates patch 0112 (display-surface reservations) from the scratch trees s110/c110 (P13 state) and s112/c112 (P13 + 0112).
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0112.patch s110 s112 bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c bsd/dev/nord2_gpu_transport.h iokit/Kernel/arm/Nord2GPU.cpp iokit/Kernel/arm/SweetSurface.h iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/SweetFramebuffer.cpp
gen cand0112.patch c110 c112 gpu/include/nord2_gpu_core.h gpu/src/nord2_gpu_core.c gpu/include/nord2_gpu_transport.h gpu/src/Nord2GPU.cpp shim/nord2-xnu/nord2_gpu_core.h shim/nord2-xnu/nord2_gpu_core.c shim/nord2-xnu/nord2_gpu_transport.h shim/nord2-xnu/Nord2GPU.cpp gpu/tests/nord2_core_test.c gpu/tests/nord2_transport_test.c gpu/tests/nord2_trap_abi_test.c
# the patches must reproduce the trees exactly
rm -rf v17; cp -r s110 v17; (cd v17 && patch -p1 -s < $S/0112.patch)
for f in bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c bsd/dev/nord2_gpu_transport.h iokit/Kernel/arm/Nord2GPU.cpp iokit/Kernel/arm/SweetSurface.h iokit/Kernel/arm/SweetSurface.cpp iokit/Kernel/arm/SweetFramebuffer.cpp; do cmp v17/$f s112/$f; done
rm -rf vc17; cp -r c110 vc17; (cd vc17 && patch -p1 -s < $S/cand0112.patch)
for f in gpu/include/nord2_gpu_core.h gpu/include/nord2_gpu_transport.h gpu/src/nord2_gpu_core.c gpu/src/Nord2GPU.cpp shim/nord2-xnu/Nord2GPU.cpp shim/nord2-xnu/nord2_gpu_core.c shim/nord2-xnu/nord2_gpu_core.h shim/nord2-xnu/nord2_gpu_transport.h gpu/tests/nord2_core_test.c gpu/tests/nord2_transport_test.c gpu/tests/nord2_trap_abi_test.c; do cmp vc17/$f c112/$f; done
# xnu-side and candidate-side copies of the shared sources must be the same bytes
for f in nord2_gpu_core.c nord2_gpu_core.h nord2_gpu_transport.h; do cmp s112/bsd/dev/$f c112/shim/nord2-xnu/$f; done
cmp c112/gpu/src/nord2_gpu_core.c s112/bsd/dev/nord2_gpu_core.c; cmp c112/gpu/include/nord2_gpu_core.h s112/bsd/dev/nord2_gpu_core.h; cmp c112/gpu/include/nord2_gpu_transport.h s112/bsd/dev/nord2_gpu_transport.h
cmp c112/gpu/src/Nord2GPU.cpp s112/iokit/Kernel/arm/Nord2GPU.cpp
cp 0112.patch $K/0112-display-surface-reservation.patch; cp cand0112.patch $K/candidate-gpu-source-0112.patch
rm -rf $K/patched-sources-0112; mkdir -p $K/patched-sources-0112/bsd/dev $K/patched-sources-0112/iokit/Kernel/arm
cp s112/bsd/dev/nord2_gpu_core.h s112/bsd/dev/nord2_gpu_core.c s112/bsd/dev/nord2_gpu_transport.h $K/patched-sources-0112/bsd/dev/
cp s112/iokit/Kernel/arm/Nord2GPU.cpp s112/iokit/Kernel/arm/SweetSurface.h s112/iokit/Kernel/arm/SweetSurface.cpp s112/iokit/Kernel/arm/SweetFramebuffer.cpp $K/patched-sources-0112/iokit/Kernel/arm/
cp c112/gpu/tests/nord2_core_test.c $K/tests-new/nord2_core_test-0112.c; cp c112/gpu/tests/nord2_transport_test.c $K/tests-new/nord2_transport_test-0112.c; cp c112/gpu/tests/nord2_trap_abi_test.c $K/tests-new/nord2_trap_abi_test.c
cp regen-0112.sh $K/regen-0112.sh
sha256sum $K/0112-*.patch $K/candidate-gpu-source-0112.patch | cut -c1-16,65-
