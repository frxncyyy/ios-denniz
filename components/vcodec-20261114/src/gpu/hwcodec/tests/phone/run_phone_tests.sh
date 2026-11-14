#!/bin/bash
set -euo pipefail
# T02 host-side wrapper for the phone test.  It never flashes, resets or
# writes the phone: it calls the guest runner through $PHONE_EXEC and reads
# the kernel log through the existing autotest channel.
#
#   PHONE_EXEC="ssh root@nord2" PHONE_GET="ssh root@nord2 cat" \
#     bash gpu/hwcodec/tests/phone/run_phone_tests.sh q10hw2 [preflight|jpeg|vcodec|daemon|all]
#
# PHONE_EXEC is any command prefix that runs one shell command on the phone
# (ssh, a serial console helper, or the main session's guest exec).  If it is
# empty the script prints the guest commands and exits 2; it does not guess.
# Results go to /tmp/n2hw-T02/phone/<name>/.
ID=T02-phone
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
W="$(cd "$ROOT/../.." && pwd)"          # the work tree root
NAME=${1:?usage: run_phone_tests.sh NAME [stage]}
STAGE=${2:-all}
OUT="/tmp/n2hw-T02/phone/$NAME"
GUEST=${GUEST:-/usr/local/bin/n2hw_phone_tests}
PHONE_EXEC=${PHONE_EXEC:-}
PHONE_GET=${PHONE_GET:-}
mkdir -p "$OUT"

if [ -z "$PHONE_EXEC" ]; then
	cat >&2 <<EOF
PHONE_EXEC is not set.  Run the guest runner on the phone yourself:

  STAGE=$STAGE $GUEST
  cat /var/tmp/n2hw/results.txt

then fetch the files under /var/tmp/n2hw/ (hw640.jpg, out.h264, *.log).
The exact expected lines are in $HERE/README.md and the milestone files.
If there is no shell/exec path, use $HERE/com.nord2.hwcodec.tests.plist
(see $HERE/INSTALL.md).
EOF
	exit 2
fi
read -r -a EXEC <<<"$PHONE_EXEC"

PY="$W/gpu/build/autotest/phone.py"
if [ ! -f "$PY" ]; then
	echo "N2TEST $ID FAIL no $PY" >&2
	exit 1
fi

echo "== wait for the autotest channel"
python3 "$PY" wait
echo "== log offset before the test"
python3 "$PY" logs "$OUT/before.log" --seconds 1
echo "== guest tests (STAGE=$STAGE)"
"${EXEC[@]}" "STAGE=$STAGE $GUEST" 2>&1 | tee "$OUT/guest.out" || true
"${EXEC[@]}" "cat /var/tmp/n2hw/results.txt" >"$OUT/results.txt" 2>&1 || true
echo "== kernel log during the test"
python3 "$PY" logs "$OUT/after.log" --seconds 1

echo "== results"
cat "$OUT/results.txt" 2>/dev/null || true

rc=0
if ! grep -q '^N2HW PHONE PASS ' "$OUT/results.txt" 2>/dev/null; then
	echo "N2TEST $ID FAIL guest runner did not report PASS" >&2
	rc=1
fi
if grep -Eq 'NORD2 VCODEC: .*(timeout|not powered|failed|still|CANNOT)' "$OUT/after.log" 2>/dev/null; then
	echo "N2TEST $ID FAIL kernel VCODEC error line (see $OUT/after.log)" >&2
	rc=1
fi
if [ "$STAGE" = all ] || [ "$STAGE" = jpeg ]; then
	if ! grep -q 'NORD2 JPEG: 640x480' "$OUT/after.log" 2>/dev/null; then
		echo "N2TEST $ID WARN no 'NORD2 JPEG: 640x480' line (J02 tool not installed?)" >&2
	fi
fi
if [ -n "$PHONE_GET" ]; then
	echo "== fetch files"
	for f in hw640.jpg out.h264 rwvc-read.log rwjpeg-selftest.log; do
		# PHONE_GET is a command prefix that prints the remote file to stdout,
		# e.g. PHONE_GET="ssh root@nord2 cat".
		$PHONE_GET "/var/tmp/n2hw/$f" >"$OUT/$f" || true
	done
fi

if [ "$rc" -eq 0 ]; then
	echo "N2TEST $ID PASS"
else
	echo "N2TEST $ID FAIL"
fi
exit "$rc"
