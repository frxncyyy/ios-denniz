#!/bin/bash
# webxr1 VM rehearsals on the CURRENT mirror (run under the global heavy lock): offline install of webxr1 and webxr1-off (txn path:
# generic_update_offline.py = journal replay + guest in the VM on a throw-away overlay) + catalog check against owned-webxr1.json, and the
# offline undo (on the mirror = shader99b it must be a no-op).
set -u
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; H=$B/webxr-20261114; MAIN=$NORD2_REPO
cd $H
python3 -c "import json,os;m=\"$B/mirror/rootfs-mirror.img\";r=json.load(open(\"rehearsal-snapshot.json\"));r[\"root\"]=json.load(open(m+\".manifest.json\"))[\"root\"];r[\"bytes\"]=os.path.getsize(m);json.dump(r,open(\"rehearsal-snapshot.json\",\"w\"),indent=1);print(\"receipt root\",r[\"root\"])"
for P in ${ONLY:-webxr1 webxr1-off webxr1-undo}; do
  G=guest-webxr1.sh; [ $P = webxr1-undo ] && G=guest-webxr1-undo.sh
  rm -rf vm-$P
  timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/payload-$P --guest $H/$G --output $H/vm-$P --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > vm-$P.offline.log 2>&1; echo "offline EXIT $?" >> vm-$P.offline.log
  python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/vm-$P --owned-json owned-webxr1.json --output vm-$P-catalog.json > vm-$P-catalog.log 2>&1; echo "catalog EXIT $?" >> vm-$P-catalog.log
  rm -f vm-$P/overlay.qcow2 vm-$P/delta.bin vm-$P/initrd.gz
done
echo REHEARSE_DONE
