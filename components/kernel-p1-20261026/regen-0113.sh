#!/bin/bash
# Regenerates patch 0113 (transport header: the IOConnectTrap6 call block yields to the frontend's identical definition) from s112/c112 -> s113/c113.
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0113.patch s112 s113 bsd/dev/nord2_gpu_transport.h
gen cand0113.patch c112 c113 gpu/include/nord2_gpu_transport.h shim/nord2-xnu/nord2_gpu_transport.h gpu/tests/nord2_trap_order_test.c
rm -rf v18; cp -r s112 v18; (cd v18 && patch -p1 -s < $S/0113.patch); cmp v18/bsd/dev/nord2_gpu_transport.h s113/bsd/dev/nord2_gpu_transport.h
rm -rf vc18; cp -r c112 vc18; (cd vc18 && patch -p1 -s < $S/cand0113.patch)
for f in gpu/include/nord2_gpu_transport.h shim/nord2-xnu/nord2_gpu_transport.h gpu/tests/nord2_trap_order_test.c; do cmp vc18/$f c113/$f; done
cmp s113/bsd/dev/nord2_gpu_transport.h c113/gpu/include/nord2_gpu_transport.h; cmp s113/bsd/dev/nord2_gpu_transport.h c113/shim/nord2-xnu/nord2_gpu_transport.h
cp 0113.patch $K/0113-transport-trap-block-yields-to-frontend.patch; cp cand0113.patch $K/candidate-gpu-source-0113.patch
rm -rf $K/patched-sources-0113; mkdir -p $K/patched-sources-0113/bsd/dev; cp s113/bsd/dev/nord2_gpu_transport.h $K/patched-sources-0113/bsd/dev/
cp c113/gpu/tests/nord2_trap_order_test.c $K/tests-new/nord2_trap_order_test.c
cp regen-0113.sh $K/regen-0113.sh
sha256sum $K/0113-*.patch $K/candidate-gpu-source-0113.patch | cut -c1-16,65-
