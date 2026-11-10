#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run_all_tests.sh -- every package's host test suite, in name order. Each package has <pkg>/run_tests.sh or
# <pkg>/<sub>/run_tests.sh (the layout of PLAN.md 4.1: vt/test, fake/test, avc/test, ...). The script prints each suite's
# last line and a total, and exits 1 if any suite failed (with the tail of the failing suite's output).
#
# usage: run_all_tests.sh [outdir] -- outdir is passed to each suite as its first argument (default: gpu/video/build/<id>).
set -uo pipefail
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
OUTARG=${1:-}

mapfile -t SUITES < <(
	{
		for f in "$HERE"/*/run_tests.sh; do [ -f "$f" ] && echo "$f"; done
		for f in "$HERE"/*/*/run_tests.sh; do [ -f "$f" ] && echo "$f"; done
	} | sort -u
)

if [ ${#SUITES[@]} -eq 0 ]; then
	echo "run_all_tests: no */run_tests.sh found under $HERE"
	exit 1
fi

passed=0
failed=0
total=0
for suite in "${SUITES[@]}"; do
	total=$((total + 1))
	name=${suite#"$HERE"/}
	log=$(mktemp)
	if [ -n "$OUTARG" ]; then
		bash "$suite" "$OUTARG" >"$log" 2>&1
	else
		bash "$suite" >"$log" 2>&1
	fi
	status=$?
	last=$(tail -n 1 "$log")
	if [ $status -eq 0 ]; then
		passed=$((passed + 1))
		printf 'ok    %-34s %s\n' "$name" "$last"
	else
		failed=$((failed + 1))
		printf 'FAIL  %-34s %s\n' "$name" "${last:-<no output>}"
		echo "----- last 25 lines of $name -----"
		tail -n 25 "$log"
		echo "-----------------------------------"
	fi
	rm -f "$log"
done

printf '%d/%d suite(s) passed, %d failed\n' "$passed" "$total" "$failed"
if [ $failed -ne 0 ]; then
	echo "run_all_tests: FAIL"
	exit 1
fi
echo "run_all_tests: PASS"
