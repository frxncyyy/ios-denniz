#!/bin/bash
# SPDX-License-Identifier: MIT
# make_series.sh -- the hwcodec kernel patches renumbered for the phone's kernel series (opus-kern, 11 Nov 2026).
#
# The phone runs q10in = q10hq series + 0296 + 0298 + 0299 (surface budget) + 0300 (surface holders); 0299/0300 are taken,
# 0301 (crash-log thread) is NOT in the series.  The hwcodec patches therefore become:
#   0310-nord2-jpegdev-vcodec.patch   = what kern/make_patch.sh calls 0299: K01 nord2_jpegdev.inc, K02 nord2_vcodec.inc,
#                                       IK nord2_vcodec_relay.inc + sweet_touch.c hooks + the two frozen ABI headers;
#                                       generated here the same way, every "0299" token in added lines renamed "0310".
#   0311-nord2-vcodec-venc-autotest.patch = opus-kern: bsd/dev/nord2_vcodec_at.inc (autotest command 8, VENC steps 0-4),
#                                       its include in sweet_touch.c, nord2_autotest.inc (command 8 + JPEG step-4 guard).
# Both apply after 0298 (kernel/b = q10at3 stage + 0298, byte-identical in sweet_touch.c/nord2_autotest.inc/nord2_hwcodec.inc
# to the q10in stage, checked below) and touch nothing that 0299/0300 (SweetSurface.cpp) touch.
#
#   bash make_series.sh            regenerate both patches from the live sources, verify, write MANIFEST.txt
#   bash make_series.sh --check    regenerate into /tmp only and fail if the pinned patches differ (drift check)
# Sources: K01/K02/IK = gpu/hwcodec/kern/bsd/dev (override with N2P_SRC_DEV), headers = gpu/hwcodec/include,
#          opus-kern = gpu/hwcodec/kern/p031x/bsd/dev.  Scratch: /tmp/n2hw-opus/series (deleted at the start).
set -euo pipefail

P=$(cd "$(dirname "$0")" && pwd)                  # gpu/hwcodec/kern/p031x
H=$(cd "$P/.." && pwd)                            # gpu/hwcodec/kern
W=$(cd "$H/../../.." && pwd)                      # work root
BASE=$W/gpu/hwcodec/kernel/b
SRC=${N2P_SRC_DEV:-$H/bsd/dev}
INC=$W/gpu/hwcodec/include
Q10IN=$W/gpu/build/device-mmu-q10in-candidate/k/kernel/stage/bsd/dev
T=/tmp/n2hw-opus/series
MODE=${1:-write}

fail() { echo "MAKE_SERIES FAILED: $*" >&2; exit 1; }
for f in "$BASE/bsd/dev/sweet_touch.c" "$SRC/nord2_vcodec_relay.inc" "$SRC/nord2_vcodec.inc" "$SRC/nord2_jpegdev.inc" \
         "$SRC/sweet_touch.c.patch" "$INC/redwood_vcodec.h" "$INC/redwood_jpeg.h" "$P/bsd/dev/nord2_vcodec_at.inc" \
         "$P/bsd/dev/sweet_touch.c.patch" "$P/bsd/dev/nord2_autotest.inc.patch" "$Q10IN/sweet_touch.c"; do
	[ -e "$f" ] || fail "missing $f"
done
for f in sweet_touch.c nord2_autotest.inc nord2_hwcodec.inc; do
	cmp -s "$BASE/bsd/dev/$f" "$Q10IN/$f" || fail "kernel/b/bsd/dev/$f differs from the q10in stage: the base is not the phone's"
done

rm -rf "$T"; mkdir -p "$T"
mkpatch() {   # $1 = a dir, $2 = b dir, $3 = output
	set +e; (cd "$T" && diff -ruN "$1" "$2" > "$3"); local rc=$?; set -e
	[ "$rc" -eq 1 ] || fail "diff $1 $2 rc=$rc"
	sed -i -E "s#^(--- )$1/([^\t]*)\t.*\$#\1a/\2#; s#^(\+\+\+ )$2/([^\t]*)\t.*\$#\1b/\2#; s#^diff -ruN $1/([^ ]*) $2/.*\$#diff -ruN a/\1 b/\1#" "$3"
}

# ---- 0310: kernel/b -> + IK hooks, the three .inc, the two headers; 0299 -> 0310 in added lines
mkdir -p "$T/a" && cp -a "$BASE/." "$T/a/"
cp -a "$T/a" "$T/b"
(cd "$T/b" && patch -p1 -s --no-backup-if-mismatch -i "$SRC/sweet_touch.c.patch") || fail "IK sweet_touch.c.patch does not apply to kernel/b"
cp "$SRC/nord2_vcodec_relay.inc" "$SRC/nord2_vcodec.inc" "$SRC/nord2_jpegdev.inc" "$T/b/bsd/dev/"
cp "$INC/redwood_vcodec.h" "$INC/redwood_jpeg.h" "$T/b/bsd/dev/"
sed -i -E '/^\s*(#include|nord2_.*_start\(\);)/ s/\b0299\b/0310/g' "$T/b/bsd/dev/sweet_touch.c"
for f in nord2_vcodec_relay.inc nord2_vcodec.inc nord2_jpegdev.inc; do sed -i -E 's/\b0299\b/0310/g' "$T/b/bsd/dev/$f"; done
mkpatch a b "$T/0310-nord2-jpegdev-vcodec.patch"

