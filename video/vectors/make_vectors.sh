#!/bin/bash
# SPDX-License-Identifier: MIT
# Generate the WP09 deterministic vectors and manifest.json.
#
#   make_vectors.sh quick        raw 176x144 (six patterns), 178x146, 640x480
#                                plus the six golden streams of squares 176x144
#   make_vectors.sh full         quick + 1280x720 x10, 1920x1080 x4, 1918x1078 x4
#   make_vectors.sh clean        remove vectors/data (it is reproducible)
#
# Options after the mode: --out DIR --manifest FILE
# Defaults: --out gpu/video/vectors/data --manifest gpu/video/vectors/manifest.json
#
# The generated data is deletable by design; check_vectors.sh regenerates
# 'quick' into a temporary directory and compares every hash of the manifest.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$#" -lt 1 ]; then
    echo "usage: $0 {quick|full|clean} [--out DIR] [--manifest FILE]" >&2
    exit 2
fi
mode="$1"
shift
case "$mode" in
quick|full|clean) ;;
*)
    echo "usage: $0 {quick|full|clean} [--out DIR] [--manifest FILE]" >&2
    exit 2
    ;;
esac

exec python3 "$here/make_vectors.py" "$mode" --out "${OUT:-$here/data}" --manifest "${MANIFEST:-$here/manifest.json}" "$@"
