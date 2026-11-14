#!/bin/bash
# pl15 VM rehearsals on the CURRENT mirror (run under the global heavy lock): offline install of gnssfix8 and gnssfix8k (txn path) +
# catalog check against the owned list, offline undo, and the gnssfix8 round trip (install, again, undo, again).
set -u
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; H=$B/gnss-20261114/pl15; MAIN=$NORD2_REPO
cd $H
python3 -c "import json,os;m=\"$B/mirror/rootfs-mirror.img\";r=json.load(open(\"rehearsal-snapshot.json\"));r[\"root\"]=json.load(open(m+\".manifest.json\"))[\"root\"];r[\"bytes\"]=os.path.getsize(m);json.dump(r,open(\"rehearsal-snapshot.json\",\"w\"),indent=1);print(\"receipt root\",r[\"root\"])"
for P in gnssfix15x gnssfix15l gnssfix15v gnssfix15x-undo; do
  rm -rf rehearsal-$P
  timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/payload-$P --guest $H/guest-$P.sh --output $H/rehearsal-$P --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > rehearsal-$P.offline.log 2>&1; echo "offline EXIT $?" >> rehearsal-$P.offline.log
  python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/rehearsal-$P --owned-json owned-${P%-undo}.json --output rehearsal-$P-catalog.json > rehearsal-$P-catalog.log 2>&1; echo "catalog EXIT $?" >> rehearsal-$P-catalog.log
done
rm -rf rehearsal-gnssfix15x-roundtrip
timeout 1500 python3 $B/final3-20261114/rehearse_roundtrip.py guest-gnssfix15xroundtrip.sh payload-gnssfix15xroundtrip rehearsal-gnssfix15x-roundtrip > rehearsal-gnssfix15x-roundtrip.out 2>&1; echo "roundtrip EXIT $?" >> rehearsal-gnssfix15x-roundtrip.out
echo REHEARSE_DONE
