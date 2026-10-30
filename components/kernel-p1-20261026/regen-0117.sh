#!/bin/bash
# Regenerates patch 0117 (preflight rail rule = the stock one, all four rails printed) from s116 (P18 state) to s117.
set -e
S=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/k89; K=$NORD2_WORK/gpu/build/kernel-p1-20261026
cd $S
gen(){ out=$1; a=$2; b=$3; shift 3; : > $out; for f in "$@"; do if [ ! -e $a/$f ]; then diff -uN --label a/$f --label b/$f /dev/null $b/$f >> $out || true; elif ! cmp -s $a/$f $b/$f; then diff -uN --label a/$f --label b/$f $a/$f $b/$f >> $out || true; fi; done; }
gen 0117.patch s116 s117 bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dvfs.h bsd/dev/nord2_cpuhvfs_init.h
rm -rf v22; cp -r s116 v22; (cd v22 && patch -p1 -s < $S/0117.patch)
for f in bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dvfs.h bsd/dev/nord2_cpuhvfs_init.h; do cmp v22/$f s117/$f; done
cmp s117/bsd/dev/nord2_cpuhvfs_init.h $K/cpuhvfs-step3/nord2_cpuhvfs_init.h
cp 0117.patch $K/0117-dvfs-preflight-rails-stock-rule.patch
rm -rf $K/patched-sources-0117; mkdir -p $K/patched-sources-0117/bsd/dev
cp s117/bsd/dev/nord2_cpufreq.c s117/bsd/dev/nord2_dvfs.h s117/bsd/dev/nord2_cpuhvfs_init.h $K/patched-sources-0117/bsd/dev/
cp nord2_dvfs_test.c $K/tests-new/nord2_dvfs_test.c
cp regen-0117.sh $K/regen-0117.sh
sha256sum $K/0117-*.patch $K/tests-new/nord2_dvfs_test.c | cut -c1-16,65-
