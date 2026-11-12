#!/bin/bash
# compile_check.sh -- the oracle for the 0299 kernel glue (package IK).
#
# 1. regenerates 0299 with make_patch.sh (round trip checked there);
# 2. builds a fresh /tmp/n2hw-ik/stage from the q10at3 stage (the q10ik source state), applies 0298 then 0299
#    exactly as the image build does, and checks the frozen header copies are byte-identical;
# 3. compiles bsd/dev/sweet_touch.c with gpu/tools/xnu_staged_compile.py and the CI flags (-Wall -Wextra -Werror),
#    exactly like gpu/build/autotest/build_p1_candidate.py (which adds only -Wno-unsafe-buffer-usage);
# 4. lists the new undefined symbols against the q10at3 sweet_touch.o and checks every one is defined in the
#    q10at3 kernel image.
#
# Success line (the acceptance line):  KERNEL COMPILE OK, new undefined symbols: 0 unknown
# On a compiler error the log is /tmp/n2hw-ik/compile.log and the error lines are printed with the owning file.
# While K01/K02 have not written their files, test_ik/run_tests.sh runs this with the stand-ins through N2IK_*.
set -euo pipefail

H=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$H/../../.." && pwd)
BASE=${N2IK_BASE:-$W/gpu/hwcodec/kernel/b}
P298=$W/gpu/hwcodec/kernel/0298-nord2-hwcodec-jpeg.patch
STAGE_DEV=${N2IK_STAGE_DEV:-$W/gpu/build/autotest/q10at3/kernel/stage/bsd/dev}
BASE_OBJ=${N2IK_BASE_OBJ:-$W/gpu/build/autotest/q10at3/kernel/sweet_touch.o}
KERNEL=${N2IK_KERNEL:-$W/gpu/build/autotest/q10at3/kernel/kernel.development.nord2-metal-q10at3}
INCLUDE=${N2IK_HEADER_DIR:-$W/gpu/hwcodec/include}
OUT=${N2IK_OUT:-$H/0299-nord2-jpegdev-vcodec.patch}
T=/tmp/n2hw-ik
S=$T/stage
OBJ=$T/sweet_touch.o
LOG=$T/compile.log

fail() { echo "KERNEL COMPILE FAILED: $*" >&2; exit 1; }
for f in "$BASE" "$P298" "$STAGE_DEV" "$BASE_OBJ" "$KERNEL" "$INCLUDE/redwood_vcodec.h" "$INCLUDE/redwood_jpeg.h"; do
	[ -e "$f" ] || fail "missing $f"
done
mkdir -p "$T"

# 1. the patch (same N2IK_* overrides are read by make_patch.sh)
bash "$H/make_patch.sh" > "$T/make_patch.log" 2>&1 || { cat "$T/make_patch.log" >&2; fail "make_patch.sh"; }
tail -2 "$T/make_patch.log"

# 2. the staged tree: q10at3 bsd/dev + 0298 + 0299, as build_p1_candidate.py builds it
rm -rf "$S"
mkdir -p "$S/bsd"
cp -a "$STAGE_DEV" "$S/bsd/dev"
(cd "$S" && patch -p1 -s --no-backup-if-mismatch -i "$P298") || fail "0298 does not apply to the q10at3 stage"
(cd "$S" && patch -p1 -s --no-backup-if-mismatch -i "$OUT") || fail "0299 does not apply after 0298"
for h in redwood_vcodec.h redwood_jpeg.h; do
	cmp -s "$S/bsd/dev/$h" "$INCLUDE/$h" || fail "staged bsd/dev/$h is not byte-identical to gpu/hwcodec/include/$h"
done

# 3. compile with the CI flags of build_p1_candidate.py
rm -f "$OBJ"
if ! nice -n 10 python3 "$W/gpu/tools/xnu_staged_compile.py" "$S/bsd/dev/sweet_touch.c" --out "$OBJ" \
        --extra=-Wno-unsafe-buffer-usage > "$LOG" 2>&1; then
	echo "--- compiler errors (owner: line says which .inc) ---" >&2
	grep -E "error:|warning:" "$LOG" | head -40 >&2 || sed -n 1,40p "$LOG" >&2
	echo "full log: $LOG" >&2
	fail "sweet_touch.c does not compile"
fi
[ -s "$OBJ" ] || fail "no object produced ($OBJ)"

# 4. new undefined symbols vs the q10at3 object, each must be defined in the kernel
llvm-nm -u "$OBJ"      | awk 'NF {print $NF}' | sort -u > "$T/new.und"
llvm-nm -u "$BASE_OBJ" | awk 'NF {print $NF}' | sort -u > "$T/base.und"
comm -13 "$T/base.und" "$T/new.und" > "$T/added.und"
llvm-nm --defined-only "$KERNEL" | awk 'NF {print $NF}' | sort -u > "$T/kernel.def"
comm -23 "$T/added.und" "$T/kernel.def" > "$T/unknown.und"

added=$(wc -l < "$T/added.und")
unknown=$(wc -l < "$T/unknown.und")
obj_bytes=$(wc -c < "$OBJ")
echo "  object: $OBJ ($obj_bytes bytes)"
echo "  new undefined symbols vs q10at3: $added"
if [ "$added" -gt 0 ]; then sed -n 1,20p "$T/added.und" | sed 's/^/    /'; fi
if [ "$unknown" -gt 0 ]; then
	echo "  UNKNOWN (not defined in $KERNEL):" >&2
	sed 's/^/    /' "$T/unknown.und" >&2
	fail "new undefined symbols: $unknown unknown"
fi
echo "KERNEL COMPILE OK, new undefined symbols: $unknown unknown"
