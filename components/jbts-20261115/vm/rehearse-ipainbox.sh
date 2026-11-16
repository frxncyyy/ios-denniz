#!/bin/bash
# ipainbox1 VM rehearsal on the CURRENT mirror (host only, throw-away overlay; the mirror has jbts1 + TrollStore since the
# 2026-11-16 snapshot): the real guest runs TWICE (apps undo of Terraria/SubwaySurf + tiny test IPA queued; second run = idempotent). Catalog vs undo_owned.py.
set -u
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/jbts-20261115; H=$S/vm; MAIN=$NORD2_REPO
cd $H; P=ipainbox
M=$B/mirror/rootfs-mirror.img
python3 -c "import json,os;m='$M';r={'format':'nord2-fastroot-1','kind':'snapshot','rehearsal_only':True,'mirror':m,'bytes':os.path.getsize(m),'root':json.load(open(m+'.manifest.json'))['root'],'per_chunk_manifest_matches':True,'phone_partition_writes':0};json.dump(r,open('rehearsal-snapshot.json','w'),indent=1);print('receipt root',r['root'])"
{ sed '/^fsck.hfsplus -f -n \/dev\/vda | tail -2$/d; /^echo INSTALL_DONE$/d' $S/guest-ipainbox1.sh
sed '/^fsck.hfsplus -f -n \/dev\/vda | tail -2$/d' $S/guest-ipainbox1.sh; } > guest-$P.sh
rm -rf vm-$P pay-$P; cp -al $S/payload-ipainbox-vmtest pay-$P
timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/pay-$P --guest $H/guest-$P.sh --output $H/vm-$P --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > vm-$P.offline.log 2>&1; echo "offline EXIT $?" >> vm-$P.offline.log
python3 $S/tools/undo_owned.py vm-$P/vm.log --apps Terraria.app,SubwaySurf.app > owned-$P.json; echo "owned EXIT $?"
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/vm-$P --owned-json owned-$P.json --output vm-$P-catalog.json > vm-$P-catalog.log 2>&1; echo "catalog EXIT $?" >> vm-$P-catalog.log
grep -a -E "IPAINBOX1|REHEARSAL|Traceback|Error" vm-$P/vm.log | grep -v JBTS_PATH | cut -c1-200; tail -1 vm-$P.offline.log; tail -2 vm-$P-catalog.log
rm -f vm-$P/overlay.qcow2 vm-$P/delta.bin vm-$P/initrd.gz; rm -rf vm-$P/inputs pay-$P
echo REHEARSE_DONE
