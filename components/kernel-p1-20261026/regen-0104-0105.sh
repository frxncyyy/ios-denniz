#!/bin/bash
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026; W=$NORD2_WORK
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0104.patch s103 s104 bsd/conf/files bsd/dev/nord2_cpufreq.h bsd/dev/nord2_cpufreq.c iokit/Kernel/arm/Nord2GPU.cpp
gen 0105.patch s104 s105 bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c bsd/dev/nord2_gpu_va.h
gen cand0104.patch c103 c104 gpu/src/Nord2GPU.cpp shim/nord2-xnu/Nord2GPU.cpp
gen cand0105.patch c104 c105 gpu/include/nord2_gpu_core.h gpu/include/nord2_gpu_va.h gpu/src/nord2_gpu_core.c shim/nord2-xnu/nord2_gpu_core.h shim/nord2-xnu/nord2_gpu_va.h shim/nord2-xnu/nord2_gpu_core.c gpu/tests/nord2_core_test.c
# round trips
rm -rf v12; cp -r s103 v12; (cd v12 && patch -p1 -s < $S/0104.patch && patch -p1 -s < $S/0105.patch)
for f in bsd/conf/files bsd/dev/nord2_cpufreq.h bsd/dev/nord2_cpufreq.c iokit/Kernel/arm/Nord2GPU.cpp bsd/dev/nord2_gpu_core.h bsd/dev/nord2_gpu_core.c bsd/dev/nord2_gpu_va.h; do cmp v12/$f s105/$f; done
rm -rf vc12; cp -r c103 vc12; (cd vc12 && patch -p1 -s < $S/cand0104.patch && patch -p1 -s < $S/cand0105.patch)
for f in gpu/src/Nord2GPU.cpp shim/nord2-xnu/Nord2GPU.cpp gpu/include/nord2_gpu_core.h gpu/include/nord2_gpu_va.h gpu/src/nord2_gpu_core.c shim/nord2-xnu/nord2_gpu_core.h shim/nord2-xnu/nord2_gpu_va.h shim/nord2-xnu/nord2_gpu_core.c gpu/tests/nord2_core_test.c; do cmp vc12/$f c105/$f; done
cp 0104.patch $K/0104-gated-cpu-dvfs-big-clusters-n2cpu.patch; cp 0105.patch $K/0105-per-context-handles-1024.patch
cp cand0104.patch $K/candidate-gpu-source-0104.patch; cp cand0105.patch $K/candidate-gpu-source-0105.patch
rm -rf $K/patched-sources-0104 $K/patched-sources-0105
mkdir -p $K/patched-sources-0104/bsd/dev $K/patched-sources-0104/bsd/conf $K/patched-sources-0104/iokit/Kernel/arm $K/patched-sources-0105/bsd/dev
cp s104/bsd/dev/nord2_cpufreq.c s104/bsd/dev/nord2_cpufreq.h $K/patched-sources-0104/bsd/dev/; cp s104/bsd/conf/files $K/patched-sources-0104/bsd/conf/; cp s104/iokit/Kernel/arm/Nord2GPU.cpp $K/patched-sources-0104/iokit/Kernel/arm/
cp s105/bsd/dev/nord2_gpu_core.h s105/bsd/dev/nord2_gpu_core.c s105/bsd/dev/nord2_gpu_va.h $K/patched-sources-0105/bsd/dev/
cp c105/gpu/tests/nord2_core_test.c $K/tests-new/nord2_core_test-0105.c
rm -rf $K/wip-0104
cp $S/regen11.sh $K/regen-0104-0105.sh
sha256sum $K/0104-*.patch $K/0105-*.patch $K/candidate-gpu-source-0104.patch $K/candidate-gpu-source-0105.patch
