#!/bin/bash
B=$NORD2_WORK/gpu/build
C=$B/device-mmu-q10bz-candidate
until [ -n "$(ls $C/ui-sched/*.img 2>/dev/null)" ] || grep -q FAILED $C/pack.done 2>/dev/null; do sleep 10; done
grep -q FAILED $C/pack.done 2>/dev/null && { echo BUILD FAILED; exit 1; }
cp -a $C/ui-sched $B/autocycle/q10bz-image; rm -rf $C/kernelb
PAYLOAD=payload-mg GUEST=guest-mg.sh OWNED=$B/apps-20261104/owned-mg.json FB_WAIT=14400 $B/autocycle/txn.sh $(ls $B/autocycle/q10bz-image/*.img) bb6
echo "txn rc=$?"
