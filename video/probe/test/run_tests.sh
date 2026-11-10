#!/bin/bash
# run_tests.sh [outdir] -- WP07's tests.  Three parts, all run from here:
#
#   1. host: build probe/probe_core.c + probe/test/t_core.c with ASan+UBSan, run the six-test runner, validate every
#      JSON line with python3, check the console lines;
#   2. iOS arm64: compile all five probe/*.c for --target=arm64-apple-ios16.0 with -Werror against the minimal
#      test scaffolding in probe/test/iosinc (the real build uses sdk/sysinc, WP01), link build/ios/n2vt_probe from
#      TBDs *generated from probe/symbols.txt*, and require LC_MAIN and no undefined symbol outside those TBDs;
#   3. the launchd plist parses and carries the documented keys.
#
# Last line: N2TEST WP07 PASS <n>/<n>  or  N2TEST WP07 FAIL <what>  (exit 1).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)              # gpu/video
OUT=${1:-$ROOT/build/WP07}
TMP=$OUT/tmp
BIN=$OUT/t_core
JSON=$TMP/probe-1.jsonl
CONSOLE=$TMP/console.txt
LOG=$TMP/test.log
IOS=$OUT/ios
mkdir -p "$TMP" "$IOS"
rm -f "$JSON" "$CONSOLE"

CC=${CC:-clang}
CFLAGS=(-std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined
        -I "$ROOT/include" -I "$ROOT/probe")

checks=0
note_ok() { checks=$((checks + 1)); echo "ok $checks - $1"; }

echo "== 1. build and run t_core (host, ASan+UBSan)"
"$CC" "${CFLAGS[@]}" -o "$BIN" "$ROOT/probe/probe_core.c" "$HERE/t_core.c"
"$CC" "${CFLAGS[@]}" -DPROBE_NO_AV -c -o "$TMP/probe_main.o" "$ROOT/probe/probe_main.c"

fail=0
if ! N2VIDEO_CONSOLE="$CONSOLE" "$BIN" "$TMP" >"$LOG" 2>&1; then
	fail=1
fi
cat "$LOG"
if [ "$fail" -ne 0 ]; then
	echo "N2TEST WP07 FAIL t_core exited non-zero"
	exit 1
fi
n=$(grep -c '^ok ' "$LOG" || true)
bad=$(grep -c '^not ok ' "$LOG" || true)
if [ "${bad:-0}" -ne 0 ]; then
	echo "N2TEST WP07 FAIL $bad of $n host checks failed"
	exit 1
fi
checks=$((checks + n))
note_ok "host t_core: $n checks"

echo "== 2. validate JSON lines with python3"
python3 - "$JSON" "$CONSOLE" <<'PY'
import json, sys

path, cons = sys.argv[1], sys.argv[2]
objs = []
with open(path, "r", encoding="utf-8") as f:
    for i, line in enumerate(f, 1):
        line = line.strip()
        if not line:
            continue
        try:
            o = json.loads(line)
        except Exception as e:
            raise SystemExit("line %d is not JSON: %s: %r" % (i, e, line[:200]))
        for k in ("test", "result", "signal", "ms", "fields"):
            if k not in o:
                raise SystemExit("line %d has no %r: %r" % (i, k, line[:200]))
        if o["result"] not in ("pass", "fail", "skip", "timeout", "crash"):
            raise SystemExit("bad result %r" % (o["result"],))
        if not isinstance(o["signal"], int) or not isinstance(o["ms"], int):
            raise SystemExit("signal/ms are not integers: %r" % (o,))
        if not isinstance(o["fields"], dict):
            raise SystemExit("fields is not an object: %r" % (o,))
        objs.append(o)
results = sorted(o["result"] for o in objs)
if results != ["crash", "fail", "pass", "pass", "pass", "pass", "skip", "timeout"]:
    raise SystemExit("the results are not the expected set: %r" % (results,))
longo = [o for o in objs if o["test"] == "t.long"][0]
if len(longo["fields"].get("long", "")) != 4095:
    raise SystemExit("the long field is %d characters, expected the 4095 truncation" % len(longo["fields"].get("long", "")))
manyo = [o for o in objs if o["test"] == "t.many"][0]
if len(manyo["fields"]) != 128:
    raise SystemExit("the 200-field test kept %d fields, expected the 128-field cap" % len(manyo["fields"]))
esc = [o for o in objs if o["test"] == "t.escape"][0]
if esc["fields"].get("s") != "a\"b\\c\nd\te":
    raise SystemExit("escaped field did not round-trip: %r" % (esc["fields"].get("s"),))
