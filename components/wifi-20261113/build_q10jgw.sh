#!/bin/bash
# q10jfi = q10jfh (q10jfe kernel + 0393 fbsync, AE2 bundle cam60-20261112/audio-q10jae.pkg 94fb2f9b...) + the display HW composition
# patches of hwcomp-20261112 (gpu/hwcodec/DISPLAY_HW_COMPOSE.md "Build/test"):
#   0394 track A  SweetSurface.cpp + sweet_fb_hw_pool.h   display pool 0xC0000000..0xD0000000, sysctl kern.n2fbhw_pool
#   0395 track B  Nord2GPU.cpp + nord2_gpu_fence.h        GPU import of pool pages, per-surface write fences
#   0396 track C  SweetFramebuffer.cpp (+sweet_fb_hw.h)   OVL layers, planner, sysctl kern.n2fbhw 0..3, auto-fallback
# GNSS=1 (or build_q10jfj.sh) builds q10jfj instead: the q10jfg merge (GNSS 0350,0351,0353..0359,0362..0364) + 0393 + the same list,
# GNSS FIRST and the display patches LAST (build_p1_candidate.py checks tracked sources against patched-sources-NNNN, NNNN <= last).
#
# usage: build_q10jfi.sh [NAME] [OUTDIR]          (OUTDIR must not exist)
#   PATCHES="0394 0395 0396"   patch list (spaces or commas; ascending; "" = rebuild the base as a recipe check). Each NNNN must be
#                              exactly one NNNN-*.patch under $HWDIR (depth <= 3) or $EXTRA_PATCH_DIRS, with patched-sources-NNNN beside it.
#   PREFLIGHT_ONLY=1           only the dry apply on a copy of the base stage (seconds; run it whenever a track delivers a patch)
#   NOPACK=1                   stop after the kernel
#   WARN_BUDGET=0              extra compiler warnings allowed per compile log over the reference build (q10jfh / q10jfg);
#                              SYSCTL_* macro / sys/sysctl.h warnings are exempt (SYSCTL_EXEMPT=0 to count them)
#   EXTRA_ARGS=""              boot-args appended (the line must stay <= 607 chars; q10jfi has 12 spare, q10jfj 4)
#   SPARE_WARN=8192            boot_spare below this prints the trim list (still an image; boot_spare <= 0 fails)
#   NORD2_PHYS_BASE/NORD2_MEM_SIZE  RAM window (ram5088-20261113): unset = 3776 MiB at 0x1_42000000 (loader3776); 0x100000000/0x13e000000 =
#                              5088 MiB (ram5g-20261103 loader, Wi-Fi layout object patches in build_p1_candidate.py, ADT dram-base/size)
# Checks (any failure exits 1, without "IMAGECHECK_OK"):
#   preflight: every patch applies to the base stage with no fuzz/offset/reject/reversed hunk, in list order, and after patch N every
#     file in patched-sources-N equals the result (catches a track's a/ that is not the real base, and cross-track file overlaps);
#   build: the same apply cleanliness in the real apply-NNNN.log, every file of each patch reported patched, the final stage equal to
#     the newest patched-sources-N holding each file; per compile log no more warnings than the reference (total and in our own sources);
#   image: check.log FAILURES none, boot_spare > 0, boot-args = reference - keepsyms=1 (+ EXTRA_ARGS) and <= 607 chars, the AE2 package
#     (sha 94fb2f9b) in the image, 0393's and every listed patch's first "NORD2..." string literal present in the kernel (GNSS: S11 too).
# Boot-args: keepsyms=1 is dropped as in q10jfg/q10jr (the packer strips the symbol table, so it is a no-op): q10jfi 595 chars, q10jfj 603.
set -euo pipefail
W=$NORD2_WORK
B=$W/gpu/build; SB=$B/scratchpad-backup-20261106
GNSS=${GNSS:-0}
if [ "$GNSS" = 1 ]; then DEF=q10jfj; REF=q10jfg; else DEF=q10jfi; REF=q10jfh; fi
NAME=${1:-$DEF}; O=${2:-$B/device-mmu-$NAME-candidate}
RC=$B/device-mmu-$REF-candidate                    # reference build: compile-log warnings, receipt (boot-args, spare)
HWDIR=${HWDIR:-$B/hwcomp-20261112}
PKG=${AUDIO_PKG:-$B/cam60-20261112/audio-q10jae.pkg}; PKG_SHA=94fb2f9b6ba09fd921d42f15f034c5618dbcd85de16af8e29ae03f99fb7584b4
PATCHES=$(echo "${PATCHES-0394 0395 0396}" | tr ',' ' '); WARN_BUDGET=${WARN_BUDGET:-0}; EXTRA_ARGS=${EXTRA_ARGS:-}; SPARE_WARN=${SPARE_WARN:-8192}
die() { echo "$*"; exit 1; }
case "${NORD2_PHYS_BASE:-0x142000000}/${NORD2_MEM_SIZE:-0xec000000}" in
  0x142000000/0xec000000) RAMTAG=3776; unset NORD2_PHYS_BASE NORD2_MEM_SIZE;;
  0x100000000/0x13e000000) RAMTAG=5088; export NORD2_PHYS_BASE NORD2_MEM_SIZE;;
  *) die "WINDOW: unsupported NORD2_PHYS_BASE/NORD2_MEM_SIZE";;
