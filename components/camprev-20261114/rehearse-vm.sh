#!/bin/bash
# camprev VM rehearsals on the CURRENT mirror (copy of y2s99c-perf/ship/rehearse-vm.sh; run under the global heavy lock, one VM at a time):
# offline install (generic_update_offline.py = journal replay + guest in the VM on a throw-away overlay) + catalog check vs owned json,
# for camvdofull, camvdoquarter and their undos (no-ops on the mirror), then the VM round trips (install / again / undo / again).
set -u
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; H=$B/camprev-20261114; MAIN=$NORD2_REPO
cd $H
python3 -c "import json,os;m=\"$B/mirror/rootfs-mirror.img\";r=json.load(open(\"rehearsal-snapshot.json\"));r[\"root\"]=json.load(open(m+\".manifest.json\"))[\"root\"];r[\"bytes\"]=os.path.getsize(m);json.dump(r,open(\"rehearsal-snapshot.json\",\"w\"),indent=1);print(\"receipt root\",r[\"root\"])"
for P in camvdofull camvdofull-undo camvdoquarter camvdoquarter-undo; do
  O=owned-${P%-undo}.json
  rm -rf vm-$P
  timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/payload-$P --guest $H/guest-$P.sh --output $H/vm-$P --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > vm-$P.offline.log 2>&1; echo "offline EXIT $?" >> vm-$P.offline.log
  python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/vm-$P --owned-json $O --output vm-$P-catalog.json > vm-$P-catalog.log 2>&1; echo "catalog EXIT $?" >> vm-$P-catalog.log
  rm -f vm-$P/overlay.qcow2 vm-$P/delta.bin vm-$P/initrd.gz
done
for P in camvdofull camvdoquarter; do
  rm -rf rt-$P; python3 rehearse_roundtrip.py guest-${P}roundtrip.sh payload-${P}roundtrip rt-$P > rt-$P.out 2>&1; echo "roundtrip EXIT $?" >> rt-$P.out
done
echo REHEARSE_DONE
