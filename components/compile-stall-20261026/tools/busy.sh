#!/bin/bash
# exit 0 if a heavy job (native build/test/replay) of this checkout runs
for p in $(pgrep -x clang; pgrep -x clang-23; pgrep -x cargo; pgrep -x rustc; pgrep -x ninja; pgrep -x cc1; pgrep -x python3); do
  c=$(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null)
  case "$c" in *busy.sh*) continue;; esac
  case "$c" in *run_host_tests*|*_host_test*|*integration_test*|*frame_bench*|*cargo\ build*|*cargo\ test*|*rustc*|*ninja*|*build_native*) exit 0;; esac
done
exit 1
