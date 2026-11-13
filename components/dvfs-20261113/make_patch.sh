#!/bin/bash
# dvfs-20261113: 0460 CPU governor (schedutil shape, activity classes, thermal ladder), 0461 GPU runtime DVFS, 0462 DRAM data-rate probe (read-only).
#  a/ = the q10jgs stage (device-mmu-q10jgs-candidate/k/kernel/stage; chain 0394..0401, 0403..0411, 0413..0418); b/ = everything applied;
#  b460/bsd/dev/nord2_cpufreq.c = nord2_cpufreq.c after 0460 (0462 adds the probe on top).  Host test: test/run.sh.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mk() {   # mk NNNN name from-dir to-dir files...
  local n=$1 name=$2 from=$3 to=$4; shift 4
  local out=$H/$n-$name.patch; : > "$out"
  for f in "$@"; do
    if [ -f "$from/$f" ]; then diff -u "$from/$f" "$to/$f" | sed -E "1s#^--- .*#--- a/$f#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true
    else diff -u /dev/null "$to/$f" | sed -E "1s#^--- .*#--- /dev/null#; 2s#^\+\+\+ .*#+++ b/$f#" >> "$out" || true; fi
  done
  (cd "$T/s" && patch -p1 --fuzz=0 --batch -i "$out") > "$H/apply-$n.log"
  ! grep -E 'offset|fuzz|FAILED|Reversed' "$H/apply-$n.log"
  rm -rf "$H/patched-sources-$n"
  for f in "$@"; do cmp "$T/s/$f" "$to/$f"; mkdir -p "$H/patched-sources-$n/$(dirname "$f")"; cp "$to/$f" "$H/patched-sources-$n/$f"; done
  sha256sum "$out"
}
mkdir "$T/s" "$T/s460" "$T/s461"; cp -a a/. "$T/s/"
# state after 0460: a + the CPU files (nord2_cpufreq.c without the DRAM probe)
cp -a a/. "$T/s460/"; for f in bsd/dev/nord2_gov.h bsd/dev/nord2_dvfs_sched.h iokit/Kernel/arm/hid/SweetHID.cpp; do cp b/$f "$T/s460/$f"; done; cp b460/bsd/dev/nord2_cpufreq.c "$T/s460/bsd/dev/nord2_cpufreq.c"
mk 0460 nord2-dvfs-sched-governor a "$T/s460" bsd/dev/nord2_cpufreq.c bsd/dev/nord2_gov.h bsd/dev/nord2_dvfs_sched.h iokit/Kernel/arm/hid/SweetHID.cpp
cp -a "$T/s460/." "$T/s461/"; for f in bsd/dev/nord2_gpu_clk.c bsd/dev/nord2_gpu_clk.h bsd/dev/nord2_gpu_core.c bsd/dev/nord2_gpu_core.h iokit/Kernel/arm/Nord2GPU.cpp; do cp b/$f "$T/s461/$f"; done
mk 0461 nord2-gpu-runtime-dvfs "$T/s460" "$T/s461" iokit/Kernel/arm/Nord2GPU.cpp bsd/dev/nord2_gpu_clk.c bsd/dev/nord2_gpu_clk.h bsd/dev/nord2_gpu_core.c bsd/dev/nord2_gpu_core.h
mk 0462 nord2-dram-rate-probe "$T/s461" b bsd/dev/nord2_cpufreq.c bsd/dev/nord2_dram_rate.h
diff -r "$T/s" b > /dev/null && echo "a + 0460 + 0461 + 0462 == b"
