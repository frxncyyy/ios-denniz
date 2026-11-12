#!/bin/bash
# ORCHESTRATOR ONLY (phone transaction). One boot of a HW-composition image with the fbhwctl job installed, interactions during the
# live console capture, verdict table at the end.
#   run_fbhw_txn.sh IMG TAG [PAYLOAD]      PAYLOAD = payload-fbhwcycle (default: modes 0,1,2,3 x 120 s) | payload-fbhwmode0..3 | payload-fbhwctl-undo
# = autocycle/txn_g.sh (rescue -> snapshot -> offline guest-fbhwctl.sh -> catalog vs owned-fbhwctl.json -> commit -> flash IMG -> capture
#   READS s into autocycle/runs/TAG/ui-usb/xnu-live.log) + fbhw_scenario.py --capture <that file> once the capture has started.
# The payload stays installed for later boots (a different PAYLOAD rewrites only the config; payload-fbhwctl-undo removes it).
# Prerequisites: IMG = q10jfi/q10jfj (kernel with 0396; IMAGECHECK_OK), apps-20261104/rehearse-fbhwctl.sh = REHEARSAL_OK.
set -euo pipefail
W=$NORD2_WORK
B=$W/gpu/build; H=$B/hwcomp-20261112/phone; IMG=$1; TAG=$2; P=${3:-payload-fbhwcycle}
[ -f "$IMG" ] && [ -d $B/apps-20261104/$P ] || { echo "usage: run_fbhw_txn.sh IMG TAG [payload-fbhw*]"; exit 1; }
case $P in payload-fbhwctl-undo) G=guest-fbhwctl-undo.sh; ARGS="--rounds 2";; payload-fbhwcycle) G=guest-fbhwctl.sh; ARGS="--cycle";;
  *) G=guest-fbhwctl.sh; ARGS="--duration ${DURATION:-300}";; esac
LOG=$B/autocycle/runs/$TAG/ui-usb/xnu-live.log; OUT=$B/autocycle/runs/$TAG/fbhw
[ ! -e $B/autocycle/runs/$TAG ] || { echo "runs/$TAG exists"; exit 1; }
READS=${READS:-900} PAYLOAD=$P GUEST=$G OWNED=$B/apps-20261104/owned-fbhwctl.json $B/autocycle/txn_g.sh "$IMG" "$TAG" > $B/autocycle/runs-$TAG.txn.out 2>&1 &
TX=$!
# wait for the capture to start and the UI to be up (SpringBoard/backboardd presents: first NORD2 FB PRESENT line), max 40 min (snapshot+offline+flash)
for i in $(seq 1 1200); do sleep 2; kill -0 $TX 2>/dev/null || { echo "txn ended before the capture"; tail -5 $B/autocycle/runs-$TAG.txn.out; exit 1; }
  [ -f $LOG ] && grep -aq 'NORD2 FB PRESENT' $LOG && break; done
sleep 20
python3 $H/fbhw_scenario.py $OUT --capture $LOG $ARGS || RC=$?
wait $TX || true
python3 $H/fbhw_verdict.py $LOG --steps $OUT/steps.jsonl --json $OUT/verdict-final.json | tee $OUT/verdict-final.txt || RC=$?
echo "txn: $(tail -1 $B/autocycle/runs-$TAG.txn.out)  verdict: $OUT/verdict-final.txt"; exit ${RC:-0}
