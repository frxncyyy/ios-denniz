#!/bin/bash
B=$NORD2_WORK/gpu/build
C=$B/device-mmu-q10bq-candidate
until [ -n "$(ls $C/ui-sched/*.img 2>/dev/null)" ] || grep -q FAILED $C/pack.done 2>/dev/null; do sleep 10; done
grep -q FAILED $C/pack.done 2>/dev/null && { echo BUILD FAILED; exit 1; }
cp -a $C/ui-sched $B/autocycle/q10bq-image; rm -rf $C/kernelb
PAYLOAD=payload-preboot GUEST=guest-preboot.sh OWNED=$B/apps-20261104/owned-preboot.json FB_WAIT=14400 $B/autocycle/txn.sh $(ls $B/autocycle/q10bq-image/*.img) bb2
echo "txn rc=$?"
