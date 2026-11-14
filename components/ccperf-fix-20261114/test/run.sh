#!/bin/bash
# ccperf-fix-20261114/test/run.sh: host tests of 0494 (DRAM register maps outside the io_submap).
#  1. evidence: iosubmap_budget.py on the extracts of the three logs (evidence/): q10jib 501 pages before the SCP maps / 3 left; q10jcc boot 2 505 / FIFO map
#     unanswered with 0 left (= FULL); q10jcc boot 1 503 / 1 left.  The extracts were cut from the logs named in their first line.
#  2. probe_maps_test: the real DRAM section of nord2_cpufreq.c, a/ (0493) and b/ (0494), against a model of io_submap (501 taken, as on the phone) and kernel_map:
#     a/ takes 4 io_submap pages and the model panics at the SCP's FIFO map (what q10jcc boot 2 did); b/ takes 0 and ends with the 3 free pages of q10jib; n2dram=1/2
#     (0121) likewise 0.  The printed DRAM RATE lines are the same in a/ and b/ (and are the phone's: shu_lv=0 ... 3733 Mbps).
#  3. static: no ml_io_map / ml_io_map_wcomb call in b/'s DRAM section; in the whole file 5 fewer call sites than a/; nothing else changed (diff = the 0494 patch).
#  4. ccperf-20261114's own host tests still pass (0492/0493 logic untouched).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); D=$(dirname "$H")
B=$NORD2_WORK/gpu/build
CC="cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 $H/iosubmap_budget.py $H/evidence/q10jib-wall4_ib.txt $H/evidence/q10jcc-boot1-wall4.txt $H/evidence/q10jcc-boot2-wall4_retry.txt \
  --expect q10jib-wall4_ib=501,3,ok q10jcc-boot1-wall4=503,1,ok q10jcc-boot2-wall4_retry=505,0,FULL
echo EVIDENCE_OK
cut_section() {   # the DRAM section: from the 0121 banner (or 0494's comment before it) up to n2dvfs_main
  python3 - "$1" "$2" <<'PY'
import sys, re
s = open(sys.argv[1]).read()
a = s.index('/* 0494: every register map') if '/* 0494: every register map' in s else s.index('/* ---- patch 0121: DRAM clock state')
b = s.index('static void\nn2dvfs_main(void)')
open(sys.argv[2], 'w').write(s[a:b])
PY
}
for v in a b; do
  cut_section $D/$v/bsd/dev/nord2_cpufreq.c $T/sec_$v.inc
  $CC -I$B/ccperf-20261114/b/bsd/dev -DEXTRACT="\"$T/sec_$v.inc\"" -o $T/probe_$v $H/probe_maps_test.c
  for n in 0 1 2; do $T/probe_$v $n > $T/out_$v$n.txt; grep '^RESULT' $T/out_$v$n.txt | sed "s/^/$v: /"; done
done
grep -q 'RESULT n2dram=0: io_submap pages taken by the DRAM reads 4, kernel_map pages 0, bad flags 0, io_submap after the SCP maps 512/512 free 0, PANIC (io_submap full) at SCP map FIFO' $T/out_a0.txt
grep -q 'RESULT n2dram=0: io_submap pages taken by the DRAM reads 0, kernel_map pages 4, bad flags 0, io_submap after the SCP maps 509/512 free 3, no panic' $T/out_b0.txt
grep -q 'RESULT n2dram=2: io_submap pages taken by the DRAM reads 0, kernel_map pages 8, bad flags 0, io_submap after the SCP maps 509/512 free 3, no panic' $T/out_b2.txt
grep -q 'PANIC' $T/out_a1.txt && grep -q 'PANIC' $T/out_a2.txt && ! grep -q 'PANIC' $T/out_b1.txt
for n in 0 1 2; do diff <(grep -v '^RESULT\|^NORD2 DRAM IOMAP:' $T/out_a$n.txt) <(grep -v '^RESULT\|^NORD2 DRAM IOMAP:' $T/out_b$n.txt); done
grep -q '^NORD2 DRAM RATE: shu_lv=0 pll=1 sdmpcw=0x8f00 prediv=1 posdiv=0 ckdiv4=0 pll_md=0 cldiv2=0 fbksel=0 dqopen=0 -> 3718 = 3733 Mbps' $T/out_b0.txt
grep -q '^NORD2 DRAM RATE: DVFSRC basic_control=00000008 (en 0, out_en 0) .* SPM dvfs_level=00000004 dvs_dfs_level=00020002 dvfs_sta=00000000 sw_flag_0=00000418' $T/out_b0.txt
grep -q '^NORD2 DRAM RATE: watch starts at shu_lv=0 3733 Mbps' $T/out_b0.txt && [ "$(grep -c 'DRAM RATE: change' $T/out_b0.txt)" = 0 ]
[ "$(grep -c '^NORD2 DRAM IOMAP: .* (ml_io_map_unmappable, outside io_submap)$' $T/out_b0.txt)" = 4 ]
echo "PROBE_MAPS_OK (same DRAM RATE lines as the phone; a/ = 0493 exhausts the io_submap at the SCP FIFO map, b/ = 0494 takes nothing from it)"
calls() { grep -v '^extern\|^ \*\|^/\*' "$1" | { grep -o 'ml_io_map\(_wcomb\)\?(' || true; } | wc -l; }
[ "$(calls $T/sec_b.inc)" = 0 ] && [ "$(calls $T/sec_a.inc)" = 5 ]
[ $(( $(calls $D/a/bsd/dev/nord2_cpufreq.c) - $(calls $D/b/bsd/dev/nord2_cpufreq.c) )) = 5 ]
{ diff -u $D/a/bsd/dev/nord2_cpufreq.c $D/b/bsd/dev/nord2_cpufreq.c || true; } | sed -E '1s#^--- .*#--- a/bsd/dev/nord2_cpufreq.c#; 2s#^\+\+\+ .*#+++ b/bsd/dev/nord2_cpufreq.c#' | cmp - $D/0494-nord2-dram-probe-kernel-map.patch
cmp $D/a/bsd/dev/nord2_cpufreq.c $B/ccperf-20261114/b/bsd/dev/nord2_cpufreq.c
echo "STATIC_OK (DRAM section: 5 io_submap call sites -> 0; base = ccperf-20261114/b)"
bash $B/ccperf-20261114/test/run.sh | tail -3
echo CCPERF_FIX_HOST_TESTS_OK