# ---- 0311: + nord2_vcodec_at.inc, its include, autotest command 8
cp -a "$T/b" "$T/c"
cp "$P/bsd/dev/nord2_vcodec_at.inc" "$T/c/bsd/dev/"
(cd "$T/c" && patch -p1 -s --no-backup-if-mismatch -i "$P/bsd/dev/sweet_touch.c.patch") || fail "p031x sweet_touch.c.patch does not apply after 0310"
(cd "$T/c" && patch -p1 -s --no-backup-if-mismatch -i "$P/bsd/dev/nord2_autotest.inc.patch") || fail "nord2_autotest.inc.patch does not apply"
mkpatch b c "$T/0311-nord2-vcodec-venc-autotest.patch"

# ---- verify on the q10in stage (the phone's source state): 0310 then 0311 apply and give exactly tree c
mkdir -p "$T/q/bsd" && cp -a "$Q10IN" "$T/q/bsd/dev"
(cd "$T/q" && patch -p1 -s --no-backup-if-mismatch -i "$T/0310-nord2-jpegdev-vcodec.patch") || fail "0310 does not apply to the q10in stage"
(cd "$T/q" && patch -p1 -s --no-backup-if-mismatch -i "$T/0311-nord2-vcodec-venc-autotest.patch") || fail "0311 does not apply after 0310"
for f in sweet_touch.c nord2_autotest.inc nord2_hwcodec.inc nord2_vcodec_relay.inc nord2_vcodec.inc nord2_jpegdev.inc nord2_vcodec_at.inc redwood_vcodec.h redwood_jpeg.h; do
	cmp -s "$T/c/bsd/dev/$f" "$T/q/bsd/dev/$f" || fail "q10in result differs: bsd/dev/$f"
done
grep -q 'nord2_vcodec_at.inc' "$T/q/bsd/dev/sweet_touch.c" || fail "include missing"
if grep -n '\b0299\b' "$T/q/bsd/dev/sweet_touch.c" | grep -v 'surface' ; then fail "a 0299 tag is left in sweet_touch.c"; fi
# ---- the hand-made deltas (not regenerated here): 0312 venc_fast, 0313 vcodec pool 384 MiB at 0xA0000000.  They must
# still apply, in order, on the q10in stage + 0310 + 0311 (the series of build_q10jg/q10jh).
for n in 0312-nord2-vcodec-venc-fast 0313-nord2-vcodec-pool-384m 0314-nord2-vcodec-adaptive-wait 0315-nord2-jpeg-hw-decode-path 0316-nord2-jpeg-ioctl-kernel-buffer 0317-nord2-jpeg-encode-eoi-trim-128; do
	[ -e "$P/$n.patch" ] || continue
	(cd "$T/q" && patch -p1 -s --no-backup-if-mismatch -i "$P/$n.patch") || fail "$n does not apply after 0310/0311"
done

if [ "$MODE" = "--check" ]; then
	for n in 0310-nord2-jpegdev-vcodec.patch 0311-nord2-vcodec-venc-autotest.patch; do
		cmp -s "$T/$n" "$P/$n" || fail "$n is stale: the sources changed since it was written (run make_series.sh)"
	done
	echo "MAKE_SERIES CHECK OK (pinned patches match the live sources)"
	exit 0
fi
cp "$T/0310-nord2-jpegdev-vcodec.patch" "$T/0311-nord2-vcodec-venc-autotest.patch" "$P/"
{
	echo "# written by make_series.sh $(date -u +%Y-%m-%dT%H:%M:%SZ); inputs then outputs (sha256)"
	(cd "$SRC" && sha256sum nord2_vcodec_relay.inc nord2_vcodec.inc nord2_jpegdev.inc sweet_touch.c.patch)
	(cd "$INC" && sha256sum redwood_vcodec.h redwood_jpeg.h)
	(cd "$P/bsd/dev" && sha256sum nord2_vcodec_at.inc sweet_touch.c.patch nord2_autotest.inc.patch)
	(cd "$P" && sha256sum 0310-nord2-jpegdev-vcodec.patch 0311-nord2-vcodec-venc-autotest.patch)
} > "$P/MANIFEST.txt"
echo "0310: $(wc -l < "$P/0310-nord2-jpegdev-vcodec.patch") lines, 0311: $(wc -l < "$P/0311-nord2-vcodec-venc-autotest.patch") lines"
echo "MAKE_SERIES OK (applied 0310 + 0311 to the q10in stage)"
