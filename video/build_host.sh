#!/bin/bash
# SPDX-License-Identifier: MIT
#
# build_host.sh [package] -- the host build entry of the video work.
#
# With a package name (vt, avc, kern, gpu, fake, probe, vectors, oracle, or any directory under gpu/video) it runs every
# run_tests.sh of that package (<pkg>/run_tests.sh and <pkg>/<sub>/run_tests.sh -- vt has two suites, for example), prints
# each suite's last line and a total, and exits 1 if any suite failed. Without a name it is run_all_tests.sh: the same over
# every package.
#
# The host builds are Linux x86-64 clang builds of the same sources; they are NOT the phone, and nothing here says anything
# about iOS except that the code compiles with the flags of PLAN.md 4.3.
set -euo pipefail
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)

if [ $# -eq 0 ]; then
	exec bash "$HERE/run_all_tests.sh"
fi

PKG=$1
DIR=$HERE/$PKG
[ -d "$DIR" ] || { echo "build_host: no package directory $DIR" >&2; exit 1; }

mapfile -t SUITES < <(
	{
		[ -f "$DIR/run_tests.sh" ] && echo "$DIR/run_tests.sh"
		for f in "$DIR"/*/run_tests.sh; do [ -f "$f" ] && echo "$f"; done
	} | sort -u
)
[ ${#SUITES[@]} -gt 0 ] || { echo "build_host: $PKG has no run_tests.sh" >&2; exit 1; }

failed=0
for suite in "${SUITES[@]}"; do
	name=${suite#"$HERE"/}
	log=$(mktemp)
	if bash "$suite" >"$log" 2>&1; then
		printf 'ok    %-34s %s\n' "$name" "$(tail -n 1 "$log")"
	else
		failed=$((failed + 1))
		printf 'FAIL  %-34s %s\n' "$name" "$(tail -n 1 "$log")"
		echo "----- last 25 lines of $name -----"
		tail -n 25 "$log"
		echo "-----------------------------------"
	fi
	rm -f "$log"
done

if [ $failed -ne 0 ]; then
	echo "build_host: $PKG FAIL ($failed of ${#SUITES[@]} suite(s))"
	exit 1
fi
echo "build_host: $PKG PASS (${#SUITES[@]} suite(s))"
