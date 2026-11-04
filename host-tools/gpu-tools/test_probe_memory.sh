#!/usr/bin/env bash
# Host-only diagnostic budget and update checks; no device access.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/probe-memory-review
${CC:-cc} -std=c11 -O1 -g -Wall -Wextra -Werror \
 -fsanitize=address,undefined -fno-omit-frame-pointer \
 tests/nord2_probe_memory_test.c -o build/probe-memory-review/memory_test
build/probe-memory-review/memory_test
python3 tests/metal_probe_analysis_test.py
python3 tests/metal_update_test.py
python3 tests/metal_install_test.py
bash tools/check_preserved_baseline.sh
bash tools/check_sync.sh
bash tools/build_metal_probe.sh --binary-only
