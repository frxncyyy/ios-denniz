#!/bin/bash
# makes payload-bt1 / payload-bt1-undo / owned-*.json from n2btd/build/n2btd and the user's own vendor firmware dump
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); B=$H/../..; V=${VENDOR_FW:-$B/baseband-recon-20261104/vendor-x/firmware}
rm -rf $H/payload-bt1 $H/payload-bt1-undo; mkdir -p $H/payload-bt1 $H/payload-bt1-undo
cp $H/../n2btd/build/n2btd $H/payload-bt1/
for n in soc3_0_ram_mcu_1a_1_hdr.bin soc3_0_ram_bt_1a_1_hdr.bin; do cp $V/$n $H/payload-bt1/; done
# bt15: the BT audio HAL plug-in tree (from the rootfs mirror) -> owned paths of its copy in HAL/
BTL=$(mktemp -d -p "$H" .bthal.XXXXXX); python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img rget /System/Library/Audio/Plug-Ins/HAL.nord2-original/BTAudioHALPlugin.driver $BTL/BTAudioHALPlugin.driver >/dev/null 2>&1 || true
(cd $BTL && find BTAudioHALPlugin.driver | sort) > $H/bthal-tree.txt; rm -rf "$BTL"
python3 -I - "$H" <<'PY'
import json,hashlib,os,sys
H=sys.argv[1]; d=H+'/payload-bt1'
files={n:hashlib.sha256(open(d+'/'+n,'rb').read()).hexdigest() for n in sorted(os.listdir(d))}
json.dump({'purpose':'bt1 (bt-20261115 piece 0723): n2btd = com.apple.BlueTool job program + per-device BD address + kernel bring-up trigger; BT firmware (user own vendor files). Needs kernel q10jfinal17bt (0720-0722).','guest':'guest-bt1.sh','files':files},open(d+'/plan.json','w'),indent=1)
json.dump({'purpose':'undo of bt1','guest':'guest-bt1-undo.sh','files':{}},open(H+'/payload-bt1-undo/plan.json','w'),indent=1)
owned=["/System/Library/xpc/launchd.plist","/private/var/db/nord2-metal/receipt.json","/usr/local/libexec/n2btd",
 "/usr/share/firmware/nord2-bt","/usr/share/firmware/nord2-bt/soc3_0_ram_mcu_1a_1_hdr.bin","/usr/share/firmware/nord2-bt/soc3_0_ram_bt_1a_1_hdr.bin",
 "/private/var/db/nord2-bt","/private/var/db/nord2-bt/enable","/private/var/db/nord2-bt/bdaddr","/private/var/db/nord2-bt/bdaddr.tmp",
 "/private/var/db/nord2-bt/stage","/private/var/db/nord2-bt/guard","/private/var/db/nord2-bt/guard.tmp","/private/var/db/nord2-bt/enable.off-reset","/private/var/db/nord2-bt/rearm","/private/var/db/nord2-bt/bluetoothd.log","/private/var/db/nord2-bt/MobileGestalt.plist.pre-bt","/private/var/db/nord2-bt/MobileGestalt.plist.n2btd-rebuild","/private/var/db/nord2-bt/mg-rebuilds","/System/Library/Audio/Plug-Ins/HAL/BTAudioHALPlugin.driver","/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist","/private/var/db/nord2-bt/n2btd.log","/private/var/db/nord2-bt/n2btd.log.1","/private/var/db/nord2-bt.json"]
owned+=['/System/Library/Audio/Plug-Ins/HAL/'+l.strip() for l in open(H+'/bthal-tree.txt') if l.strip() and l.strip()!='BTAudioHALPlugin.driver']
owned+=['/Library/Audio/Plug-Ins/HAL/'+l.strip() for l in open(H+'/bthal-tree.txt') if l.strip()]
for n in ('owned-bt1.json','owned-bt1-undo.json','owned-bt1probe.json'): json.dump(owned,open(H+'/'+n,'w'),indent=0)
import shutil
shutil.rmtree(H+'/payload-bt1probe',ignore_errors=True); shutil.copytree(d,H+'/payload-bt1probe')
pl=json.load(open(d+'/plan.json')); pl['stage']='1'; pl['guest']='guest-bt1probe.sh'; pl['purpose']='bt1probe: stage 1 = the kernel stops after S1 (AP-side register reads only, no conn_infra access, nothing powered)'
json.dump(pl,open(H+'/payload-bt1probe/plan.json','w'),indent=1)
shutil.copy(H+'/guest-bt1.sh',H+'/guest-bt1probe.sh')
print(json.dumps(files,indent=1))
PY
