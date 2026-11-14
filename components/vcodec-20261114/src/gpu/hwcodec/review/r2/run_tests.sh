#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_tests.sh -- self-check of the R2 review package.  It does NOT re-run the reviewed packages'
# suites (those are their own oracles and are run separately by the reviewer and by R1/hw-lead);
# it checks that the review deliverable is complete and that this directory obeys the worker rules.
#
#   bash gpu/hwcodec/review/r2/run_tests.sh
#
# Optional: N2R2_FULL=1 also runs the K01/K02/J02/IP/D01 package suites and prints their last lines.
set -euo pipefail

H=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$H/../../../.." && pwd)
F=$H/FINDINGS.md
S=$H/STATUS.md
pass=0
fail=0

ck() { if eval "$2"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL $1"; fi; }

[ -f "$F" ] || { echo "N2TEST R2 FAIL FINDINGS.md missing"; exit 1; }
[ -f "$S" ] || { echo "N2TEST R2 FAIL STATUS.md missing"; exit 1; }

# 1. every package of tracks K, J, P, D has a verdict row
for p in K01 K02 IK J02 J04 IP P03 P04 D01; do
	ck "verdict row $p" "grep -q '^| $p |' '$F'"
done

# 2. the acceptance-relevant sections exist
ck "findings table" "grep -q '^## Findings' '$F'"
ck "rule sweep section" "grep -q '^## Rule-compliance sweep' '$F'"
ck "test evidence section" "grep -q '^## Tests run' '$F'"
ck "limits section" "grep -q '^## Limits of this review' '$F'"

# 3. the high findings named in the DONE message are present and have an owner/status
for id in F1 F2 F3 F6 F9 F14 F15 F22; do
	ck "finding $id present" "grep -q '^| $id |' '$F'"
done

# 4. no finding row is empty of a status or severity
ck "no malformed finding rows" "! grep -E '^\\| F[0-9]+ \\|[^|]*\\| *\\| *\\|' '$F'"

# 5. rule compliance of this directory
ck "review dir <2MB" "[ \$(du -sk '$H' | cut -f1) -lt 2048 ]"
ck "no file >300k" "[ -z \"\$(find '$H' -type f -size +300k -print -quit)\" ]"
ck "no binaries" "[ -z \"\$(find '$H' -type f \\( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.dylib' \\) -print -quit)\" ]"
ck "FINDINGS starts with SPDX" "head -1 '$F' | grep -q 'SPDX-License-Identifier: MIT'"
ck "STATUS starts with SPDX-equivalent" "grep -q 'SPDX-License-Identifier: MIT' '$S'"

# 6. optional: the reviewed packages' own suites (last lines only)
if [ "${N2R2_FULL:-0}" = 1 ]; then
	for d in kern/test_k01 kern/test_k02 jpeg/test_j02 daemon/test facts; do
		echo "== $d =="
		if bash "$W/gpu/hwcodec/$d/run_tests.sh" 2>&1 | tail -1; then pass=$((pass + 1)); else fail=$((fail + 1)); fi
	done
fi

total=$((pass + fail))
if [ "$fail" -ne 0 ]; then
	echo "N2TEST R2 FAIL $fail/$total"
	exit 1
fi
echo "N2TEST R2 PASS $pass/$pass"
