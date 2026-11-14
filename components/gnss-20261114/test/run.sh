#!/bin/bash
# gnss-20261114 host tests: the exact c/ (0580+0581) and b/ (0580) nord2_gpsdl.inc + nord2_gnss_pwr.inc with kernel shims
# (test_pipe.c), plus the original 0350 power-core tests (gpu/hwcodec/gnss/run_tests.sh).
set -euo pipefail
H=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d -p "$H/test" .t.XXXXXX); trap 'rm -rf "$T"' EXIT
for v in o n m l k j i h g f e d c b; do
  mkdir -p $T/$v/x/y/z; cp $H/$v/bsd/dev/nord2_gpsdl.inc $H/$v/bsd/dev/nord2_gnss_pwr.inc $T/$v/x/y/z/; : > $T/$v/gnss_pwr.h
  D=""; [ $v = c ] && D=-DPIPE_0581; [ $v = d ] && D="-DPIPE_0581 -DLNA_0582"; [ $v = e ] && D="-DPIPE_0581 -DTRACE_0583"; [ $v = f ] && D="-DPIPE_0581 -DTRACE_0583"; [ $v = g ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582"; [ $v = h ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582"; [ $v = i ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582 -DGAP_0587"; [ $v = j ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582 -DGAP_0587 -DRX_0588"; [ $v = k ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582 -DGAP_0587 -DRX_0588 -DGIC_0589"; [ $v = l ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582 -DGAP_0587 -DRX_0588 -DGIC_0589 -DTX_0590"; [ $v = m ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582 -DGAP_0587 -DRX_0588 -DGIC_0589 -DTX_0590 -DXCHG_0591"; [ $v = n ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582 -DGAP_0587 -DRX_0588 -DGIC_0589 -DTX_0590 -DXCHG_0591"; [ $v = o ] && D="-DPIPE_0581 -DTRACE_0583 -DLNA_0582 -DGAP_0587 -DRX_0588 -DGIC_0589 -DTX_0590 -DXCHG_0591 -DLNA_0593"
  cc -O1 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter $D -I$T/$v/x/y/z -o $T/$v/t $H/test/test_pipe.c
  echo "== $v/"; $T/$v/t | tail -2
done
bash $H/../../hwcodec/gnss/run_tests.sh | tail -1
