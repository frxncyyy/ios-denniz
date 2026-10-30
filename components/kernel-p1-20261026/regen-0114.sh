#!/bin/bash
# Regenerates patch 0114 (read-only MCUPM probe n2mcu=2/3) from s113 (P15 state) to s114.
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0114.patch s113 s114 bsd/dev/nord2_cpufreq.c bsd/dev/nord2_mcupm_scan.h
rm -rf v19; cp -r s113 v19; (cd v19 && patch -p1 -s < $S/0114.patch)
for f in bsd/dev/nord2_cpufreq.c bsd/dev/nord2_mcupm_scan.h; do cmp v19/$f s114/$f; done
cp 0114.patch $K/0114-mcupm-readonly-probe2-n2mcu2.patch
rm -rf $K/patched-sources-0114; mkdir -p $K/patched-sources-0114/bsd/dev; cp s114/bsd/dev/nord2_cpufreq.c s114/bsd/dev/nord2_mcupm_scan.h $K/patched-sources-0114/bsd/dev/
cp nord2_mcupm_scan_test.c $K/tests-new/nord2_mcupm_scan_test.c
cp regen-0114.sh $K/regen-0114.sh
sha256sum $K/0114-*.patch $K/tests-new/nord2_mcupm_scan_test.c | cut -c1-16,65-
