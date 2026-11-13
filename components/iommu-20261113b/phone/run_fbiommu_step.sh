#!/bin/bash
# ORCHESTRATOR ONLY (phone transaction). One display-IOMMU step = one boot:  run_fbiommu_step.sh STEP IMG TAG
#   STEP = t1 (arm only) | t2 (one OVL port, identity, 3x2 s) | t3 (both ports, 3x2 s) | t4 (map-only dry run) | t5 (IOVA layers + colour
#          test: PHOTO) | t6 (all layers, 5 min) | undo (removes the job + config: a persisted config re-runs at EVERY iOS boot)
#        T1 bisect (0409, image q10jgj; guest-fbiommusub.sh): t1idle (T1 + SCEN=idle) | t1m3 (T1 then HW mode 3 + pool) | poolonly (pool=1, no M4U
#          write) | sub01 (pool map + tables) | sub02 (PT_BASE) | sub04 (TFRP) | sub08 (TLB invalidate) | sub10 (read-only larb dump) |
#          sub80 (0x0e then undo 30 s later) | subseq (FAST: 0x01,0x02,0x04,0x08,0x80 at job t=20/45/70/95/120 in one boot) |
#          ctl0 (control: payload-fbhwmode0 = CPU path, nothing armed) | subx2 (io_submap test: 0x01,0x02,0x04,0x08 at job t=95..110,
#          i.e. the first IOMMU register map AFTER the SCP delayed maps at ~108 s uptime; forces SCEN=fast READS=240)
#   SCEN = default (fbhw_scenario home,photos) | idle (no touch at all until the capture ends) | disc (from the job's 'N2FBHWCTL done':
#          0-120 s idle, 120-240 s home swipes only, then the Photos grid until the capture ends) | fast (fast_scroll.py: from UI up + 10 s,
#          Photos grid scrolled continuously ~2 swipes/s, no idle, no home gesture, until the capture ends; READS default 180).
#          t1idle forces SCEN=idle.  FAST pair:  SCEN=fast run_fbiommu_step.sh subseq <q10jgj> iofs1 ; SCEN=fast run_fbiommu_step.sh ctl0 <q10jgj> ioct1
# Caller first: phone.py reset fastboot.  IMG = q10jgi (0394..0401,0403,0404-on0403,0405,0406,0407-on0403,0408; no 0402; selftest default 0,
# hwInit accepts MMU_EN set by the armed IOMMU):
#   I=gpu/build/device-mmu-q10jgi-candidate/ui-sched/nord2-ios-metal-q10jgi-*.img
#   iommu-20261113b/phone/run_fbiommu_step.sh t1 $I iot1     (then t2..t6 = iot2..iot6, next only on PASS; t5 also needs the PHOTO)
#   iommu-20261113b/phone/run_fbiommu_step.sh undo $I iotu   (after the last step or any FAIL)
#   T1 bisect on q10jgj:  SCEN=disc iommu-20261113b/phone/run_fbiommu_step.sh sub02 gpu/build/device-mmu-q10jgj-candidate/ui-sched/*.img iosub02
# Sysctls used (all in q10jgi): kern.n2fbiommu_probe, kern.n2fbiommu_stage (t1-t3), kern.n2fbiommu + kern.n2fbhw (t4-t6),
# kern.n2fbiommutest + kern.n2fbhwtest=6 (t5; kern.n2fbhwselftest stays 0, never needed).  q10jfnis (c7132034) also works for t1-t4.
# For undo any working kernel (q10jgb + payload-fbhwmode3 afterwards = the delivered state).
# = autocycle/txn_g.sh (rescue -> snapshot -> offline guest -> catalog vs owned-fbiommu-t.json -> commit -> flash IMG -> ONE capture of
#   READS<=600 s into autocycle/runs/TAG/ui-usb/xnu-live.log) + fbhw_scenario.py --scenarios home,photos --capture <that file> (no
#   phone.py logs, no second reader, no app-switch gestures).  t1-t3: the scenario starts only after 'N2FBHWCTL done' (stage window
#   without touches); t5: after the test 6 hold (PHOTO printed with the time).  Verdict: fbiommu_step_verdict.py STEP.
# Prerequisite: apps-20261104/rehearse-fbiommu-t.sh -> REHEARSAL_OK (rehearse-fbiommu-t.log).
set -euo pipefail
W=$NORD2_WORK
B=$W/gpu/build; H=$B/hwcomp-20261112/phone; V=$B/iommu-20261113b/phone; STEP=${1:-}; IMG=${2:-}; TAG=${3:-}
SCEN=${SCEN:-default}; RL=rehearse-fbiommu-t.log
case $STEP in
  t1|t2|t3) P=payload-fbiommu-$STEP; G=guest-fbiommu.sh; ARGS="--duration ${DURATION:-150}";;
  ctl0)     P=payload-fbhwmode0; G=guest-fbhwctl.sh; RL=; ARGS="--duration ${DURATION:-150}";;
  t1idle|t1m3|poolonly|sub01|sub02|sub04|sub08|sub10|sub80|subseq|subx2) P=payload-fbiommu-$STEP; G=guest-fbiommusub.sh; RL=rehearse-fbiommusub.log
            ARGS="--duration ${DURATION:-150}"; [ $STEP = t1idle ] && SCEN=idle; [ $STEP = subx2 ] && { SCEN=fast; READS=${READS:-240}; };;
  t4|t5|t6) P=payload-fbiommu-$STEP; G=guest-fbiommu.sh; ARGS="--cycle";;      # cycle = until 15 s after 'N2FBHWCTL done'
  undo)     P=payload-fbiommu-undo; G=guest-fbhwctl-undo.sh; ARGS="--rounds 1";;
  *) echo "usage: run_fbiommu_step.sh t1..t6|t1idle|t1m3|poolonly|sub01|sub02|sub04|sub08|sub10|sub80|undo IMG TAG"; exit 1;;
