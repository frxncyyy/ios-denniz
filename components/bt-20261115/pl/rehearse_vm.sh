#!/bin/bash
# offline (VM) rehearsal of the txn_bt1.sh path on the CURRENT rootfs mirror, like gnss-20261114/pl20/rehearse.sh: generic_update_offline.py
# (journal replay + guest in the rootfs VM, which asserts INSTALL_DONE / CC_JOB_ADDED / CC_RECEIPT_UPDATED / fsck OK) + catalog check
# against the owned list, for bt1probe, bt1 and the undo. Only when no phone transaction is using the mirror. Writes nothing on the phone.
set -u
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; H=$B/bt-20261115/pl; MAIN=$NORD2_REPO
cd $H
cp $B/gnss-20261114/pl20/rehearsal-snapshot.json rehearsal-snapshot.json
python3 -c "import json,os;m=\"$B/mirror/rootfs-mirror.img\";r=json.load(open(\"rehearsal-snapshot.json\"));r[\"root\"]=json.load(open(m+\".manifest.json\"))[\"root\"];r[\"bytes\"]=os.path.getsize(m);json.dump(r,open(\"rehearsal-snapshot.json\",\"w\"),indent=1);print(\"receipt root\",r[\"root\"])"
for P in bt1probe bt1 bt1-undo; do
  rm -rf rehearsal-$P
  timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/payload-$P --guest $H/guest-$P.sh --output $H/rehearsal-$P --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > rehearsal-$P.offline.log 2>&1; echo "offline EXIT $?" >> rehearsal-$P.offline.log
  O=owned-$P.json; [ -f $O ] || O=owned-bt1.json
  python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/rehearsal-$P --owned-json $O --output rehearsal-$P-catalog.json > rehearsal-$P-catalog.log 2>&1; echo "catalog EXIT $?" >> rehearsal-$P-catalog.log
  echo "== $P: $(tail -1 rehearsal-$P.offline.log) / $(tail -1 rehearsal-$P-catalog.log) | markers: $(grep -a -o -E 'BT1_OK|BT1_UNDO_OK|BT1_STAGE [^ ]*|CC_JOB_ADDED|CC_RECEIPT_UPDATED|INSTALL_DONE|_REFUSED|Traceback' rehearsal-$P/vm.log 2>/dev/null | sort -u | tr '\n' ' ')"
done
echo REHEARSE_VM_DONE
