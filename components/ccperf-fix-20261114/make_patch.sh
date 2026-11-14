#!/bin/bash
# ccperf-fix-20261114: 0494 = the optional DRAM register reads of nord2_cpufreq.c (0121 n2dram, 0462/0493 probe + watch) map with ml_io_map_unmappable
# (kernel_map) instead of ml_io_map (io_submap).  Root cause of the q10jcc resets: 0493's probe took 4 of the 11 io_submap pages left at that point, the SCP
# thread's maps at boot + 90 s need 8.  a/ = ccperf-20261114/b (chain after 0493; 0491 touches other files), b/ = fixed.
# Host tests: test/run.sh.  Merge check on a copy of integ-20261114: merge_check.sh.  Nothing here writes into integ-20261114 or ccperf-20261114.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
n=0494; name=nord2-dram-probe-kernel-map; files="bsd/dev/nord2_cpufreq.c"
out=$H/$n-$name.patch; : > "$out"
for f in $files; do diff -u "a/$f" "b/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; done
mkdir "$T/s"; cp -a a/. "$T/s/"
(cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
rm -rf "$H/patched-sources-$n"
for f in $files; do cmp "$T/s/$f" "b/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "b/$f" "$H/patched-sources-$n/$f"; done
sha256sum "$out"
diff -r "$T/s" b > /dev/null && echo "a + 0494 == b"
