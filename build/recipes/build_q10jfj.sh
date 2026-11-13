#!/bin/bash
# q10jfj = the final phone image: q10jfg (GNSS 0350,0351,0353..0359,0362,0363,0364 merged with q10jfe's camera/display line) + 0393 (fbsync)
# + the display HW composition list (default 0394 0395 0396). Series order: base, GNSS, 0390..0393, then the HW list LAST
# (build_p1_candidate.py checks the tracked sources against patched-sources-NNNN with NNNN <= series[-1]).
# Reference build for warnings, boot-args and spare: q10jfg (boot-args 603 chars incl. n2gps=1 -> only 4 free: no room for n2fbhw=N,
# use the payload-fbhw* launchd job / sysctl instead). Budget: q10jfg spare 53248; 0393 costs 4096 (q10jfe->q10jfh) -> ~49152 before 0394..0396.
# Everything else (env PATCHES, PREFLIGHT_ONLY, NOPACK, WARN_BUDGET, EXTRA_ARGS, SPARE_WARN, checks) is build_q10jfi.sh's.
# usage: build_q10jfj.sh [NAME] [OUTDIR]
GNSS=1 exec "$(dirname "$0")/build_q10jfi.sh" "${1:-q10jfj}" "${2:-}"