esac
[ -f "$IMG" ] && [ -n "$TAG" ] && [ -d $B/apps-20261104/$P ] || { echo "usage: run_fbiommu_step.sh t1..t6|undo IMG TAG"; exit 1; }
[ -z "$RL" ] || grep -q '^REHEARSAL_OK$' $B/apps-20261104/$RL || { echo "$RL not REHEARSAL_OK"; exit 1; }
case $SCEN in default|idle|disc|fast) ;; *) echo "SCEN must be default|idle|disc|fast"; exit 1;; esac
[ "$SCEN" = fast ] && READS=${READS:-180}
READS=${READS:-600}; [ "$READS" -le 600 ] || { echo "READS must be <= 600"; exit 1; }
LOG=$B/autocycle/runs/$TAG/ui-usb/xnu-live.log; OUT=$B/autocycle/runs/$TAG/fbhw
[ ! -e $B/autocycle/runs/$TAG ] || { echo "runs/$TAG exists"; exit 1; }
echo "fbiommu step $STEP (SCEN $SCEN): payload $P, image $(sha256sum "$IMG" | cut -c1-16), tag $TAG, READS $READS"
READS=$READS PAYLOAD=$P GUEST=$G OWNED=${OWNED:-$B/apps-20261104/owned-fbiommu-t.json} $B/autocycle/txn_g.sh "$IMG" "$TAG" > $B/autocycle/runs-$TAG.txn.out 2>&1 &
TX=$!
# capture up + UI up (first NORD2 FB PRESENT), max 40 min (snapshot + offline + flash)
for i in $(seq 1 1200); do sleep 2; kill -0 $TX 2>/dev/null || { echo "txn ended before the capture"; tail -5 $B/autocycle/runs-$TAG.txn.out; exit 1; }
  [ -f $LOG ] && grep -aq 'NORD2 FB PRESENT' $LOG && break; done
gate() { for i in $(seq 1 $2); do sleep 2; kill -0 $TX 2>/dev/null || return 0; grep -aq "$1" $LOG && return 0; done; echo "gate '$1' not seen"; }
case $SCEN/$STEP in
  fast/*) sleep 10;;
  */t1|*/t2|*/t3|*/t1idle|*/t1m3|*/poolonly|*/sub*|*/ctl0) gate 'N2FBHWCTL done' 150; grep -a 'NORD2 FB IOMMU: STAGE\|stage watchdog\|stage port' $LOG | tail -8 || true;;
  */t5) gate 'N2FBHWCTL iommu hwtest' 200; echo "PHOTO NOW $(date +%T): test 6 through the IOMMU (both strips must read R,G,B)"; sleep 15;;
  *) sleep 20;;
esac
mkdir -p $OUT
if [ "$SCEN" = fast ]; then
  kill -0 $TX 2>/dev/null && { python3 $V/fast_scroll.py $OUT --capture $LOG --seconds $READS || RC=$?; }
elif [ "$SCEN" = idle ]; then echo "SCEN idle: no touches until the capture ends ($(date +%T))"
elif [ "$SCEN" = disc ]; then
  echo "SCEN disc: 0-120 s idle from $(date +%T)"; for i in $(seq 1 60); do sleep 2; kill -0 $TX 2>/dev/null || break; done
  if kill -0 $TX 2>/dev/null; then echo "SCEN disc: home swipes only $(date +%T)"; python3 $H/fbhw_scenario.py $OUT/disc-home --capture $LOG --scenarios home --duration 120 || RC=$?; fi
  if kill -0 $TX 2>/dev/null; then echo "SCEN disc: Photos grid $(date +%T)"; python3 $H/fbhw_scenario.py $OUT/disc-photos --capture $LOG --scenarios photos --duration ${DURATION:-240} || RC=$?; fi
elif kill -0 $TX 2>/dev/null; then
  python3 $H/fbhw_scenario.py $OUT --capture $LOG --scenarios home,photos $ARGS || RC=$?
fi
wait $TX || true
mkdir -p $OUT
[ "$STEP" = undo ] || python3 $V/fbiommu_step_verdict.py $STEP $LOG --usb $B/autocycle/runs/$TAG/usb.log --steps $OUT/steps.jsonl --json $OUT/step-verdict.json | tee $OUT/step-verdict.txt || RC=$?
echo "txn: $(tail -1 $B/autocycle/runs-$TAG.txn.out)  verdict: $OUT/step-verdict.txt"
echo "NOTE: the step's config stays installed and re-runs at every iOS boot until the next step or: run_fbiommu_step.sh undo <img> <tag>"
exit ${RC:-0}
