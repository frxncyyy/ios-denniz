#!/bin/bash
# SPDX-License-Identifier: MIT
# LEAD. The plug-in WITH the H.264 encoder, from a private copy of exactly these sources:
#   vt/n2vt_shim.c n2vt_log.c n2avc_vt.c (WP01, WP05) + avc2/n2avc_core.c (lead) + avc/n2avc_inter.c n2avc_me.c n2avc_rc.c (WP04)
#   + kern/n2vk_ref.c n2vk_dispatch.c (WP08);  and the probe (probe/*.c + probe/av).
# usage: build_plugin.sh [outdir]   (default gpu/video/build/plugin1)
set -euo pipefail
V=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$V/build/plugin1}
T=$OUT/tree
rm -rf "$T"; mkdir -p "$T/vt" "$T/probe/av" "$T/avc" "$T/kern" "$T/vectors" "$OUT/products"
cp -r "$V/include" "$V/sdk" "$T/"
cp "$V/build_ios.sh" "$T/"
cp "$V/vt/n2vt_shim.c" "$V/vt/n2vt_log.c" "$V/vt/n2vt_log.h" "$V/vt/n2avc_vt.c" "$V/vt/n2avc_vt.h" "$V/vt/symbols.txt" "$T/vt/"
cp "$V/avc2/n2avc_core.c" "$V/avc2/n2avc_deblock.c" "$V/avc2/symbols.txt" "$V/avc2/n2avc_inter.c" "$V/avc2/n2avc_me.c" "$V/avc2/n2avc_rc.c" "$V/avc2/n2avc_inter_priv.h" "$T/avc/"
cp "$V/kern/n2vk_ref.c" "$T/kern/"
# (the thread-local scratch of n2vk_backend_name would pull in __tlv_bootstrap: a plain static is enough for a name)
sed 's/static _Thread_local char answer/static char answer/' "$V/kern/n2vk_dispatch.c" > "$T/kern/n2vk_dispatch.c"
grep -q snprintf "$T/kern/n2vk_dispatch.c" && sed -i '0,/#include/s//#include <stdio.h>\n#include/' "$T/kern/n2vk_dispatch.c"
[ -f "$V/kern/symbols.txt" ] && cp "$V/kern/symbols.txt" "$T/kern/"
cp "$V"/probe/*.c "$V"/probe/*.h "$V/probe/symbols.txt" "$T/probe/"
cp "$V"/probe/av/*.c "$V"/probe/av/*.h "$V/probe/av/symbols.txt" "$T/probe/av/"
[ -f "$V/vectors/embedded_vectors.h" ] && cp "$V/vectors/embedded_vectors.h" "$T/vectors/"
PROBE_NO_AV=0 bash "$T/build_ios.sh" "$OUT/products" > "$OUT/build.log" 2>&1 || { grep -v warning "$OUT/build.log" | tail -30; exit 1; }
cp "$OUT/products/Slim.videoencoder" "$OUT/products/n2vt_probe" "$OUT/"
( cd "$OUT" && sha256sum Slim.videoencoder n2vt_probe | tee SHA256SUMS && ls -l Slim.videoencoder n2vt_probe )
