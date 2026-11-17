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
orig=rec.get('original_job')
if orig and cfg['LaunchDaemons'].get(KEY)!=orig:
    cfg['LaunchDaemons'][KEY]=orig; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid); print('BT1_UNDO job restored', orig['ProgramArguments'])
for p in (GATE, BIN, DB+'/stage', DB+'/guard', DB+'/guard.tmp', GATE+'.off-reset', DB+'/rearm'):
    if os.path.lexists(R+p): os.remove(R+p); print('BT1_UNDO removed', p)
if os.path.isdir(R+FW) and rec.get('fw_dir_made', True): shutil.rmtree(R+FW); print('BT1_UNDO removed', FW)
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
