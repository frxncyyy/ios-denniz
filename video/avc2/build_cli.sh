#!/bin/bash
# SPDX-License-Identifier: MIT
# LEAD. Builds the encoder CLI from avc2/n2avc_core.c + WP04's inter files + the kernels. usage: build_cli.sh [out] [extra clang flags]
set -euo pipefail
V=$(cd "$(dirname "$0")/.." && pwd); OUT=${1:-$V/build/lead/avc2/n2avc_cli}; shift || true
mkdir -p "$(dirname "$OUT")"
I=${N2AVC_INTER_DIR:-$V/avc2}     # the PINNED inter/me/rc of avc2 by default; N2AVC_INTER_DIR=$V/avc judges WP04's working files
clang -std=c11 -O2 -g -Wall -Wextra -pthread -I "$V/include" -I "$I" -I "$V/kern" "$@" \
  "$V/avc2/n2avc_core.c" "$V/avc2/n2avc_deblock.c" "$I/n2avc_inter.c" "$I/n2avc_me.c" "$I/n2avc_rc.c" "$V/kern/n2vk_ref.c" "$V/kern/n2vk_dispatch.c" \
  "$V/avc2/n2avc_cli.c" -o "$OUT" -lm
