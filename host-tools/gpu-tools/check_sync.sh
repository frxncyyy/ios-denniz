#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
for f in gpu/include/nord2_gpu_*.h gpu/src/nord2_gpu_*.c gpu/src/Nord2GPU.cpp; do
  cmp "$f" "shim/nord2-xnu/$(basename "$f")"
done
echo "all canonical GPU kernel modules are in sync"
