#!/bin/bash
# gfxall1/gfxall2 VM rehearsals on the CURRENT mirror (shader99b) under the heavy lock: txn path (generic_update_offline.py) + catalog vs owned-gfxall.json.
# journal replay + guest in the VM on a throw-away overlay) + catalog check against owned-gfxall.json, each guest CHAINED after
# webxr1's own guest (the phone after the webxr1 transaction): chain (webxr1 + gfx100a), chain-off, chain-undo (webxr1 + gfx100a + undo).
set -u
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; H=$B/gfx200-20261114/shipall/vm; MAIN=$NORD2_REPO
cd $H
declare -A OWNED_FOR=([gfxall8-seeded]=../owned-gfxall8-seeded.json [gfxall9-seeded]=../owned-gfxall9-seeded.json [gfxall11-seeded]=../owned-gfxall11-seeded.json [gfxall13-seeded]=../owned-gfxall13-seeded.json [gfxall14-seeded]=../owned-gfxall14-seeded.json [gfxall15-seeded]=../owned-gfxall15-seeded.json [gfxall16-seeded]=../owned-gfxall16-seeded.json)
python3 -c "import json,os;m=\"$B/mirror/rootfs-mirror.img\";r=json.load(open(\"rehearsal-snapshot.json\"));r[\"root\"]=json.load(open(m+\".manifest.json\"))[\"root\"];r[\"bytes\"]=os.path.getsize(m);json.dump(r,open(\"rehearsal-snapshot.json\",\"w\"),indent=1);print(\"receipt root\",r[\"root\"])"
for P in ${ONLY:-gfxall1 gfxall2 gfxall2-off gfxall2-chainundo}; do
  rm -rf vm-$P
  timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/payload-$P --guest $H/guest-$P.sh --output $H/vm-$P --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > vm-$P.offline.log 2>&1; echo "offline EXIT $?" >> vm-$P.offline.log
  python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/vm-$P --owned-json ${OWNED_FOR[$P]:-../owned-gfxall.json} --output vm-$P-catalog.json > vm-$P-catalog.log 2>&1; echo "catalog EXIT $?" >> vm-$P-catalog.log
  rm -f vm-$P/overlay.qcow2 vm-$P/delta.bin vm-$P/initrd.gz
done
echo REHEARSE_DONE
