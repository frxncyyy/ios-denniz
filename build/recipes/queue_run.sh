#!/bin/bash
# queue_run.sh WAIT_TAG "TAG PAYLOAD OWNED IMGNAME" ...
# Waits until cc-txn-WAIT_TAG/events has "capture done" (or FAILED), then for each job: phone.py reset fastboot, txn.sh with the payload, waits for capture done.
# Results of each run are fetched later with vcodec_check.py fetch <tag> (the next txn's snapshot contains them).
set -u
W=$NORD2_WORK
B=$W/gpu/build; A=$B/apps-20261104; FB=~/Android/Sdk/platform-tools/fastboot
WAIT=$1; shift
waitdone() { until grep -qE "capture done|FAILED" $B/cc-txn-$1/events 2>/dev/null; do sleep 5; done; tail -1 $B/cc-txn-$1/events; }
echo "$(date -Is) waiting for $WAIT"; waitdone $WAIT
for job in "$@"; do
  set -- $job; TAG=$1; PAYLOAD=$2; OWNED=$3; IMGN=$4
  IMG=$B/device-mmu-$IMGN-candidate/ui-sched/nord2-ios-metal-$IMGN-ui654fb60dvfs-cap3500-ram3776-sched.img
  echo "$(date -Is) start $TAG payload=$PAYLOAD image=$IMGN"
  timeout 60 python3 $B/autotest/phone.py reset fastboot | tail -1; sleep 20
  $FB devices | head -1
  PAYLOAD=$PAYLOAD GUEST=guest-vcodec.sh OWNED=$A/$OWNED timeout 900 bash $B/autocycle/txn.sh $IMG $TAG > $B/autocycle/runs-$TAG.out 2>&1 &
  sleep 3
  waitdone $TAG
done
echo "$(date -Is) queue done"
