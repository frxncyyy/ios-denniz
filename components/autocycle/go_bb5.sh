#!/bin/bash
B=$NORD2_WORK/gpu/build
C=$B/device-mmu-q10bs-candidate
until [ -n "$(ls $C/ui-sched/*.img 2>/dev/null)" ] || grep -q FAILED $C/pack.done 2>/dev/null; do sleep 10; done
grep -q FAILED $C/pack.done 2>/dev/null && { echo BUILD FAILED; exit 1; }
cp -a $C/ui-sched $B/autocycle/q10bs-image; rm -rf $C/kernelb
PAYLOAD=payload-preboot2 GUEST=guest-preboot2.sh OWNED=$B/apps-20261104/owned-preboot2.json FB_WAIT=1800 $B/autocycle/txn.sh $(ls $B/autocycle/q10bs-image/*.img) bb5
echo "txn rc=$?"
