#!/bin/bash
# 0402b (hwmulti-20261113-on0401b/phone): the one-boot feature cycle (payload-fbm0402cycle, guest-fbm0402.sh) = run_fbiommu_txn.sh with
# the 0402b guest/catalog, a capture long enough for 90 s + 5 x 180 s, the scenario for that whole time, then fbhw_verdict.py,
# cpufree_verdict.py and fbm0402_phases.py (which phase was running when the log ended).
#   run_fbm0402_txn.sh IMG TAG [payload-fbm0402cycle2 (default: 0402c, A / D 4L / B2 scaling+4L / C2 +H6, 150 s) | payload-fbm0402cycle | *-undo]
# ORCHESTRATOR ONLY (phone transaction).  IMG = a 0394..0401 + 0402b image (IMAGECHECK_OK); apps-20261104/rehearse-fbm0402.sh = REHEARSAL_OK.
set -euo pipefail
W=$NORD2_WORK
B=$W/gpu/build; H=$B/hwcomp-20261112/phone; M=$B/hwmulti-20261113-on0401c; IMG=$1; TAG=$2; P=${3:-payload-fbm0402cycle2}
[ -f "$IMG" ] && [ -d $B/apps-20261104/$P ] || { echo "usage: run_fbm0402_txn.sh IMG TAG [payload-fbm0402cycle2|payload-fbm0402cycle|*-undo]"; exit 1; }
case $P in payload-fbm0402cycle-undo) G=guest-fbm0402-undo.sh; ARGS="--rounds 2"; OW=owned-fbm0402-undo.json;;
  payload-fbm0402cycle2-undo) G=guest-fbm0402-undo.sh; ARGS="--rounds 2"; OW=owned-fbm0402-undo.json;;
  payload-fbm0402cycle2) G=guest-fbm0402.sh; ARGS="--duration ${DURATION:-720} --scenarios ${SCEN:-switch,home,cc,switch,photos}"; OW=owned-fbm0402.json;;
  payload-fbm0402cycle) G=guest-fbm0402.sh; ARGS="--duration ${DURATION:-1080} --scenarios ${SCEN:-switch,home,cc,switch,photos}"; OW=owned-fbm0402.json;;
  *) echo "unknown payload $P"; exit 1;; esac
LOG=$B/autocycle/runs/$TAG/ui-usb/xnu-live.log; OUT=$B/autocycle/runs/$TAG/fbhw
[ ! -e $B/autocycle/runs/$TAG ] || { echo "runs/$TAG exists"; exit 1; }
READS=${READS:-1500} PAYLOAD=$P GUEST=$G OWNED=$B/apps-20261104/$OW $B/autocycle/txn_g.sh "$IMG" "$TAG" > $B/autocycle/runs-$TAG.txn.out 2>&1 &
TX=$!
for i in $(seq 1 1200); do sleep 2; kill -0 $TX 2>/dev/null || { echo "txn ended before the capture"; tail -5 $B/autocycle/runs-$TAG.txn.out; exit 1; }
  [ -f $LOG ] && grep -aq 'NORD2 FB PRESENT' $LOG && break; done
sleep 20
python3 $H/fbhw_scenario.py $OUT --capture $LOG $ARGS || RC=$?
wait $TX || true
python3 $H/fbhw_verdict.py $LOG --steps $OUT/steps.jsonl --json $OUT/verdict-final.json | tee $OUT/verdict-final.txt || RC=$?
python3 $M/cpufree_verdict.py $LOG | tee $OUT/cpufree-verdict.txt || true
python3 $M/phone/fbm0402_phases.py $LOG | tee $OUT/fbm0402-phases.txt || RC=$?
echo "txn: $(tail -1 $B/autocycle/runs-$TAG.txn.out)  verdicts: $OUT/verdict-final.txt $OUT/cpufree-verdict.txt $OUT/fbm0402-phases.txt"; exit ${RC:-0}
