#!/bin/bash
# Regenerates patch 0118 (DVFS bring-up: announce+flush before every access, stage limits n2dvst, only LL/L rails read, small share mapping) from s117 to s118.
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0118.patch s117 s118 bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dvfs.h
rm -rf v23; cp -r s117 v23; (cd v23 && patch -p1 -s < $S/0118.patch)
for f in bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dvfs.h; do cmp v23/$f s118/$f; done
cp 0118.patch $K/0118-dvfs-announce-flush-stages-two-rail-reads.patch
rm -rf $K/patched-sources-0118; mkdir -p $K/patched-sources-0118/bsd/dev
cp s118/bsd/dev/nord2_cpufreq.c s118/bsd/dev/nord2_dvfs.h $K/patched-sources-0118/bsd/dev/
cp nord2_dvfs_test.c $K/tests-new/nord2_dvfs_test.c
cp regen-0118.sh $K/regen-0118.sh
sha256sum $K/0118-*.patch $K/tests-new/nord2_dvfs_test.c | cut -c1-16,65-