esac; echo "window: ${RAMTAG} MiB"
[ ! -e "$O" ] || die "$O exists"
[ "$(sha256sum "$PKG" | cut -c1-64)" = "$PKG_SHA" ] || die "PACKAGE: $PKG is not audio-q10jae.pkg ($PKG_SHA)"
[ -d "$RC/k/kernel/stage" ] || die "reference $RC has no stage"

# ---- locate the patches
NEWPD=""; NEWP=""; NEWPS=""; prev=0393
for n in $PATCHES; do
  [[ $n =~ ^[0-9]{4}$ ]] || die "PATCHES: bad number '$n'"
  [ $((10#$n)) -gt $((10#$prev)) ] || die "PATCHES: $n not ascending (after $prev)"; prev=$n
  f=$(find $HWDIR ${EXTRA_PATCH_DIRS:-} -maxdepth 3 -name "$n-*.patch" -not -path '*/patched-sources*' -not -path '*/.pre/*' 2>/dev/null | sort -u)
  [ "$(echo "$f" | grep -c .)" = 1 ] || die "PATCHES: need exactly one $n-*.patch under $HWDIR ${EXTRA_PATCH_DIRS:-}, found: [$f]"
  d=$(dirname "$f"); [ -d "$d/patched-sources-$n" ] || die "PATCHES: $d/patched-sources-$n missing (build_p1_candidate.py checks tracked files against it)"
  case " $NEWPD " in *" $d "*) ;; *) NEWPD="$NEWPD $d";; esac; NEWP="$NEWP $f"; NEWPS="$NEWPS $d/patched-sources-$n"
done
echo "patches:${NEWP:- (none: base recipe)}"

# ---- preflight on a copy of the base stage (q10jfh, or q10jfg + 0393)
[ "${PREFLIGHT_ONLY:-0}" = 1 ] && trap 'rm -rf "$O"' EXIT
PRE=$O/.pre; mkdir -p "$PRE"; cp -a "$RC/k/kernel/stage" "$PRE/s"
PRELIST="$NEWP"; [ "$GNSS" = 1 ] && PRELIST="$B/fbsync-20261112/0393-nord2-fb-sync.patch $NEWP"
for f in $PRELIST; do
  n=$(basename "$f" | cut -c1-4); d=$(dirname "$f")
  (cd "$PRE/s" && patch -p1 --batch -i "$f") > "$PRE/apply-$n.log" 2>&1 || { cat "$PRE/apply-$n.log"; die "PREFLIGHT: $n does not apply"; }
  ! grep -E 'Hunk .* FAILED|offset -?[0-9]+ lines?|fuzz|Reversed|rej' "$PRE/apply-$n.log" || die "PREFLIGHT: $n applies with fuzz/offset/reject (above)"
  for p in $(grep '^+++ ' "$f" | awk '{print $2}' | sed 's#^b/##'); do grep -q "patching file $p" "$PRE/apply-$n.log" || die "PREFLIGHT: $n did not patch $p"; done
  (cd "$d/patched-sources-$n" && find . -type f | sed 's#^\./##') | while read -r r; do
    cmp -s "$d/patched-sources-$n/$r" "$PRE/s/$r" || die "PREFLIGHT: patched-sources-$n/$r differs from base+patches (the patch's a/ is not this base?)"; done
  echo "preflight $n ok ($(grep -c '^patching file' "$PRE/apply-$n.log") files)"
done
rm -rf "$PRE/s"
[ "${PREFLIGHT_ONLY:-0}" = 1 ] && { echo PREFLIGHT_OK; exit 0; }

# ---- kernel
BASE=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298,0299,0300,0310,0311,0312,0313,0314,0315,0316,0317,0320,0321,0330,0331,0340
GNSS_SERIES=0350,0351,0353,0354,0355,0356,0357,0358,0359,0362,0363,0364
if [ "$GNSS" = 1 ]; then SERIES=$BASE,$GNSS_SERIES,0390,0391,0392,0393; else SERIES=$BASE,0390,0391,0392,0393; fi
for n in $PATCHES; do SERIES=$SERIES,$n; done
DIRS="$SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $B/surfbudget-20261111 $B/surfholders-20261111 $W/gpu/hwcodec/kernel $W/gpu/hwcodec/kern/p031x $B/audio-capture-20261111/patches $B/i2c8cam-20261111 $B/cam60-20261112"
[ "$GNSS" = 1 ] && DIRS="$DIRS $B/gnss-20261112 $B/gnss-safe-20261112 $B/gnss-safe2-20261112 $B/gnss-safe3-20261112 $B/gnss-ladder-20261112 $B/gnss-ladder2-20261112 $B/gnss-probe-20261112 $B/gnss-dl-20261112 $B/gnss-dl2-20261112 $B/gnss-dl3-20261112 $B/gnss-dl4-20261112"
DIRS="$DIRS $B/fbblend-20261112 $B/fbdiag-20261112 $B/fbretain-20261112 $B/fbsync-20261112$NEWPD"
PD=""; for d in $DIRS; do PD="$PD --patch-dir $d"; done
mkdir -p $O/k; cd $W
echo "series ...,$(echo $SERIES | rev | cut -d, -f1-8 | rev)"
python3 $B/wifi-20261113/build_p1_candidate_wifi.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $NAME --out $O/k/kernel > $O/k/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/k/kernel.stdout; ls -t $O/k/kernel/*.log | head -1 | xargs tail -20; exit 1; }
L=$O/k/kernel
# apply: clean everywhere, each new patch's files patched
! grep -l -E 'Hunk .* FAILED|offset -?[0-9]+ lines?|fuzz|Reversed' $L/apply-*.log || die "APPLY: fuzz/offset/failed hunks above"
for f in $B/fbsync-20261112/0393-nord2-fb-sync.patch $NEWP; do n=$(basename $f | cut -c1-4)
  for p in $(grep '^+++ ' "$f" | awk '{print $2}' | sed 's#^b/##'); do grep -q "patching file $p" $L/apply-$n.log || die "APPLY: $n $p"; done; done
if [ "$GNSS" = 1 ]; then
  for p in 0350:nord2_gpsdl.inc 0351:sweet_touch.c 0353:nord2_gpsdl.inc 0354:sweet_touch.c 0355:nord2_gpsdl.inc 0356:nord2_gpsdl.inc 0357:nord2_gnss_pwr.inc 0358:nord2_gpsdl.inc 0359:sweet_touch.c 0362:nord2_gpsdl.inc 0363:nord2_gpsdl.inc 0363:sweet_touch.c 0364:nord2_gpsdl.inc; do
    grep -q "patching file bsd/dev/${p#*:}" $L/apply-${p%%:*}.log || die "APPLY: ${p%%:*} ${p#*:}"; done
fi
# sources: the final stage = the newest patched-sources-N (0393 and the list) holding each file
for d in $B/fbsync-20261112/patched-sources-0393 $NEWPS; do
  (cd $d && find . -type f | sed 's#^\./##') | while read -r r; do
    best=$d; for e in $NEWPS; do
      [ "${e##*-}" -gt "${best##*-}" ] && [ -f "$e/$r" ] && best=$e; done
    cmp -s "$best/$r" "$L/stage/$r" || die "SOURCES: stage $r differs from $(basename $best)"; done; done
# warnings: per compile log, total and in our own sources (stage paths), never more than the reference + WARN_BUDGET.
# Exempt (counted, printed): SYSCTL_* macro expansions in C++ (reserved sysctl__ identifiers, oid_link initializer, startup cast) and
# sys/sysctl.h itself -- every new kern.n2fbhw* sysctl in a .cpp brings ~4 per macro + ~16 for the first include. SYSCTL_EXEMPT=0 counts them.
python3 - "$L" "$RC/k/kernel" "$WARN_BUDGET" "${SYSCTL_EXEMPT:-1}" "$REF" <<'PY' || die "COMPILE: warning budget (above)"
import sys, re, glob, os
L, R, budget, exempt, ref = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4] == '1', sys.argv[5]
loc = re.compile(r'^(\S+?):(\d+):\d+: warning:')
src = {}
def line(path, n):
    if path not in src:
        try: src[path] = open(path, errors='replace').read().split('\n')
        except OSError: src[path] = []
    t = src[path]; return t[n - 1] if 0 < n <= len(t) else ''
def count(log):
    tot = own = ex = 0; owns = []
    for l in open(log, errors='replace'):
        if 'warning:' not in l: continue
        m = loc.match(l); f, n = (m.group(1), int(m.group(2))) if m else ('', 0)
        if exempt and (f.endswith('/sysctl.h') or ('/k/kernel/stage/' in f and line(f, n).lstrip().startswith('SYSCTL_'))):
            ex += 1; continue
        tot += 1
        if '/k/kernel/stage/' in f: own += 1; owns.append(l.split('/k/kernel/stage/')[-1].strip()[:200])
    return tot, own, ex, owns
bad = 0
for f in sorted(glob.glob(L + '/*-compile.log')):
    b = os.path.basename(f); r = os.path.join(R, b)
    n, o, x, owns = count(f)
    if os.path.exists(r): m, mo, mx, rowns = count(r)
    else: m, mo, mx, rowns = None, 0, 0, []; print('compile log %s is new (no %s reference): only own-source warnings are budgeted' % (b, ref))
    key = lambda w: re.sub(r':\d+:\d+:', ':', w)
    from collections import Counter
    owns = list((Counter(map(key, owns)) - Counter(map(key, rowns))).elements())      # only the warnings the reference lacks
    if x != mx: print('%s: %d SYSCTL-macro/sysctl.h warnings exempt (%s %d)' % (b, x, ref, mx))
    if m is not None and n > m + budget: print('COMPILE: %s %d warnings > %s %d (+%d)' % (b, n, ref, m, budget)); bad = 1
    if o > mo + budget:
        print('COMPILE: %s %d own-source warnings > %s %d (+%d); new ones:' % (b, o, ref, mo, budget)); bad = 1
        for w in owns[:12]: print('   ', w)
sys.exit(bad)
PY
echo KERNEL OK
[ "${NOPACK:-0}" = 1 ] && { rm -rf "$O/.pre"; exit 0; }

# ---- image
XA="n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37"; [ "$GNSS" = 1 ] && XA="$XA n2gps=1"; [ -n "$EXTRA_ARGS" ] && XA="$XA $EXTRA_ARGS"
mkdir -p $O/.tmp; TMPDIR=$O/.tmp python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --drop-arg keepsyms=1 --kernel $L/kernel.development.nord2-metal-$NAME --system-metal --audio-package $PKG --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram${RAMTAG}-sched.img --extra-args "$XA" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/.tmp $O/.pre $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $L/kernel.startup.a
grep -h "FAILURES" $O/ui-sched/check.log
grep -q "FAILURES: none" $O/ui-sched/check.log || die "IMAGECHECK: check.log failures"
python3 - $O/ui-sched/image-receipt.json $RC/ui-sched/image-receipt.json "$EXTRA_ARGS" $SPARE_WARN "$PATCHES" <<'PY' || die "IMAGECHECK: receipt"
import json,sys
a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); extra=sys.argv[3].split(); warn=int(sys.argv[4])
assert a['boot_spare']>0, 'boot_spare %d'%a['boot_spare']
want=[x for x in b['commandline'].split() if x!='keepsyms=1']+extra
assert a['commandline'].split()==want, ('commandline', a['commandline'])
assert len(a['commandline'])<=607, 'commandline length %d'%len(a['commandline'])
d=b['boot_spare']-a['boot_spare']
print('boot_spare', a['boot_spare'], 'bytes', a['bytes'], 'cmdlen', len(a['commandline']), '(%d free)'%(607-len(a['commandline'])),
      '| patches [%s] cost %d B vs reference spare %d'%(sys.argv[5], d, b['boot_spare']))
if a['boot_spare']<warn:
    print('BUDGET WARNING: boot_spare %d < %d. Trim, largest first (hwcomp-20261112/spare_table.py): GNSS ladders 0356 (6144) + 0359 (4096),'
          ' L1 probe 0358 (2048), split ladders 0363 (2048) -- the probe/ladder ioctls are only for bring-up; or drop 0391 (fb diag, 2048).'%(a['boot_spare'],warn))
PY
grep -q "audio package replaced: .* sha256 $PKG_SHA" $O/ui-sched.stdout || die "IMAGECHECK: package"
if [ "$RAMTAG" = 5088 ]; then
  grep -q '^5088 MiB window patched' $O/k/kernel.stdout || die "IMAGECHECK: kernel lacks the 5088 Wi-Fi layout patch"
  python3 - $O/ui-sched/image-receipt.json $O/ui-sched/gpu.adt <<'PY' || die "IMAGECHECK: 5088 window"
import json,sys,struct
a=json.load(open(sys.argv[1])); g=open(sys.argv[2],'rb').read()
assert a['phys_base']=='0x100000000' and a['mem_size']=='0x13e000000' and a['loader'].endswith('ram5g-20261103/loader/loader-Image'),a
for k,v in [(b'dram-base',0x100000000),(b'dram-size',0x13e000000)]:
    assert g.count(k.ljust(32,b'\0')+struct.pack('<I',8)+struct.pack('<Q',v))==1,k
print('window 5088 ok: loader',a['loader_sha256'][:16])
PY
fi
K=$L/kernel.development.nord2-metal-$NAME
grep -a -q 'NORD2 FB SYNC: pushes=' $K || die "IMAGECHECK: 0393 not in kernel"
grep -a -q "layers outside a swap's mask kept %s (0392)" $K || die "IMAGECHECK: 0392 not in kernel"
[ "$GNSS" = 1 ] && { grep -a -q 'S11 RESULT: DSP booted (50/50 fragments acked)' $K || die "IMAGECHECK: GNSS code not in kernel"; }
for f in $NEWP; do n=$(basename $f | cut -c1-4)
  m=$(grep '^+[^+]' "$f" | grep -o '"NORD2[^"%\\]\{6,\}' | head -1 | cut -c2-)
  if [ -z "$m" ]; then echo "IMAGECHECK: $n has no \"NORD2...\" string literal: marker check skipped"; continue; fi
  grep -a -q -F "$m" $K || die "IMAGECHECK: $n marker '$m' not in kernel"; echo "marker $n '$m' ok"; done
sha256sum $O/ui-sched/*.img
echo "done IMAGECHECK_OK"; tail -14 $O/ui-sched.stdout
