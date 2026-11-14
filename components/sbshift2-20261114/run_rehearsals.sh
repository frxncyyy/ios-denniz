#!/bin/bash
# VM rehearsals of sbshift2 against the CURRENT mirror (read-only; qcow2 overlays). Run under the global heavy lock.
set -uo pipefail
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/sbshift2-20261114; MAIN=$NORD2_REPO
V=${1:-22}
cd $S
rm -rf rehearsal-sbshift2-roundtrip rehearsal-sbshift2-$V
python3 rehearse_roundtrip.py guest-sbshift2roundtrip.sh payload-sbshift2roundtrip rehearsal-sbshift2-roundtrip > rehearsal-sbshift2-roundtrip.out 2>&1; echo RT_RC $?
grep -a "SBSHIFT\|RT_\|MANIFEST\|fsck\|\*\* " rehearsal-sbshift2-roundtrip/vm.log | cut -c1-260
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $S/rehearsal-snapshot.json --payload $S/payload-sbshift2-$V --guest $S/guest-sbshift2-$V.sh --output $S/rehearsal-sbshift2-$V --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > rehearsal-sbshift2-$V.offline.log 2>&1; echo OFFLINE_RC $?
grep -a "SBSHIFT\|INSTALL_DONE\|The volume" rehearsal-sbshift2-$V/vm.log | cut -c1-240
cp owned-sbshift2.json rehearsal-sbshift2-$V/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $S/rehearsal-snapshot.json --update $S/rehearsal-sbshift2-$V --owned-json $S/rehearsal-sbshift2-$V/owned.json --output $S/rehearsal-sbshift2-$V/catalog.json > rehearsal-sbshift2-$V.catalog.log 2>&1; echo CATALOG_RC $?
tail -3 rehearsal-sbshift2-$V.catalog.log