passes = [o for o in objs if o["test"] == "t.pass"][0]
if passes["fields"].get("answer") != "42" or passes["fields"].get("pi") != "3.14":
    raise SystemExit("pass fields wrong: %r" % (passes["fields"],))
crash = [o for o in objs if o["test"] == "t.crash"][0]
if crash["signal"] == 0 and "exit" not in crash["fields"]:
    raise SystemExit("crash line has neither a signal nor an exit code: %r" % (crash,))
with open(cons, "r", encoding="utf-8", errors="replace") as f:
    lines = f.read().splitlines()
for name in ("t.pass", "t.fail", "t.skip", "t.hang", "t.crash", "t.escape"):
    if not any(l.startswith("N2VIDEO probe: " + name + " ") for l in lines):
        raise SystemExit("no console line for %s (file %s)" % (name, cons))
print("json ok: %d lines, results %s" % (len(objs), ",".join(results)))
PY
note_ok "JSON and console lines validated by python3"

echo "== 3. compile all probe sources for arm64-apple-ios16.0"
RES=$(clang -print-resource-dir)
IOSFLAGS=(-c --target=arm64-apple-ios16.0 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc
          -isystem "$RES/include" -isystem "$HERE/iosinc" -I "$ROOT/include" -I "$ROOT/probe"
          -Wall -Wextra -Werror -DPROBE_NO_AV)
for f in probe_core probe_main probe_vt probe_img probe_audio; do
	clang "${IOSFLAGS[@]}" -o "$IOS/$f.o" "$ROOT/probe/$f.c"
done
note_ok "five sources compile for iOS with -Werror"

echo "== 4. link n2vt_probe from TBDs generated from probe/symbols.txt"
mkdir -p "$OUT/tbd"
python3 - "$ROOT/probe/symbols.txt" "$OUT/tbd" <<'PY'
import collections, os, sys
src, outdir = sys.argv[1], sys.argv[2]
libs = collections.OrderedDict()
for line in open(src):
    line = line.rstrip("\n")
    if not line.strip():
        continue
    lib, sym = line.split("\t")
    libs.setdefault(lib, []).append(sym)
for lib, syms in libs.items():
    name = os.path.basename(lib).replace(".framework", "")
    if lib.startswith("/usr/lib"):
        name = "libSystem"
    body = ("--- !tapi-tbd\ntbd-version: 4\ntargets: [ arm64-ios ]\ninstall-name: '%s'\ncurrent-version: 1\nexports:\n"
            "  - targets: [ arm64-ios ]\n    symbols: [ %s ]\n...\n") % (lib, ", ".join(syms))
    with open(os.path.join(outdir, name + ".tbd"), "w") as f:
        f.write(body)
print("tbd ok: %d libraries" % len(libs))
PY
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -o "$IOS/n2vt_probe" \
	"$IOS/probe_core.o" "$IOS/probe_main.o" "$IOS/probe_vt.o" "$IOS/probe_img.o" "$IOS/probe_audio.o" \
	"$OUT"/tbd/*.tbd -e _main
file "$IOS/n2vt_probe"
llvm-objdump --macho --private-headers "$IOS/n2vt_probe" | grep -q LC_MAIN
note_ok "linked Mach-O with LC_MAIN"
llvm-nm -u "$IOS/n2vt_probe" | sed 's/^ *//' | sort -u > "$TMP/imports.txt"
cut -f2 "$ROOT/probe/symbols.txt" | sort -u > "$TMP/listed.txt"
if ! diff -u "$TMP/listed.txt" "$TMP/imports.txt" >"$TMP/imports.diff"; then
	echo "N2TEST WP07 FAIL probe/symbols.txt and the linked imports differ:"
	cat "$TMP/imports.diff"
	exit 1
fi
note_ok "the $(wc -l < "$TMP/imports.txt" | tr -d ' ') imported symbols are exactly probe/symbols.txt"

echo "== 5. launchd plist"
python3 - "$ROOT/probe/com.nord2.videoprobe.plist" <<'PY'
import plistlib, sys
d = plistlib.load(open(sys.argv[1], "rb"))
assert d["Label"] == "com.nord2.videoprobe", d
assert d["ProgramArguments"] == ["/usr/libexec/n2vt_probe"], d
assert d["RunAtLoad"] is True and d["UserName"] == "root" and d["ProcessType"] == "Background", d
assert "KeepAlive" not in d, d
assert d["StandardErrorPath"] == "/private/var/db/nord2-video/probe.stderr", d
print("plist ok: %s" % d["Label"])
PY
note_ok "plist parses and carries the documented keys"

echo "N2TEST WP07 PASS $checks/$checks"
