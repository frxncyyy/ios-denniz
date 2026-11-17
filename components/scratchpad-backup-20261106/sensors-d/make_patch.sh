#!/bin/bash
# 0284: SCP sensor hub + Apple-vendor sensor HID services. a/ = the q10ed stage (after 0282), b/ = patched.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
OUT=$H/0284-nord2-scp-sensors-daily.patch
{ diff -u a/bsd/dev/sweet_touch.c b/bsd/dev/sweet_touch.c || true
  diff -uN a/bsd/dev/nord2_scp.inc b/bsd/dev/nord2_scp.inc || true
  diff -uN a/bsd/dev/nord2_hang.inc b/bsd/dev/nord2_hang.inc || true
  diff -uN a/bsd/dev/nord2_cam.inc b/bsd/dev/nord2_cam.inc || true
  diff -uN a/bsd/dev/nord2_cam_tables.h b/bsd/dev/nord2_cam_tables.h || true
  diff -uN a/bsd/dev/nord2_seninf.inc b/bsd/dev/nord2_seninf.inc || true
  diff -uN a/bsd/dev/nord2_camsv.inc b/bsd/dev/nord2_camsv.inc || true
  diff -uN a/bsd/dev/nord2_camdev.inc b/bsd/dev/nord2_camdev.inc || true
  diff -u a/bsd/dev/audio_i2c8.c b/bsd/dev/audio_i2c8.c || true
  diff -urN a/bsd/dev/n2seninf b/bsd/dev/n2seninf || true
  diff -u a/iokit/Kernel/arm/hid/SweetHID.cpp b/iokit/Kernel/arm/hid/SweetHID.cpp || true; } | sed -E 's#^(---|\+\+\+) ([ab]/[^\t]*)\t.*#\1 \2#' > "$OUT"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cp -a a/. "$T/"; (cd "$T" && patch -s -p1 -i "$OUT")
for f in bsd/dev/sweet_touch.c bsd/dev/nord2_scp.inc bsd/dev/nord2_hang.inc bsd/dev/nord2_cam.inc bsd/dev/nord2_cam_tables.h bsd/dev/nord2_seninf.inc bsd/dev/nord2_camsv.inc bsd/dev/nord2_camdev.inc bsd/dev/audio_i2c8.c $(cd b && ls bsd/dev/n2seninf/*) iokit/Kernel/arm/hid/SweetHID.cpp; do cmp "$T/$f" "b/$f"; done
sha256sum "$OUT"
