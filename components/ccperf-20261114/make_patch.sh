#!/bin/bash
# ccperf-20261114: 0492 GPU DVFS UI profile (kern.n2gdvfs +8: never below the cold-start OPP, up at 80 % while the UI animates), 0493 DRAM rate probe
# from the boot line (n2drp) + watch.  a/ = integ-20261114/chain/s after 0490 (q10jia; 0491 of wifi-fix-20261114 touches other files), b/ = both applied.
# Host tests: test/run.sh.  Nothing here writes into integ-20261114.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mk() {   # mk NNNN name from-dir to-dir files...
  local n=$1 name=$2 from=$3 to=$4; shift 4
  local out=$H/$n-$name.patch; : > "$out"
  for f in "$@"; do diff -u "$from/$f" "$to/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; done
  (cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
  ! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
  rm -rf "$H/patched-sources-$n"
  for f in "$@"; do cmp "$T/s/$f" "$to/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "$to/$f" "$H/patched-sources-$n/$f"; done
  sha256sum "$out"
}
mkdir "$T/s" "$T/s492"; cp -a a/. "$T/s/"
cp -a a/. "$T/s492/"; for f in bsd/dev/nord2_gov.h iokit/Kernel/arm/Nord2GPU.cpp; do cp b/$f "$T/s492/$f"; done
mk 0492 nord2-gpu-dvfs-ui-profile a "$T/s492" bsd/dev/nord2_gov.h iokit/Kernel/arm/Nord2GPU.cpp
mk 0493 nord2-dram-rate-bootarg-watch "$T/s492" b bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dram_rate.h
diff -r "$T/s" b > /dev/null && echo "a + 0492 + 0493 == b"
