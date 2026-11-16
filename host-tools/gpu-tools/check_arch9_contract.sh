#!/bin/sh
# Reject accidental Tegu arch11/CSF transplant in the production backend.
set -eu
cd "$(dirname "$0")/../.."
bad=0
for f in gpu/src/*.c gpu/src/*.cpp gpu/include/*.h shim/nord2-xnu/nord2_gpu_* \
         shim/nord2-xnu/Nord2GPU.cpp; do
  if grep -nE '0xb8a21030|pan_get_nir_shader_compiler_options\(11|MALI_JOB_TYPE_CSF|CSG_|csf_|v10_pack' "$f"; then
    echo "arch9 contract violation: $f" >&2
    bad=1
  fi
done
[ "$bad" = 0 ]
echo "arch9 contract: no CSF/arch11/v10 constants in production sources"
