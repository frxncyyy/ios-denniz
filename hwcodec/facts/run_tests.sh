#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# D01 test: the import census, the instruction census, the /dev/vcu usage scan, and the FACTS.md lint.
# Everything is built/written under /tmp/n2hw-d01/. Reads the vendor objects from the extracted /vendor
# copy at run time (N2HW_VENDOR_DIR overrides). No phone, no network, no writes in the tree.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1   # no __pycache__ in the tree

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(cd "$HERE/../../.." && pwd)"        # .../work
OUT=/tmp/n2hw-d01
mkdir -p "$OUT"
PASS=0
FAIL=0
ok()   { PASS=$((PASS+1)); }
bad()  { FAIL=$((FAIL+1)); echo "FAIL: $*"; }
check(){ if eval "$2"; then ok; else bad "$1"; fi; }

rm -rf "$HERE/__pycache__"          # a stale one from an interactive import must not fail the test
VENDOR="${N2HW_VENDOR_DIR:-$WORK/gpu/build/cam-vendor-20261109/vfs}"
echo "D01: vendor=$VENDOR"

# ---------------------------------------------------------------- 1. imports
python3 "$HERE/imports.py" > "$OUT/imports.txt"
tail="$(tail -1 "$OUT/imports.txt")"
check "imports.py last line is '149 imports' (got '$tail')" '[ "$tail" = "149 imports" ]'
check "six shim tables present" '[ "$(grep -c "^== " "$OUT/imports.txt")" -eq 6 ]'
sum=0
for t in string heap thread stdio android data; do
  n="$(python3 "$HERE/imports.py" --table "$t" | grep -c '^  ')"
  sum=$((sum + n))
done
check "per-table counts sum to 149 (got $sum)" '[ "$sum" -eq 149 ]'
for s in malloc __android_log_print stderr __stack_chk_guard pthread_mutex_lock ioctl; do
  check "import $s is in the list" "grep -q '^  $s ' '$OUT/imports.txt'"
done
check "imports.py --per-object lists vpud" "python3 '$HERE/imports.py' --per-object | grep -q '^bin/vpud:'"
if N2HW_VENDOR_DIR=/nonexistent python3 "$HERE/imports.py" >/dev/null 2>&1; then
  bad "imports.py accepted a missing vendor directory"
else
  ok
fi

# ---------------------------------------------------------------- 2. census
python3 "$HERE/census.py" --json "$OUT/census.json" --min-coverage 60 > "$OUT/census.txt" || true
grep -E "^(vpud|lib)" "$OUT/census.txt" > "$OUT/census-summary.txt"
check "census covered all five objects" '[ "$(wc -l < "$OUT/census-summary.txt")" -eq 5 ]'
check "census found more than 300000 reachable instructions" \
  "python3 - <<'EOF'
import json,sys
d=json.load(open('$OUT/census.json'))
n=sum(sum(r['insns'].values()) for r in d.values())
print('reachable instructions:', n)
sys.exit(0 if n > 300000 else 1)
EOF"
check "no reachable A32 instruction (all Thumb)" \
  "python3 - <<'EOF'
import json,sys
d=json.load(open('$OUT/census.json'))
n=sum(r['insns'].get('ARM',0) for r in d.values())
print('A32 instructions:', n)
sys.exit(0 if n == 0 else 1)
EOF"
check "every object has a V14 (16-bit T32) count above 1000" \
  "python3 - <<'EOF'
import json,sys
d=json.load(open('$OUT/census.json'))
for name,r in d.items():
    n=sum(v for k,v in r['classes'].get('5',{}).items())
    if n < 1000:
        print(name, n); sys.exit(1)
EOF"
check "the five documented coverage figures are within 3 points" \
  "python3 - <<'EOF'
