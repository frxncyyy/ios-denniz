#!/bin/bash
# final3-20261114/rehearse.sh: all VM rehearsals of final3 on the CURRENT mirror (one heavy job at a time: run under the global lock).
# 1 round trip (install / again / undo / again), 2 offline update on the mirror via the txn tool (generic_update_offline.py) + catalog check
# (owned list and empty list), 3 refusal chain (forced HAPTIC1_REFUSED -> all blocks skipped, FINAL3_REFUSED), 4 offline undo guest.
set -u
ONLY=${ONLY:-all}   # ONLY=offline: steps 2 and 4 only
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; H=$B/final3-20261114; MAIN=$NORD2_REPO
cd $H; python3 make_guests.py
if [ $ONLY = all ]; then rm -rf rehearsal-final3-roundtrip rehearsal-final3-refusal; fi; rm -rf rehearsal-final3 rehearsal-final3-undo
[ $ONLY = all ] && timeout 1500 python3 rehearse_roundtrip.py guest-final3roundtrip.sh payload-final3roundtrip rehearsal-final3-roundtrip > rehearsal-final3-roundtrip.out 2>&1 && echo "roundtrip EXIT 0" >> rehearsal-final3-roundtrip.out
timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/payload-final3 --guest $H/guest-final3.sh --output $H/rehearsal-final3 --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > rehearsal-final3.offline.log 2>&1; echo "offline EXIT $?" >> rehearsal-final3.offline.log
echo '[]' > rehearsal-final3/owned-empty.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/rehearsal-final3 --owned-json owned-final3.json --output rehearsal-final3/catalog-owned.json > rehearsal-final3/catalog-owned.log 2>&1; echo "catalog EXIT $?" >> rehearsal-final3/catalog-owned.log
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/rehearsal-final3 --owned-json rehearsal-final3/owned-empty.json --output rehearsal-final3/catalog-empty.json > rehearsal-final3/catalog-empty.log 2>&1; echo "catalog-empty EXIT $?" >> rehearsal-final3/catalog-empty.log
[ $ONLY = all ] && timeout 1500 python3 rehearse_roundtrip.py guest-final3-refusaltest.sh payload-final3 rehearsal-final3-refusal > rehearsal-final3-refusal.out 2>&1 && echo "refusal EXIT 0" >> rehearsal-final3-refusal.out
timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/payload-final3-undo --guest $H/guest-final3-undo.sh --output $H/rehearsal-final3-undo --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > rehearsal-final3-undo.offline.log 2>&1; echo "offline-undo EXIT $?" >> rehearsal-final3-undo.offline.log
echo REHEARSE_DONE
