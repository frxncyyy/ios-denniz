#!/bin/bash
# rehearsal of guest-bt1.sh / guest-bt1-undo.sh on a fake root taken from the rootfs mirror (launchd.plist + Metal receipt):
# install, idempotent re-install, job content, undo == start, refusal when launchd.plist differs from the Metal receipt.
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/../..; F=$(mktemp -d -p "$A" .rehearse-bt1.XXXXXX); trap 'rm -rf "$F"' EXIT
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
$H get /System/Library/xpc $F/System/Library/xpc launchd.plist >/dev/null; rm -f $F/System/Library/xpc/launchd.plist.sig
$H get /private/var/db/nord2-metal $F/private/var/db/nord2-metal receipt.json >/dev/null
MGD=/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches
$H get $MGD $F$MGD com.apple.MobileGestalt.plist >/dev/null 2>&1 || true; rm -f $F$MGD/com.apple.MobileGestalt.plist.pre*
MGHAS=0; [ -f $F$MGD/com.apple.MobileGestalt.plist ] && MGHAS=1 && MGSHA=$(sha256sum $F$MGD/com.apple.MobileGestalt.plist | cut -c1-64); echo "mirror MobileGestalt cache present: $MGHAS"
mkdir -p $F/usr/local/libexec $F/usr/share $F/private/var/db
HALD=/System/Library/Audio/Plug-Ins; LHALD=/Library/Audio/Plug-Ins; mkdir -p $F$HALD $F$LHALD
# bt16: the audio installer's whole checked layout from the mirror (System HAL, parked originals, /Library HAL + HAL.disabled)
for d in $HALD/HAL $HALD/HAL.nord2-original $LHALD/HAL $LHALD/HAL.disabled; do $H rget $d $F$d >/dev/null 2>&1; mkdir -p $F$d; done
python3 -I $A/../test/t_audio_check.py $F $F/.audio-base.json | tail -3
# the phone may already carry bt1 (bt18b..): take its receipt / binary / firmware / state dir too
$H get /private/var/db $F/private/var/db nord2-bt.json >/dev/null 2>&1 || true
$H get /usr/local/libexec $F/usr/local/libexec n2btd >/dev/null 2>&1 || true
$H get /usr/share/firmware/nord2-bt $F/usr/share/firmware/nord2-bt soc3_0_ram_mcu_1a_1_hdr.bin soc3_0_ram_bt_1a_1_hdr.bin >/dev/null 2>&1 || true
$H get /private/var/db/nord2-bt $F/private/var/db/nord2-bt enable bdaddr stage guard enable.off-reset >/dev/null 2>&1 || true
rmdir $F/usr/share/firmware/nord2-bt $F/usr/share/firmware $F/private/var/db/nord2-bt 2>/dev/null || true
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g; s|/src/|$A/$2/|g; s|^import os, json, hashlib, plistlib\(.*\)$|import os, json, hashlib, plistlib\1\nos.chown=lambda *a: None\nos.lchown=lambda *a: None|" | python3 -; }
st() { python3 - "$F" <<'PY'
import plistlib,sys,os,hashlib,json
F=sys.argv[1]; d=plistlib.load(open(F+'/System/Library/xpc/launchd.plist','rb'))['LaunchDaemons']
j=d.get('/System/Library/LaunchDaemons/com.apple.BlueTool.plist')
r=json.load(open(F+'/private/var/db/nord2-metal/receipt.json'))['installed_launchd_sha256']
h=hashlib.sha256(open(F+'/System/Library/xpc/launchd.plist','rb').read()).hexdigest()
fw=F+'/usr/share/firmware/nord2-bt'
print('jobs',len(d),'bluetool',json.dumps(j,sort_keys=True),'receipt==plist',r==h,'libexec',sorted(os.listdir(F+'/usr/local/libexec')),
      'fw',sorted(os.listdir(fw)) if os.path.isdir(fw) else None,'db',sorted(os.listdir(F+'/private/var/db')))
PY
}
S0=$(st); echo "mirror:  $S0"
if echo "$S0" | grep -q '"/usr/local/libexec/n2btd"'; then
  echo "--- the mirror already has bt1: undo it first (the phone's real undo path)"
  run guest-bt1-undo.sh payload-bt1-undo | tee $F/.o0; grep -q BT1_UNDO_OK $F/.o0
  S0=$(st); echo "$S0" | grep -q '"ProgramArguments": \["/usr/sbin/BlueTool", "-R"\]'; echo "undo of the phone's bt1 restores Apple's job: ok"
