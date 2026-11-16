set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# undo of guest-bt1.sh: com.apple.BlueTool job restored from the receipt (Metal receipt updated); n2btd, firmware dir (if bt1
# made it), gate and receipt removed. The address file /private/var/db/nord2-bt/bdaddr and n2btd logs are KEPT (same address
# after a reinstall); remove /private/var/db/nord2-bt by hand to forget it.
import os, json, hashlib, plistlib, shutil
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2btd'; FW='/usr/share/firmware/nord2-bt'; DB='/private/var/db/nord2-bt'; GATE=DB+'/enable'; REC='/private/var/db/nord2-bt.json'
KEY='/System/Library/LaunchDaemons/com.apple.BlueTool.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; cfg=plistlib.loads(raw)
rc=json.load(open(R+MR)); assert rc.get('installed_launchd_sha256')==sha(raw), 'launchd.plist differs from the Metal receipt: not touching it'
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
orig=rec.get('original_job'); borig=rec.get('original_btd_job'); BKEY='/System/Library/LaunchDaemons/com.apple.bluetoothd.plist'
changed=False
if borig and cfg['LaunchDaemons'].get(BKEY)!=borig: cfg['LaunchDaemons'][BKEY]=borig; changed=True; print('BT1_UNDO bluetoothd job restored')
if orig and cfg['LaunchDaemons'].get(KEY)!=orig: cfg['LaunchDaemons'][KEY]=orig; changed=True
if changed:
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid); print('BT1_UNDO job restored', orig['ProgramArguments'])
for p in (GATE, BIN, DB+'/stage', DB+'/guard', DB+'/guard.tmp', GATE+'.off-reset', DB+'/rearm', DB+'/bluetoothd.log'):
    if os.path.lexists(R+p): os.remove(R+p); print('BT1_UNDO removed', p)
if os.path.isdir(R+FW) and rec.get('fw_dir_made', True): shutil.rmtree(R+FW); print('BT1_UNDO removed', FW)
BTHP='/System/Library/Audio/Plug-Ins/HAL/BTAudioHALPlugin.driver'
if rec.get('bt_hal_added') and os.path.isdir(R+BTHP): shutil.rmtree(R+BTHP); print('BT1_UNDO removed', BTHP)
BTHL='/Library/Audio/Plug-Ins/HAL/BTAudioHALPlugin.driver'
if rec.get('bt_hal_lib_added') and os.path.isdir(R+BTHL): shutil.rmtree(R+BTHL); print('BT1_UNDO removed', BTHL)
MG='/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist'
for b in (DB+'/MobileGestalt.plist.pre-bt', DB+'/MobileGestalt.plist.n2btd-rebuild'):
    if os.path.exists(R+b):
        st=os.stat(R+b); os.replace(R+b, R+MG); print('BT1_UNDO MobileGestalt cache restored from', b); break
for p in (DB+'/MobileGestalt.plist.pre-bt', DB+'/MobileGestalt.plist.n2btd-rebuild', DB+'/mg-rebuilds'):
    if os.path.exists(R+p): os.remove(R+p)
if os.path.exists(R+REC): os.remove(R+REC); print('BT1_UNDO removed', REC)
print('BT1_UNDO_OK')
os.sync()
GUEST
sync
umount /work/root
echo 'CC_JOB_ADDED bt1 (BlueTool job program -> n2btd)'
echo 'CC_RECEIPT_UPDATED metal receipt launchd sha (bt1)'
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
