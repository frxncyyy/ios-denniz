#!/bin/bash
# SPDX-License-Identifier: MIT -- runs libmnl.so under the Unicorn AArch64 emulator (mnl_emu.py). Needs the unicorn wheel
# (pip install --target ~/.cache/n2mnld-pydeps unicorn==2.1.4; already installed on this host). Log in runs/<tag>.log.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); TAG=${1:-run}; mkdir -p $D/runs
LIB=${LIB:-$D/../../../build/cam-vendor-20261109/vfs/lib64/mt6893/libmnl.so}
PYTHONPATH=${PYDEPS:-$HOME/.cache/n2mnld-pydeps} timeout 600 python3 $D/mnl_emu.py "$LIB" --steps ${STEPS:-50000000} --log $D/runs/$TAG.log > $D/runs/$TAG.out 2>&1
grep -E '^SUMMARY' $D/runs/$TAG.log
