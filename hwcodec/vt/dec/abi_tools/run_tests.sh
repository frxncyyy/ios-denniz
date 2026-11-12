#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# DEC08 host test: verify every fact in ABI.md against the phone's dyld shared cache, and verify that ABI.md is exactly
# what n2decabi.py regenerates (the proposed header is compiled with -Werror as part of the check).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
export N2_DYLD_CACHE=${N2_DYLD_CACHE:-$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64}
export N2_TMP=${N2_TMP:-/tmp/n2hw-DEC08}
export PYTHONDONTWRITEBYTECODE=1
mkdir -p "$N2_TMP"
if [ ! -f "$N2_DYLD_CACHE" ]; then
	echo "N2TEST DEC08 FAIL cache not found: $N2_DYLD_CACHE (set N2_DYLD_CACHE)"
	exit 1
fi
python3 "$HERE/n2decabi.py" --check