fi
echo "start:   $S0"
run guest-bt1.sh payload-bt1 | tee $F/.o1; S1=$(st); echo "install: $S1"
grep -q 'BT1_BTHAL BTAudioHALPlugin.driver copied into /Library/Audio/Plug-Ins/HAL' $F/.o1; diff -r $F$HALD/HAL.nord2-original/BTAudioHALPlugin.driver $F$LHALD/HAL/BTAudioHALPlugin.driver >/dev/null; echo "BT audio HAL plug-in copied to /Library HAL (identical tree): ok"
python3 -I $A/../test/t_audio_check.py $F $F/.audio-base.json; echo "kernel audio installer layout check after install: ok"
grep -q BT1_OK $F/.o1; grep -q BT1_BTDLOG $F/.o1; if [ $MGHAS = 1 ]; then grep -q 'BT1_MGCACHE moved aside' $F/.o1; [ ! -e $F$MGD/com.apple.MobileGestalt.plist ]; [ -f $F/private/var/db/nord2-bt/MobileGestalt.plist.pre-bt ]; else grep -q 'BT1_MGCACHE absent' $F/.o1; fi
python3 - "$F" <<'PY2'
import plistlib,sys
j=plistlib.load(open(sys.argv[1]+"/System/Library/xpc/launchd.plist","rb"))["LaunchDaemons"]["/System/Library/LaunchDaemons/com.apple.bluetoothd.plist"]
assert j["EnvironmentVariables"]["OS_ACTIVITY_DT_MODE"]=="YES" and j["StandardErrorPath"]=="/private/var/db/nord2-bt/bluetoothd.log" and j["ProgramArguments"]==["/usr/sbin/bluetoothd"], j
print("bluetoothd job: os_log -> stderr -> file: ok")
PY2
echo "$S1" | grep -q '"EnablePressuredExit": false.*"MachServices": {"com.apple.BlueTool": true}.*"ProgramArguments": \["/usr/local/libexec/n2btd"\].*"RunAtLoad": true.*"UserName": "root"'
echo "$S1" | grep -q "receipt==plist True libexec \['n2btd'\] fw \['soc3_0_ram_bt_1a_1_hdr.bin', 'soc3_0_ram_mcu_1a_1_hdr.bin'\]"
[ -f $F/private/var/db/nord2-bt/enable ]
run guest-bt1.sh payload-bt1 > $F/.o2; S2=$(st); [ "$S1" = "$S2" ]; grep -q 'BT1_JOB already' $F/.o2; echo "idempotent ok"
# bt16: a phone left in the bt35b layout (bt15 copy in /System/.../HAL, receipt bt_hal_added) is repaired by the install
rm -rf $F$LHALD/HAL/BTAudioHALPlugin.driver; cp -a $F$HALD/HAL.nord2-original/BTAudioHALPlugin.driver $F$HALD/HAL/
python3 - $F <<'PY3'
import json,sys
p=sys.argv[1]+'/private/var/db/nord2-bt.json'; r=json.load(open(p)); r['bt_hal_added']=True; r['bt_hal_lib_added']=False; json.dump(r,open(p,'w'))
PY3
! python3 -I $A/../test/t_audio_check.py $F $F/.audio-base.json >/dev/null; echo "bt35b layout detected by the audio check (would refuse launchd): ok"
run guest-bt1.sh payload-bt1 > $F/.o2b; grep -q 'removed the bt15 copy' $F/.o2b; grep -q 'copied into /Library/Audio/Plug-Ins/HAL' $F/.o2b
python3 -I $A/../test/t_audio_check.py $F $F/.audio-base.json; echo "install repairs the bt35b layout (System HAL back to RedwoodAudio only): ok"
echo "aa:bb:cc:11:22:33" > $F/private/var/db/nord2-bt/bdaddr
run guest-bt1-undo.sh payload-bt1-undo | tee $F/.o3; S3=$(st); echo "undo:    $S3"; grep -q BT1_UNDO_OK $F/.o3
[ "$(echo "$S0" | sed 's/ db .*//')" = "$(echo "$S3" | sed 's/ db .*//')" ]; [ -f $F/private/var/db/nord2-bt/bdaddr ]; echo "undo == start (address kept)"
[ ! -e $F$HALD/HAL/BTAudioHALPlugin.driver ] && [ ! -e $F$LHALD/HAL/BTAudioHALPlugin.driver ]; echo "undo removed the BT audio HAL plug-in copy: ok"
python3 -I $A/../test/t_audio_check.py $F $F/.audio-base.json; echo "kernel audio installer layout check after undo: ok"
if [ $MGHAS = 1 ]; then [ "$(sha256sum $F$MGD/com.apple.MobileGestalt.plist | cut -c1-64)" = "$MGSHA" ]; echo "undo restored the original MobileGestalt cache: ok"; fi
python3 -c "import plistlib,sys;j=plistlib.load(open(sys.argv[1]+'/System/Library/xpc/launchd.plist','rb'))['LaunchDaemons']['/System/Library/LaunchDaemons/com.apple.bluetoothd.plist'];assert 'StandardErrorPath' not in j and 'OS_ACTIVITY_DT_MODE' not in j.get('EnvironmentVariables',{}),j;print('undo restored the bluetoothd job: ok')" $F
run guest-bt1probe.sh payload-bt1probe > $F/.o5; grep -q "BT1_STAGE 1" $F/.o5; grep -qx 1 $F/private/var/db/nord2-bt/stage; touch $F/private/var/db/nord2-bt/guard $F/private/var/db/nord2-bt/enable.off-reset
run guest-bt1.sh payload-bt1 > $F/.o6; grep -q "BT1_STAGE full" $F/.o6; [ ! -e $F/private/var/db/nord2-bt/stage ]; [ ! -e $F/private/var/db/nord2-bt/guard ]; [ ! -e $F/private/var/db/nord2-bt/enable.off-reset ]; grep -q BT1_REARM $F/.o6; echo "probe variant writes the stage file, bt1 over it removes it: ok"
run guest-bt1-undo.sh payload-bt1-undo > /dev/null
python3 -c "import json,sys;p=sys.argv[1];r=json.load(open(p));r['installed_launchd_sha256']='0'*64;json.dump(r,open(p,'w'))" $F/private/var/db/nord2-metal/receipt.json
if run guest-bt1.sh payload-bt1 > $F/.o4 2>&1; then echo "refusal test FAILED"; exit 1; fi
grep -q 'differs from the Metal receipt' $F/.o4; echo "refused on a launchd.plist that differs from the receipt: ok"
echo REHEARSAL_BT1_OK
