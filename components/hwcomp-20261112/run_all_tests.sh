#!/bin/bash
# Host verification of the display HW composition work (all tracks) + the display patches it builds on. Fails loudly (exit 1, FAILED list).
#   run_all_tests.sh [-k PATTERN] [--rehearse] [--preflight]
# Discovery (re-run whenever a track adds a test; nothing to register):
#   test dirs  = gpu/build/{fbblend,fbretain,fbsync,hwcomp}-20261112/test and every hwcomp-20261112/**/test (depth <= 3)
#   in a dir with run_tests.sh / run_test.sh: that script is the test (exit 0 required);
#   otherwise every *.c / *.cpp in it is one test: cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined, include path
#     = the -I/-D flags of the test's own recipe line (first comment line starting with cc/c++/clang, relative to the test dir), then
#     <test>/../b/{iokit/Kernel/arm,bsd/dev}, <test>/.., every hwcomp-20261112/*/b (track dirs like pool-0394/b), hwcomp-20261112
#     (contract headers), hwcomp-20261112/b, the q10jfh stage (so a track's own b/ wins, then the shipped sources). Leak checks off. -lm. A first-line comment containing
#     "RUN_ALL_TESTS_SKIP" skips a file (e.g. a helper included by another test). NOWERROR=1 drops -Werror.
#   pass = exit 0 and no output line starting with "FAIL" (a line "ALL OK"/"ok" is not required); 300 s timeout per test.
#   own:  phone/test_fbhw_verdict.py (verdict parser), fbhwctl/test_fbhwctl.c (mode daemon), fbhwctl iOS link (build_ios.sh),
#         phone/fbhw_scenario.py --dry-run, the build scripts' syntax.
#   --rehearse: apps-20261104/rehearse-fbhwctl.sh (payload install/undo on a fake root from the mirror; needs mirror/rootfs-mirror.img)
#   --preflight: build_q10jfi.sh and build_q10jfj.sh PREFLIGHT_ONLY=1 with every 039[4-9] patch present in hwcomp-20261112
set -uo pipefail
H=$(cd "$(dirname "$0")" && pwd); B=$(dirname "$H"); ST=$B/device-mmu-q10jfh-candidate/k/kernel/stage
T=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$T"' EXIT
PAT=""; REH=0; PRE=0
while [ $# -gt 0 ]; do case $1 in -k) PAT=$2; shift;; --rehearse) REH=1;; --preflight) PRE=1;; *) echo "unknown $1"; exit 2;; esac; shift; done
pass=0; FAILED=()
run() { # name cmd...   (runs, checks, prints one line)
  local n=$1; shift; [ -n "$PAT" ] && [[ $n != *$PAT* ]] && return
  local o=$T/out.$pass.$RANDOM t0=$SECONDS
  if [ "$(type -t "$1")" = function ]; then "$@" > "$o" 2>&1; else timeout 300 "$@" > "$o" 2>&1; fi; local rc=$?
  if [ $rc -ne 0 ] || grep -q '^FAIL' "$o"; then
    FAILED+=("$n"); echo "FAIL  $n (rc=$rc, $((SECONDS - t0)) s)"; grep -E '^FAIL|error|Error|ERROR|runtime error|AddressSanitizer' "$o" | head -15 | sed 's/^/      /'
    [ -s "$o" ] && { echo "      --- last lines:"; tail -5 "$o" | sed 's/^/      /'; }
  else pass=$((pass + 1)); echo "ok    $n ($((SECONDS - t0)) s) $(tail -1 "$o" | cut -c1-90)"; fi
}
export ASAN_OPTIONS=detect_leaks=0   # host tests may keep their buffers; leaks are not what is tested
ccflags="-O1 -g -Wall -Wextra ${NOWERROR:+-Wno-error}"; [ -z "${NOWERROR:-}" ] && ccflags="$ccflags -Werror"
c_test() { # file (a shell function: run() does not wrap it in timeout, the binary gets its own)
  local f=$1 d; d=$(dirname "$f"); local x=$T/$(echo "$f" | md5sum | cut -c1-8)
  # the test's own recipe first (first comment line starting with cc/c++/clang: its -I/-D flags, relative to the test dir), then
  # <test>/../b, every track's hwcomp-20261112/*/b, hwcomp-20261112 itself (contract headers), the shared hwcomp b/, the q10jfh stage
  local own; own=$(grep -m1 -E '^[ /*]*(cc|c\+\+|clang) ' "$f" | grep -oE '(-I|-D) ?[^ ]+' | sed -E "s#^-I ?([^/])#-I$d/\1#" | tr '\n' ' ')
  local inc="$own -I$d/../b/iokit/Kernel/arm -I$d/../b/bsd/dev -I$d/.."
  for t in $H/*/b; do inc="$inc -I$t/iokit/Kernel/arm -I$t/bsd/dev"; done
  inc="$inc -I$H -I$H/b/iokit/Kernel/arm -I$H/b/bsd/dev -I$ST/iokit/Kernel/arm -I$ST/bsd/dev"
  local c=cc; [[ $f == *.cpp ]] && c=c++
  $c $ccflags -fsanitize=address,undefined -fno-sanitize-recover=all $inc -o "$x" "$f" -lm && (cd "$d" && timeout 300 "$x")
}
DIRS=$( (for d in fbblend fbretain fbsync hwcomp; do [ -d $B/$d-20261112/test ] && echo $B/$d-20261112/test; done
         find $H -maxdepth 3 -type d -name test -not -path '*/.t.*') | sort -u)
echo "== track tests ($(echo $DIRS | wc -w) dirs)"
for d in $DIRS; do
  rel=${d#$B/}
  if [ -x $d/run_tests.sh ] || [ -x $d/run_test.sh ]; then s=$(ls $d/run_tests.sh $d/run_test.sh 2>/dev/null | head -1); run "$rel/$(basename $s)" bash "$s"; continue; fi
  for f in $(ls $d/*.c $d/*.cpp 2>/dev/null); do
    head -1 "$f" | grep -q RUN_ALL_TESTS_SKIP && { echo "skip  $rel/$(basename $f)"; continue; }
    run "$rel/$(basename $f)" c_test "$f"; done
done
echo "== own (track D)"
run phone/test_fbhw_verdict.py python3 $H/phone/test_fbhw_verdict.py
run phone/fbhw_verdict.py-on-fbh1 python3 $H/phone/fbhw_verdict.py $B/autocycle/runs/fbh1/ui-usb/xnu-live.log --mode 0
run fbhwctl/test_fbhwctl.c c_test $H/fbhwctl/test_fbhwctl.c
run fbhwctl/build_ios.sh bash $H/fbhwctl/build_ios.sh $T/fbhwctl
run phone/fbhw_scenario.py-dry python3 $H/phone/fbhw_scenario.py $T/scen --dry-run --rounds 1
run build-scripts-syntax bash -c "bash -n $B/build_q10jfi.sh && bash -n $B/build_q10jfj.sh && bash -n $H/phone/run_fbhw_txn.sh && bash -n $H/fbhwctl/make_payloads.sh && echo syntax ok"
[ $REH = 1 ] && run apps-20261104/rehearse-fbhwctl.sh bash $B/apps-20261104/rehearse-fbhwctl.sh
if [ $PRE = 1 ]; then
  P=$(find $H -maxdepth 3 -name '039[4-9]-*.patch' -not -path '*/patched-sources*' -printf '%f\n' | cut -c1-4 | sort -u | tr '\n' ' ')
  echo "== preflight with patches: [${P}]"
  run preflight-q10jfi env PATCHES="$P" PREFLIGHT_ONLY=1 $B/build_q10jfi.sh pre $T/pre-i
  run preflight-q10jfj env PATCHES="$P" PREFLIGHT_ONLY=1 $B/build_q10jfj.sh pre $T/pre-j
fi
echo
if [ ${#FAILED[@]} -gt 0 ]; then
  echo "########################################################"; echo "### FAILED ${#FAILED[@]} (passed $pass):"; printf '###   %s\n' "${FAILED[@]}"
  echo "########################################################"; exit 1
fi
echo "ALL HOST TESTS OK ($pass)"
