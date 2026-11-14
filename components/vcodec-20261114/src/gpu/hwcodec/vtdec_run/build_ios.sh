#!/bin/bash
# SPDX-License-Identifier: MIT
# decrun (12 Nov 2026): builds n2vt_decrun for iOS 16 arm64 with gpu/video's toolchain (same flags, sysinc, TBD stubs and
# ad-hoc signature as n2vt_probe in gpu/video/build_ios.sh; the existing gpu/video/sdk/lib/*.tbd are used as they are,
# make_tbds.py is not re-run).  usage: build_ios.sh [OUTDIR]   (default /tmp/n2hw-decrun/ios)   prints DECRUN BUILD OK
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); VIDEO=$(cd "$HERE/../../video" && pwd)
OUT=${1:-/tmp/n2hw-decrun/ios}; mkdir -p "$OUT"
RES=$(clang -print-resource-dir)
CFLAGS=(-std=c11 -O2 -fno-common -fvisibility=hidden -ffreestanding -nostdinc -isystem "$RES/include" -isystem "$VIDEO/sdk/sysinc"
        -I "$VIDEO/include" -I "$VIDEO/probe" -Wall -Wextra)
# dr5: the host reference (expected.json, expected_md5.py) compiled in, so the phone prints only a compact comparison
python3 - "$HERE/expected.json" "$OUT/n2vt_expected.h" <<'PY2'
import json, sys
e = json.load(open(sys.argv[1])); o = open(sys.argv[2], 'w')
o.write('/* generated from expected.json by build_ios.sh */\nstruct n2dr_exp { const char *clip; int n; const char *const *md5; };\n')
for i, (k, v) in enumerate(sorted(e.items())):
    o.write('static const char *const n2dr_md5_%d[] = {%s};\n' % (i, ','.join('"%s"' % m for m in v['md5'])))
o.write('static const struct n2dr_exp n2dr_exp[] = {%s};\n' % ','.join('{"%s",%d,n2dr_md5_%d}' % (k, len(v['md5']), i) for i, (k, v) in enumerate(sorted(e.items()))))
o.write('#define N2DR_NEXP %d\n' % len(e))
PY2
clang -c --target=arm64-apple-ios16.0 "${CFLAGS[@]}" -DN2DR_EXPECTED -I "$OUT" -o "$OUT/n2vt_decrun.o" "$HERE/n2vt_decrun.c"
STRONG=(); WEAK=()
for t in "$VIDEO"/sdk/lib/*.tbd; do case $(basename "$t") in libSystem.tbd|CoreFoundation.tbd) STRONG+=("$t") ;; *) WEAK+=(-weak_library "$t") ;; esac; done
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign -dead_strip_dylibs -o "$OUT/n2vt_decrun" "$OUT/n2vt_decrun.o" \
	"${STRONG[@]}" "${WEAK[@]}"
python3 - "$VIDEO" "$OUT/n2vt_decrun" <<'PY'
import glob, os, subprocess, sys
covered = set()
for tbd in glob.glob(os.path.join(sys.argv[1], 'sdk', 'lib', '*.tbd')):
    for line in open(tbd):
        if line.startswith('      - '): covered.add(line[8:].strip())
und = [l.strip() for l in subprocess.run(['llvm-nm', '-u', sys.argv[2]], capture_output=True, text=True, check=True).stdout.splitlines() if l.strip()]
miss = [u for u in und if u not in covered]
if miss: sys.exit('undefined symbols not covered by a TBD: ' + ' '.join(miss))
print('   undefined symbols: %d, all covered' % len(und))
PY
file "$OUT/n2vt_decrun" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "not an arm64 Mach-O" >&2; exit 1; }
llvm-objdump --macho --dylibs-used "$OUT/n2vt_decrun" | sed 1d
sha256sum "$OUT/n2vt_decrun"; echo "DECRUN BUILD OK $OUT/n2vt_decrun"