import json,sys
want={'vpud':94.66,'libvpud_vcodec.so':67.01,'libvcodecdrv.so':88.27,'libvcodec_utility.so':78.11,'libvcodec_oal.so':100.09}
d=json.load(open('$OUT/census.json'))
bad=0
for k,v in want.items():
    got=d[k]['fn_coverage_pct']
    if abs(got-v)>3.0:
        print('%s %.2f vs %.2f' % (k,got,v)); bad=1
sys.exit(bad)
EOF"
check "undecodable addresses are all inside .text" \
  "python3 - <<'EOF'
import json,sys
d=json.load(open('$OUT/census.json'))
for r in d.values():
    lo,size=r['text']
    for mode,a in r['undecodable']:
        if not (lo <= a < lo+size): sys.exit(1)
EOF"
check "census.py --classes names V14..V20" "python3 '$HERE/census.py' --classes --min-coverage 60 --library libvcodec_oal.so | grep -q 'V14'"

# ---------------------------------------------------------------- 3. VCU usage
python3 "$HERE/vcu_usage.py" > "$OUT/vcu.txt"
for pair in "0x13cb4 0x40587600" "0x17f3e 0xc0207606" "0x18444 0xc0207607" "0x18ba0 0x40207608" "0x18cac 0x40107609"; do
  site="${pair% *}"; req="${pair#* }"
  check "ioctl $req at $site" "grep -q '$site .*ioctl.*$req' '$OUT/vcu.txt'"
done
check "/dev/vcu is opened" "grep -q \"path='/dev/vcu'\" '$OUT/vcu.txt'"
check "/dev/vpu is opened by the decoder path" "grep -q \"path='/dev/vpu'\" '$OUT/vcu.txt'"
check "the VDEC register window mmap uses offset 0x16000000" "grep -q '0x1e1ea .*mmap' '$OUT/vcu.txt'"
check "/dev/mtk_sched is opened" "grep -q \"path='/dev/mtk_sched'\" '$OUT/vcu.txt'"

# ---------------------------------------------------------------- 4. FACTS.md lint
check "FACTS.md has the eight required sections" \
  "python3 - <<'EOF'
import sys
t=open('$WORK/gpu/hwcodec/FACTS.md').read()
need=['## 1. Hardware','## 2. Phone results','## 3. Imports','## 4. Instruction census','## 5. VCU ABI','## 6. AP <-> VCU','## 7. iOS side','## 8. Open questions']
sys.exit(0 if all(n in t for n in need) else 1)
EOF"
check "every FACTS.md table row carries a [V] or [I] mark and an evidence column" \
  "python3 - <<'EOF'
import sys
bad=[]
for i,l in enumerate(open('$WORK/gpu/hwcodec/FACTS.md'),1):
    if not l.startswith('| ') or l.startswith('| # |') or l.startswith('|---'):
        continue
    cells=[c.strip() for c in l.strip().strip('|').split('|')]
    if len(cells) != 5:
        bad.append((i,'%d cells' % len(cells)))
        continue
    if cells[3] not in ('[V]','[I]','[V]/[I]'):
        bad.append((i,'mark %r' % cells[3]))
    if not cells[2] or cells[2] in ('-','?'):
        bad.append((i,'no evidence'))
for b in bad[:10]: print(b)
sys.exit(1 if bad else 0)
EOF"
check "no TODO/TBD/FIXME in FACTS.md" "! grep -qE 'TODO|TBD|FIXME' '$WORK/gpu/hwcodec/FACTS.md'"
check "facts/ has no file over 300 kB" '[ -z "$(find "$WORK/gpu/hwcodec/facts" -type f -size +300k)" ]'
check "facts/ has no binaries" '[ -z "$(find "$WORK/gpu/hwcodec/facts" -type f \( -name "*.o" -o -name "*.so" -o -name "*.a" -o -name "*.pyc" \))" ]'

echo "checks passed: $PASS, failed: $FAIL"
if [ "$FAIL" -ne 0 ]; then
  echo "N2TEST D01 FAIL $FAIL checks"
  exit 1
fi
echo "N2TEST D01 PASS $PASS/$PASS"
