#!/bin/bash
# Regenerates patch 0115 (stage-2 MCUPM ping n2mcuping=1) from s114 (P16 state) to s115.
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0115.patch s114 s115 bsd/dev/nord2_cpufreq.c bsd/dev/nord2_mcupm_scan.h
rm -rf v20; cp -r s114 v20; (cd v20 && patch -p1 -s < $S/0115.patch)
for f in bsd/dev/nord2_cpufreq.c bsd/dev/nord2_mcupm_scan.h; do cmp v20/$f s115/$f; done
cp 0115.patch $K/0115-mcupm-stage2-ping-n2mcuping.patch
rm -rf $K/patched-sources-0115; mkdir -p $K/patched-sources-0115/bsd/dev; cp s115/bsd/dev/nord2_cpufreq.c s115/bsd/dev/nord2_mcupm_scan.h $K/patched-sources-0115/bsd/dev/
cp nord2_mcupm_scan_test.c $K/tests-new/nord2_mcupm_scan_test.c
cp regen-0115.sh $K/regen-0115.sh
sha256sum $K/0115-*.patch $K/tests-new/nord2_mcupm_scan_test.c | cut -c1-16,65-
