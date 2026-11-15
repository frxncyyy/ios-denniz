#!/bin/bash
B=$NORD2_WORK/gpu/build
PAYLOAD=payload-cclog GUEST=guest-cclog.sh OWNED=$B/apps-20261104/owned-ccenv.json FB_WAIT=1800 $B/autocycle/txn.sh $(ls $B/autocycle/q10br-image/*.img) bb4
echo "txn rc=$?"
