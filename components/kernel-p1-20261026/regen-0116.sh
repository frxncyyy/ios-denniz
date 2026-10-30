#!/bin/bash
# Regenerates patch 0116 (CPU DVFS bring-up n2dvfs, PMIC read export) from s115 (P17 state) to s116, and the candidate mirror of the Nord2GPU.cpp change.
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0116.patch s115 s116 bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dvfs.h bsd/dev/nord2_cpuhvfs_init.h bsd/dev/nord2_cpuhvfs_tables.h iokit/Kernel/arm/Nord2GPU.cpp
gen cand0116.patch c113 c116 gpu/src/Nord2GPU.cpp shim/nord2-xnu/Nord2GPU.cpp
rm -rf v21; cp -r s115 v21; (cd v21 && patch -p1 -s < $S/0116.patch)
for f in bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dvfs.h bsd/dev/nord2_cpuhvfs_init.h bsd/dev/nord2_cpuhvfs_tables.h iokit/Kernel/arm/Nord2GPU.cpp; do cmp v21/$f s116/$f; done
rm -rf vc21; cp -r c113 vc21; (cd vc21 && patch -p1 -s < $S/cand0116.patch); for f in gpu/src/Nord2GPU.cpp shim/nord2-xnu/Nord2GPU.cpp; do cmp vc21/$f c116/$f; done
cmp c116/gpu/src/Nord2GPU.cpp s116/iokit/Kernel/arm/Nord2GPU.cpp
cp 0116.patch $K/0116-cpu-dvfs-bringup-n2dvfs.patch; cp cand0116.patch $K/candidate-gpu-source-0116.patch
rm -rf $K/patched-sources-0116; mkdir -p $K/patched-sources-0116/bsd/dev $K/patched-sources-0116/iokit/Kernel/arm
cp s116/bsd/dev/nord2_cpufreq.c s116/bsd/dev/nord2_dvfs.h s116/bsd/dev/nord2_cpuhvfs_init.h s116/bsd/dev/nord2_cpuhvfs_tables.h $K/patched-sources-0116/bsd/dev/
cp s116/iokit/Kernel/arm/Nord2GPU.cpp $K/patched-sources-0116/iokit/Kernel/arm/
cp nord2_dvfs_test.c $K/tests-new/nord2_dvfs_test.c
cp regen-0116.sh $K/regen-0116.sh
sha256sum $K/0116-*.patch $K/candidate-gpu-source-0116.patch $K/tests-new/nord2_dvfs_test.c | cut -c1-16,65-
