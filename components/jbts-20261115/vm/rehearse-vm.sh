#!/bin/bash
# jbts1 VM rehearsals on the CURRENT mirror (host only; throw-away qcow2 overlay; the mirror is never written):
#   install            guest-jbts1                       catalog vs owned-jbts1.json
#   again              install + install (idempotent)    catalog vs owned-jbts1.json
#   undo-clean         undo on the clean mirror          (nothing to undo)
#   chain-undo         install + undo                    catalog vs undo_owned.py(vm.log) + owned-jbts1.json
#   chain-off          install + jbts1-off               catalog vs owned-jbts1.json + owned-jbts1-flags.json
# usage: [ONLY="install again ..."] vm/rehearse-vm.sh     (one VM at a time; ~2-4 min each)
set -u
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/jbts-20261115; H=$S/vm; MAIN=$NORD2_REPO
cd $H
M=$B/mirror/rootfs-mirror.img
python3 -c "import json,os;m='$M';r={'format':'nord2-fastroot-1','kind':'snapshot','rehearsal_only':True,'mirror':m,'bytes':os.path.getsize(m),'root':json.load(open(m+'.manifest.json'))['root'],'per_chunk_manifest_matches':True,'phone_partition_writes':0};json.dump(r,open('rehearsal-snapshot.json','w'),indent=1);print('receipt root',r['root'])"
mkpay() {  # mkpay NAME SECOND_PAYLOAD (plan of the second step -> plan2.json)
  rm -rf pay-$1; mkdir pay-$1; cp -al $S/payload-jbts1/. pay-$1/
  [ -n "$2" ] && cp $S/payload-$2/plan.json pay-$1/plan2.json
}
second() { sed 's#/src/plan.json#/src/plan2.json#' $1 | sed '/^fsck.hfsplus -f -n \/dev\/vda | tail -2$/d'; }
for P in ${ONLY:-install again undo-clean chain-undo chain-off}; do
  rm -rf vm-$P; G=guest-$P.sh; OWN=owned-$P.json
  case $P in
    install) mkpay $P ""; cp $S/guest-jbts1.sh $G; cp $S/owned-jbts1.json $OWN ;;
    again) mkpay $P jbts1; { cat $S/guest-jbts1.sh; second $S/guest-jbts1.sh; } > $G; cp $S/owned-jbts1.json $OWN ;;
    undo-clean) rm -rf pay-$P; cp -al $S/payload-jbts1-undo pay-$P; cp $S/guest-jbts1-undo.sh $G ;;
    chain-undo) mkpay $P jbts1-undo; { cat $S/guest-jbts1.sh; second $S/guest-jbts1-undo.sh; } > $G ;;
    chain-off) mkpay $P jbts1-off; { cat $S/guest-jbts1.sh; second $S/guest-jbts1-flags.sh; } > $G
               python3 -c "import json;a=json.load(open('$S/owned-jbts1.json'))+json.load(open('$S/owned-jbts1-flags.json'));json.dump(sorted(set(a)),open('$OWN','w'))" ;;
  esac
  timeout 1500 python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $H/rehearsal-snapshot.json --payload $H/pay-$P --guest $H/$G --output $H/vm-$P --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > vm-$P.offline.log 2>&1; echo "offline EXIT $?" >> vm-$P.offline.log
  case $P in undo-clean|chain-undo)
    python3 -c "import json,subprocess;a=json.loads(subprocess.run(['python3','$S/tools/undo_owned.py','vm-$P/vm.log'],capture_output=True,check=True).stdout)+json.load(open('$S/owned-jbts1.json'));json.dump(sorted(set(a)),open('$OWN','w'))" ;;
  esac
  python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $H/rehearsal-snapshot.json --update $H/vm-$P --owned-json $OWN --output vm-$P-catalog.json > vm-$P-catalog.log 2>&1; echo "catalog EXIT $?" >> vm-$P-catalog.log
  grep -a -E "JBTS1|Traceback|Error" vm-$P/vm.log | cut -c1-200 > vm-$P.summary; tail -1 vm-$P.offline.log >> vm-$P.summary; tail -2 vm-$P-catalog.log >> vm-$P.summary
  rm -f vm-$P/overlay.qcow2 vm-$P/delta.bin vm-$P/initrd.gz; rm -rf vm-$P/inputs pay-$P
  echo "== $P"; cat vm-$P.summary
done
echo REHEARSE_DONE
