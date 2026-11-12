#!/bin/bash
# SPDX-License-Identifier: MIT -- mnl_emu2.py runner (virtual clock + fake DSP replies). usage: run_emu2.sh TAG [extra args]
# Runs inside nord2-metal.slice, nice 19, CPUs 0,1 (CLAUDE.md thermal limits). Log runs/TAG.log, TX dump runs/TAG.tx.bin/.json
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); TAG=${1:-e}; shift || true; mkdir -p $D/runs
LIB=${LIB:-$D/../../../build/cam-vendor-20261109/vfs/lib64/mt6893/libmnl.so}
systemd-run --user --quiet --scope --slice=nord2-metal.slice nice -n 19 taskset -c 0,1 timeout ${TMO:-1200} \
  python3 -I $D/mnl_emu2.py "$LIB" --steps ${STEPS:-50000000} --log $D/runs/$TAG.log --tx-bin $D/runs/$TAG.tx.bin --tx-json $D/runs/$TAG.tx.json "$@" > $D/runs/$TAG.out 2>&1 || true
grep -E '^SUMMARY' $D/runs/$TAG.log; tail -3 $D/runs/$TAG.out
