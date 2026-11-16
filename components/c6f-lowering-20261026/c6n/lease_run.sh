#!/usr/bin/env bash
# Usage: lease_run.sh <log> <command...>: wait for the shared heavy lease, then run nice 10.
log=$1;shift
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan|fsjobs|probe_host)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 10; done
cd $NORD2_WORK/gpu
nice -n 10 "$@" > "$log" 2>&1; echo "rc=$?" >> "$log"
